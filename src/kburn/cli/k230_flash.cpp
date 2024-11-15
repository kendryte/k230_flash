#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <iomanip>

#include <stdexcept>

#include <cstdio>

#include <CLI/CLI.hpp>

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

struct AddrFilePartition {
    unsigned long address;
    std::string file_path;
    unsigned long partition_max_size = 0;  // default value
};

// Overload the >> operator for AddrFilePartition
std::istream& operator>>(std::istream& in, AddrFilePartition& value) {
    std::string input;
    std::getline(in, input);  // Read the entire input line

    std::stringstream ss(input);
    std::string token;

    // Parse the address (first part)
    if (!std::getline(ss, token, ',')) {
        in.setstate(std::ios::failbit);
        return in;
    }
    value.address = std::stoul(token, nullptr, 0);  // Convert address from string to unsigned long

    // Parse the file path (second part)
    if (!std::getline(ss, token, ',')) {
        in.setstate(std::ios::failbit);
        return in;
    }
    value.file_path = token;  // Assign file path

    // Parse the partition max size (third part, optional)
    if (std::getline(ss, token, ',')) {
        value.partition_max_size = std::stoul(token, nullptr, 0);  // Convert partition max size
    } else {
        value.partition_max_size = 0;  // Default to 0 if not provided
    }

    return in;
}

