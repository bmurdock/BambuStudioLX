#include "SafeZipExtraction.hpp"
#include "SafeArchivePath.hpp"
#include "miniz_extension.hpp"
#include <boost/nowide/cstdio.hpp>
#include <cstdio>
#include <set>
#include <vector>

namespace Slic3r {

bool extract_zip_safely(const std::filesystem::path& filename,
                        const std::filesystem::path& destination,
                        bool allow_links, const std::function<bool()>& canceled,
                        std::string& error)
{
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!open_zip_reader(&zip, filename.u8string())) { error = "Cannot open plugin archive"; return false; }
    struct Close { mz_zip_archive* zip; ~Close() { close_zip_reader(zip); } } close{&zip};
    try {
        const auto root = std::filesystem::weakly_canonical(destination);
        struct Entry { mz_uint index; std::string name; bool directory; std::string link; };
        std::vector<Entry> entries;
        std::set<std::filesystem::path> names;
        const mz_uint count = mz_zip_reader_get_num_files(&zip);
        if (count > 10000) throw std::runtime_error("Too many archive members");
        std::uint64_t total = 0;
        for (mz_uint i = 0; i < count; ++i) {
            if (canceled && canceled()) throw std::runtime_error("Plugin extraction canceled");
            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(&zip, i, &stat)) throw std::runtime_error("Invalid archive member");
            const auto path = safe_archive_path(root, stat.m_filename);
            if (!names.insert(path).second) throw std::runtime_error("Duplicate archive member");
            if (stat.m_uncomp_size > 256ULL * 1024 * 1024 ||
                stat.m_uncomp_size > 1024ULL * 1024 * 1024 - total)
                throw std::runtime_error("Archive exceeds extraction size limit");
            total += stat.m_uncomp_size;
            const unsigned type = (stat.m_external_attr >> 16) & 0170000;
            Entry entry{i, stat.m_filename, stat.m_is_directory != 0, {}};
            if (type == 0120000) {
                if (!allow_links || stat.m_uncomp_size == 0 || stat.m_uncomp_size > 4096)
                    throw std::runtime_error("Unsupported archive link");
                entry.link.resize(static_cast<std::size_t>(stat.m_uncomp_size));
                if (!mz_zip_reader_extract_to_mem(&zip, i, entry.link.data(), entry.link.size(), 0))
                    throw std::runtime_error("Cannot read archive link");
                if (entry.link.find('\0') != std::string::npos || entry.link.find(':') != std::string::npos)
                    throw std::runtime_error("Invalid archive link target");
                std::replace(entry.link.begin(), entry.link.end(), '\\', '/');
                const auto target = std::filesystem::u8path(entry.link);
                if (target.is_absolute() || target.has_root_name() || target.has_root_directory())
                    throw std::runtime_error("Absolute archive link target");
                const auto inside = (path.parent_path() / target).lexically_normal().lexically_relative(root);
                if (inside.empty() || *inside.begin() == "..")
                    throw std::runtime_error("Archive link escapes its destination");
            } else if (type != 0 && type != 0040000 && type != 0100000) {
                throw std::runtime_error("Unsupported archive member type");
            }
            entries.push_back(std::move(entry));
        }
        // No files are written until every member and link has passed preflight.
        for (const auto& entry : entries) {
            if (!entry.link.empty()) continue;
            if (canceled && canceled()) throw std::runtime_error("Plugin extraction canceled");
            const auto path = safe_archive_path(root, entry.name);
            std::filesystem::create_directories(entry.directory ? path : path.parent_path());
            if (entry.directory) continue;
            FILE* file = boost::nowide::fopen(path.u8string().c_str(), "wb");
            if (!file) throw std::runtime_error("Cannot create extracted file");
            const bool extracted = mz_zip_reader_extract_to_cfile(&zip, entry.index, file, 0);
            const bool closed = std::fclose(file) == 0;
            if (!extracted || !closed) throw std::runtime_error("Archive extraction failed");
        }
        for (const auto& entry : entries) {
            if (entry.link.empty()) continue;
            if (canceled && canceled()) throw std::runtime_error("Plugin extraction canceled");
            const auto path = safe_archive_path(root, entry.name);
            std::filesystem::create_directories(path.parent_path());
            std::filesystem::create_symlink(std::filesystem::u8path(entry.link), path);
        }
        // Resolve complete link chains only after all links exist.
        for (const auto& entry : entries) {
            if (entry.link.empty()) continue;
            const auto path = root / std::filesystem::u8path(entry.name);
            const auto inside = std::filesystem::weakly_canonical(path).lexically_relative(root);
            if (inside.empty() || *inside.begin() == "..")
                throw std::runtime_error("Archive link chain escapes its destination");
        }
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

} // namespace Slic3r
