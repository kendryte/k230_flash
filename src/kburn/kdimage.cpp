#include "kdimage.h"

#include <filesystem>

namespace Kendryte_Burning_Tool {

static const unsigned long crc32_table[] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d};

static inline uint32_t crc32_byte(uint32_t accum, uint8_t delta)
{
    return crc32_table[(accum ^ delta) & 0xff] ^ (accum >> 8);
}

uint32_t crc32(uint32_t crc, const unsigned char *buf, uint32_t len)
{
    uint32_t res = crc ^ 0xffffffffL;

    for(uint32_t i = 0; i < len; i++) {
        res = crc32_byte(res, buf[i]);
    }
    return res ^ 0xffffffffL;
}

std::string to_hex_string(const unsigned char *data, size_t length) {
    // Each byte is represented by 2 hex characters, so allocate 2 * length + 1 (for null terminator)
    std::string result(length * 2, '\0'); // Pre-allocate the string

    for (size_t i = 0; i < length; ++i) {
        // Use snprintf to format each byte as a 2-character hex string
        std::snprintf(&result[i * 2], 3, "%02x", data[i]);
    }

    return result;
}

KburnImageItemList *get_kdimage_items(const std::string &image_path) {
    KburnKdImage::instance()->open(image_path);

    return KburnKdImage::instance()->items();
}

size_t get_kdimage_max_offset(void) {
    return KburnKdImage::instance()->max_offset();
}

KburnKdImage *KburnKdImage::_instance = NULL;

KburnKdImage *KburnKdImage::instance() {
    if (NULL == KburnKdImage::_instance) {
        createInstance();
    }

    return KburnKdImage::_instance;
}

void KburnKdImage::createInstance() {
  if (NULL != KburnKdImage::_instance) {
    spdlog::error("KburnKdImage instance is created.");
    return;
  }
  auto instance = new KburnKdImage();

  KburnKdImage::_instance = instance;
}

void KburnKdImage::deleteInstance() {
  delete KburnKdImage::_instance;
  KburnKdImage::_instance = NULL;
}

// Dump header information using spdlog
void KburnKdImage::dump_header(void) {
    spdlog::debug("Dumping header information:");
    spdlog::debug("\tHeader Magic: 0x{:X}", _header.img_hdr_magic);
    spdlog::debug("\tHeader CRC32: 0x{:X}", _header.img_hdr_crc32);
    spdlog::debug("\tHeader Flag: 0x{:X}", _header.img_hdr_flag);
    spdlog::debug("\tHeader Version: 0x{:X}", _header.img_hdr_version);
    spdlog::debug("\tPart Table Num: {}", _header.part_tbl_num);
    spdlog::debug("\tPart Table CRC32: 0x{:X}", _header.part_tbl_crc32);
    spdlog::debug("\tImage Info: {}", _header.image_info);
    spdlog::debug("\tChip Info: {}", _header.chip_info);
    spdlog::debug("\tBoard Info: {}", _header.board_info);
}

// Dump parts information using spdlog
void KburnKdImage::dump_parts(std::vector<struct kd_img_part_t> parts) {
    spdlog::debug("Dumping parts information:");
    for (const auto &part : parts) {
        spdlog::debug("Part Name: {}", part.part_name);
        spdlog::debug("\tPart Magic: 0x{:X}", part.part_magic);
        spdlog::debug("\tPart Offset: 0x{:X}", part.part_offset);
        spdlog::debug("\tPart Size: 0x{:X}", part.part_size);
        spdlog::debug("\tPart Erase Size: 0x{:X}", part.part_erase_size);
        spdlog::debug("\tPart Max Size: 0x{:X}", part.part_max_size);
        spdlog::debug("\tPart Flag: 0x{:X}", part.part_flag);
        spdlog::debug("\tPart Content Offset: 0x{:X}", part.part_content_offset);
        spdlog::debug("\tPart Content Size: 0x{:X}", part.part_content_size);
        spdlog::debug("\tPart Content SHA256: {:02X}", fmt::join(part.part_content_sha256, ""));
    }
}

