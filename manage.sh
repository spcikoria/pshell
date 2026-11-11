#!/bin/bash
#
# Build manage script
#

# exit on errors
set -e

BUILD_DIR="build"
DEBUG_DIR="$BUILD_DIR/debug"
RELEASE_DIR="$BUILD_DIR/release"
CROSS_FILE="cross/arm-none-eabi.txt"
# Folder in bsp/, e.g. BOARD=nucleo_h503rb ./manage.sh init
BOARD="${BOARD:-nucleo_h503rb}"

usage() {
    echo
    echo "Usage: $0 <command>"
    echo
    echo " Commands:"
    echo "=========="
    echo " init                                     Initialize build environment"
    echo " clean                                    Clean out all old binaries and other"
    echo " release                                  Build software in release mode"
    echo " debug                                    Build software in debug mode"
}

check_init() {
    if [ ! -d "$DEBUG_DIR" ] || [ ! -d "$RELEASE_DIR" ]; then
        echo "Error: Project is not initialized! Please run 'manage.sh init' first." >&2
        exit 1
    fi
}

arg="$1"
if [[ -z $arg ]] ; then
    usage
    exit 1
fi

case $arg in
    init)
        echo "Initializing BuildEnv..."
        meson setup $DEBUG_DIR --buildtype=debug --cross-file $CROSS_FILE -Dboard=$BOARD
        # minsize: -Os with debug info in the ELF only (not in the .bin), smaller than -O3 for the 128 KB flash
        meson setup $RELEASE_DIR --buildtype=minsize --cross-file $CROSS_FILE -Dboard=$BOARD
        ;;
    clean)
        echo "Cleaning up..."
        rm -rf $BUILD_DIR
        ;;
    release)
        check_init
        echo "Building release..."
        meson compile -C $RELEASE_DIR
        ;;
    debug)
        check_init
        echo "Building debug..."
        meson compile -C $DEBUG_DIR
        ;;
    *)
        echo "Unknown argument: ${arg}" >&2
        usage
        exit 1
        ;;
esac
