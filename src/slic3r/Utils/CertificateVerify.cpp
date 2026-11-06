#if defined(_WIN32)
// Selecting an embedded signature requires the Windows 8 WinTrust structures.
// Set the SDK target before Boost/OpenSSL can include Windows headers.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#if !defined(NTDDI_VERSION) || NTDDI_VERSION < 0x06020000
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x06020000
#endif
#endif

#include "CertificateVerify.hpp"
#include "libslic3r_version.h"

#include <array>
#include <optional>
#include <string>
#include <vector>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <memory>
#include <openssl/evp.h>
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include "libslic3r/Log.hpp"

namespace Slic3r {

#if defined(_WIN32)

#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

    static std::wstring utf8_to_w(const std::string& s) {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
    }

    static std::wstring wself_path() {
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return std::wstring(buf, buf + n);
    }

    static bool sha256_win(const BYTE* data, DWORD len, std::array<uint8_t, 32>& out) {
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
            return false;
        bool ok = false;
        if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            if (CryptHashData(hHash, data, len, 0)) {
                DWORD cb = (DWORD)out.size();
                ok = (CryptGetHashParam(hHash, HP_HASHVAL, out.data(), &cb, 0) && cb == out.size());
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
        return ok;
    }

    static std::string wide_to_utf8(const std::wstring& w) {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    static std::optional<SignerSummary> summarize_file_win(const std::wstring& path) {
        WINTRUST_FILE_INFO file{};
        file.cbStruct = sizeof(file);
        file.pcwszFilePath = path.c_str();
        WINTRUST_SIGNATURE_SETTINGS signatures{};
        signatures.cbStruct = sizeof(signatures);
        signatures.dwFlags = WSS_VERIFY_SPECIFIC;
        signatures.dwIndex = 0; // same embedded signer subsequently inspected
        WINTRUST_DATA trust{};
        trust.cbStruct = sizeof(trust);
        trust.dwUIChoice = WTD_UI_NONE;
        trust.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
        trust.dwUnionChoice = WTD_CHOICE_FILE;
        trust.pFile = &file;
        trust.dwStateAction = WTD_STATEACTION_VERIFY;
        trust.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
        trust.pSignatureSettings = &signatures;
        GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        const LONG verified = WinVerifyTrust(nullptr, &policy, &trust);
        trust.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(nullptr, &policy, &trust);
        if (verified != ERROR_SUCCESS) return std::nullopt;

        HCERTSTORE hStore = nullptr;
        HCRYPTMSG  hMsg = nullptr;

        if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE,
            path.c_str(),
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
            CERT_QUERY_FORMAT_FLAG_BINARY,
            0, nullptr, nullptr, nullptr,
            &hStore, &hMsg, nullptr)) {
            return std::nullopt;
        }

        DWORD cb = 0;
        if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &cb) || cb == 0) {
            CryptMsgClose(hMsg); CertCloseStore(hStore, 0);
            return std::nullopt;
        }
        std::vector<BYTE> v(cb);
        if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, v.data(), &cb)) {
            CryptMsgClose(hMsg); CertCloseStore(hStore, 0);
            return std::nullopt;
        }
        auto* si = reinterpret_cast<PCMSG_SIGNER_INFO>(v.data());

        CERT_INFO ci{};
        ci.Issuer = si->Issuer;
        ci.SerialNumber = si->SerialNumber;

        PCCERT_CONTEXT pCert = CertFindCertificateInStore(
            hStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
            CERT_FIND_SUBJECT_CERT, &ci, nullptr);

        if (!pCert) {
            CryptMsgClose(hMsg); CertCloseStore(hStore, 0);
            return std::nullopt;
        }
        struct CertificateResources {
            PCCERT_CONTEXT certificate;
            HCRYPTMSG message;
            HCERTSTORE store;
            ~CertificateResources() {
                CertFreeCertificateContext(certificate);
                CryptMsgClose(message);
                CertCloseStore(store, 0);
            }
        } resources{pCert, hMsg, hStore};

        SignerSummary s{};

        // 1) cert_sha256
        DWORD cbHash = (DWORD)s.cert_sha256.size();
        if (!CertGetCertificateContextProperty(
            pCert, CERT_SHA256_HASH_PROP_ID,
            s.cert_sha256.data(), &cbHash) || cbHash != s.cert_sha256.size()) {
            sha256_win(pCert->pbCertEncoded, pCert->cbCertEncoded, s.cert_sha256);
        }

        // 2) spki_sha256 SPKI -> DER -> hash
        DWORD cbSpki = 0;
        if (!CryptEncodeObjectEx(
            X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO,
            &pCert->pCertInfo->SubjectPublicKeyInfo,
            0, nullptr, nullptr, &cbSpki)) {
            return std::nullopt;
        }

        std::vector<BYTE> spki(cbSpki);
        if (!CryptEncodeObjectEx(
            X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO,
            &pCert->pCertInfo->SubjectPublicKeyInfo,
            0, nullptr, spki.data(), &cbSpki)) {
            return std::nullopt;
        }

        sha256_win(spki.data(), cbSpki, s.spki_sha256);

        // 3) subject CN/issuer
        {
            wchar_t buf[1024];
            DWORD n1 = CertGetNameStringW(pCert, CERT_NAME_ATTR_TYPE, 0, const_cast<char*>(szOID_COMMON_NAME), buf, 1024);
            if (n1 > 1) s.subject_cn = wide_to_utf8(std::wstring(buf, buf + n1 - 1));
            DWORD n2 = CertGetNameStringW(pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG,
                nullptr, buf, 1024);
            if (n2 > 0) s.issuer_dn = wide_to_utf8(std::wstring(buf, buf + (n2 ? n2 - 1 : 0)));
        }

        return s;
    }

    std::optional<SignerSummary> SummarizeSelf() {
        try {
            return summarize_file_win(wself_path());
        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(info) << e.what();
            return std::nullopt;
        }
    }

    std::optional<SignerSummary> SummarizeModule(const std::string& path_utf8) {
        try {
            return summarize_file_win(utf8_to_w(path_utf8));
        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(info) << e.what();
            return std::nullopt;
        }
    }