bool KburnKdImage::parse_parts(void) {
    uint32_t read_crc32, calc_crc32;

    if(!_image_file.is_open()) {
        spdlog::error("Error: image file not opened");

        return false;
    }

    _image_file.seekg(0, std::ios::beg); // Go back to the beginning

    // Read the header
    _image_file.read(reinterpret_cast<char *>(&_header), sizeof(kd_img_hdr_t));
    if (_header.img_hdr_magic != KDIMG_HADER_MAGIC) {
        spdlog::error("Error: Invalid image header magic! 0x{:08X} != 0x{:08X}", KDIMG_HADER_MAGIC, _header.img_hdr_magic);

        return false;
    }

    // Verify header CRC32
    read_crc32 = _header.img_hdr_crc32;
    _header.img_hdr_crc32 = 0x00;

    calc_crc32 = crc32(0, reinterpret_cast<const unsigned char *>(&_header), sizeof(kd_img_hdr_t));
    if(read_crc32 != calc_crc32) {
        spdlog::error("Error: Invalid image header checksum! 0x{:08X} != 0x{:08X}", read_crc32, calc_crc32);

        return false;
    }
    _header.img_hdr_crc32 = read_crc32;

    // Read the part table
    size_t sizePartsContent = _header.part_tbl_num * sizeof(kd_img_part_t);
    std::vector<char> part_table_content(sizePartsContent);
    _image_file.read(part_table_content.data(), sizePartsContent);

    // Verify part table CRC32
    calc_crc32 = crc32(0, reinterpret_cast<const unsigned char *>(part_table_content.data()), sizePartsContent);
    if (calc_crc32 != _header.part_tbl_crc32) {
        spdlog::error("Error: Invalid part table checksum!");

        return false;
    }

    // Parse parts
    _curr_parts.clear();
    for (size_t i = 0; i < sizePartsContent; i += sizeof(kd_img_part_t)) {
        kd_img_part_t part;
        std::memcpy(&part, part_table_content.data() + i, sizeof(kd_img_part_t));

        if (part.part_magic != KDIMG_PART_MAGIC) {
            spdlog::error("Error: Invalid part header magic!");
            return false;
        }

        _curr_parts.push_back(part);
    }

    return true;
}

bool KburnKdImage::extract_parts(void) {
    if (!_image_file.is_open()) {
        spdlog::error("Error: image file not opened");
        return false;
    }

    // Create a temporary directory
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "BurnImageItemsCli";
    if (!std::filesystem::exists(tempDir)) {
        std::filesystem::create_directory(tempDir);
    }

    // Iterate over all files and subdirectories in the temporary directory
    for (const auto &entry : std::filesystem::directory_iterator(tempDir)) {
        try {
            // Remove the file or directory
            std::filesystem::remove_all(entry.path());
        } catch (const std::filesystem::filesystem_error &e) {
            spdlog::error("Failed to remove {}: {}", entry.path().string(), e.what());
        }
    }

    _items.clear();

    for (const auto &part : _curr_parts) {
        if (part.part_magic != KDIMG_PART_MAGIC) {
            spdlog::error("Error: Invalid part header magic!");
            return false;
        }

        // Initialize SHA-256
        SHA256 sha256;

        std::stringstream offset_str;
        offset_str << "_0x" << std::setfill('0') << std::setw(8) << std::hex << part.part_offset;

        // Create the filename
        std::string tempFileName = (tempDir / (std::string(part.part_name) + offset_str.str() + ".bin")).string();

        std::ofstream tempFile(tempFileName, std::ios::binary);

        if (!tempFile.is_open()) {
            spdlog::error("Error: Could not create temp file: {}", tempFileName);
            return false;
        }

        // Extract data in chunks
        uint64_t remainingSize = part.part_content_size;
        uint64_t currentOffset = part.part_content_offset;

        while (remainingSize > 0) {
            size_t bytesToRead = std::min(ChunkSize, static_cast<size_t>(remainingSize));

            _image_file.seekg(currentOffset);
            std::vector<char> chunkData(bytesToRead);
            _image_file.read(chunkData.data(), bytesToRead);

            if (_image_file.gcount() != bytesToRead) {
                spdlog::error("Error: Failed to read chunk at offset: {}", currentOffset);
                return false;
            }

            // Update SHA-256
            sha256.update(chunkData.data(), bytesToRead);

            // Write to temp file
            tempFile.write(chunkData.data(), bytesToRead);

            currentOffset += bytesToRead;
            remainingSize -= bytesToRead;
        }

        // Handle padding
        if (part.part_content_size < part.part_size) {
            uint32_t padding = part.part_size - part.part_content_size;
            if (padding > 4096) {
                spdlog::error("Error: Align part size too large: {}", padding);
                return false;
            } else {
                std::vector<char> paddingData(padding, 0xFF);
                tempFile.write(paddingData.data(), padding);
            }
        }

        tempFile.close();

        // Finalize SHA-256
        std::string calculatedHash = sha256.final();
        std::string partContentHash = to_hex_string(part.part_content_sha256, sizeof(part.part_content_sha256));

        // Compare hashes
        if (calculatedHash != partContentHash) {
            spdlog::error("Error: SHA-256 mismatch for part: {}", part.part_name);
            spdlog::error("Calculated SHA-256: {}", calculatedHash);
            spdlog::error("Expected SHA-256:   {}", partContentHash);
            return false;
        }

        // Write SHA-256 hash to a .sha256 file
        std::string sha256FileName = tempFileName + ".sha256";
        std::ofstream sha256File(sha256FileName, std::ios::binary);
        if (!sha256File.is_open()) {
            spdlog::error("Error: Could not create SHA-256 file: {}", sha256FileName);
            return false;
        }
        sha256File << calculatedHash;
        sha256File.close();

        // Add to list
        KburnImageItem_t item;
        item.partName = part.part_name;
        item.partOffset = part.part_offset;
        item.partSize = part.part_max_size;
        item.partEraseSize = part.part_erase_size;
        item.fileName = tempFileName;
        item.fileSize = part.part_size;

        _items.push(item);

        spdlog::debug("extract part {} to {}", part.part_name, tempFileName);
    }

    std::sort(_last_parts.begin(), _last_parts.end());

    return 0x00 != _items.size();
}

