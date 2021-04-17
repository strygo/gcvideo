#!/bin/bash

set -e

function build () {
    local TARGET="$1"

    make TARGET=$TARGET

    zip -9jX binaries/gcvideo-$VERSION-$TARGET.zip \
        build/gcvideo-dvi-$TARGET-$VERSION-M25P40-complete.xsvf \
        build/gcvideo-dvi-$TARGET-$VERSION-spirom-complete.bin \
        build/gcvideo-dvi-$TARGET-$VERSION-spirom-impact.cfi \
        build/gcvideo-dvi-$TARGET-$VERSION-spirom-impact.mcs
}

if [ ! -d codegens ]; then
    echo "You seem to be running this from the wrong directory."
    exit 1
fi

rm -rf build

VERSION=`make printversion`

mkdir -p binaries

build dual-wii
