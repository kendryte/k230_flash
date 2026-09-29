#include "kdimage.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace Kendryte_Burning_Tool;

namespace {

uint32_t test_crc32(const void *data, size_t size)
{
    uint32_t crc = 0xffffffffu;
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}

bool write_image(const std::filesystem::path &path, uint32_t content_size,
                 bool corrupt_header_crc)
{
    kd_img_hdr_t header{};
    kd_img_part_t part{};
    const std::array<uint8_t, 4> content{0x11, 0x22, 0x33, 0x44};

    header.img_hdr_magic = KDIMG_HADER_MAGIC;
    header.img_hdr_version = 2;
    header.part_tbl_num = 1;

    part.part_magic = KDIMG_PART_MAGIC;
    part.part_offset = 0x1000;
    part.part_size = 4096;
    part.part_erase_size = 0x2000;
    part.part_max_size = 0x4000;
    part.part_content_offset = sizeof(header) + sizeof(part);
    part.part_content_size = content_size;
    std::memcpy(part.part_name, "rootfs", 7);
    for (size_t index = 0; index < sizeof(part.part_content_sha256); ++index)
        part.part_content_sha256[index] = static_cast<uint8_t>(index);

    header.part_tbl_crc32 = test_crc32(&part, sizeof(part));
    header.img_hdr_crc32 = test_crc32(&header, sizeof(header));
    if (corrupt_header_crc)
        ++header.img_hdr_crc32;

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(&header), sizeof(header));
    output.write(reinterpret_cast<const char *>(&part), sizeof(part));
    output.write(reinterpret_cast<const char *>(content.data()), content.size());
    return output.good();
}

bool write_sparse_image(const std::filesystem::path &path, uint32_t content_size)
{
    kd_img_hdr_t header{};
    kd_img_part_t part{};

    header.img_hdr_magic = KDIMG_HADER_MAGIC;
    header.img_hdr_version = 2;
    header.part_tbl_num = 1;

    part.part_magic = KDIMG_PART_MAGIC;
    part.part_offset = 0x1000;
    part.part_size = content_size;
    part.part_max_size = content_size;
    part.part_content_offset = sizeof(header) + sizeof(part);
    part.part_content_size = content_size;
    std::memcpy(part.part_name, "large", 6);

    header.part_tbl_crc32 = test_crc32(&part, sizeof(part));
    header.img_hdr_crc32 = test_crc32(&header, sizeof(header));

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(&header), sizeof(header));
    output.write(reinterpret_cast<const char *>(&part), sizeof(part));
    output.seekp(static_cast<std::streamoff>(part.part_content_offset) +
                 content_size - 1);
    output.put('\0');
    return output.good();
}

bool expect_valid_image(const std::filesystem::path &path)
{
    KburnImageItemList *items = get_kdimage_items(path.string());
    if (!items || items->size() != 1) {
        std::cerr << "valid image did not produce one partition\n";
        return false;
    }

    const KburnImageItem_t &item = (*items)[0];
    bool passed = true;
    passed &= item.partName == "rootfs";
    passed &= item.fileName == path.string();
    passed &= item.fileOffset == sizeof(kd_img_hdr_t) + sizeof(kd_img_part_t);
    passed &= item.dataSize == 4;
    passed &= item.fileSize == 4096;
    passed &= item.paddingValue == 0xff;
    passed &= item.verifyDataHash;
    for (size_t index = 0; index < item.dataSha256.size(); ++index)
        passed &= item.dataSha256[index] == static_cast<uint8_t>(index);

    if (!passed)
        std::cerr << "partition source metadata was not preserved\n";
    return passed;
}

} // namespace

int main()
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "kburn-kdimage-test.kdimg";
    bool passed = write_image(path, 4, false) && expect_valid_image(path);

    KburnImageItem_t large_raw{};
    constexpr uint64_t large_offset = UINT64_C(5) * 1024 * 1024 * 1024;
    constexpr uint64_t large_size = UINT64_C(5) * 1024 * 1024 * 1024;
    large_raw.partOffset = large_offset;
    large_raw.fileSize = large_size;
    if (large_raw.partOffset != large_offset || large_raw.fileSize != large_size) {
        std::cerr << "64-bit raw image metadata was truncated\n";
        passed = false;
    }

    constexpr uint32_t large_content_size = 512u * 1024u * 1024u;
    passed &= write_sparse_image(path, large_content_size);
    KburnImageItemList *large_items = get_kdimage_items(path.string());
    if (!large_items || large_items->size() != 1 ||
        (*large_items)[0].fileName != path.string() ||
        (*large_items)[0].dataSize != large_content_size) {
        std::cerr << "large image was not kept as a direct source view\n";
        passed = false;
    }

    passed &= write_image(path, 5, false);
    if (get_kdimage_items(path.string()) != nullptr) {
        std::cerr << "truncated partition content was accepted\n";
        passed = false;
    }

    passed &= write_image(path, 4, true);
    if (get_kdimage_items(path.string()) != nullptr) {
        std::cerr << "invalid header checksum was accepted\n";
        passed = false;
    }

    std::error_code error;
    std::filesystem::remove(path, error);
    KburnKdImage::deleteInstance();
    return passed ? 0 : 1;
}
