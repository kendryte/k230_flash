#include <CLI/CLI.hpp>
#include <string>
#include <vector>
#include <stdexcept>
#include <fstream>

#include <iostream>
#include <chrono>
#include <iomanip>

#include <cstdio>

#include <kburn.h>
#include <k230/kburn_k230.h>

using namespace std;
using namespace std::chrono;

using namespace Kendryte_Burning_Tool;

CLI::Validator ValidLoadAddress([](std::string &input) {
    try {
        unsigned long address = std::stoul(input, nullptr, 0);

        if (address >= 0x80300000 && address <= 0x80400000) {
            input = std::to_string(address);
        }
    } catch (const std::invalid_argument&) {
        return "Invalid number: " + input;
    } catch (const std::out_of_range&) {
        return "Number out of range: " + input;
    }

    return std::string();
}, "address between 0x80300000 - 0x80400000");

uint64_t round_up(uint64_t value, uint64_t multiple) {
    return (value + multiple - 1) / multiple * multiple;
}

const char *dev_type_str(enum kburn_usb_dev_type type) {
    const char *type_str[] = {"INVALID", "BROM", "UBOOT"};

    if(type >= KBURN_USB_DEV_MAX) {
        return "OUT-OF-RANGE";
    }

    return type_str[type];
}

bool fileExists(const std::string& filename) {
    std::ifstream file(filename);
    return file.good(); // Returns true if the file can be opened
}

char* readFile(const std::string& filename, size_t& fileSize) {
    // Open the file in binary mode
    std::ifstream file(filename, std::ios::binary);
    
    // Check if the file was opened successfully
    if (!file) {
        throw std::runtime_error("Could not open file: " + filename);
    }
    
    // Seek to the end to get the size
    file.seekg(0, std::ios::end);
    fileSize = file.tellg(); // Get the size of the file
    file.seekg(0, std::ios::beg); // Go back to the beginning

    // Allocate memory for the file content
    char* buffer = new char[fileSize];

    // Read the file into the buffer
    if (!file.read(buffer, fileSize)) {
        delete[] buffer; // Clean up on failure

        printf("Failed to read file: %s", filename.c_str());

        return nullptr;
    }

    // Close the file
    file.close();

    return buffer; // Return the buffer
}

struct kburn_usb_dev_info open_device(const std::string& path = "", bool checkisUboot = false) {
    KBurnUSBDeviceList * device_list = list_usb_device_with_vid_pid();
    
    // Check if a specific path is provided
    if (!path.empty()) {
        for (auto it = device_list->begin(); it != device_list->end(); ++it) {
            const auto& dev = *it;
            if (dev.path == path) {
                if (checkisUboot && dev.type != KBURN_USB_DEV_UBOOT) {
                    throw std::invalid_argument("Device found, but it's not of type UBOOT");
                }
                return dev; // Device found and matches criteria
            }
        }
        throw std::runtime_error("No devices available to open.");
        // throw std::invalid_argument("No device found at path: " + path);
    }

    // If no path is provided, open the first available device
    for (auto it = device_list->begin(); it != device_list->end(); ++it) {
        const auto& dev = *it;
        if (checkisUboot) {
            if (dev.type == KBURN_USB_DEV_UBOOT) {
                return dev; // Return the first UBOOT type device
            }
        } else {
            return dev; // Return the first available device
        }
    }

    throw std::runtime_error("No devices available to open.");
}

struct kburn_usb_dev_info poll_and_open_device(const std::string& path = "", bool checkisUboot = false, int poll_interval = 2, int timeout = -1) {
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        try {
            auto device = open_device(path, checkisUboot);

            printf("Device found and opened: %s, type: %s\n", device.path, dev_type_str(device.type));
            return device;
        } catch (const std::invalid_argument& e) {
            printf("Error: %s\n", e.what());
        } catch (const std::runtime_error& e) {
            printf("No devices found. Polling...\n");
        }

        if (timeout > 0) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > timeout) {
                throw std::runtime_error("Timeout reached while polling for device");
            }
        }

        do_sleep(poll_interval);
    }
}

