# AV build comparison

Fork-only investigation of v0.9.14, source commit
`9fc7aa2cb9a06ab7ec47a1a6b217bb3b652cf52e`. The harness and release source use
separate checkouts. Emulator source, release version, optimization, netplay,
updater, cheats, and stripping remain identical. No build cache is restored.
The workflow publishes diagnostic artifacts, not releases.

## Completed comparison

Run: https://github.com/nickthename/RMG-K/actions/runs/37978187008

| Environment | GCC / Qt | SHA-256 | VirusTotal on October 9, 2026 |
| --- | --- | --- | --- |
| Current release recipe | 16.2.0-4 / 6.11.2 | `68bb318713a05c2f13b8ded1450c5aa3a9404fb0617da4f90d67e6e64be45cc1` | 8/71 |
| Local UCRT64 package set on Actions | 15.2.0-13 / 6.11.0 | `9e02fcd389780928eb8f7a2375f9e04cc9822a33b838e712f742faa161f3aea2` | 0/69; Google and Zillya failed |

All eight vendors flagging the current recipe returned Undetected for the pinned
sample: ALYac, Arcabit, BitDefender, CTX, Emsisoft, eScan, GData, and VIPRE.
The current detections were Yogi variants. This implicates an environment
change but does not establish which package caused it or prove file safety.

Other reference samples:

- Official v0.9.14 (25/71): `bd2cc5876e81a35de0750d7c168f0a4b2f58d1aa0350491a41b113098a67bfda`
- Local build of identical source (0/71): `019cb4f7789518f62c2d2ca59b8cb133e592ea16eca929d61c165a5d863f04be`
- Release provenance: https://github.com/Jay-Day/RMG-K/actions/runs/33579212384

## Isolation matrix

Every job starts with the same 86-package baseline in `local-ucrt64.tsv`.
A named overlay replaces only its matching package names and adds any required
split packages. All archives use exact versions and SHA-256 checks. The actual
installed UCRT64 inventory must equal the effective lock file.

| Job | Only change from baseline |
| --- | --- |
| local-ucrt64 | None; repeated control |
| gcc16 (disabled) | Incompatible with the old C++ dependencies; no valid AV result |
| binutils247 | Binutils 2.47-4 (linker, assembler, resource compiler, strip) |
| mingw-runtime | MinGW headers, CRT, winpthreads, libwinpthread r426 |
| manifest | Windows default manifest 20260815-1 |

Compiler runtimes stay together. Other libraries, including Qt, retain the
baseline versions. These are diagnostic package combinations, not a release
configuration until their runtime compatibility has been tested. Pacman resolves
normal dependencies; the harness never disables dependency checks.

Overlay versions match the first current-recipe run. GCC archives and hashes
were fetched from the official MSYS2 repository; other overlay hashes came from
its `ucrt64.db` on October 9. GCC subsequently advanced to 16.2.0-5, so the
experiment explicitly retains 16.2.0-4. Archived package URLs may expire; the
job fails rather than substituting versions.

## Running and examining results

Push the harness on `av-test` in nickthename/RMG-K. Its fork guard prevents use
elsewhere. The push starts four Windows 2025 jobs. `workflow_dispatch` is also
available once GitHub recognizes the workflow on the default branch. No changes
to master are required for the push trigger.

Each job uploads normal stripped and unstripped EXEs, a portable folder, and
build/environment diagnostics. Scan the normal stripped EXE first. Preserve its
hash, scan time, vendor names, effective package lock, and runner image. Compare
against the repeated baseline before attributing a change to an overlay.
Diagnostics include the clean source status, package inventory, PE headers and
sections, symbols, and link command. No local toolchain packages are changed.

The workflow does not submit to VirusTotal automatically. A clean result for one
hash cannot guarantee the next release will remain clean. If multiple individual
overlays stay clean, test interactions or the remaining Qt/dependency changes.

The GCC-only test failed before configuration: GCC 16 removes emulated TLS
exports required by the older CMake and other C++ dependencies. See
https://www.msys2.org/news/#2026-05-11-native-thread-local-storage-tls-with-gcc-16.
It cannot isolate compiler code generation without a compatible dependency set.

The current-recipe unstripped executable also scored 8/71 (Lazy variants, same
eight vendors), so retaining its symbols did not resolve detection. SHA-256:
`ca028b63ded98a6ee521e594eada6ba7e0d2f1f819020ecdc7654bc7e5a33a84`.
