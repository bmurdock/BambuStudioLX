#include <catch2/catch.hpp>
#include <chrono>
#include "libslic3r/SafeArchivePath.hpp"

TEST_CASE("Archive destinations reject traversal and absolute names", "[archive]")
{
    const auto root = std::filesystem::temp_directory_path() / "bambu-archive-path-tests";
    for (const std::string name : {"../outside", "folder/../../outside", "..\\outside",
                                   "/outside", "C:\\outside", "file:stream", "."})
        REQUIRE_THROWS_AS(Slic3r::safe_archive_path(root, name), std::invalid_argument);
    REQUIRE_THROWS_AS(Slic3r::safe_archive_path(root, std::string("file\0hidden", 11)), std::invalid_argument);
    REQUIRE(Slic3r::safe_archive_path(root, "machine/profile.json") ==
            std::filesystem::weakly_canonical(root) / "machine/profile.json");
    REQUIRE(Slic3r::safe_archive_path(root, "打印机/配置.json").filename() ==
            std::filesystem::u8path("配置.json"));
}

TEST_CASE("Archive destinations reject existing symlinks outside the root", "[archive]")
{
    const auto root = std::filesystem::temp_directory_path() /
                      ("bambu-archive-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove_all(p); } } cleanup{root};
    std::filesystem::create_directories(root / "inside");
    std::filesystem::create_directories(root / "outside");
    std::error_code ec;
    std::filesystem::create_directory_symlink(root / "outside", root / "inside/link", ec);
    if (ec) { WARN("Symlink creation unavailable: " << ec.message()); return; }
    REQUIRE_THROWS_AS(Slic3r::safe_archive_path(root / "inside", "link/file"), std::invalid_argument);
    std::filesystem::create_symlink(root / "outside/new-file", root / "inside/dangling-file");
    REQUIRE_THROWS_AS(Slic3r::safe_archive_path(root / "inside", "dangling-file"), std::invalid_argument);
    std::filesystem::create_directory_symlink(root / "missing-directory", root / "inside/dangling-directory");
    REQUIRE_THROWS_AS(Slic3r::safe_archive_path(root / "inside", "dangling-directory/file"), std::invalid_argument);
}
