#!/bin/bash

export OS_TARGET=$(fpc -iTO)
export CPU_TARGET=$(fpc -iTP)

export ARCH=$CPU_TARGET-$OS_TARGET

cd $(dirname "$0")

rm -rf release
mkdir -p release/wcx/diskdir
mkdir -p release/wdx/crx_wdx
mkdir -p release/wdx/exif
mkdir -p release/wdx/ooinfo
mkdir -p release/wdx/ooxml
mkdir -p release/wdx/mediainfo
mkdir -p release/wdx/translitwdx
mkdir -p release/wdx/similarity
mkdir -p release/wdx/xpi_wdx
mkdir -p release/wfx/gvfs
mkdir -p release/wfx/rclone
mkdir -p release/wlx/gstplayer
mkdir -p release/wlx/fileinfo

make -C wcx/diskdir/src clean all
install -m 644 wcx/diskdir/diskdir.wcx release/wcx/diskdir/
install -m 644 wcx/diskdir/*.txt       release/wcx/diskdir/

make -C wdx/crx_wdx clean all
install -m 644 wdx/crx_wdx/crx_wdx.wdx release/wdx/crx_wdx/

make -C wdx/exif clean all
install -m 644 wdx/exif/exif.wdx release/wdx/exif/
install -m 644 wdx/exif/exif.lng release/wdx/exif/
install -m 644 wdx/exif/*.txt    release/wdx/exif/

make -C  wdx/ooinfo/src clean all
install -m 644 wdx/ooinfo/ooinfo.wdx release/wdx/ooinfo/
install -m 644 wdx/ooinfo/ooinfo.lng release/wdx/ooinfo/
install -m 644 wdx/ooinfo/*.txt      release/wdx/ooinfo/

make -C wdx/ooxml/src clean all
install -m 644 wdx/ooxml/ooxml.wdx release/wdx/ooxml/
install -m 644 wdx/ooxml/*.txt     release/wdx/ooxml/

make -C wdx/similarity/src clean all
install -m 644 wdx/similarity/similarity.wdx  release/wdx/similarity/
install -m 644 wdx/similarity/similarity.ini  release/wdx/similarity/
install -m 644 wdx/similarity/readme.txt      release/wdx/similarity/

make -C  wdx/xpi_wdx/src clean all
install -m 644 wdx/xpi_wdx/xpi_wdx.wdx release/wdx/xpi_wdx/

make -C wfx/gvfs/src clean all
install -m 644 wfx/gvfs/gvfs.wfx release/wfx/gvfs/

lazbuild --build-mode=Release wfx/rclone/src/rclone.lpi
install -m 644 wfx/rclone/rclone.wfx     release/wfx/rclone/
install -m 644 wfx/rclone/src/rclone.ico release/wfx/rclone/
install -m 644 wfx/rclone/COPYING.txt    release/wfx/rclone/

make -C wlx/gstplayer/src clean all
install -m 644 wlx/gstplayer/gstplayer.wlx release/wlx/gstplayer/
install -m 644 wlx/gstplayer/readme.txt    release/wlx/gstplayer/

wlx/fileinfo/build.sh
install -m 644 wlx/fileinfo/fileinfo.wlx* release/wlx/fileinfo/
install -m 755 wlx/fileinfo/fileinfo.sh   release/wlx/fileinfo/
install -m 644 wlx/fileinfo/*.txt         release/wlx/fileinfo/

install -m 644 wdx/mediainfo/luajit/*.lua      release/wdx/mediainfo/
install -m 644 wdx/translitwdx/translitwdx.lua release/wdx/translitwdx/
install -m 644 wdx/translitwdx/readme.txt      release/wdx/translitwdx/

# --- CMake-based WLX plugins (GTK3 and/or Qt6, each built as far as its
# available toolkit dependencies allow -- each plugin's own CMakeLists
# defaults ENABLE_GTK3/ENABLE_QT6 to ON and silently skips a toolkit target
# whose dependencies aren't present, hence the "[ -f ... ] &&" guards below
# rather than assuming both always got built) ---

# csvview: Qt6 required, GTK3 optional
mkdir -p release/wlx/csvview
mkdir -p wlx/csvview/build
(cd wlx/csvview/build && cmake .. && make)
install -m 644 wlx/csvview/build/csvview_qt6.wlx release/wlx/csvview/
[ -f wlx/csvview/build/csvview_gtk3.wlx ] && install -m 644 wlx/csvview/build/csvview_gtk3.wlx release/wlx/csvview/
cp -r wlx/csvview/langs release/wlx/csvview/
install -m 644 wlx/csvview/*.md release/wlx/csvview/
install -m 644 wlx/csvview/*.png release/wlx/csvview/

# dbview: Qt6 required, GTK3 optional
mkdir -p release/wlx/dbview
mkdir -p wlx/dbview/build
(cd wlx/dbview/build && cmake .. && make)
install -m 644 wlx/dbview/build/dbview_qt6.wlx release/wlx/dbview/
[ -f wlx/dbview/build/dbview_gtk3.wlx ] && install -m 644 wlx/dbview/build/dbview_gtk3.wlx release/wlx/dbview/
install -m 644 wlx/dbview/*.md release/wlx/dbview/
install -m 644 wlx/dbview/*.png release/wlx/dbview/

# structview: Qt6 required, GTK3 optional
mkdir -p release/wlx/structview
mkdir -p wlx/structview/build
(cd wlx/structview/build && cmake .. && make)
install -m 644 wlx/structview/build/structview_qt6.wlx release/wlx/structview/
[ -f wlx/structview/build/structview_gtk3.wlx ] && install -m 644 wlx/structview/build/structview_gtk3.wlx release/wlx/structview/
install -m 644 wlx/structview/*.md release/wlx/structview/
install -m 644 wlx/structview/*.png release/wlx/structview/

# certview: Qt6 required, GTK3 optional
mkdir -p release/wlx/certview
mkdir -p wlx/certview/build
(cd wlx/certview/build && cmake .. && make)
install -m 644 wlx/certview/build/certview_qt6.wlx release/wlx/certview/
[ -f wlx/certview/build/certview_gtk3.wlx ] && install -m 644 wlx/certview/build/certview_gtk3.wlx release/wlx/certview/
install -m 644 wlx/certview/*.md release/wlx/certview/

# sourceview: GTK3 only, no Qt6 variant exists
mkdir -p release/wlx/sourceview
mkdir -p wlx/sourceview/build
(cd wlx/sourceview/build && cmake .. && make)
install -m 644 wlx/sourceview/build/sourceview_gtk3.wlx release/wlx/sourceview/
install -m 644 wlx/sourceview/*.md release/wlx/sourceview/

# htmlview: Qt6/QtWebEngine and GTK3/WebKitGTK, both optional. Tolerant of
# failure for the same reason as kpartview below -- qt6-webengine and
# webkit2gtk are large optional packages, not present on every
# distro/runner, and neither edition is required for the other to build.
mkdir -p release/wlx/htmlview
mkdir -p wlx/htmlview/build
(cd wlx/htmlview/build && cmake .. && make) || echo "htmlview build failed (likely missing Qt6 WebEngineWidgets or webkit2gtk-4.1) -- skipping"
[ -f wlx/htmlview/build/htmlview_qt6.wlx ] && install -m 644 wlx/htmlview/build/htmlview_qt6.wlx release/wlx/htmlview/
[ -f wlx/htmlview/build/htmlview_gtk3.wlx ] && install -m 644 wlx/htmlview/build/htmlview_gtk3.wlx release/wlx/htmlview/
install -m 644 wlx/htmlview/*.md release/wlx/htmlview/

# kpartview: Qt6/KDE Frameworks 6 only (KParts has no GTK3 equivalent).
# Tolerant of failure: KF6 packaging availability varies by distro/runner,
# and this is the only plugin that depends on it besides kate.
mkdir -p release/wlx/kpartview
mkdir -p wlx/kpartview/build
(cd wlx/kpartview/build && cmake .. && make) || echo "kpartview build failed (likely missing KDE Frameworks 6) -- skipping"
[ -f wlx/kpartview/build/kpartview_host_qt6.wlx ] && install -m 644 wlx/kpartview/build/kpartview_host_qt6.wlx release/wlx/kpartview/
install -m 644 wlx/kpartview/*.md release/wlx/kpartview/
install -m 644 wlx/kpartview/*.png release/wlx/kpartview/

# officeview: Qt6 required, GTK3 optional
mkdir -p release/wlx/officeview
mkdir -p wlx/officeview/build
(cd wlx/officeview/build && cmake .. && make)
install -m 644 wlx/officeview/build/officeview_qt6.wlx release/wlx/officeview/
[ -f wlx/officeview/build/officeview_gtk3.wlx ] && install -m 644 wlx/officeview/build/officeview_gtk3.wlx release/wlx/officeview/
install -m 644 wlx/officeview/*.md release/wlx/officeview/

# logview: Qt6 required, GTK3 optional
mkdir -p release/wlx/logview
mkdir -p wlx/logview/build
(cd wlx/logview/build && cmake .. && make)
install -m 644 wlx/logview/build/logviewer.wlx release/wlx/logview/
[ -f wlx/logview/build/logviewer_gtk3.wlx ] && install -m 644 wlx/logview/build/logviewer_gtk3.wlx release/wlx/logview/
install -m 644 wlx/logview/*.md release/wlx/logview/
install -m 644 wlx/logview/*.png release/wlx/logview/

# mpv_wayland: Qt6 required, GTK3 optional
mkdir -p release/wlx/mpv_wayland
mkdir -p wlx/mpv_wayland/build
(cd wlx/mpv_wayland/build && cmake .. && make)
install -m 644 wlx/mpv_wayland/build/mpv_wayland_qt6.wlx release/wlx/mpv_wayland/
[ -f wlx/mpv_wayland/build/mpv_wayland_gtk3.wlx ] && install -m 644 wlx/mpv_wayland/build/mpv_wayland_gtk3.wlx release/wlx/mpv_wayland/
install -m 644 wlx/mpv_wayland/*.md release/wlx/mpv_wayland/
install -m 644 wlx/mpv_wayland/*.png release/wlx/mpv_wayland/

# kate: Qt6/KDE Frameworks 6 only (KTextEditor has no GTK3 equivalent).
# Tolerant of failure, same reasoning as kpartview above.
mkdir -p release/wlx/kate
mkdir -p wlx/kate/build
(cd wlx/kate/build && cmake .. && make) || echo "kate build failed (likely missing KDE Frameworks 6) -- skipping"
[ -f wlx/kate/build/kate_qt6.wlx ] && install -m 644 wlx/kate/build/kate_qt6.wlx release/wlx/kate/
install -m 644 wlx/kate/*.md release/wlx/kate/
install -m 644 wlx/kate/*.png release/wlx/kate/

# diagramview: Qt6 required, GTK3 optional
mkdir -p release/wlx/diagramview
mkdir -p wlx/diagramview/build
(cd wlx/diagramview/build && cmake .. && make)
install -m 644 wlx/diagramview/build/diagramview_qt6.wlx release/wlx/diagramview/
[ -f wlx/diagramview/build/diagramview_gtk3.wlx ] && install -m 644 wlx/diagramview/build/diagramview_gtk3.wlx release/wlx/diagramview/
install -m 644 wlx/diagramview/config.json release/wlx/diagramview/
install -m 644 wlx/diagramview/*.md release/wlx/diagramview/
install -m 644 wlx/diagramview/*.png release/wlx/diagramview/

# mdk: Qt6 required, GTK3 optional
mkdir -p release/wlx/mdk
mkdir -p wlx/mdk/build
(cd wlx/mdk/build && cmake .. && make)
install -m 644 wlx/mdk/build/mdk_qt6.wlx release/wlx/mdk/
[ -f wlx/mdk/build/mdk_gtk3.wlx ] && install -m 644 wlx/mdk/build/mdk_gtk3.wlx release/wlx/mdk/
install -m 644 wlx/mdk/*.md release/wlx/mdk/

# markdownview ships in THREE variants, from the same sources, differing
# only in two CMake options. They get separate release directories so the
# zip loop below emits one download each:
#
#   markdownview           light   -- everything rendered remotely,   ~10MB
#   markdownview-vlcharts  charts  -- + local vl-convert figures,     ~94MB
#   markdownview-offline   offline -- + local Mermaid/PlantUML,      ~103MB
#
# The .wlx files inside are deliberately named the same in all three, so a
# user can swap variants by replacing the file and keeps their settings
# (all read the same ini and self-seeded CSS). They are alternatives, not
# co-installable -- that is why one name is correct rather than a clash.
#
# None ships a top-level markdownview.css: each self-seeds a correct
# default next to its own ini on first run if none is found (see
# src/core/markdown_engine.cpp), so a possibly-stale repo copy isn't
# needed and would just be a second, inconsistent source of truth.
build_markdownview_variant() {
  variant_dir="$1"      # release/ subdirectory to populate
  vlconvert="$2"        # ENABLE_VLCONVERT: ON or OFF
  local_diagrams="$3"   # ENABLE_LOCAL_DIAGRAMS: ON or OFF
  # Keyed on the variant name, not on the option values -- vlcharts and
  # offline both build with ENABLE_VLCONVERT=ON, so keying on that would
  # make them share (and clobber) one build directory.
  build_dir="wlx/markdownview/build-${variant_dir}"

  mkdir -p "release/wlx/${variant_dir}" "$build_dir"
  (cd "$build_dir" && cmake -DENABLE_VLCONVERT="$vlconvert" \
                            -DENABLE_LOCAL_DIAGRAMS="$local_diagrams" ../ && make)
  install -m 644 "${build_dir}/markdownview_qt6.wlx" "release/wlx/${variant_dir}/"
  [ -f "${build_dir}/markdownview_gtk3.wlx" ] \
    && install -m 644 "${build_dir}/markdownview_gtk3.wlx" "release/wlx/${variant_dir}/"
}

build_markdownview_variant markdownview          OFF OFF
build_markdownview_variant markdownview-vlcharts ON  OFF

# The offline variant needs a vendored Rust archive that this build does not
# produce (it needs a Rust toolchain; see wlx/markdownview/3rdparty/supramark/
# README.md). Skipped rather than fatal when it's absent, so a checkout
# without it still produces the other two downloads.
if [ -f wlx/markdownview/3rdparty/supramark/libmarkdownview_diagrams.a ]; then
  build_markdownview_variant markdownview-offline ON ON
else
  echo "build.sh: skipping markdownview-offline -- 3rdparty/supramark/libmarkdownview_diagrams.a not present"
fi

# Zip each plugin individually (rather than one big tarball) so a user only
# has to download the plugin(s) they actually want -- keeps free-tier GitHub
# release download quotas from being burned on unrelated plugins. READMEs and
# screenshots stay in release/ (for anyone browsing the build output) but are
# excluded from the zip itself -- they're documentation, not runtime files,
# and only add dead weight to the download.
pushd release
for category_dir in */; do
  for plugin_dir in "$category_dir"*/; do
    plugin_dir=${plugin_dir%/}
    plugin_name=$(basename "$plugin_dir")
    (cd "$plugin_dir" && zip -rq "../../../${plugin_name}-$(date +%y.%m.%d)-$ARCH.zip" . \
      -x "*.md" "*.png" "*.jpg" "*.jpeg" "*.gif")
  done
done
popd
