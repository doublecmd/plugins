#!/bin/bash

export OS_TARGET=$(fpc -iTO)
export CPU_TARGET=$(fpc -iTP)

export ARCH=$CPU_TARGET-$OS_TARGET

cd $(dirname "$0")

rm -rf release
mkdir -p release/wcx/diskdir
mkdir -p release/wdx/exif
mkdir -p release/wdx/ooinfo
mkdir -p release/wdx/ooxml
mkdir -p release/wdx/translitwdx
mkdir -p release/wdx/similarity
mkdir -p release/wdx/xpi_wdx
mkdir -p release/wfx/gvfs
mkdir -p release/wlx/gstplayer
mkdir -p release/wlx/fileinfo

make -C wcx/diskdir/src clean all
install -m 644 wcx/diskdir/diskdir.wcx release/wcx/diskdir/
install -m 644 wcx/diskdir/*.txt       release/wcx/diskdir/

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
install -m 644 wdx/similarity/similarity.wdx release/wdx/similarity/
install -m 644 wdx/similarity/leven.ini      release/wdx/similarity/
install -m 644 wdx/similarity/readme.txt     release/wdx/similarity/

make -C  wdx/xpi_wdx/src clean all
install -m 644 wdx/xpi_wdx/xpi_wdx.wdx release/wdx/xpi_wdx/

make -C wfx/gvfs/src clean all
install -m 644 wfx/gvfs/gvfs.wfx release/wfx/gvfs/

make -C wlx/gstplayer/src clean all
install -m 644 wlx/gstplayer/gstplayer.wlx release/wlx/gstplayer/
install -m 644 wlx/gstplayer/readme.txt    release/wlx/gstplayer/

wlx/fileinfo/build.sh
install -m 644 wlx/fileinfo/fileinfo.wlx* release/wlx/fileinfo/
install -m 755 wlx/fileinfo/fileinfo.sh   release/wlx/fileinfo/
install -m 644 wlx/fileinfo/*.txt         release/wlx/fileinfo/

install -m 644 wdx/translitwdx/translitwdx.lua release/wdx/translitwdx/
install -m 644 wdx/translitwdx/readme.txt      release/wdx/translitwdx/

pushd release
tar -czpf ../plugins-$(date +%y.%m.%d)-$ARCH.tar.gz *
popd
