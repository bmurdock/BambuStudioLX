#include <catch2/catch.hpp>
#include <boost/filesystem.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "libslic3r/I18N.hpp"
#include "libslic3r/SafeZipExtraction.hpp"
#include "miniz.h"

namespace {

struct ArchiveEntry
{
    std::string name;
    std::string contents;
    bool symlink{false};
};

struct TempPaths
{
    std::vector<std::filesystem::path> paths;
    ~TempPaths()
    {
        std::error_code ignored;
        for (const auto &path : paths)
            std::filesystem::remove_all(path, ignored);
    }
};

std::filesystem::path unique_temp_path(const char *pattern)
{
    return std::filesystem::temp_directory_path() / boost::filesystem::unique_path(pattern).string();
}

void write_u16(std::vector<unsigned char> &bytes, std::size_t offset, mz_uint16 value)
{
    bytes[offset] = static_cast<unsigned char>(value & 0xff);
    bytes[offset + 1] = static_cast<unsigned char>(value >> 8);
}

void write_u32(std::vector<unsigned char> &bytes, std::size_t offset, mz_uint32 value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<unsigned char>(value >> (i * 8));
}

void patch_name(std::vector<unsigned char> &bytes, std::size_t offset, std::size_t name_offset,
                std::size_t name_size, const std::string &name)
{
    REQUIRE(name.size() == name_size);
    std::copy(name.begin(), name.end(), bytes.begin() + offset + name_offset);
}

std::filesystem::path make_archive(const std::vector<ArchiveEntry> &entries, TempPaths &temp)
{
    mz_zip_archive writer;
    mz_zip_zero_struct(&writer);
    REQUIRE(mz_zip_writer_init_heap(&writer, 0, 1024));
    for (const auto &entry : entries) {
        const bool needs_name_patch = entry.name.front() == '/' || entry.name.find(':') != std::string::npos;
        const std::string archive_name = needs_name_patch ? std::string(entry.name.size(), 'x') : entry.name;
        REQUIRE(mz_zip_writer_add_mem(&writer, archive_name.c_str(), entry.contents.data(), entry.contents.size(), MZ_DEFAULT_COMPRESSION));
    }

    void *heap = nullptr;
    size_t heap_size = 0;
    REQUIRE(mz_zip_writer_finalize_heap_archive(&writer, &heap, &heap_size));
    REQUIRE(mz_zip_writer_end(&writer));

    std::vector<unsigned char> bytes(static_cast<unsigned char *>(heap), static_cast<unsigned char *>(heap) + heap_size);
    mz_free(heap);

    std::size_t entry_index = 0;
    std::size_t local_index = 0;
    for (std::size_t offset = 0; offset + 30 <= bytes.size() && entry_index < entries.size();) {
        if (bytes[offset] == 0x50 && bytes[offset + 1] == 0x4b && bytes[offset + 2] == 0x03 && bytes[offset + 3] == 0x04) {
            const auto name_size = static_cast<std::size_t>(bytes[offset + 26] | (bytes[offset + 27] << 8));
            patch_name(bytes, offset, 30, name_size, entries[local_index++].name);
        }
        if (bytes[offset] != 0x50 || bytes[offset + 1] != 0x4b || bytes[offset + 2] != 0x01 || bytes[offset + 3] != 0x02)
            ++offset;
        else {
            const auto name_size = static_cast<std::size_t>(bytes[offset + 28] | (bytes[offset + 29] << 8));
            patch_name(bytes, offset, 46, name_size, entries[entry_index].name);
            if (entries[entry_index].symlink) {
                write_u16(bytes, offset + 4, static_cast<mz_uint16>((3u << 8) | 20u));
                write_u32(bytes, offset + 38, static_cast<mz_uint32>(0120000 | 0777) << 16);
            }
            ++entry_index;
            ++offset;
        }
    }
    REQUIRE(entry_index == entries.size());

    const auto archive = unique_temp_path("safe-zip-%%%%%%.zip");
    std::ofstream output(archive, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
    output.close();
    temp.paths.push_back(archive);
    return archive;
}

bool extract(const std::vector<ArchiveEntry> &entries, const std::filesystem::path &destination,
             TempPaths &temp, bool allow_links = true, const std::function<bool()> &canceled = {})
{
    const auto archive = make_archive(entries, temp);
    std::string error;
    const bool result = Slic3r::extract_zip_safely(archive, destination, allow_links, canceled, error);
    return result;
}

std::filesystem::path new_destination(TempPaths &temp)
{
    const auto destination = unique_temp_path("safe-zip-dest-%%%%%%");
    temp.paths.push_back(destination);
    return destination;
}

} // namespace

TEST_CASE("safe ZIP extraction accepts regular files", "[safe-zip]")
{
    TempPaths temp;
    const auto destination = new_destination(temp);
    REQUIRE(extract({{"file.txt", "contents"}, {"nested/data.bin", "data"}}, destination, temp));
    REQUIRE(std::filesystem::is_regular_file(destination / "file.txt"));
    REQUIRE(std::filesystem::is_regular_file(destination / "nested/data.bin"));
}

TEST_CASE("safe ZIP extraction preflights unsafe member names", "[safe-zip]")
{
    for (const std::string name : {"../escape.txt", "/absolute.txt", "..\\escape.txt", "C:\\escape.txt"}) {
        SECTION(name) {
            TempPaths temp;
            const auto destination = new_destination(temp);
            REQUIRE_FALSE(extract({{"valid.txt", "must not be written"}, {name, "bad"}}, destination, temp));
            REQUIRE_FALSE(std::filesystem::exists(destination / "valid.txt"));
        }
    }
}

TEST_CASE("safe ZIP extraction rejects normalized duplicates", "[safe-zip]")
{
    TempPaths temp;
    REQUIRE_FALSE(extract({{"same.txt", "one"}, {"same.txt", "two"}}, new_destination(temp), temp));
}

TEST_CASE("safe ZIP extraction rejects existing symlink path components", "[safe-zip]")
{
    for (const bool escaping : {false, true}) {
        SECTION(escaping ? "escaping symlink" : "dangling symlink") {
            TempPaths temp;
            const auto destination = new_destination(temp);
            std::filesystem::create_directories(destination);
            const auto target = escaping ? std::filesystem::temp_directory_path() / "safe-zip-outside" : destination / "missing";
            std::filesystem::create_symlink(target, destination / "link");
            REQUIRE_FALSE(extract({{"link/file.txt", "bad"}}, destination, temp));
        }
    }
}

TEST_CASE("safe ZIP extraction validates and preserves contained framework links", "[safe-zip]")
{
    TempPaths temp;
    const auto destination = new_destination(temp);
    const std::vector<ArchiveEntry> entries = {
        {"Framework/Versions/A/Framework", "binary"},
        {"Framework/Versions/Current", "A", true},
        {"Framework/Framework", "Versions/Current/Framework", true},
    };
    REQUIRE(extract(entries, destination, temp));
    REQUIRE(std::filesystem::is_symlink(destination / "Framework/Versions/Current"));
    REQUIRE(std::filesystem::is_symlink(destination / "Framework/Framework"));
    REQUIRE(std::filesystem::equivalent(destination / "Framework/Framework", destination / "Framework/Versions/A/Framework"));
}

TEST_CASE("safe ZIP extraction rejects unsafe and disallowed links", "[safe-zip]")
{
    TempPaths temp;
    REQUIRE_FALSE(extract({{"link", "../outside", true}}, new_destination(temp), temp));
    REQUIRE_FALSE(extract({{"link", "inside", true}}, new_destination(temp), temp, false));
}

TEST_CASE("safe ZIP extraction honors cancellation", "[safe-zip]")
{
    TempPaths temp;
    const auto destination = new_destination(temp);
    REQUIRE_FALSE(extract({{"file.txt", "contents"}}, destination, temp, true, [] { return true; }));
    REQUIRE_FALSE(std::filesystem::exists(destination / "file.txt"));
}
