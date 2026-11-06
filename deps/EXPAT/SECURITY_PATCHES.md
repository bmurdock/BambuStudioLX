# Expat security backports

The bundled source is Expat 2.9.0 with the merged upstream fixes below. At the
time of this change, upstream had not released 2.9.1.

| Advisory | Upstream fix | Purpose |
| --- | --- | --- |
| CVE-2026-113351 | [PR 1431](https://github.com/libexpat/libexpat/pull/1431) | Account for retained allocations after parser reset and preserve configured protections. |
| CVE-2026-115506 | [PR 1433](https://github.com/libexpat/libexpat/pull/1433) | Reject excessive entity expansion before performing the expansion work. |

`security-patches.json` records upstream merge commits, patch hashes, adaptation
notes, and the SHA-256 of the complete bundled source tree. The patches in
`patches/` preserve the original 2.9.0 integer-overflow guards and adapt debug
formatting/pointer subtraction to its older internal types. They can be applied
in manifest order from the repository root with `git apply --recount`.

`scripts/security/generate_dependency_sbom.py` checks the exact source and patch
hashes before emitting an OpenVEX `fixed` statement for these two backports.
Grype retains the advisory matches as repaired findings rather than pretending
the source is an unmodified new release. The vulnerable-version canary runs
without VEX and must still detect the three expected advisories.

When updating Expat, review whether the new release contains these fixes, rerun
the parser-reset and early-amplification regressions, then update or remove the
backport evidence and VEX generation. Do not regenerate hashes to silence a
scanner finding without reviewing the corresponding source changes.
