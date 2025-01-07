#pragma once

#include "kburn.h"
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>

#include <string>
#include <vector>
#include <stdexcept>

#include "picosha2.h"

namespace Kendryte_Burning_Tool {
#define KDIMG_HADER_MAGIC   (0x27CB8F93)
#define KDIMG_PART_MAGIC    (0x91DF6DA4)

struct alignas(512) kd_img_hdr_t {
    uint32_t img_hdr_magic;
    uint32_t img_hdr_crc32;
    uint32_t img_hdr_flag;
	uint32_t img_hdr_version;

    uint32_t part_tbl_num;
    uint32_t part_tbl_crc32;

    char image_info[32];
    char chip_info[32];
    char board_info[64];
};
static_assert(sizeof(struct kd_img_hdr_t) == 512, "Size of kd_img_part_t struct is not 512 bytes!");

struct alignas(256) kd_img_part_t {
    uint32_t part_magic;
    uint32_t part_offset;   // align to 4096
    uint32_t part_size;     // align to 4096
    uint32_t part_erase_size;
    uint32_t part_max_size;
    uint32_t part_flag;

    uint32_t part_content_offset;
    uint32_t part_content_size;
	uint8_t  part_content_sha256[32];

    char part_name[32];

    // Overload the equality operator
    bool operator==(const kd_img_part_t &other) const {
        return part_offset == other.part_offset &&
               memcmp(part_content_sha256, other.part_content_sha256, sizeof(part_content_sha256)) == 0 &&
               strncmp(part_name, other.part_name, sizeof(part_name)) == 0;
    }

    bool operator < (const struct kd_img_part_t& other) const {
		return part_offset < other.part_offset;
    }
};
static_assert(sizeof(struct kd_img_part_t) == 256, "Size of kd_img_part_t struct is not 256 bytes!");

struct KburnImageItem_t {
	bool operator < (const struct KburnImageItem_t& other) const {
		return partOffset < other.partOffset;
    }

	std::string partName;
	uint32_t partOffset;
	uint32_t partSize;
	uint32_t partEraseSize;

	std::string fileName;
	uint32_t fileSize;
};

class KBURN_API KburnImageItemList {
public:
    KburnImageItemList() {}

    // Custom iterator class
    class Iterator {
    public:
        Iterator(struct KburnImageItem_t *ptr) : ptr_(ptr) {}

        struct KburnImageItem_t &operator*() const { return *ptr_; }
        struct KburnImageItem_t *operator->() const { return ptr_; }

        Iterator &operator++() {
            ++ptr_;
            return *this;
        }

        bool operator==(const Iterator &other) const { return ptr_ == other.ptr_; }
        bool operator!=(const Iterator &other) const { return ptr_ != other.ptr_; }

    private:
        struct KburnImageItem_t *ptr_;
    };

    // Method to get the number of items
    size_t size() const {
        return data_.size();
    }

    // Methods to return the iterator
    Iterator begin() { return Iterator(data_.data()); }
    Iterator end() { return Iterator(data_.data() + data_.size()); }

    // Add a new BurnImageItem_t to the list
    void push(const struct KburnImageItem_t &item) {
        data_.push_back(item);
    }

    // Sort the list by partOffset
    void sort() {
        std::sort(data_.begin(), data_.end());
    }

    // Access an item by index
    struct KburnImageItem_t &operator[](size_t index) {
        return data_[index];
    }

    // Const access an item by index
    const struct KburnImageItem_t &operator[](size_t index) const {
        return data_[index];
    }

    // Clear the list
    void clear() {
        data_.clear();
    }
private:
    std::vector<struct KburnImageItem_t> data_;
};

class SHA256 {
public:
    SHA256() {
        // Initialize the hash256_one_by_one object
        hasher_.init();
    }

    void update(const void *data, size_t length) {
        // Update the hash with new data
        const unsigned char *byteData = reinterpret_cast<const unsigned char*>(data);
        hasher_.process(byteData, byteData + length);
    }

    std::string final() {
        // Finalize the hash and return the result
        hasher_.finish();
        return get_hash_hex_string(hasher_);
    }

    static constexpr uint32_t SHA256_DIGEST_LENGTH = 32;

private:
    picosha2::hash256_one_by_one hasher_; // Incremental SHA-256 hasher
};

class KBURN_API KburnKdImage {
public:
    KburnKdImage() {}
    KburnKdImage(const std::string &path):_image_path(path) {}
    ~KburnKdImage() {}

    void open(const std::string &path) {
        _image_path = path;
    }

    size_t max_offset(void) {
        size_t curr, max = 0x00;

        for(const auto &part : _curr_parts) {
            curr = part.part_offset + part.part_max_size;
            if(curr > max) {
                max = curr;
            }
        }
        return max;
    }
    KburnImageItemList *items(void);

    static KburnKdImage *instance();
    static void deleteInstance();

private:
    static KburnKdImage *_instance;

    static constexpr size_t ChunkSize = 4 * 1024 * 1024;      // 4 MiB

    std::string _image_path;
    std::ifstream _image_file;
    KburnImageItemList _items;

    struct kd_img_hdr_t _header;
    std::vector<struct kd_img_part_t> _curr_parts;
    std::vector<struct kd_img_part_t> _last_parts;
private:
    static void createInstance();

    bool parse_parts(void);
    bool extract_parts(void);
    void get_parts_from_temp(void);
    void convert_parts_to_items();

    void dump_header(void);
    void dump_parts(std::vector<struct kd_img_part_t> parts);
};

KBURN_API KburnImageItemList *get_kdimage_items(const std::string &image_path);

KBURN_API size_t get_kdimage_max_offset(void);

}; // namespace Kendryte_Burning_Tool