int main(int argc, char **argv) {
    struct kburn_usb_dev_info dev;

    CLI::App app{"Kendryte Burning Tool"};

    bool auto_reboot = false;
    app.add_flag("--auto-reboot", auto_reboot, "Enable automatic reboot.");

    bool list_device = false;
    app.add_flag("-l,--list-device", list_device, "List devices");

    enum KBurnMediumType medium_type = KBURN_MEDIUM_EMMC;
    std::map<std::string, KBurnMediumType> medium_map = {
        {"EMMC", KBURN_MEDIUM_EMMC},
        {"SDCARD", KBURN_MEDIUM_SDCARD},
        {"SPI_NAND", KBURN_MEDIUM_SPI_NAND},
        {"SPI_NOR", KBURN_MEDIUM_SPI_NOR},
        {"OTP", KBURN_MEDIUM_OTP}
    };
    app.add_option("-m,--medium-type", medium_type, "Specify the medium type, Default EMMC")
        ->transform(CLI::CheckedTransformer(medium_map, CLI::ignore_case))
        ->default_str("EMMC");

    std::string device_address;
    app.add_option("-d,--device-address", device_address, "Device address (format: 1-1 or 3-1), which is the result from '--list-device'")
        ->default_str("");

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
    app.add_option("--log-level", log_level, "Set the logging level, Default WARN")
        ->transform(CLI::CheckedTransformer(log_level_map, CLI::ignore_case))
        ->default_str("WARN");

    std::vector<AddrFilePartition> addr_filename_pairs;

    app.add_option("addr_filename", addr_filename_pairs, "Address, file path, and optional partition max size")
        ->expected(-1);

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
    read_data_group->add_option("--read-address", read_data_address, "The address where reading data starts, Default 0x00")
        ->check(CLI::Number)
        ->default_str("0x00");

    unsigned long read_data_size = 4096;
    read_data_group->add_option("--read-size", read_data_size, "The size of the data to read, Default 4096")
        ->check(CLI::Number)
        ->default_str("4096");

    std::string read_data_file = "data.bin";
    read_data_group->add_option("--read-file", read_data_file, "The path where read data will be saved, Default data.bin")
        ->default_str("data.bin");

    // erase
    auto *erase_group = app.add_option_group("Erase Medium Options", "Options related to the medium erase");

    bool erase_medium = false;
    erase_group->add_flag("--erase-medium", erase_medium, "Erase the medium.");

    unsigned long erase_medium_address = 0x00;
    erase_group->add_option("--erase-address", erase_medium_address, "The address where erase medium starts, Default 0x00")
        ->check(CLI::Number)
        ->default_str("0x00");

    unsigned long erase_medium_size = 4096;
    erase_group->add_option("--erase-size", erase_medium_size, "The size of the meidum to erase, Default 0x00")
        ->check(CLI::Number)
        ->default_str("0x00");

    CLI11_PARSE(app, argc, argv);

    printf("K230 Flash Start.\n");

    kburn_initialize();
    spdlog_set_log_level(static_cast<int>(log_level));

    size_t file_offset_max = 0;

    if(list_device) {
        KBurnUSBDeviceList * device_list = list_usb_device_with_vid_pid();

        printf("Available Device: %zd\n", device_list->size());

        for (auto it = device_list->begin(); it != device_list->end(); ++it) {
            const auto& dev = *it;

            printf("\tdevice: %04X:%04X, path %s, type %s\n", dev.vid, dev.pid, dev.path, dev_type_str(dev.type));
        }
        goto _exit;
    }

    std::sort(addr_filename_pairs.begin(), addr_filename_pairs.end(),
            [](const AddrFilePartition& a, const AddrFilePartition& b) {
                return a.address < b.address;
            });

    if((false == read_data) && (false == erase_medium)) {
        if(0x00 == addr_filename_pairs.size()) {
            printf("the following arguments are required: <address> <filename> [partition_max_size]\n");
            goto _exit;
        }

        for (size_t i = 0; i < addr_filename_pairs.size(); ++i) {
            unsigned long address = addr_filename_pairs[i].address;  // Accessing address from struct
            const std::string &filename = addr_filename_pairs[i].file_path;  // Accessing file path from struct
            unsigned long partition_max_size = addr_filename_pairs[i].partition_max_size;  // Optional, may be 0 if not set

            std::ifstream file(filename, std::ios::binary);
            if (!file) {
                printf("Error: File not found - %s.\n", filename.c_str());
                return 1;
            }

            file.seekg(0, std::ios::end);
            size_t fileSize = file.tellg();
            file.seekg(0, std::ios::beg); // Reset the file pointer to the beginning if needed

            if(4096 <= fileSize) {
                fileSize = round_up(fileSize, 4096);
            }

            // Check if partition_max_size is set and apply the necessary logic
            if (partition_max_size != 0) {
                if (fileSize > partition_max_size) {
                    printf("Error: File size exceeds partition max size for %s.\n", filename.c_str());
                    return 1;
                }
            }

            // Check for overlaps with the next item
            if (i < addr_filename_pairs.size() - 1) { // Ensure not to go out of bounds
                unsigned long next_address = addr_filename_pairs[i + 1].address;  // Accessing next address from struct
                if (address + fileSize > next_address) {
                    printf("Warning: Overlap detected between %s and %s.\n",
                        filename.c_str(), addr_filename_pairs[i + 1].file_path.c_str());
                }

                if(spdlog::level::level_enum::debug >= log_level) {
                    printf("address %u file %s length %u, next address %u\n", address, filename.c_str(), fileSize, next_address);
                }
            }

            // Update max file offset
            file_offset_max = std::max(file_offset_max, address + fileSize);

            // printf("Write %s to 0x%08X, Size: %ld\n", filename.c_str(), address, fileSize);
        }
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
        } else if(erase_medium) {
            if(0x00 != erase_medium_size) {
                printf("Erase 0x%08lX to 0x%08lX start.\n", erase_medium_address, erase_medium_address + erase_medium_size);

                // Get the start time point
                auto start = std::chrono::high_resolution_clock::now();

                if(false == uboot_burner->erase(erase_medium_address, erase_medium_size)) {
                    printf("Erase 0x%08lX to 0x%08lX failed.\n", erase_medium_address, erase_medium_address + erase_medium_size);

                    delete uboot_burner;
                    goto _exit;
                }
                // Get the end time point
                auto end = std::chrono::high_resolution_clock::now();

                // Calculate the duration
                std::chrono::duration<double> elapsed = end - start;

                printf("Erase 0x%08lX to 0x%08lX done, use %.2f sec.\n", erase_medium_address, erase_medium_address + erase_medium_size, elapsed.count());
            } else {
                printf("Erase size is 0.\n");
            }
        }else {
            if(file_offset_max > medium_info->capacity) {
                printf("Files exceed the capacity of meidum.\n");

                delete uboot_burner;
                goto _exit;
            }

            for (const auto& entry : addr_filename_pairs) {
                unsigned long address = entry.address;  // Accessing address from AddrFilePartition
                const std::string &filename = entry.file_path;  // Accessing file path from AddrFilePartition
                unsigned long partition_max_size = entry.partition_max_size;  // Accessing optional partition_max_size

                size_t file_size;
                char *file_data = readFile(filename, file_size);

                printf("Write %s to 0x%08lX, Size: %zd.\n", filename.c_str(), address, file_size);

                if(false == uboot_burner->write(file_data, file_size, address, partition_max_size)) {
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
