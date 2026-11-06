#include <catch2/catch.hpp>
#include "slic3r/Utils/CertificateVerify.hpp"
#include "libslic3r_version.h"
#include "libslic3r/SafeZipExtraction.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

TEST_CASE("Windows plugin publisher pins constrain third-party dependency roles", "[plugin_trust]")
{
    const auto signer = [](const std::string& hex) {
        Slic3r::SignerSummary result;
        for (std::size_t i = 0; i < result.spki_sha256.size(); ++i)
            result.spki_sha256[i] = static_cast<std::uint8_t>(std::stoul(hex.substr(2 * i, 2), nullptr, 16));
        return result;
    };
    const auto bambu = signer("7b333f1d565f44b35bc36073296dc03a712ce2243b027ecf8218b8f12949dfbb");
    const auto agora = signer("1591f0859b88b95431d7e00e67b6ad3771c9472d1a1e199736d72056ab35f977");
    REQUIRE(Slic3r::IsTrustedWindowsPluginPublisher(bambu, "bambu_networking.dll"));
    REQUIRE(Slic3r::IsTrustedWindowsPluginPublisher(bambu, "BambuSource.dll"));
    for (const std::string name : {"agora_rtc_sdk.dll", "libagora-ffmpeg.dll", "libaosl.dll", "LIBAGORA-SOUNDTOUCH.DLL"})
        REQUIRE(Slic3r::IsTrustedWindowsPluginPublisher(agora, name));
    REQUIRE_FALSE(Slic3r::IsTrustedWindowsPluginPublisher(agora, "bambu_networking.dll"));
    REQUIRE_FALSE(Slic3r::IsTrustedWindowsPluginPublisher(agora, "BambuSource.dll"));
    REQUIRE_FALSE(Slic3r::IsTrustedWindowsPluginPublisher(agora, "arbitrary.dll"));
    REQUIRE_FALSE(Slic3r::IsTrustedWindowsPluginPublisher(Slic3r::SignerSummary{}, "bambu_networking.dll"));
}

TEST_CASE("Unsigned plugin bypass requires an explicit Linux development opt-in", "[plugin_trust]")
{
    REQUIRE_FALSE(Slic3r::AllowUnverifiedPluginsForDevelopment(false));
#if defined(__linux__) && !BBL_RELEASE_TO_PUBLIC && !defined(NDEBUG)
    REQUIRE(Slic3r::AllowUnverifiedPluginsForDevelopment(true));
#else
    REQUIRE_FALSE(Slic3r::AllowUnverifiedPluginsForDevelopment(true));
#endif
}

TEST_CASE("Missing signer evidence never matches a publisher", "[plugin_trust]")
{
    Slic3r::SignerSummary a, b;
    REQUIRE_FALSE(Slic3r::IsSamePublisher(a, b));
    a.spki_sha256[0] = b.spki_sha256[0] = 1;
    REQUIRE(Slic3r::IsSamePublisher(a, b));
    a.team_id = "publisher-a";
    b.team_id = "publisher-b";
    REQUIRE_FALSE(Slic3r::IsSamePublisher(a, b));
}

