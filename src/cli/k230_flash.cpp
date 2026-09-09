#include <algorithm>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <cinttypes>
#include <iomanip>
#include <memory>

#include <stdexcept>

#include <cstdio>
#include <cctype>    // for std::tolower

#include <CLI/CLI.hpp>

#include <kburn.h>
#include <kdimage.h>
#include <k230/kburn_k230.h>

using namespace std;
using namespace std::chrono;

using namespace Kendryte_Burning_Tool;

struct ParsedArguments {
    std::vector<std::string> values;
    std::vector<char *> argv;
    std::vector<std::pair<uint64_t, std::string>> flash_inputs;
    std::string command;
};

static ParsedArguments parseArguments(int argc, char **argv) {
    ParsedArguments parsed;
    parsed.values.emplace_back(argv[0]);
    std::vector<std::string> positional;
    bool consumes_value = false;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (consumes_value) {
            parsed.values.push_back(argument);
            consumes_value = false;
            continue;
        }
        if (!argument.empty() && argument[0] != '-') {
            if (parsed.command.empty() &&
                (argument == "devices" || argument == "flash" ||
                 argument == "read" || argument == "erase")) {
                parsed.command = argument;
                parsed.values.push_back(argument);
                continue;
            }
            positional.push_back(argument);
            continue;
        }
        parsed.values.push_back(argument);
        consumes_value = argument == "-m" || argument == "--medium-type" ||
            argument == "-d" || argument == "--device-address" ||
            argument == "--log-level" || argument == "--loader-address" ||
            argument == "--loader" || argument == "--address" ||
            argument == "--size" || argument == "--read-file";
    }

    if (positional.size() % 2 != 0) {
        throw std::invalid_argument(
            "flash images must be specified as ADDRESS FILE pairs");
    }
    for (size_t index = 0; index < positional.size(); index += 2) {
        size_t consumed = 0;
        uint64_t address = 0;
        try {
            address = std::stoull(positional[index], &consumed, 0);
        } catch (const std::exception&) {
            throw std::invalid_argument("invalid flash address: " + positional[index]);
        }
        if (consumed != positional[index].size()) {
            throw std::invalid_argument("invalid flash address: " + positional[index]);
        }
        parsed.flash_inputs.emplace_back(address, positional[index + 1]);
    }

    parsed.argv.reserve(parsed.values.size());
    for (std::string &argument : parsed.values) {
        parsed.argv.push_back(argument.data());
    }
    return parsed;
}