#elif defined(__APPLE__)

#include <cstring>
#include <TargetConditionals.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <Security/SecCertificateOIDs.h>
#include <CommonCrypto/CommonCrypto.h>

    static bool sha256_mac(const void* data, size_t len, std::array<uint8_t, 32>& out) {
        CC_SHA256_CTX ctx; CC_SHA256_Init(&ctx);
        CC_SHA256_Update(&ctx, data, (CC_LONG)len);
        CC_SHA256_Final(out.data(), &ctx);
        return true;
    }

    static std::string cfstr(CFStringRef s) {
        if (!s) return {};
        CFIndex len = CFStringGetLength(s);
        CFIndex max = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8);
        std::string out(static_cast<size_t>(max) + 1, '\0');
        if (CFStringGetCString(s, out.data(), max, kCFStringEncodingUTF8)) {
            out.resize(strlen(out.c_str()));
            return out;
        }
        return {};
    }

    static std::string dn_from_cert(SecCertificateRef cert) {
        CFStringRef s = SecCertificateCopySubjectSummary(cert);
        std::string out = cfstr(s);
        if (s) CFRelease(s);
        return out;
    }

    static std::string cn_from_cert(SecCertificateRef cert) {
        const void *keys[] = { kSecOIDCommonName };
        CFArrayRef key_array = CFArrayCreate(nullptr, keys, 1, &kCFTypeArrayCallBacks);
        if (!key_array) return {};

        CFDictionaryRef values = SecCertificateCopyValues(cert, key_array, nullptr);
        CFRelease(key_array);
        if (!values) return {};

        CFDictionaryRef cn_info = static_cast<CFDictionaryRef>(CFDictionaryGetValue(values, kSecOIDCommonName));
        CFTypeRef       cn_value = cn_info ? CFDictionaryGetValue(cn_info, kSecPropertyKeyValue) : nullptr;
        std::string     out      = cn_value && CFGetTypeID(cn_value) == CFStringGetTypeID() ? cfstr(static_cast<CFStringRef>(cn_value)) : std::string();
        CFRelease(values);
        return out;
    }

    static bool fill_from_static_code(SecStaticCodeRef code, SignerSummary& s) {
        if (SecStaticCodeCheckValidity(code, kSecCSStrictValidate | kSecCSCheckAllArchitectures,
                                       nullptr) != errSecSuccess)
            return false;
        CFDictionaryRef info = nullptr;
        auto            copy_inf_ret = SecCodeCopySigningInformation(code, kSecCSSigningInformation | kSecCSRequirementInformation | kSecCSInternalInformation, &info);
        if (copy_inf_ret != errSecSuccess) {
            return false;
        }
        if (!info) {
            return false;
        }

        if (auto tid = (CFStringRef)CFDictionaryGetValue(info, kSecCodeInfoTeamIdentifier)) {
            s.team_id = cfstr(tid);
        }

        // cert chain
        CFArrayRef certs = (CFArrayRef)CFDictionaryGetValue(info, kSecCodeInfoCertificates);
        if (!certs || CFArrayGetCount(certs) == 0) {
            CFRelease(info);
            return false;
        }
        SecCertificateRef leaf = (SecCertificateRef)CFArrayGetValueAtIndex(certs, 0);

        // 1) cert_sha256
        if (auto der = SecCertificateCopyData(leaf)) {
            sha256_mac(CFDataGetBytePtr(der), CFDataGetLength(der), s.cert_sha256);
            CFRelease(der);
        }

        // 2) spki_sha256
        if (auto pk = SecCertificateCopyKey(leaf)) {
            CFDataRef raw = SecKeyCopyExternalRepresentation(pk, nullptr);
            if (raw) {
                sha256_mac(CFDataGetBytePtr(raw), CFDataGetLength(raw), s.spki_sha256);
                CFRelease(raw);
            }
            CFRelease(pk);
        }

        // 3) subject / issuer
        s.subject_dn = dn_from_cert(leaf);
        s.subject_cn = cn_from_cert(leaf);
        if (CFArrayGetCount(certs) >= 2) {
            SecCertificateRef iss = (SecCertificateRef)CFArrayGetValueAtIndex(certs, 1);
            s.issuer_dn = dn_from_cert(iss);
        }

        CFRelease(info);
        return true;
    }

    std::optional<SignerSummary> SummarizeSelf() {
        try {
            SecCodeRef dyn = nullptr;
            auto       copy_ret = SecCodeCopySelf(kSecCSDefaultFlags, &dyn);
            if (copy_ret != errSecSuccess) {
                return std::nullopt;
            }
            if (!dyn) {
                return std::nullopt;
            }
            SecStaticCodeRef st = nullptr;
            auto copy_static_ret = SecCodeCopyStaticCode(dyn, kSecCSDefaultFlags, &st);
            CFRelease(dyn);
            if (!st) {
                return std::nullopt;
            }

            SignerSummary s{};
            bool          ok = fill_from_static_code(st, s);
            CFRelease(st);
            return ok ? std::optional<SignerSummary>(s) : std::nullopt;
        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(info) << e.what();
            return std::nullopt;
        }
    }

    std::optional<SignerSummary> SummarizeModule(const std::string& path_utf8) {
        try {
            CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8 *) path_utf8.c_str(), path_utf8.size(), false);
            if (!url) return std::nullopt;

            SecStaticCodeRef code = nullptr;
            OSStatus         st   = SecStaticCodeCreateWithPath(url, kSecCSDefaultFlags, &code);
            CFRelease(url);
            if (st != errSecSuccess || !code) return std::nullopt;

            SignerSummary s{};
            bool          ok = fill_from_static_code(code, s);
            CFRelease(code);
            return ok ? std::optional<SignerSummary>(s) : std::nullopt;
        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(info) << e.what();
            return std::nullopt;
        }
    }

