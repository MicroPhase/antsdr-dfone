#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "${script_dir}/.." && pwd)"
build_dir="${project_dir}/build-appimage"
appdir="${build_dir}/AppDir"
dist_dir="${project_dir}/dist"
desktop_file="${project_dir}/packaging/linux/dfone-user-gui.desktop"
icon_file="${project_dir}/packaging/linux/dfone-user-gui.svg"

linuxdeploy_bin="${LINUXDEPLOY:-$(command -v linuxdeploy || true)}"
if [[ -z "${linuxdeploy_bin}" ]]; then
  cat >&2 <<'EOF'
linuxdeploy was not found.

Install linuxdeploy, or set LINUXDEPLOY=/path/to/linuxdeploy-x86_64.AppImage.
Example:
  chmod +x linuxdeploy-x86_64.AppImage
  LINUXDEPLOY=/path/to/linuxdeploy-x86_64.AppImage host_app/DFONE/user_gui/tools/package_appimage.sh
EOF
  exit 1
fi

cmake -S "${project_dir}" -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DDFONE_USER_BUILD_GUI=ON
cmake --build "${build_dir}" --config Release --target dfone_user_gui -j

cmake -E rm -rf "${appdir}" "${dist_dir}"
cmake -E make_directory "${appdir}/usr/bin" "${appdir}/usr/share/applications" \
  "${appdir}/usr/share/icons/hicolor/scalable/apps" "${dist_dir}"
cmake -E copy "${build_dir}/dfone_user_gui" "${appdir}/usr/bin/dfone_user_gui"
cmake -E copy "${desktop_file}" "${appdir}/usr/share/applications/dfone-user-gui.desktop"
cmake -E copy "${icon_file}" "${appdir}/usr/share/icons/hicolor/scalable/apps/dfone-user-gui.svg"

(cd "${dist_dir}" && \
  OUTPUT="DFONE_User_GUI-x86_64.AppImage" \
  "${linuxdeploy_bin}" \
    --appdir "${appdir}" \
    --executable "${appdir}/usr/bin/dfone_user_gui" \
    --desktop-file "${desktop_file}" \
    --icon-file "${icon_file}" \
    --output appimage)

echo "AppImage output:"
find "${dist_dir}" -maxdepth 1 -name '*.AppImage' -print
