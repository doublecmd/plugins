#!/bin/sh
curdir=$(cd "$(dirname "$0")"; pwd)
cd ${curdir}
tmp=`uname -a | grep "x86_64"`
echo $arch
if [ -n "$tmp" ]; then arch="64"; fi
rm -rf fileinfo.wlx*
gcc -shared -o fileinfo.wlx$arch fileinfo.c `pkg-config --cflags --libs gtk+-2.0` -fPIC
upx -9 fileinfo.wlx$arch

