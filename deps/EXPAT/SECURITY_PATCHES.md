# Expat security backports

The bundled source is Expat 2.9.0 with the merged upstream fixes below. Expat
2.10.0 includes all four fixes; these backports keep this dependency update
focused while the fork synchronizes with Bambu Studio upstream.

| Advisory | Upstream fix | Purpose |
| --- | --- | --- |
| CVE-2026-113351 | [PR 1431](https://github.com/libexpat/libexpat/pull/1431) | Account for retained allocations after parser reset and preserve configured protections. |
| CVE-2026-115506 | [PR 1433](https://github.com/libexpat/libexpat/pull/1433) | Reject excessive entity expansion before performing the expansion work. |
| CVE-2026-117610 | [PR 1444](https://github.com/libexpat/libexpat/pull/1444) | Handle an initial non-final empty external entity parse without a crash. |
| CVE-2026-118370 | [PR 1450](https://github.com/libexpat/libexpat/pull/1450) | Prevent signed overflow in attribute and namespace-prefix indexes. |

`security-patches.json` records upstream merge commits, patch hashes, adaptation
notes, and the SHA-256 of the complete bundled source tree. The patches in
The first two patches in `patches/` preserve the original 2.9.0 integer-overflow
guards and adapt debug formatting/pointer subtraction to its older internal
types. The two new source patches apply the corresponding upstream changes
unchanged. All four can be applied in manifest order from the repository root
with `git apply --recount`.

`scripts/security/generate_dependency_sbom.py` checks the exact source and patch
hashes before emitting an OpenVEX `fixed` statement for these four backports.
Grype retains the advisory matches as repaired findings rather than pretending
the source is an unmodified new release. The vulnerable-version canary runs
without VEX and must still detect the three expected advisories.

When updating Expat, review whether the new release contains these fixes, rerun
the parser-reset and early-amplification regressions, then update or remove the
backport evidence and VEX generation. Do not regenerate hashes to silence a
scanner finding without reviewing the corresponding source changes.
