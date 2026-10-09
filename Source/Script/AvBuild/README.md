# AV build comparison

This fork-only workflow builds release v0.9.14 (commit
`9fc7aa2cb9a06ab7ec47a1a6b217bb3b652cf52e`) twice without changing emulator source.
The separate source checkout ensures the test harness commit does not change the
application version or source under test. Both jobs enable netplay, the updater,
angrylion, and cheats, use Release optimization, and strip installed executables.
No compiler/build cache is restored. The workflow does not create releases, tags,
commits, or VirusTotal submissions.

## Run on nickthename/RMG-K

1. Create a branch named `av-test` in this worktree.
2. Commit only `.github/workflows/av-build.yml` and `Source/Script/AvBuild/`.
3. Push `av-test` to `origin` (nickthename/RMG-K). The push starts the workflow.
4. Open Actions -> AV build comparison on the fork. Download both `executables`
   artifacts and scan `stripped/RMG-K.exe` from each first. Record the SHA-256,
   detection count, detection names, and scan time.
5. Keep the corresponding diagnostics artifacts. Repeat with "Re-run all jobs"
   after the first comparison, without changing the source or harness.

`workflow_dispatch` is also declared, but GitHub requires the workflow to exist on
the default branch before manual dispatch is available. The `av-test` push trigger
avoids needing to change master for this experiment. Pushes to this branch run two
Windows jobs; the existing build workflow's push trigger only matches version tags.
The job guard restricts this harness to nickthename/RMG-K.

## Environments

- **current-release-recipe** installs current MSYS2 packages using the Windows
  release dependency list, including its minizip 1.3.1 exception. This is today's
  recipe, not a reconstruction of the September release environment. Inspect the
  recorded package versions; do not assume it still installs GCC 16.2.0.
- **local-ucrt64** installs all 86 UCRT64 packages recorded from the local build
  environment that produced the 0/71 sample on October 9, 2026. Versions and SHA-256
  hashes come from installed package metadata and the local pacman archive cache.
  Archives are fetched from the official MSYS2 server, verified against the lock
  file, and installed in one transaction into a fresh UCRT64 environment. Exact
  installed versions are checked afterward. GCC is 15.2.0-13 and Qt is 6.11.0.

The Windows runner and MSYS host tools are common to both jobs and are not pinned
to the local Windows installation. This reproduces the UCRT64 compiler/dependency
set, not the entire local machine. Old MSYS2 archive URLs can eventually expire;
the job fails instead of silently substituting a package. No changes are made to
the developer's local toolchain.

## Artifacts and interpretation

Each environment uploads a portable build, executables for scanning, and
build/environment diagnostics. The executable artifact contains both the normal
stripped executable and its unstripped counterpart from the same link step. Start
with the stripped pair; use the unstripped files only as a secondary comparison.
These are scan samples; use the portable artifact for runtime testing.

| Current recipe | Local package set on GitHub | Interpretation / next step |
| --- | --- | --- |
| Flagged | Clean | Environment difference reproduced on GitHub. Preserve this package set; isolate compatible toolchain/dependency changes next. |
| Clean | Clean | Today's release recipe also produces a clean sample. Repeat, compare versions with the September build, and investigate the old environment/artifact specifically. |
| Flagged | Flagged | Local UCRT64 versions alone do not reproduce the local result. Compare runner, host tools, flags, paths, and full detection reports. |
| Clean | Flagged | The pinned package set does not help on this runner. Repeat and compare diagnostics before drawing a toolchain conclusion. |

A single clean hash does not guarantee future builds will remain clean, and a
change in detections does not identify GCC alone. Do not downgrade only GCC while
leaving potentially incompatible newer runtime libraries in place. Preserve
package inventories for repeats because the current recipe can change over time.

Known reference hashes:

- Official v0.9.14 EXE (25/71): `bd2cc5876e81a35de0750d7c168f0a4b2f58d1aa0350491a41b113098a67bfda`
- Same source built locally (0/71): `019cb4f7789518f62c2d2ca59b8cb133e592ea16eca929d61c165a5d863f04be`

Release provenance: https://github.com/Jay-Day/RMG-K/actions/runs/33579212384