void KburnKdImage::get_parts_from_temp(void) {
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "BurnImageItemsCli";

    _last_parts.clear();
    if (!std::filesystem::exists(tempDir)) {
        return;
    }

    // Iterate over all files in the temporary directory
    for (const auto &entry : std::filesystem::directory_iterator(tempDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bin") {
            // Extract part name and offset from the filename
            std::string filename = entry.path().stem().string(); // Remove extension
            size_t offsetPos = filename.find("_0x");

            if (offsetPos == std::string::npos) {
                spdlog::warn("Skipping invalid file: {}", entry.path().string());
                continue;
            }

            std::string partName = filename.substr(0, offsetPos);
            std::string offsetStr = filename.substr(offsetPos + 1); // Skip "_"
            uint64_t partOffset = std::stoull(offsetStr, nullptr, 16); // Convert hex string to uint64_t

            spdlog::debug("filename {}, partName {}, partOffset {}({})", filename, partName, partOffset, offsetStr);

            // Read the corresponding .sha256 file
            std::filesystem::path sha256FilePath = entry.path().string() + ".sha256";
            if (!std::filesystem::exists(sha256FilePath)) {
                spdlog::warn("SHA-256 file not found for part: {}", partName);
                continue;
            }

            std::ifstream sha256File(sha256FilePath, std::ios::binary);
            if (!sha256File.is_open()) {
                spdlog::warn("Failed to open SHA-256 file: {}", sha256FilePath.string());
                continue;
            }

            std::string partContentSha256((std::istreambuf_iterator<char>(sha256File)), std::istreambuf_iterator<char>());
            sha256File.close();

            // Populate the kd_img_part_t struct
            struct kd_img_part_t part = {};

            // Set part name
            std::strncpy(part.part_name, partName.c_str(), sizeof(part.part_name) - 1);
            part.part_name[sizeof(part.part_name) - 1] = '\0'; // Ensure null-termination

            // Set part offset
            part.part_offset = static_cast<uint32_t>(partOffset);

            // Set part content SHA-256
            if (partContentSha256.size() == 64) { // SHA-256 hash is 64 characters in hex
                for (size_t i = 0; i < 32; ++i) {
                    std::string byteStr = partContentSha256.substr(i * 2, 2);
                    part.part_content_sha256[i] = static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16));
                }
            } else {
                spdlog::warn("Invalid SHA-256 hash length for part: {}", partName);
                continue;
            }

            // Add the part to _last_parts
            _last_parts.push_back(part);

            spdlog::debug("Loaded part {} from {}", part.part_name, entry.path().string());
        }
    }
    std::sort(_last_parts.begin(), _last_parts.end());
}

void KburnKdImage::convert_parts_to_items(void) {
    _items.clear();

    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "BurnImageItemsCli";
    if (!std::filesystem::exists(tempDir)) {
        spdlog::error("temp folder not exist\n");
        return;
    }

    for (const auto &part : _curr_parts) {
        std::stringstream offset_str;
        offset_str << "_0x" << std::setfill('0') << std::setw(8) << std::hex << part.part_offset;

        std::string tempFileName = (tempDir / (std::string(part.part_name) + offset_str.str() + ".bin")).string();

        std::ifstream tempFile(tempFileName, std::ios::binary);
        if (!tempFile.is_open()) {
            spdlog::error("Error: Could not open temp file: {}", tempFileName);
            return;
        }
        tempFile.close();

        // Add to list
        KburnImageItem_t item;
        item.partName = part.part_name;
        item.partOffset = part.part_offset;
        item.partSize = part.part_max_size;
        item.partEraseSize = part.part_erase_size;
        item.fileName = tempFileName;
        item.fileSize = part.part_size;

        _items.push(item);
    }
    std::sort(_last_parts.begin(), _last_parts.end());
}

KburnImageItemList * KburnKdImage::items(void) {
    if(_image_file.is_open()) {
        _image_file.close();
    }

    _image_file.open(_image_path, std::ios::binary);

    if(!_image_file.is_open()) {
        spdlog::error("Failed to open image file {}", _image_path);

        _image_file.close();
        return nullptr;
    }

    if(!parse_parts()) {
        spdlog::error("Failed to parse kdimage part table");

        _image_file.close();
        return nullptr;
    }

    get_parts_from_temp();

    spdlog::debug("image header:");
    dump_header();

    spdlog::debug("current image parts:");
    dump_parts(_curr_parts);

    spdlog::debug("last image parts:");
    dump_parts(_last_parts);

    if(_last_parts != _curr_parts) {
        extract_parts();
    } else {
        convert_parts_to_items();
    }

    return &_items;
}

};
