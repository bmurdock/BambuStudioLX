#ifndef slic3r_SafeArchivePath_hpp_
#define slic3r_SafeArchivePath_hpp_

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace Slic3r {

// ZIP names use '/', but reject Windows separators and drive/ADS names on
// every platform so the same archive has the same containment policy.
inline std::filesystem::path safe_archive_path(const std::filesystem::path& root,
                                               std::string member)
{
    if (member.empty() || member.find('\0') != std::string::npos ||
        member.find(':') != std::string::npos)
        throw std::invalid_argument("Invalid archive member name");
    std::replace(member.begin(), member.end(), '\\', '/');
    const auto relative = std::filesystem::u8path(member);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
        throw std::invalid_argument("Absolute archive member path");
    for (const auto& part : relative)
        if (part == "..")
            throw std::invalid_argument("Archive member traverses its destination");

    const auto base = std::filesystem::weakly_canonical(root);
    auto candidate = base;
    for (const auto& part : relative) {
        candidate /= part;
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(candidate, ec);
        // weakly_canonical alone cannot resolve dangling symlinks; fopen
        // would still follow a dangling final link and create its target.
        if (std::filesystem::is_symlink(status))
            throw std::invalid_argument("Archive destination contains a symlink");
        if (ec && ec != std::errc::no_such_file_or_directory)
            throw std::filesystem::filesystem_error("Inspecting archive destination", candidate, ec);
    }
    const auto destination = std::filesystem::weakly_canonical(base / relative);
    const auto inside = destination.lexically_relative(base);
    if (inside.empty() || inside == "." || *inside.begin() == "..")
        throw std::invalid_argument("Archive member escapes its destination");
    return destination;
}

} // namespace Slic3r
#endif
