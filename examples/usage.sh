#!/bin/bash

# Example usage scenarios for cpdd

echo "=== CPDD Usage Examples ==="
echo

# Create example directories and files
echo "Setting up example directories..."
mkdir -p example/{src,dest,reference}
mkdir -p example/src/subdir

# Create some test files
echo "Content of file A" > example/src/file_a.txt
echo "Content of file B" > example/src/file_b.txt
echo "Content of file C" > example/src/subdir/file_c.txt

# Create reference files (some identical, some different)
echo "Content of file A" > example/reference/ref_file1.txt  # Same as file_a.txt
echo "Different content" > example/reference/ref_file2.txt  # Different content
echo "Content of file C" > example/reference/another_name.txt  # Same as file_c.txt

echo "Example files created."
echo

# Example 1: Basic copy
echo "1. Basic file copy:"
echo "   ./cpdd example/src/file_a.txt example/dest/copied_file.txt"
echo

# Example 2: Recursive directory copy
echo "2. Recursive directory copy:"
echo "   ./cpdd -R example/src example/dest"
echo

# Example 3: Copy with hard links to reference directory
echo "3. Copy with hard links to matching reference files:"
echo "   ./cpdd -r example/reference -H -R example/src example/dest"
echo

# Example 4: Copy with symbolic links and verbose output
echo "4. Copy with symbolic links and verbose output:"
echo "   ./cpdd -r example/reference -s -R -v example/src example/dest"
echo

# Example 5: Show help
echo "5. Show help:"
echo "   ./cpdd --help"
echo

echo "File matching logic:"
echo "- Files are matched by size first (fastest check)"
echo "- Then by MD5 checksum (medium speed, high accuracy)"  
echo "- Finally by byte-by-byte comparison (slowest, 100% accurate)"
echo
echo "When a match is found in the reference directory:"
echo "- Hard link (-H): Creates a hard link to the reference file"
echo "- Symbolic link (-s): Creates a symbolic link to the reference file"
echo "- No link option: File is copied normally"
echo

echo "Clean up example directories with: rm -rf example/"