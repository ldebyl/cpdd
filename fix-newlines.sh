#!/bin/bash

# fix-newlines.sh - Add trailing newlines to source files per POSIX standard
# Usage: ./fix-newlines.sh

echo "Adding trailing newlines to source files..."

fixed=0
total=0

# Find all C source and header files
find . -name "*.c" -o -name "*.h" | while read -r file; do
    total=$((total + 1))
    
    # Check if file ends with newline
    if [[ -s "$file" ]] && [[ "$(tail -c1 "$file" 2>/dev/null)" != "" ]]; then
        echo "Fixing: $file"
        echo "" >> "$file"
        fixed=$((fixed + 1))
    fi
done

echo "Done. Fixed $fixed files out of $total total source files."