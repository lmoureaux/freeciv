#! /usr/bin/env bash

# How to use this script:
#
# * Configure freeciv with --prefix=
# * Create directory bootstrap/android/build/
# * Run make
# * Run make install DESTDIR=$PWD/bootstrap/android/
# * cd bootstrap/android/
# * Make a copy this script and fill the configuration below
# * Fill paths in androiddeployqt-config.json
# * Run without arguments
# 
# The apk is created in $PKGDIR/build/outputs/apk/debug/build-debug.apk

# Tools info

QT_ROOT=/path/to/qt                   # Path to Qt for Android
ADQT=$QT_ROOT/bin/androiddeployqt     # Path to the androiddeployqt tool
JDK=/usr/lib/jvm/java-8-openjdk-amd64 # Path to a native JVM installation (Java 8 required)

# Build info

ARCH=x86_64         # The architecture you are compiling for (only one is supported by the script)
PLATFORM=android-29 # The Android platform to build for

# Package paths (note: changing these is untested)

PKGDIR=$PWD/build # Change the build directory
FC_DESTDIR=$PWD   # Must match the argument supplied to make install

# == End of configuration

# Create structure and copy files
mkdir -p $PKGDIR/assets/data $PKGDIR/libs/$ARCH
cp -r $FC_DESTDIR/freeciv/* $PKGDIR/assets/data  # Freeciv looks for files in "data" (without /) first. This is the only path without a / (required for Android assets)
cp $FC_DESTDIR/lib/*.so $PKGDIR/libs/$ARCH # More architectures can probably be added by running this script several times with different freeciv

# Create apk
$ADQT --output $PKGDIR --input $PWD/androiddeployqt-settings.json --android-platform $PLATFORM --jdk $JDK --verbose --gradle --no-strip

ls -lh $PKGDIR/build/outputs/apk/debug/$(basename $PKGDIR)-debug.apk

