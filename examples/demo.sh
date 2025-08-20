#!/bin/bash

# Demo script showing testgen and cpdd working together

echo "=== CPDD and TestGen Demo ==="
echo

# Clean up any previous runs
echo "Cleaning up previous demo runs..."
rm -rf /tmp/demo_ref /tmp/demo_src /tmp/demo_dest
echo

# Generate test data
echo "1. Generating test data with testgen:"
echo "   - 50 files, 8 directories"
echo "   - 60% of source files will be duplicates of reference files"
echo
./testgen -v -f 50 -d 8 -p 60 /tmp/demo_ref /tmp/demo_src
echo

# Show what was created
echo "2. Test data generated:"
echo "Reference directory:"
find /tmp/demo_ref -type f | wc -l | xargs echo "  Files:"
find /tmp/demo_ref -type d | wc -l | xargs echo "  Directories:"
echo
echo "Source directory:"
find /tmp/demo_src -type f | wc -l | xargs echo "  Files:"
find /tmp/demo_src -type d | wc -l | xargs echo "  Directories:"
echo

# Test cpdd with hard links
echo "3. Running cpdd with hard links (-H):"
echo "   cpdd -r /tmp/demo_ref -H -R -v /tmp/demo_src /tmp/demo_dest"
echo
./cpdd -r /tmp/demo_ref -H -R -v /tmp/demo_src /tmp/demo_dest
echo

# Check results
echo "4. Results analysis:"
total_dest=$(find /tmp/demo_dest -type f | wc -l)
echo "  Files created in destination: $total_dest"

# Count hard links (files with link count > 1)
hard_links=$(find /tmp/demo_dest -type f -links +1 | wc -l)
echo "  Files that are hard links: $hard_links"

# Calculate space savings (rough estimate)
ref_size=$(du -sb /tmp/demo_ref | cut -f1)
src_size=$(du -sb /tmp/demo_src | cut -f1)
dest_size=$(du -sb /tmp/demo_dest | cut -f1)

echo "  Reference directory size: $ref_size bytes"
echo "  Source directory size: $src_size bytes"
echo "  Destination directory size: $dest_size bytes"

if [ $dest_size -lt $src_size ]; then
    savings=$((src_size - dest_size))
    echo "  Space saved by linking: $savings bytes"
fi
echo

# Test with symbolic links
echo "5. Testing with symbolic links (-s):"
rm -rf /tmp/demo_dest
./cpdd -r /tmp/demo_ref -s -R /tmp/demo_src /tmp/demo_dest
symbolic_links=$(find /tmp/demo_dest -type l | wc -l)
echo "  Symbolic links created: $symbolic_links"
echo

echo "Demo completed! Test directories:"
echo "  Reference: /tmp/demo_ref"
echo "  Source: /tmp/demo_src" 
echo "  Destination: /tmp/demo_dest"
echo
echo "Clean up with: rm -rf /tmp/demo_*"