#!/usr/bin/env python3
"""Generate authoritative CycloneDX components from repository-owned recipes."""
from __future__ import annotations

import hashlib
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def required(pattern: str, text: str, label: str) -> str:
    match = re.search(pattern, text)
    if not match:
        raise SystemExit(f"cannot extract {label}")
    return match.group(1)


def source_digest(directory: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(path for path in directory.rglob("*") if path.is_file()):
        digest.update(str(path.relative_to(ROOT)).encode())
        digest.update(b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
        digest.update(b"\n")
    return digest.hexdigest()


def component(name: str, version: str, purl: str, license_name: str, source: str, sha256: str | None, cpe: str | None = None) -> dict:
    result = {
        "type": "library", "name": name, "version": version, "purl": purl,
        "licenses": [{"license": {"name": license_name}}],
        "externalReferences": [{"type": "distribution", "url": source}],
        "properties": [{"name": "source:evidence", "value": source}],
    }
    if sha256:
        result["hashes"] = [{"alg": "SHA-256", "content": sha256}]
    if cpe:
        result["cpe"] = cpe
    return result


openssl = read("deps/OpenSSL/OpenSSL.cmake")
curl = read("deps/CURL/CURL.cmake")
expat_h = read("deps/EXPAT/expat/expat.h")
wx = read("deps/wxWidgets/wxWidgets.cmake")
manifest = json.loads(read("resources/web/include/vendor-manifest.json"))
openssl_url = required(r'set\(url_str\s+"([^"]+)"', openssl, "OpenSSL URL")
openssl_hash = required(r'set\(url_hash\s+"SHA256=([0-9a-fA-F]+)"', openssl, "OpenSSL hash")
openssl_version = required(r"openssl-([0-9]+\.[0-9]+\.[0-9]+)\.tar\.gz", openssl_url, "OpenSSL version")
curl_url = required(r"(?m)^\s*URL\s+([^\s]+)", curl, "curl URL")
curl_hash = required(r"(?m)^\s*URL_HASH\s+SHA256=([0-9a-fA-F]+)", curl, "curl hash")
curl_version = required(r"curl-([0-9]+\.[0-9]+\.[0-9]+)\.tar\.gz", curl_url, "curl version")
expat_version = ".".join(required(rf"#\s*define XML_{part}_VERSION\s+(\d+)", expat_h, part) for part in ("MAJOR", "MINOR", "MICRO"))
expat_source_hash = source_digest(ROOT / "deps/EXPAT/expat")
backports = json.loads(read("deps/EXPAT/security-patches.json"))
if backports["base_version"] != expat_version or backports["source_tree_sha256"] != expat_source_hash:
    raise SystemExit("Expat backport evidence does not match the bundled source; review patches before generating VEX")
for patch in backports["patches"]:
    if hashlib.sha256((ROOT / patch["local_patch"]).read_bytes()).hexdigest() != patch["local_patch_sha256"]:
        raise SystemExit(f"Expat backport patch hash mismatch: {patch['local_patch']}")
wx_commit = required(r"GIT_TAG\s+([0-9a-f]{40})", wx, "wxWidgets commit")
jquery = manifest.get("jquery", {})
if jquery.get("version") != "3.7.1" or not jquery.get("sha256") or not jquery.get("files"):
    raise SystemExit("incomplete jQuery vendor manifest")
for relative in jquery["files"]:
    path = ROOT / "resources/web" / relative
    if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != jquery["sha256"]:
        raise SystemExit(f"jQuery manifest hash mismatch: {relative}")

components = [
    component("openssl", openssl_version, f"pkg:generic/openssl@{openssl_version}", "Apache-2.0", openssl_url, openssl_hash, f"cpe:2.3:a:openssl:openssl:{openssl_version}:*:*:*:*:*:*:*"),
    component("curl", curl_version, f"pkg:generic/curl@{curl_version}", "curl", curl_url, curl_hash, f"cpe:2.3:a:haxx:libcurl:{curl_version}:*:*:*:*:*:*:*"),
    component("expat", expat_version, f"pkg:generic/expat@{expat_version}", "MIT", "https://github.com/libexpat/libexpat", expat_source_hash, f"cpe:2.3:a:libexpat_project:libexpat:{expat_version}:*:*:*:*:*:*:*"),
    component("wxWidgets", wx_commit, f"pkg:generic/wxWidgets@{wx_commit}", "LGPL-2.0-or-later WITH WxWindows-exception-3.1", f"https://github.com/bambulab/wxWidgets/commit/{wx_commit}", None),
    component("jquery", jquery["version"], f"pkg:npm/jquery@{jquery['version']}", jquery["license"], jquery["source"], jquery["sha256"]),
]
output = {"bomFormat": "CycloneDX", "specVersion": "1.5", "version": 1, "metadata": {"tools": [{"vendor": "BambuStudio", "name": "generate_dependency_sbom.py"}]}, "components": components}
destination = Path(sys.argv[1] if len(sys.argv) > 1 else "sbom/authoritative-components.cdx.json")
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
vex = {
    "@context": "https://openvex.dev/ns/v0.2.0",
    "@id": f"urn:sha256:{expat_source_hash}",
    "author": "BambuStudioLX maintainers",
    "timestamp": datetime.now(timezone.utc).isoformat(),
    "version": 1,
    "statements": [{
        "vulnerability": {"name": patch["vulnerability"]},
        "products": [{"@id": f"pkg:generic/expat@{expat_version}"}],
        "status": "fixed",
        "action_statement": f"Backported merged upstream fix {patch['upstream_pr']} ({patch['merge_commit']}); bundled source tree SHA-256 {expat_source_hash} verified before producing this statement.",
    } for patch in backports["patches"]],
}
(destination.parent / "native-backports.vex.json").write_text(json.dumps(vex, indent=2) + "\n", encoding="utf-8")
print(f"wrote {destination} ({len(components)} authoritative components)")
