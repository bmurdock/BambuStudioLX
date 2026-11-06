#ifndef slic3r_SafeZipExtraction_hpp_
#define slic3r_SafeZipExtraction_hpp_

#include <filesystem>
#include <functional>
#include <string>

namespace Slic3r {

// Used for private staging directories. Links are needed by macOS frameworks;
// they are created only after regular files, and must remain inside the root.
bool extract_zip_safely(const std::filesystem::path& archive,
                        const std::filesystem::path& destination,
                        bool allow_contained_links, const std::function<bool()>& canceled,
                        std::string& error);

} // namespace Slic3r
#endif