CLI::Validator ValidLoadAddress([](std::string &input) {
    try {
        unsigned long long address = std::stoull(input, nullptr, 0);

        if (address >= 0x80300000 && address <= 0x80400000) {
            input = std::to_string(address);
			return std::string();
        }
		return "Address outside 0x80300000 - 0x80400000: " + input;
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

uint64_t round_down(uint64_t value, uint64_t multiple) {
    return value - (value % multiple);
}

bool get_physical_write_size(const KburnImageItem_t& item, uint64_t file_size,
			     const K230::kburn_medium_info& medium,
			     uint64_t *physical_size) {
	if (!physical_size || !file_size || !medium.blk_size ||
	    file_size > UINT64_MAX - (medium.blk_size - 1))
		return false;

	uint64_t transfer_size = round_up(file_size, medium.blk_size);
	if (KBURN_FLAG_SPI_NAND_WRITE_WITH_OOB == KBURN_FLAG_FLAG(item.partFlag)) {
		uint64_t page_size = KBURN_FLAG_VAL1(item.partFlag);
		uint64_t oob_size = KBURN_FLAG_VAL2(item.partFlag);
		if (medium.type != KBURN_MEDIUM_SPI_NAND ||
		    page_size != medium.blk_size || !oob_size ||
		    page_size + oob_size < page_size ||
		    file_size > UINT64_MAX - (page_size + oob_size - 1))
			return false;
		transfer_size = round_up(file_size, page_size + oob_size);
		*physical_size = transfer_size / (page_size + oob_size) * page_size;
	} else {
		*physical_size = transfer_size;
	}
	return true;
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

bool hasSuffixCaseInsensitive(std::string filename, std::string suffix) {
    // Convert both strings to lowercase
    std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);

    // Perform the suffix check
    if (filename.length() >= suffix.length()) {
        return filename.compare(filename.length() - suffix.length(), suffix.length(), suffix) == 0;
    }
    return false;
}

struct kburn_usb_dev_info open_device(const std::string& path = "", bool checkisUboot = false) {
    std::unique_ptr<KBurnUSBDeviceList> device_list(list_usb_device_with_vid_pid());
	if (!device_list)
		throw std::runtime_error("Unable to enumerate USB devices.");
    
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

struct kburn_usb_dev_info poll_and_open_device(const std::string& path = "", bool checkisUboot = false, int poll_interval = 2000, int timeout = -1) {
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
    double percent = total ? (double)iteration / total * 100 : 0.0;

    // Create the progress bar (50 characters wide)
    int bar_width = 50;
    int filled_length = static_cast<int>(percent / 2);
    string bar(filled_length, '=');
    bar += string(bar_width - filled_length, '-');

    // Calculate elapsed time
    steady_clock::time_point current_time = steady_clock::now();
    duration<double> elapsed_time = duration_cast<duration<double>>(current_time - progress_start_time);

    // Calculate speed in iterations per second (converted to KB/s)
    double speed = elapsed_time.count() > 0.0
        ? (iteration / 1024.0) / elapsed_time.count()
        : 0.0;

    // Display the progress bar, percent complete, and speed using printf
    printf("\r|%s| %.2f%% Complete - Speed: %.2f KB/s", bar.c_str(), percent, speed);

    // Check if the iteration is complete
    if (iteration >= total) {
        printf("\n");
    }

    fflush(stdout);  // Flush the output to update the terminal
};

int main(int argc, char **argv) {
    ParsedArguments parsed;
    try {
        parsed = parseArguments(argc, argv);
    } catch (const std::exception &error) {
        fprintf(stderr, "%s\n", error.what());
        return EXIT_FAILURE;
    }
    argc = static_cast<int>(parsed.argv.size());
    argv = parsed.argv.data();

    int exit_code = EXIT_FAILURE;
    size_t file_offset_max = 0;
    struct kburn_usb_dev_info dev;
    KburnImageItemList *kdimg_items;

    CLI::App app{
        "Flash images to Kendryte K230/K230D devices."};
    app.name("k230_flash_cli");
    app.set_help_flag("-h,--help", "Show this help message and exit");
    app.require_subcommand(1);
    app.footer(
        "Examples:\n"
        "  k230_flash_cli flash 0x1000 boot.bin 0x8000 app.bin\n"
        "  k230_flash_cli devices\n"
        "  k230_flash_cli read --address 0 --size 0x100000 --read-file backup.bin\n"
        "  k230_flash_cli erase --address 0 --size 0x20000\n\n"
        "Raw image addresses must be aligned to the medium erase size.\n"
        "Use --auto-reboot to reboot after a successful write.");

    CLI::App *devices_command = app.add_subcommand("devices", "List connected devices");
    CLI::App *flash_command = app.add_subcommand("flash", "Flash one or more ADDRESS FILE pairs");
    CLI::App *read_command = app.add_subcommand("read", "Read data from the device");
    CLI::App *erase_command = app.add_subcommand("erase", "Erase a range of the medium");
    devices_command->fallthrough();
    flash_command->fallthrough();
    read_command->fallthrough();
    erase_command->fallthrough();

    bool auto_reboot = false;
    app.add_flag("--auto-reboot", auto_reboot, "Enable automatic reboot after flashing.");

    std::string device_address;
    bool list_device = false;
    app.add_flag("-l,--list-device", list_device, "List connected devices");
    app.add_option("-d,--device-address", device_address, "Device address (format: 1-1 or 3-1), shown by '--list-device'")
        ->default_str("");

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

    bool custom_loader = false;
    uint64_t loader_address = 0x80360000;
    std::string loader_file;
    auto *loader_address_option = app.add_option("--loader-address", loader_address, "Custom loader load address")
        ->check(ValidLoadAddress)->default_str("0x80360000");
    auto *loader_file_option = app.add_option("--loader", loader_file, "Path to a custom loader");

    uint64_t read_data_address = 0x00;
    uint64_t read_data_size = 4096;
    std::string read_data_file = "data.bin";
    auto *read_address_option = app.add_option("--address", read_data_address, "Read or erase start address")
        ->check(CLI::Number)->default_str("0x00");
    auto *read_size_option = app.add_option("--size", read_data_size, "Number of bytes to read or erase")
        ->check(CLI::Number)->default_str("4096");
    auto *read_file_option = app.add_option("--read-file", read_data_file, "Output file")
        ->default_str("data.bin");

    uint64_t &erase_medium_address = read_data_address;
    uint64_t &erase_medium_size = read_data_size;

    CLI11_PARSE(app, argc, argv);

    custom_loader = loader_file_option->count() > 0;
    if (loader_address_option->count() > 0 && !custom_loader) {
        fprintf(stderr, "--loader-address requires --loader.\n");
        return EXIT_FAILURE;
    }
    bool read_data = parsed.command == "read";
    bool erase_medium = parsed.command == "erase";
    list_device = parsed.command == "devices" || list_device;
    if (parsed.command == "flash" && (read_data || erase_medium)) {
        fprintf(stderr, "flash does not accept read or erase options.\n");
        return EXIT_FAILURE;
    }
    if (parsed.command == "read" && erase_medium) {
        fprintf(stderr, "read does not accept erase options.\n");
        return EXIT_FAILURE;
    }
    if (parsed.command == "erase" && read_data) {
        fprintf(stderr, "erase does not accept read options.\n");
        return EXIT_FAILURE;
    }
    if (read_data && erase_medium) {
        fprintf(stderr, "read and erase options cannot be used together.\n");
        return EXIT_FAILURE;
    }

    printf("K230 Flash Start.\n");

    kburn_initialize();
    spdlog_set_log_level(static_cast<int>(log_level));

    if(list_device) {
        std::unique_ptr<KBurnUSBDeviceList> device_list(list_usb_device_with_vid_pid());
		if (!device_list) {
			fprintf(stderr, "Unable to enumerate USB devices.\n");
			goto _exit;
		}

        printf("Available Device: %zd\n", device_list->size());

        for (auto it = device_list->begin(); it != device_list->end(); ++it) {
            const auto& dev = *it;

            printf("\tdevice: %04X:%04X, path %s, type %s\n", dev.vid, dev.pid, dev.path, dev_type_str(dev.type));
        }
		exit_code = EXIT_SUCCESS;
        goto _exit;
    }

    if((false == read_data) && (false == erase_medium)) {
        if (parsed.flash_inputs.empty()) {
            printf("at least one ADDRESS FILE pair is required\n");
            goto _exit;
        }

		kdimg_items = new KburnImageItemList();
		for (const auto &input : parsed.flash_inputs) {
			uint64_t raw_address = input.first;
			const std::string& write_file = input.second;
			if (!fileExists(write_file)) {
				printf("file %s not exist\n", write_file.c_str());
				goto _exit;
			}

			const uint64_t input_size = std::filesystem::file_size(write_file);
			if (!input_size) {
				printf("Image file %s is empty.\n", write_file.c_str());
				goto _exit;
			}

			if (hasSuffixCaseInsensitive(write_file, std::string(".kdimg"))) {
				std::unique_ptr<KburnImageItemList> items(get_kdimage_items(write_file));
				if (!items) {
					printf("Parse *.kdimg failed: %s\n", write_file.c_str());
					goto _exit;
				}
				for (auto it = items->begin(); it != items->end(); ++it) {
					const struct KburnImageItem_t item = *it;
					kdimg_items->push(item);
					if (item.partName == std::string("loader")) {
						custom_loader = true;
						loader_file = item.fileName;
                        loader_address = 0x80360000;
					}
				}
				continue;
			}

			if (raw_address > UINT32_MAX || input_size > UINT32_MAX ||
				input_size > UINT32_MAX - raw_address) {
				printf("Raw image address and size must fit in the 32-bit image layout.\n");
				goto _exit;
			}

			struct KburnImageItem_t item;
			item.partName = std::string("image");
			item.partOffset = static_cast<uint32_t>(raw_address);
			item.partSize = 0;
			item.partEraseSize = 0x00;
			item.partFlag = 0x00;
			item.fileName = write_file;
			item.fileSize = static_cast<uint32_t>(input_size);
			kdimg_items->push(item);
			raw_address += input_size;
			file_offset_max = static_cast<size_t>(std::max<uint64_t>(file_offset_max, raw_address));
		}
    }

    if(custom_loader) {
        if(!fileExists(loader_file)) {
            printf("--loader file does not exist: %s\n", loader_file.c_str());
            goto _exit;
        }
    }

    try {
        dev = poll_and_open_device(device_address);
    } catch (const std::exception& error) {
        fprintf(stderr, "Unable to open device: %s\n", error.what());
        goto _exit;
    }

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
		std::unique_ptr<char[]> owned_loader;

        if(custom_loader) {
			owned_loader.reset(readFile(loader_file, loader_size));
			loader_data = owned_loader.get();
        } else {
            brom_burner->get_loader(&loader_data, &loader_size);
        }

        if(nullptr == loader_data || 0x00 == loader_size) {
            printf("fatal error, get loader failed.\n");

            delete brom_burner;
            goto _exit;
        }

        if(false == brom_burner->write(loader_data, loader_size, loader_address)) {
            printf("fatal error, write loader failed.\n");

            delete brom_burner;
            goto _exit;
        }

        if(false == brom_burner->boot_from(loader_address)) {
            printf("fatal error, boot loader failed.\n");

            delete brom_burner;
            goto _exit;
        }

        delete brom_burner;

        do_sleep(1000);

#ifdef __ANDROID__
        kburn_deinitialize();
        do_sleep(1000);
        kburn_initialize();
#endif

        try {
            dev = poll_and_open_device(dev.path, true);
        } catch (const std::invalid_argument& e) {
            printf("Error: %s\n", e.what());
            goto _exit;
        } catch (const std::runtime_error& e) {
            printf("No devices found. Polling...\n");
            goto _exit;
        }

        do_sleep(100);

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
            if (read_data_address > medium_info->capacity ||
                read_data_size > (medium_info->capacity - read_data_address)) {
                printf("The requested data size exceeds the capacity of the medium.\n");
                delete uboot_burner;
                goto _exit;
            }

			if (!read_data_size) {
				printf("Read size must be greater than zero.\n");
				delete uboot_burner;
				goto _exit;
			}
			constexpr size_t read_chunk_size = 16 * 1024 * 1024;
			std::vector<uint8_t> file_data(
				static_cast<size_t>(std::min<uint64_t>(read_data_size,
								       read_chunk_size)));

            printf("Reading %" PRIu64 " bytes from 0x%08" PRIX64 " and saving to %s.\n", read_data_size, read_data_address, read_data_file.c_str());

			FILE *file = fopen(read_data_file.c_str(), "wb");
            if (file == NULL) {
                printf("Failed to open file %s for writing.\n", read_data_file.c_str());
                delete uboot_burner;
                goto _exit;
            }


			uint64_t read_offset = 0;
			while (read_offset < read_data_size) {
				size_t chunk = static_cast<size_t>(std::min<uint64_t>(
					read_data_size - read_offset, file_data.size()));
				if (!uboot_burner->read(file_data.data(), chunk,
							 read_data_address + read_offset)) {
					printf("Failed to read at 0x%08" PRIX64
					       "; %s contains partial data.\n",
					       read_data_address + read_offset,
					       read_data_file.c_str());
					fclose(file);
					delete uboot_burner;
					goto _exit;
				}
				size_t written_size = fwrite(file_data.data(), 1, chunk, file);
				if (written_size != chunk) {
					printf("Error: Failed to write all data to %s. Written %zu bytes.\n",
					       read_data_file.c_str(), written_size);
					fclose(file);
					delete uboot_burner;
					goto _exit;
				}
				read_offset += chunk;
			}

            // Successfully saved the data
            printf("Successfully read and saved %" PRIu64 " bytes to %s.\n", read_data_size, read_data_file.c_str());

            // Clean up resources
            fclose(file);
		} else if(erase_medium) {
			if(0x00 != erase_medium_size) {
                printf("Erase 0x%08" PRIX64 " to 0x%08" PRIX64 " start.\n", erase_medium_address, erase_medium_address + erase_medium_size);

                // Get the start time point
                auto start = std::chrono::high_resolution_clock::now();

                if(false == uboot_burner->erase(erase_medium_address, erase_medium_size)) {
                    printf("Erase 0x%08" PRIX64 " to 0x%08" PRIX64 " failed.\n", erase_medium_address, erase_medium_address + erase_medium_size);

                    delete uboot_burner;
                    goto _exit;
                }
                // Get the end time point
                auto end = std::chrono::high_resolution_clock::now();

                // Calculate the duration
                std::chrono::duration<double> elapsed = end - start;

                printf("Erase 0x%08" PRIX64 " to 0x%08" PRIX64 " done, use %.2f sec.\n", erase_medium_address, erase_medium_address + erase_medium_size, elapsed.count());
			} else {
				printf("Erase size is 0.\n");
				delete uboot_burner;
				goto _exit;
			}
        }else {
            if(file_offset_max > medium_info->capacity) {
                printf("Files exceed the capacity of meidum.\n");

                delete uboot_burner;
                goto _exit;
            }

            for (auto it = kdimg_items->begin(); it != kdimg_items->end(); ++it) {
                const struct KburnImageItem_t item = *it;

                std::ifstream file(item.fileName, std::ios::binary);
                if (!file.is_open()) {
                    printf("Failed to open %s\n", item.fileName.c_str());
                    delete uboot_burner;
                    goto _exit;
                }

					file.seekg(0, std::ios::end);
					std::streamoff file_size_value = file.tellg();
					if (file_size_value <= 0 ||
					    static_cast<uint64_t>(file_size_value) > SIZE_MAX) {
						printf("Error: Invalid file size for part %s.\n",
						       item.partName.c_str());
						delete uboot_burner;
						goto _exit;
					}
					size_t file_size = static_cast<size_t>(file_size_value);
				file.seekg(0, std::ios::beg);
					uint64_t physical_file_size;
					uint64_t layout_extent;
					if (!get_physical_write_size(item, file_size, *medium_info,
								     &physical_file_size) ||
					    (item.partSize && physical_file_size > item.partSize)) {
						printf("Error: Invalid size or OOB layout for part %s.\n",
						       item.partName.c_str());
						delete uboot_burner;
						goto _exit;
					}
					layout_extent = std::max<uint64_t>(
						physical_file_size,
						std::max<uint64_t>(item.partSize, item.partEraseSize));
					if (item.partOffset > medium_info->capacity ||
					    layout_extent > medium_info->capacity - item.partOffset) {
						printf("Error: Part %s exceeds medium capacity.\n",
						       item.partName.c_str());
						delete uboot_burner;
						goto _exit;
					}

                uint64_t medium_erase_size = medium_info->erase_size;
                if (medium_erase_size == 0) {
                    printf("Error: Unable to get medium erase size.\n");
                    delete uboot_burner;
                    goto _exit;
                }

                if (item.partOffset % medium_erase_size != 0) {
					printf("Error: Part %s offset 0x%08" PRIX32
					       " is not aligned to erase size %" PRIu64 ".\n",
					       item.partName.c_str(), item.partOffset,
					       medium_erase_size);
                    delete uboot_burner;
                    goto _exit;
                }

                if (item.partEraseSize > 0) {
					uint64_t erase_start = round_up(
						item.partOffset + physical_file_size,
						medium_erase_size);
                    uint64_t erase_end = round_down(item.partOffset + static_cast<uint64_t>(item.partEraseSize), medium_erase_size);
                    uint64_t erase_range = erase_end > erase_start ? erase_end - erase_start : 0;

                    if (erase_range > 0) {
						printf("Erase %s remain from 0x%08" PRIX64
						       " to 0x%08" PRIX64 ", Size: %" PRIu64 ".\n",
						       item.partName.c_str(), erase_start, erase_end,
						       erase_range);

                        if (!uboot_burner->erase(erase_start, erase_range)) {
							printf("Erase %s remain from 0x%08" PRIX64
							       " to 0x%08" PRIX64 " failed.\n",
							       item.partName.c_str(), erase_start, erase_end);
                            delete uboot_burner;
                            goto _exit;
                        }
                    }
                }

				printf("Write %s to 0x%08" PRIX32 ", Size: %zu.\n",
				       item.fileName.c_str(), item.partOffset, file_size);

                if (false == uboot_burner->write_stream(file, file_size, item.partOffset, item.partSize, item.partFlag)) {
					printf("Write %s to 0x%08" PRIX32 " failed.\n",
					       item.fileName.c_str(), item.partOffset);
                    delete uboot_burner;
                    goto _exit;
                }
                file.close();
            }
        }

        if(auto_reboot && !read_data && !erase_medium) {
            printf("Auto reset board after write.\n");
            uboot_burner->reboot();
        }

        delete uboot_burner;
		exit_code = EXIT_SUCCESS;
    }

_exit:
    kburn_deinitialize();

    return exit_code;
}
