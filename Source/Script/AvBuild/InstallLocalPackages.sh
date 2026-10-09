#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
lock_file="$script_dir/local-ucrt64.tsv"
cache_dir="$(pwd)/av-packages"
mkdir -p "$cache_dir"

# Overlay only the named package group; keep every other baseline package pinned.
variant="${AV_ENVIRONMENT:-local-ucrt64}"
if [[ "$variant" != "local-ucrt64" ]]; then
    overlay="$script_dir/$variant.tsv"
    [[ -f "$overlay" ]] || { echo "Unknown package variant: $variant" >&2; exit 1; }
    awk -F '\t' '!/^#/ && NF == 4 { rows[$1] = $0 } END { for (name in rows) print rows[name] }' \
        "$lock_file" "$overlay" | LC_ALL=C sort > "$cache_dir/effective.tsv"
    lock_file="$cache_dir/effective.tsv"
fi
mkdir -p results/diagnostics
cp "$lock_file" results/diagnostics/effective-packages.tsv

# Install the entire UCRT64 set together into a fresh environment. Never mix
# older compiler runtimes with preinstalled, newer UCRT64 libraries.
installed_ucrt="$(pacman -Qq | sed -n '/^mingw-w64-ucrt-x86_64-/p')"
if [[ -n "$installed_ucrt" ]]; then
    echo "Expected a fresh MSYS2 installation without UCRT64 packages." >&2
    exit 1
fi

packages=()
expected="$cache_dir/expected.txt"
: > "$expected"
while IFS=$'\t' read -r name version archive checksum; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    curl --fail --location --retry 3 --proto '=https' --proto-redir '=https' \
        "https://repo.msys2.org/mingw/ucrt64/$archive" -o "$cache_dir/$archive"
    (cd "$cache_dir" && printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check -)
    packages+=("$cache_dir/$archive")
    printf '%s %s\n' "$name" "$version" >> "$expected"
done < "$lock_file"

[[ ${#packages[@]} -gt 0 ]]
pacman -U --needed --noconfirm "${packages[@]}"
pacman -Q | sed -n '/^mingw-w64-ucrt-x86_64-/p' | LC_ALL=C sort > "$cache_dir/actual.txt"
LC_ALL=C sort "$expected" -o "$expected"
diff -u "$expected" "$cache_dir/actual.txt"
