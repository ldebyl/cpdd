#!/bin/sh
#
# Install script for cpdd
#

set -e

PLATFORM=$(uname | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)

# Default install paths
BIN_DIR="${PREFIX:-/usr/local}/bin"
MAN_DIR="${PREFIX:-/usr/local}/share/man/man1"

echo "Installing cpdd for $PLATFORM-$ARCH..."

# Check if we're root or have sudo
if [ "$(id -u)" != "0" ]; then
    if command -v sudo >/dev/null; then
        SUDO="sudo"
    else
        echo "Error: Need root privileges or sudo to install to $BIN_DIR"
        echo "Try: PREFIX=\$HOME/.local $0"
        exit 1
    fi
fi

# Install binaries
echo "Installing binaries to $BIN_DIR..."
$SUDO mkdir -p "$BIN_DIR"
$SUDO cp cpdd syndir "$BIN_DIR/"
$SUDO chmod 755 "$BIN_DIR/cpdd" "$BIN_DIR/syndir"

# Install man pages
if [ -d "man" ]; then
    echo "Installing man pages to $MAN_DIR..."
    $SUDO mkdir -p "$MAN_DIR"
    $SUDO cp man/*.1 "$MAN_DIR/"
    $SUDO chmod 644 "$MAN_DIR"/cpdd.1 "$MAN_DIR"/syndir.1
fi

echo "Installation complete!"
echo "Try: cpdd --help"