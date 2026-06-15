#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "${script_dir}/.." && pwd)"
build_dir="${project_dir}/build-release"

if [[ "$(uname -s)" == "Linux" ]]; then
  exec "${script_dir}/package_appimage.sh"
fi

cmake -S "${project_dir}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}" --config Release --target dfone_user_gui -j

echo "Release binary output:"
find "${build_dir}" -maxdepth 2 \( -name 'dfone_user_gui' -o -name 'dfone_user_gui.exe' \) -print
