#!/usr/bin/env bash
set -euo pipefail

: "${SOURCE_COMMIT:?Missing release commit}"
: "${APP_VERSION:?Missing release version}"
: "${AV_ENVIRONMENT:?Missing environment label}"
workspace="$(pwd)"
src="$workspace/source"
build="$src/Build/Release"
results="$workspace/results"
mkdir -p "$results/diagnostics" "$results/scan/stripped" "$results/scan/unstripped"

actual_commit="$(git -C "$src" rev-parse HEAD)"
if [[ "$actual_commit" != "$SOURCE_COMMIT" ]]; then
    echo "Expected source $SOURCE_COMMIT, found $actual_commit" >&2
    exit 1
fi
git -C "$src" status --porcelain > "$results/diagnostics/source-before.txt"
if [[ -s "$results/diagnostics/source-before.txt" ]]; then
    echo "Release source checkout is not clean:" >&2
    head -30 "$results/diagnostics/source-before.txt" >&2
    exit 1
fi
pacman -Q > "$results/diagnostics/packages-before.txt"
{
    printf 'Source commit: %s\nApp version: %s\nEnvironment: %s\n' \
        "$actual_commit" "$APP_VERSION" "$AV_ENVIRONMENT"
    printf 'Workflow commit: %s\nRun ID: %s\nAttempt: %s\nRunner image: %s %s\n' \
        "$GITHUB_SHA" "$GITHUB_RUN_ID" "$GITHUB_RUN_ATTEMPT" "${ImageOS:-unknown}" "${ImageVersion:-unknown}"
    gcc --version
    ld --version
    cmake --version
    qtpaths6 --qt-version
    uname -a
} > "$results/diagnostics/environment.txt"

{
    cmake -S "$src" -B "$build" -G 'MSYS Makefiles' \
        -DCMAKE_BUILD_TYPE=Release -DPORTABLE_INSTALL=ON \
        -DNETPLAY=ON -DUPDATER=ON -DUSE_ANGRYLION=ON -DINSTALL_CHEATS=ON \
        -DUSE_CCACHE=OFF -DKAILLERA_APP_VERSION_OVERRIDE="$APP_VERSION"
    cmake --build "$build" --parallel "$(nproc)"
    cp "$build/Source/RMG/RMG-K.exe" "$results/scan/unstripped/RMG-K.exe"
    cmake --install "$build" --strip --prefix="$src"
    cmake --build "$build" --target=bundle_dependencies
    touch "$src/Bin/Release/portable.txt"
    cp "$src/Bin/Release/RMG-K.exe" "$results/scan/stripped/RMG-K.exe"
} 2>&1 | tee "$results/diagnostics/build.log"

(cd "$results/scan" && sha256sum stripped/RMG-K.exe unstripped/RMG-K.exe > SHA256SUMS.txt)
objdump -p "$results/scan/stripped/RMG-K.exe" > "$results/diagnostics/pe-headers.txt"
objdump -h "$results/scan/stripped/RMG-K.exe" > "$results/diagnostics/pe-sections.txt"
nm -n -C "$results/scan/unstripped/RMG-K.exe" > "$results/diagnostics/symbols.txt"
cp "$build/Source/RMG/CMakeFiles/RMG.dir/link.txt" "$results/diagnostics/link.txt"
cp "$results/diagnostics/environment.txt" "$results/scan/"
{
    printf '### %s\n\nSource: `%s` (%s)\n\n' "$AV_ENVIRONMENT" "$actual_commit" "$APP_VERSION"
    printf 'Scan `stripped/RMG-K.exe` first. The unstripped file is an optional comparison.\n\n```text\n'
    cat "$results/scan/SHA256SUMS.txt"
    printf '```\n'
} >> "$GITHUB_STEP_SUMMARY"
