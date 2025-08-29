#!/bin/sh
mkdir -p docs
if command -v groff >/dev/null 2>&1; then
    groff -man -Tascii "$1" | col -bx > "$2"
elif command -v nroff >/dev/null 2>&1; then
    nroff -man "$1" | col -bx > "$2"
elif command -v man >/dev/null 2>&1; then
    MANWIDTH=80 man -l "$1" | col -bx > "$2"
else
    echo "Warning: No man page formatter found. Install man, groff, or nroff to generate text documentation."
    echo "Fallback: copying raw file with .txt extension"
    cp "$1" "$2"
fi