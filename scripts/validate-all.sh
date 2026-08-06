#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
ksoloti_repo="${KSOLOTI_REPO:-/Users/lanceship/Projects/ksoloti}"

if [[ ! -f "$ksoloti_repo/firmware/axoloti_defines.h" ||
      ! -f "$ksoloti_repo/firmware/axoloti_memory.h" ]]; then
  echo "Ksoloti firmware headers not found under: $ksoloti_repo" >&2
  echo "Set KSOLOTI_REPO to a complete Ksoloti checkout." >&2
  exit 1
fi

command -v xmllint >/dev/null
command -v clang++ >/dev/null

validated=0
shopt -s nullglob

for project_dir in "$repo_root"/projects/*-gills; do
  patch_files=("$project_dir"/*.axp)
  object_files=("$project_dir"/*.axo)
  header_files=("$project_dir"/*.h)
  entry_headers=()

  if [[ ${#patch_files[@]} -eq 0 || ${#object_files[@]} -eq 0 ||
        ${#header_files[@]} -eq 0 ]]; then
    echo "Incomplete instrument project: $project_dir" >&2
    exit 1
  fi

  xmllint --noout "${patch_files[@]}" "${object_files[@]}"

  for object_file in "${object_files[@]}"; do
    while IFS= read -r include_path; do
      if [[ "$include_path" == ./* ]]; then
        resolved="$project_dir/${include_path#./}"
        if [[ ! -f "$resolved" ]]; then
          echo "Missing local include: $include_path (from $object_file)" >&2
          exit 1
        fi
        if [[ "$resolved" == *.h ]]; then
          entry_headers+=("$resolved")
        fi
      fi
    done < <(sed -n 's|.*<include>\([^<]*\)</include>.*|\1|p' "$object_file")
  done

  if [[ ${#entry_headers[@]} -eq 0 ]]; then
    entry_headers=("${header_files[@]}")
  fi

  for header_file in "${entry_headers[@]}"; do
    clang++ -std=gnu++11 -fsyntax-only -x c++ \
      -Wno-asm-operand-widths \
      -include cstddef \
      -include "$ksoloti_repo/firmware/axoloti_defines.h" \
      -include "$ksoloti_repo/firmware/axoloti_memory.h" \
      -I"$ksoloti_repo/firmware" \
      -I"$ksoloti_repo/firmware/mutable_instruments" \
      "$header_file"
  done

  validated=$((validated + 1))
  printf 'validated %s\n' "$(basename "$project_dir")"
done

printf 'validated %d instrument projects\n' "$validated"