TEST_CASE("Unsigned and nonexistent plugin files are rejected", "[plugin_trust]")
{
    const auto file = std::filesystem::temp_directory_path() /
        ("bambu-unsigned-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove(p); } } cleanup{file};
    REQUIRE_FALSE(Slic3r::VerifyPluginFile(file.u8string()));
    std::ofstream(file, std::ios::binary) << "unsigned plugin content";
    REQUIRE_FALSE(Slic3r::VerifyPluginFile(file.u8string()));
    REQUIRE_FALSE(Slic3r::VerifyCameraToolsArchive(file.u8string()));
    REQUIRE_FALSE(Slic3r::VerifyCameraToolsDirectory(file.parent_path().u8string() + "/missing-camera-tools"));
}

TEST_CASE("Pinned camera package authenticates helpers and configuration", "[plugin_trust]")
{
    const char* archive = std::getenv("BAMBU_TEST_CAMERA_ARCHIVE");
    if (!archive) {
        WARN("Set BAMBU_TEST_CAMERA_ARCHIVE to the reviewed platform camera ZIP for the positive digest check");
        return;
    }
    REQUIRE(Slic3r::VerifyCameraToolsArchive(archive));
    const auto root = std::filesystem::temp_directory_path() /
        ("bambu-camera-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove_all(p); } } cleanup{root};
    std::filesystem::create_directory(root);
    const auto copy = root / "camera.zip";
    std::filesystem::copy_file(std::filesystem::u8path(archive), copy);
    const auto tools = root / "tools";
    std::string error;
    REQUIRE(Slic3r::extract_zip_safely(copy, tools, false, {}, error));
    REQUIRE(Slic3r::VerifyCameraToolsDirectory(tools.u8string()));
    std::ifstream config_input(tools / "ffmpeg.cfg", std::ios::binary);
    const std::string original_config{std::istreambuf_iterator<char>(config_input), std::istreambuf_iterator<char>()};
    config_input.close();
    std::ofstream(tools / "ffmpeg.cfg", std::ios::app) << "-y\n/tmp/untrusted-output\n";
    REQUIRE_FALSE(Slic3r::VerifyCameraToolsDirectory(tools.u8string()));
    std::ofstream(tools / "ffmpeg.cfg", std::ios::binary) << original_config;
    REQUIRE(Slic3r::VerifyCameraToolsDirectory(tools.u8string()));
#ifdef _WIN32
    const auto helper = tools / "bambu_source.exe";
#else
    const auto helper = tools / "bambu_source";
#endif
    std::ofstream(helper, std::ios::app | std::ios::binary) << "tamper";
    REQUIRE_FALSE(Slic3r::VerifyCameraToolsDirectory(tools.u8string()));
    std::ofstream(copy, std::ios::app | std::ios::binary) << "tamper";
    REQUIRE_FALSE(Slic3r::VerifyCameraToolsArchive(copy.u8string()));
}

#ifdef __APPLE__
TEST_CASE("A complete trusted macOS plugin directory passes authentication", "[plugin_trust]")
{
    const char* directory = std::getenv("BAMBU_TEST_SIGNED_PLUGIN_DIRECTORY");
    if (!directory) {
        WARN("Set BAMBU_TEST_SIGNED_PLUGIN_DIRECTORY to a genuine Bambu plugin installation for the complete-package check");
        return;
    }
    REQUIRE(Slic3r::VerifyPluginDirectory(directory));
}

TEST_CASE("A trusted macOS plugin rejects modification of signed bytes", "[plugin_trust]")
{
    const char* signed_plugin = std::getenv("BAMBU_TEST_SIGNED_PLUGIN");
    if (!signed_plugin) {
        WARN("Set BAMBU_TEST_SIGNED_PLUGIN to a genuine Bambu-signed dylib for the positive trust check");
        return;
    }
    REQUIRE(Slic3r::VerifyPluginFile(signed_plugin));
    const auto root = std::filesystem::temp_directory_path() /
        ("bambu-signed-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove_all(p); } } cleanup{root};
    std::filesystem::create_directory(root);
    const auto copy = root / "plugin.dylib";
    std::filesystem::copy_file(std::filesystem::u8path(signed_plugin), copy);
    REQUIRE(Slic3r::VerifyPluginDirectory(root.u8string()));
    std::fstream file(copy, std::ios::in | std::ios::out | std::ios::binary);
    file.seekg(16384);
    char byte = 0;
    REQUIRE(bool(file.read(&byte, 1)));
    file.seekp(16384);
    byte ^= 1;
    file.write(&byte, 1);
    file.close();
    REQUIRE_FALSE(Slic3r::VerifyPluginFile(copy.u8string()));
    REQUIRE_FALSE(Slic3r::VerifyPluginDirectory(root.u8string()));
}
#endif
