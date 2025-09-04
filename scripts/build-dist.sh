#!/bin/bash
#
# build-dist.sh - Build distribution binaries for multiple platforms
#
# Copyright (c) 2025 Lee de Byl <lee@32kb.net>
# Licensed under the MIT License

set -e

VERSION=${1:-$(git describe --tags --always 2>/dev/null || echo "dev")}
DIST_DIR="dist"

echo "🏗️  Building cpdd distribution v$VERSION"

# Clean and create dist directory
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

# Function to build for a platform
build_platform() {
    local platform=$1
    local cc=${2:-cc}
    local strip=${3:-strip}
    local ext=${4:-}
    
    echo "Building for $platform..."
    
    # Clean previous build
    make clean >/dev/null 2>&1
    
    # Build with static linking
    CC="$cc" make static >/dev/null 2>&1
    
    # Create platform directory
    mkdir -p "$DIST_DIR/$platform"
    
    # Copy binaries and strip them
    cp cpdd "$DIST_DIR/$platform/cpdd$ext"
    cp syndir "$DIST_DIR/$platform/syndir$ext"
    "$strip" "$DIST_DIR/$platform/cpdd$ext" 2>/dev/null || true
    "$strip" "$DIST_DIR/$platform/syndir$ext" 2>/dev/null || true
    
    # Copy documentation
    cp README.md "$DIST_DIR/$platform/" 2>/dev/null || echo "Warning: README.md not found"
    cp -r man "$DIST_DIR/$platform/" 2>/dev/null || echo "Warning: man directory not found"
    
    # Create archive
    cd "$DIST_DIR"
    tar czf "cpdd-$VERSION-$platform.tar.gz" "$platform"
    cd ..
    
    echo "✅ Built: $DIST_DIR/cpdd-$VERSION-$platform.tar.gz"
}

# Detect current platform and build
UNAME=$(uname)
ARCH=$(uname -m)

case "$UNAME" in
    Linux)
        PLATFORM="linux-$ARCH"
        build_platform "$PLATFORM" "gcc" "strip"
        ;;
    Darwin)
        PLATFORM="macos-$ARCH"  
        build_platform "$PLATFORM" "clang" "strip"
        ;;
    FreeBSD|OpenBSD|NetBSD)
        PLATFORM="bsd-$ARCH"
        build_platform "$PLATFORM" "cc" "strip"
        ;;
    *)
        PLATFORM="unix-$ARCH"
        build_platform "$PLATFORM" "cc" "strip"
        ;;
esac

echo
echo "📦 Distribution built:"
ls -la "$DIST_DIR"/*.tar.gz

echo
echo "📋 To install:"
echo "  tar xzf cpdd-$VERSION-$PLATFORM.tar.gz"
echo "  sudo cp $PLATFORM/cpdd $PLATFORM/syndir /usr/local/bin/"
echo "  sudo cp $PLATFORM/man/*.1 /usr/local/share/man/man1/"