#else
    std::optional<SignerSummary> SummarizeSelf() { return std::nullopt; }
    std::optional<SignerSummary> SummarizeModule(const std::string&) { return std::nullopt; }
#endif

    bool AllowUnverifiedPluginsForDevelopment(bool explicitly_enabled)
    {
#if defined(__linux__) && !BBL_RELEASE_TO_PUBLIC && !defined(NDEBUG)
        return explicitly_enabled;
#else
        (void)explicitly_enabled;
        return false;
#endif
    }

    static std::string digest_hex(const unsigned char* bytes, unsigned length)
    {
        constexpr char hex[] = "0123456789abcdef";
        std::string result;
        for (unsigned i = 0; i < length; ++i) {
            result += hex[bytes[i] >> 4];
            result += hex[bytes[i] & 15];
        }
        return result;
    }

    bool IsTrustedWindowsPluginPublisher(const SignerSummary& signer, std::string filename)
    {
        // SPKI SHA-256 pins from the official Windows 02.08.04.60 plugin:
        // Shanghai Lunkuo (Bambu) and Shanghai Zhaoyan (Agora SDK dependencies).
        // The personal fork's application signer is deliberately independent.
        const auto spki = digest_hex(signer.spki_sha256.data(), signer.spki_sha256.size());
        if (spki == "7b333f1d565f44b35bc36073296dc03a712ce2243b027ecf8218b8f12949dfbb") return true;
        if (spki != "1591f0859b88b95431d7e00e67b6ad3771c9472d1a1e199736d72056ab35f977") return false;
        std::transform(filename.begin(), filename.end(), filename.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return filename == "agora_rtc_sdk.dll" || filename == "libagora-ffmpeg.dll" ||
               filename == "libagora-soundtouch.dll" || filename == "libaosl.dll";
    }

    static bool file_matches_sha256(const std::filesystem::path& path, const char* expected)
    {
        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path))) return false;
        std::ifstream input(path, std::ios::binary);
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        if (!input || !ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) return false;
        char buffer[65536];
        while (input.read(buffer, sizeof(buffer)) || input.gcount())
            if (EVP_DigestUpdate(ctx.get(), buffer, static_cast<std::size_t>(input.gcount())) != 1) return false;
        if (!input.eof()) return false;
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned length = 0;
        if (EVP_DigestFinal_ex(ctx.get(), digest, &length) != 1 || length != 32) return false;
        return digest_hex(digest, length) == expected;
    }

    // Bambu camera tools 02.08.00.01, retrieved over HTTPS from the Bambu
    // resource API and public-cdn.bblmw.com/upgrade/studio/cameratools/
    // 02.08.00.01/ddf767841a/{mac,win,linux}_02.08.00.01.zip.
    // These helpers are unsigned: a version change requires reviewing new pins.
    bool VerifyCameraToolsArchive(const std::string& path_utf8)
    {
        try {
#if defined(_WIN32)
            const char* expected = "8e2885fe0850e5ac208b3077e97e31d166ec83d408b01d28736c7887b0158628";
#elif defined(__APPLE__)
            const char* expected = "6671bca18aaa38c689ec44b688576b800f7f1751876c960969adaa9f899555c2";
#else
            const char* expected = "c73326eddc96480fe76c790c3bb704f68bb51ed002aa2f2560ac2beabf1a45d0";
#endif
            return file_matches_sha256(std::filesystem::u8path(path_utf8), expected);
        } catch (const std::exception&) { return false; }
    }

    bool VerifyCameraToolsDirectory(const std::string& path_utf8)
    {
        try {
            const auto root = std::filesystem::u8path(path_utf8);
#if defined(_WIN32)
            const char* source = "bambu_source.exe";
            const char* ffmpeg = "ffmpeg.exe";
            const char* source_hash = "1fcdbc674e4bcc09c1b7e3ffca6f1cb9827e0898669868843f07b65405ebe4ce";
            const char* ffmpeg_hash = "a85236625da5f557c6a1b934f6d34f5a76619718fbd909228c9aa57088f9edd3";
#elif defined(__APPLE__)
            const char* source = "bambu_source";
            const char* ffmpeg = "ffmpeg";
            const char* source_hash = "5fdbd81a12a692f743385cfc737f6fe8cfb007eb7f85b9d7896b43264cf26a10";
            const char* ffmpeg_hash = "cd7db3f74581ecd0fabd67a51169886f71aebb9e659876e5f0b9b416c2eee0a7";
#else
            const char* source = "bambu_source";
            const char* ffmpeg = "ffmpeg";
            const char* source_hash = "1549ed57ab6c44ca5f9e5398b7f829f06cfffbb85c9194acdedb16bf88898114";
            const char* ffmpeg_hash = "bb4be90af5deefe054c4165f68764a0c6ff989f6e9103abc80daa27ed10573bf";
#endif
            if (!file_matches_sha256(root / source, source_hash) ||
                !file_matches_sha256(root / ffmpeg, ffmpeg_hash) ||
                !file_matches_sha256(root / "ffmpeg.cfg", "31e76aeed9aff978f6c5f9f9e0b7bfb6d9fa7234d32318f46877c20660e0c82a") ||
                !file_matches_sha256(root / "ffmpeg.sdp", "c129604475e40dee2ab19157f9c222586442ea9c72ae2f9a9f91982ad5f8c26e")) return false;
            for (const auto& entry : std::filesystem::directory_iterator(root)) {
                const auto name = entry.path().filename().u8string();
                if (name == source || name == ffmpeg || name == "ffmpeg.cfg" || name == "ffmpeg.sdp" ||
                    name == "url.txt" || name == "backup") continue;
#if defined(_WIN32)
                if (entry.path().extension() == ".dll" && VerifyPluginFile(entry.path().u8string())) continue;
#endif
                return false;
            }
            return true;
        } catch (const std::exception&) { return false; }
    }

    bool VerifyPluginFile(const std::string& path_utf8)
    {
#if defined(__APPLE__)
        // Bambu's Developer ID is independent of this personal fork's signer.
        // Trust the Apple certificate chain AND the publisher, never metadata alone.
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr,
            reinterpret_cast<const UInt8*>(path_utf8.data()), path_utf8.size(), false);
        if (!url) return false;
        SecStaticCodeRef code = nullptr;
        const OSStatus created = SecStaticCodeCreateWithPath(url, kSecCSDefaultFlags, &code);
        CFRelease(url);
        if (created != errSecSuccess || !code) return false;
        SecRequirementRef requirement = nullptr;
        const OSStatus parsed = SecRequirementCreateWithString(
            CFSTR("anchor apple generic and certificate leaf[subject.OU] = \"T3UBR9Y3B2\""),
            kSecCSDefaultFlags, &requirement);
        const bool verified = parsed == errSecSuccess && requirement &&
            SecStaticCodeCheckValidity(code, kSecCSStrictValidate | kSecCSCheckAllArchitectures,
                                       requirement) == errSecSuccess;
        if (requirement) CFRelease(requirement);
        CFRelease(code);
        return verified;
