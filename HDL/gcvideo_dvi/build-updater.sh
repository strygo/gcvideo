#!/bin/bash

set -e

./build-all.sh

VERSION=`make printversion`
UPDATER_GC=../../Updater/obj-gc/gcvupdater.dol
UPDATER_WII=../../Updater/obj-wii/gcvupdater.dol

if [ ! -e $UPDATER_GC ] || [ ! -e $UPDATER_WII ]; then
    pushd ../../Updater
    make TARGET=gc
    make TARGET=wii
    popd
fi

HBCDIR=build/GCVideo-Updater-$VERSION
mkdir $HBCDIR

./scripts/buildupdate.pl $UPDATER_WII $HBCDIR/boot.dol \
                         build/main-dual-wii/toplevel_wiidual.tagmain

./scripts/xmlgen.pl $VERSION $HBCDIR/meta.xml
cd build
zip -9Xr ../binaries/updater-$VERSION-wii.zip GCVideo-Updater-$VERSION
