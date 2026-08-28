#!/bin/bash
# Builds the GitHub Release description: one row per plugin actually
# packaged by build.sh, with its toolkit(s) and a link to its README.md
# (pinned to this release's tag so the link doesn't drift as the plugin
# changes on master later) when one exists.
#
# Usage: generate_release_notes.sh <repo "owner/name"> <tag> > notes.md

set -euo pipefail
REPO="$1"
TAG="$2"

readme_link() {
    local path="$1"
    if [ -f "$path/README.md" ]; then
        echo "[README](https://github.com/$REPO/blob/$TAG/$path/README.md)"
    else
        echo "—"
    fi
}

row() {
    local path="$1" toolkit="$2" description="$3"
    printf '| %s | %s | %s | %s |\n' "$(basename "$path")" "$toolkit" "$description" "$(readme_link "$path")"
}

echo "## Plugins in this release"
echo
echo "| Plugin | Toolkit | What it does | Docs |"
echo "|---|---|---|---|"
row wcx/diskdir       "Lazarus/FPC"      "Disk usage/free-space info column"
row wdx/crx_wdx       "Rust"             "Chrome extension (.crx) metadata"
row wdx/exif          "Lazarus/FPC"      "EXIF metadata for images"
row wdx/ooinfo        "Lazarus/FPC"      "OpenOffice document info"
row wdx/ooxml         "Lazarus/FPC"      "Office Open XML document info"
row wdx/similarity    "Lazarus/FPC"      "File similarity / near-duplicate detection"
row wdx/xpi_wdx       "Lazarus/FPC"      "Firefox extension (.xpi) metadata"
row wdx/mediainfo     "Lua"              "Media file metadata via mediainfo CLI"
row wdx/translitwdx   "Lua"              "Cyrillic transliteration"
row wfx/gvfs          "Lazarus/FPC"      "Browse GVFS network filesystems"
row wfx/rclone        "Lazarus/FPC"      "Browse rclone cloud storage remotes"
row wlx/gstplayer     "GTK2 + GStreamer" "Simple media player in Quick View"
row wlx/fileinfo      "Shell script"     "File info via command-line utilities"
row wlx/csvview       "GTK3 + Qt6"       "CSV/TSV spreadsheet-like grid viewer/editor"
row wlx/dbview        "GTK3 + Qt6"       "Multi-engine DB viewer/editor (SQLite, DuckDB, LMDB, ...)"
row wlx/structview    "GTK3 + Qt6"       "JSON/XML/INI structured text viewer/editor"
row wlx/sourceview    "GTK3 only"        "Text editor with syntax highlighting"
row wlx/kpartview     "Qt6 + KDE Frameworks 6" "Universal KDE KParts host (Okular, etc.)"
row wlx/officeview    "GTK3 + Qt6"       "MS Office / OpenDocument file preview"
row wlx/logview       "GTK3 + Qt6"       "Large log file viewer with search/filter"
row wlx/mpv_wayland   "GTK3 + Qt6"       "Video playback via libmpv"
row wlx/kate          "Qt6 + KDE Frameworks 6" "Text editor with syntax highlighting"
row wlx/diagramview   "GTK3 + Qt6"       "Mermaid/PlantUML diagram viewer"
row wlx/mdk           "GTK3 + Qt6"       "Multimedia preview via MDK SDK"
row wlx/markdownview  "GTK3 + Qt6"       "Rich Markdown preview"
echo
echo "Each plugin is packaged as its own zip below -- grab only the ones you need."
