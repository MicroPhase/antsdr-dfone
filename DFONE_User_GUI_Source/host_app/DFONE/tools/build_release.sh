#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "${script_dir}/.." && pwd)"

cmake -S "${project_dir}" -B "${project_dir}/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "${project_dir}/build" -j
