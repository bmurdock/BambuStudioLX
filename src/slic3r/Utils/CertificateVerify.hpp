#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <sstream>
#include <optional>

namespace Slic3r {
    struct SignerSummary {
        // generic
        std::array<uint8_t, 32> cert_sha256;
        std::array<uint8_t, 32> spki_sha256;
        std::string subject_dn;
        std::string subject_cn;
        std::string issuer_dn;

        // macOS only
        std::string team_id;

        SignerSummary() {
            cert_sha256.fill(0);
            spki_sha256.fill(0);
        }

        std::string as_print() const {
            std::stringstream ss;
            ss << " subject_dn: " << subject_dn << " subject_cn: " << subject_cn << " issuer_dn: " << issuer_dn << " team_id: " << team_id;
            return ss.str();
        }
    };

    // Explicit opt-in is limited to non-public Linux Debug builds.
    bool AllowUnverifiedPluginsForDevelopment(bool explicitly_enabled);
    // Fail closed if the platform cannot authenticate this plugin publisher.
    bool VerifyPluginFile(const std::string& path_utf8);
    bool VerifyPluginDirectory(const std::string& path_utf8);
    // Apply only to a signer obtained after successful WinVerifyTrust validation.
    bool IsTrustedWindowsPluginPublisher(const SignerSummary& signer, std::string filename);
    // Camera tools lack publisher signatures. Only reviewed, pinned bytes are trusted.
    bool VerifyCameraToolsArchive(const std::string& path_utf8);
    bool VerifyCameraToolsDirectory(const std::string& path_utf8);

    bool IsSamePublisher(const SignerSummary& a, const SignerSummary& b);

    std::optional<SignerSummary> SummarizeSelf();

    std::optional<SignerSummary> SummarizeModule(const std::string& path_utf8);
}