// Global variable to store start time
steady_clock::time_point progress_start_time;

KBurner::progress_fn_t progress = [](void* ctx, size_t iteration, size_t total) {
    if (iteration == 0) {
        // Set the start time when iteration is 0
        progress_start_time = steady_clock::now();
    }

    // Calculate percentage completion
    double percent = (double)iteration / total * 100;

    // Create the progress bar (50 characters wide)
    int bar_width = 50;
    int filled_length = static_cast<int>(percent / 2);
    string bar(filled_length, '=');
    bar += string(bar_width - filled_length, '-');

    // Calculate elapsed time
    steady_clock::time_point current_time = steady_clock::now();
    duration<double> elapsed_time = duration_cast<duration<double>>(current_time - progress_start_time);

    // Calculate speed in iterations per second (converted to KB/s)
    double speed = (iteration / 1024.0) / elapsed_time.count();

    // Display the progress bar, percent complete, and speed using printf
    printf("\r|%s| %.2f%% Complete - Speed: %.2f KB/s", bar.c_str(), percent, speed);

    // Check if the iteration is complete
    if (iteration >= total) {
        printf("\n");
    }

    fflush(stdout);  // Flush the output to update the terminal
};

int main(int argc, char **argv) {
    struct kburn_usb_dev_info dev;

    CLI::App app{"Kendryte Burning Tool"};

    bool list_device = false;
    app.add_flag("-l,--list-device", list_device, "List devices");

    spdlog::level::level_enum log_level = spdlog::level::level_enum::warn;
    std::map<std::string, spdlog::level::level_enum> log_level_map = {
        {"TRACE", spdlog::level::level_enum::trace},
        {"DEBUG", spdlog::level::level_enum::debug},
        {"INFO", spdlog::level::level_enum::info},
        {"WARN", spdlog::level::level_enum::warn},
        {"ERROR", spdlog::level::level_enum::err},
        {"CRITICAL", spdlog::level::level_enum::critical},
        {"OFF", spdlog::level::level_enum::off},
    };
    app.add_option("--log-level", log_level, "Set the logging level")
        ->transform(CLI::CheckedTransformer(log_level_map, CLI::ignore_case))
        ->default_str("WARN");

    enum KBurnMediumType medium_type = KBURN_MEDIUM_EMMC;
    std::map<std::string, KBurnMediumType> medium_map = {
        {"EMMC", KBURN_MEDIUM_EMMC},
        {"SDCARD", KBURN_MEDIUM_SDCARD},
        {"SPI_NAND", KBURN_MEDIUM_SPI_NAND},
        {"SPI_NOR", KBURN_MEDIUM_SPI_NOR},
        {"OTP", KBURN_MEDIUM_OTP}
    };
    app.add_option("-m,--medium-type", medium_type, "Specify the medium type")
        ->transform(CLI::CheckedTransformer(medium_map, CLI::ignore_case))
        ->default_str("EMMC");

    std::string device_address;
    app.add_option("-d,--device-address", device_address, "Device address (format: 1-1 or 3-1), which is the result from '--list-device'")
        ->default_str("");

    bool auto_reboot = false;
    app.add_flag("--auto-reboot", auto_reboot, "Enable automatic reboot.");

    // loader
    auto *loader_group = app.add_option_group("Custom Loader Options", "Options related to the custom loader");

    bool custom_loader = false;
    loader_group->add_flag("--custom-loader", custom_loader, "Enable custom loader");

    unsigned long load_address = 0x80360000;
    loader_group->add_option("--load-address", load_address, "The address where will loader run, Default 0x80360000")
        ->check(ValidLoadAddress)
        ->default_str("0x80360000");

    std::string loader_file;
    loader_group->add_option("--loader-file", loader_file, "Path to the custom loader file");

    // read data
    auto *read_data_group = app.add_option_group("Read data Options", "Options related to reading data from the device");

    bool read_data = false;
    read_data_group->add_flag("--read-data", read_data, "Read data from the device.");

    unsigned long read_data_address = 0x00;
    read_data_group->add_option("--read-address", read_data_address, "The address where reading data starts, default 0x00")
        ->check(CLI::Number)
        ->default_str("0x00");

    unsigned long read_data_size = 4096;
    read_data_group->add_option("--read-size", read_data_size, "The size of the data to read, default 4096")
        ->check(CLI::Number)
        ->default_str("4096");

    std::string read_data_file = "data.bin";
    read_data_group->add_option("--read-file", read_data_file, "The path where read data will be saved, default data.bin")
        ->default_str("data.bin");

    std::vector<std::pair<unsigned long, std::string>> addr_filename_pairs;
    app.add_option("addr_filename", addr_filename_pairs, "Pairs of addresses followed by binary filenames")
        ->expected(-1);

    CLI11_PARSE(app, argc, argv);

    printf("K230 Flash Start.\n");

    kburn_initialize();
    spdlog_set_log_level(static_cast<int>(log_level));

    size_t file_offset_max = 0;

    std::sort(addr_filename_pairs.begin(), addr_filename_pairs.end(),
              [](const auto& a, const auto& b) {
                  return a.first < b.first;
              });

    // Iterate through the pairs and check for overlaps
    if(false == read_data) {
        for (size_t i = 0; i < addr_filename_pairs.size(); ++i) {
            unsigned long address = addr_filename_pairs[i].first;
            const std::string &filename = addr_filename_pairs[i].second;

            std::ifstream file(filename, std::ios::binary);
            if (!file) {
                printf("Error: File not found - %s.\n", filename.c_str());
                return 1;
            }

            file.seekg(0, std::ios::end);
            size_t fileSize = file.tellg();
            file.seekg(0, std::ios::beg); // Reset the file pointer to the beginning if needed

            fileSize = round_up(fileSize, 4096);

            // Check for overlaps with the next item
            if (i < addr_filename_pairs.size() - 1) { // Ensure not to go out of bounds
                unsigned long next_address = addr_filename_pairs[i + 1].first;
                if (address + fileSize > next_address) {
                    printf("Warning: Overlap detected between %s and %s.\n",
                        filename.c_str(), addr_filename_pairs[i + 1].second.c_str());
                }
            }

            // Update max file offset
            file_offset_max = std::max(file_offset_max, address + fileSize);

            // printf("Write %s to 0x%08X, Size: %ld\n", filename.c_str(), address, fileSize);
        }
    }

    if(list_device) {
        KBurnUSBDeviceList * device_list = list_usb_device_with_vid_pid();

        printf("Available Device: %zd\n", device_list->size());

        for (auto it = device_list->begin(); it != device_list->end(); ++it) {
            const auto& dev = *it;

            printf("\tdevice: %04X:%04X, path %s, type %s\n", dev.vid, dev.pid, dev.path, dev_type_str(dev.type));
        }
        goto _exit;
    }

    if((false == read_data) && (0x00 == addr_filename_pairs.size())) {
        printf("the following arguments are required: <address> <filename>\n");
        goto _exit;
    }

    if(custom_loader) {
        if(!fileExists(loader_file)) {
            printf("--loader-file is required when --custom-loader is set.\n");
            goto _exit;
        }
    }

    dev = poll_and_open_device(device_address);

    printf("use device %04X:%04X, path %s, type %s\n", dev.vid, dev.pid, dev.path, dev_type_str(dev.type));

    if(KBURN_USB_DEV_BROM == dev.type) {
        auto burner = request_burner_with_info(dev);
        if (burner == nullptr) {
            printf("fatal error, request brom burner failed.\n");
            goto _exit;
        }

        K230::K230BROMBurner *brom_burner = reinterpret_cast<K230::K230BROMBurner *>(burner);

        brom_burner->register_progress_fn(progress, NULL);

        brom_burner->set_medium_type(medium_type);

        const char *loader_data;
        size_t loader_size;

        if(custom_loader) {
            loader_data = readFile(loader_file, loader_size);
        } else {
            brom_burner->get_loader(&loader_data, &loader_size);
        }

        if(nullptr == loader_data || 0x00 == loader_size) {
            printf("fatal error, get loader failed.\n");

            delete brom_burner;
            goto _exit;
        }

        if(false == brom_burner->write(loader_data, loader_size, load_address)) {
            printf("fatal error, write loader failed.\n");

            delete brom_burner;
            goto _exit;
        }

        if(false == brom_burner->boot_from(load_address)) {
            printf("fatal error, boot loader failed.\n");

            delete brom_burner;
            goto _exit;
        }

        delete brom_burner;

        do_sleep(1000);

        try {
            dev = poll_and_open_device(dev.path, true);
        } catch (const std::invalid_argument& e) {
            printf("Error: %s\n", e.what());
            goto _exit;
        } catch (const std::runtime_error& e) {
            printf("No devices found. Polling...\n");
            goto _exit;
        }

        printf("use device %04X:%04X, path %s, type %s.\n", dev.vid, dev.pid, dev.path, dev_type_str(dev.type));
    }

    if(KBURN_USB_DEV_UBOOT == dev.type) {
        auto burner = request_burner_with_info(dev);
        if (burner == nullptr) {
            printf("fatal error, request uboot burner failed.\n");
            goto _exit;
        }

        K230::K230UBOOTBurner *uboot_burner = reinterpret_cast<K230::K230UBOOTBurner *>(burner);
        
        uboot_burner->register_progress_fn(progress, NULL);

        uboot_burner->set_medium_type(medium_type);

        if(false == uboot_burner->probe()) {
            printf("Can't probe medium as configure.\n");

            delete uboot_burner;
            goto _exit;
        }

        struct K230::kburn_medium_info *medium_info = uboot_burner->get_medium_info();

        if (read_data) {
            // Ensure the read size is within the medium's capacity
            if (read_data_size > medium_info->capacity) {
                printf("The requested data size exceeds the capacity of the medium.\n");
                delete uboot_burner;
                goto _exit;
            }

            // TODO: read data with chunk buffer.
            // Allocate buffer for reading data
            std::vector<uint8_t> file_data;

            printf("Reading %lu bytes from 0x%08lX and saving to %s.\n", read_data_size, read_data_address, read_data_file.c_str());

            file_data.resize(read_data_size, 0);

            // Perform the read operation
            if (!uboot_burner->read(file_data.data(), read_data_size, read_data_address)) {
                printf("Failed to read %lu bytes from 0x%08lX.\n", read_data_size, read_data_address);

                delete uboot_burner;
                goto _exit;
            }

            // Save the read data to the specified file
            FILE *file = fopen(read_data_file.c_str(), "wb");
            if (file == NULL) {
                printf("Failed to open file %s for writing.\n", read_data_file.c_str());
                delete uboot_burner;
                goto _exit;
            }

            size_t written_size = fwrite(file_data.data(), 1, read_data_size, file);
            if (written_size != read_data_size) {
                printf("Error: Failed to write all data to %s. Written %zu bytes.\n", read_data_file.c_str(), written_size);
                fclose(file);
                delete uboot_burner;
                goto _exit;
            }

            // Successfully saved the data
            printf("Successfully read and saved %lu bytes to %s.\n", read_data_size, read_data_file.c_str());

            // Clean up resources
            fclose(file);
        } else {
            if(file_offset_max > medium_info->capacity) {
                printf("Files exceed the capacity of meidum.\n");

                delete uboot_burner;
                goto _exit;
            }
            for (const auto& pair : addr_filename_pairs) {
                unsigned long address = pair.first;
                const std::string &filename = pair.second;

                size_t file_size;
                char *file_data = readFile(filename, file_size);

                printf("Write %s to 0x%08lX, Size: %zd.\n", filename.c_str(), address, file_size);

                if(false == uboot_burner->write(file_data, file_size, address)) {
                    printf("Write %s to 0x%08lX failed.\n", filename.c_str(), address);

                    delete uboot_burner;
                    goto _exit;
                }
                delete []file_data;
            }
        }

        if(auto_reboot) {
            printf("Auto reset board after write.\n");
            uboot_burner->reboot();
        }

        delete uboot_burner;
    }

_exit:
    kburn_deinitialize();

    return 0;
}