#elif defined(_WIN32)
        const auto module = SummarizeModule(path_utf8);
        return module && IsTrustedWindowsPluginPublisher(*module,
            std::filesystem::u8path(path_utf8).filename().u8string());
#else
        // No authenticated publisher format is supplied for Linux plugins.
        (void)path_utf8;
        return false;
#endif
    }

    bool VerifyPluginDirectory(const std::string& path_utf8)
    {
        try {
            const auto root = std::filesystem::u8path(path_utf8);
            bool found_binary = false;
            for (auto it = std::filesystem::recursive_directory_iterator(root);
                 it != std::filesystem::recursive_directory_iterator(); ++it) {
                if (it.depth() == 0 && it->path().filename() == "backup") {
                    it.disable_recursion_pending();
                    continue;
                }
#if defined(__APPLE__)
                if (it->is_directory() && it->path().extension() == ".framework" &&
                    !VerifyPluginFile(it->path().u8string())) return false;
#endif
                if (!it->is_regular_file()) continue;
                bool native = false;
#if defined(_WIN32)
                auto extension = it->path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                native = extension == ".dll" || extension == ".exe";
#elif defined(__APPLE__)
                std::ifstream input(it->path(), std::ios::binary);
                unsigned char magic[4]{};
                if (!input.read(reinterpret_cast<char*>(magic), sizeof(magic))) continue;
                const std::uint32_t signature = (std::uint32_t(magic[0]) << 24) |
                    (std::uint32_t(magic[1]) << 16) | (std::uint32_t(magic[2]) << 8) | magic[3];
                native = signature == 0xfeedface || signature == 0xcefaedfe ||
                         signature == 0xfeedfacf || signature == 0xcffaedfe ||
                         signature == 0xcafebabe || signature == 0xbebafeca ||
                         signature == 0xcafebabf || signature == 0xbfbafeca;
#else
                native = it->path().string().find(".so") != std::string::npos;
#endif
                if (native) {
                    found_binary = true;
                    if (!VerifyPluginFile(it->path().u8string())) return false;
                }
            }
            return found_binary;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool IsSamePublisher(const SignerSummary& a, const SignerSummary& b)
    {
        if (!a.team_id.empty() && !b.team_id.empty()) return a.team_id == b.team_id;
        const std::array<uint8_t, 32> empty{};
        if (a.spki_sha256 != empty && a.spki_sha256 == b.spki_sha256) return true;
        return a.cert_sha256 != empty && a.cert_sha256 == b.cert_sha256;
    }
}
