#!/bin/bash

# Test script for cpdd multiple source functionality

echo "=== CPDD Multiple Sources Test Suite ==="
echo

# Test result tracking
TESTS_PASSED=0
TESTS_FAILED=0

# Helper functions
print_test() {
    echo "Test: $1"
}

print_pass() {
    echo "PASS: $1"
    ((TESTS_PASSED++))
}

print_fail() {
    echo "FAIL: $1"
    ((TESTS_FAILED++))
}

cleanup() {
    rm -rf /tmp/cpdd_test_*
}

# Ensure we have cpdd binary
if [ ! -x "./cpdd" ]; then
    echo "Error: cpdd binary not found. Run 'make' first."
    exit 1
fi

echo "Cleaning up any previous test files..."
cleanup
echo

# Test 1: Multiple files to directory
print_test "Multiple files to directory"
mkdir -p /tmp/cpdd_test_dest
echo "file 1 content" > /tmp/cpdd_test_file1.txt
echo "file 2 content" > /tmp/cpdd_test_file2.txt
echo "file 3 content" > /tmp/cpdd_test_file3.txt

./cpdd -v /tmp/cpdd_test_file1.txt /tmp/cpdd_test_file2.txt /tmp/cpdd_test_file3.txt /tmp/cpdd_test_dest/

if [ -f "/tmp/cpdd_test_dest/cpdd_test_file1.txt" ] && 
   [ -f "/tmp/cpdd_test_dest/cpdd_test_file2.txt" ] && 
   [ -f "/tmp/cpdd_test_dest/cpdd_test_file3.txt" ]; then
    print_pass "All files copied to destination directory"
else
    print_fail "Files not found in destination directory"
fi
echo

# Test 2: Multiple directories (recursive)
print_test "Multiple directories with recursive copy"
mkdir -p /tmp/cpdd_test_src1/subdir /tmp/cpdd_test_src2
echo "src1 main file" > /tmp/cpdd_test_src1/main.txt
echo "src1 sub file" > /tmp/cpdd_test_src1/subdir/sub.txt
echo "src2 file" > /tmp/cpdd_test_src2/file.txt

rm -rf /tmp/cpdd_test_dest
mkdir -p /tmp/cpdd_test_dest

./cpdd -R -v /tmp/cpdd_test_src1 /tmp/cpdd_test_src2 /tmp/cpdd_test_dest

if [ -f "/tmp/cpdd_test_dest/cpdd_test_src1/main.txt" ] && 
   [ -f "/tmp/cpdd_test_dest/cpdd_test_src1/subdir/sub.txt" ] && 
   [ -f "/tmp/cpdd_test_dest/cpdd_test_src2/file.txt" ]; then
    print_pass "Directory structures preserved correctly"
else
    print_fail "Directory structures not copied correctly"
fi
echo

# Test 3: Single file to specific destination
print_test "Single file to specific destination (non-directory)"
echo "single file content" > /tmp/cpdd_test_single.txt

./cpdd /tmp/cpdd_test_single.txt /tmp/cpdd_test_single_dest.txt

if [ -f "/tmp/cpdd_test_single_dest.txt" ] && 
   [ "$(cat /tmp/cpdd_test_single_dest.txt)" = "single file content" ]; then
    print_pass "Single file copied to specific destination"
else
    print_fail "Single file not copied correctly"
fi
echo

# Test 4: Mixed sources (files and directories)
print_test "Mixed sources - files and directories"
mkdir -p /tmp/cpdd_test_mixdir
echo "mixed file content" > /tmp/cpdd_test_mixfile.txt
echo "dir content" > /tmp/cpdd_test_mixdir/dirfile.txt

rm -rf /tmp/cpdd_test_dest
mkdir -p /tmp/cpdd_test_dest

./cpdd -R -v /tmp/cpdd_test_mixfile.txt /tmp/cpdd_test_mixdir /tmp/cpdd_test_dest

if [ -f "/tmp/cpdd_test_dest/cpdd_test_mixfile.txt" ] && 
   [ -f "/tmp/cpdd_test_dest/cpdd_test_mixdir/dirfile.txt" ]; then
    print_pass "Mixed sources copied correctly"
else
    print_fail "Mixed sources not copied correctly"
fi
echo

# Test 5: Reference directory with multiple sources (deduplication)
print_test "Multiple sources with reference directory (deduplication)"
mkdir -p /tmp/cpdd_test_ref
echo "duplicate content" > /tmp/cpdd_test_ref/reference.txt
echo "duplicate content" > /tmp/cpdd_test_dup1.txt
echo "duplicate content" > /tmp/cpdd_test_dup2.txt
echo "unique content" > /tmp/cpdd_test_unique.txt

rm -rf /tmp/cpdd_test_dest
mkdir -p /tmp/cpdd_test_dest

./cpdd -r /tmp/cpdd_test_ref -v /tmp/cpdd_test_dup1.txt /tmp/cpdd_test_dup2.txt /tmp/cpdd_test_unique.txt /tmp/cpdd_test_dest

# Check if files exist
if [ -f "/tmp/cpdd_test_dest/cpdd_test_dup1.txt" ] && 
   [ -f "/tmp/cpdd_test_dest/cpdd_test_dup2.txt" ] && 
   [ -f "/tmp/cpdd_test_dest/cpdd_test_unique.txt" ]; then
    print_pass "Reference directory processing completed (files exist)"
else
    print_fail "Files not found after reference directory processing"
fi
echo

# Test 6: Error handling - multiple sources to existing regular file
print_test "Error handling: multiple sources to existing regular file"
echo "existing file" > /tmp/cpdd_test_existing.txt
echo "source1" > /tmp/cpdd_test_src_a.txt
echo "source2" > /tmp/cpdd_test_src_b.txt

# This should fail with an error
if ! ./cpdd /tmp/cpdd_test_src_a.txt /tmp/cpdd_test_src_b.txt /tmp/cpdd_test_existing.txt 2>/dev/null; then
    print_pass "Correctly rejected multiple sources to regular file"
else
    print_fail "Should have rejected multiple sources to regular file"
fi
echo

# Test 7: Simulated globbing (multiple .txt files)
print_test "Simulated globbing test (multiple .txt files)"
echo "glob1" > /tmp/cpdd_test_glob1.txt
echo "glob2" > /tmp/cpdd_test_glob2.txt
echo "glob3" > /tmp/cpdd_test_glob3.txt
echo "not included" > /tmp/cpdd_test_glob.dat

rm -rf /tmp/cpdd_test_dest
mkdir -p /tmp/cpdd_test_dest

# Simulate shell glob expansion
./cpdd -v /tmp/cpdd_test_glob1.txt /tmp/cpdd_test_glob2.txt /tmp/cpdd_test_glob3.txt /tmp/cpdd_test_dest

if [ -f "/tmp/cpdd_test_dest/cpdd_test_glob1.txt" ] && 
   [ -f "/tmp/cpdd_test_dest/cpdd_test_glob2.txt" ] && 
   [ -f "/tmp/cpdd_test_dest/cpdd_test_glob3.txt" ] && 
   [ ! -f "/tmp/cpdd_test_dest/cpdd_test_glob.dat" ]; then
    print_pass "Globbing simulation worked correctly"
else
    print_fail "Globbing simulation failed"
fi
echo

# Test 8: Destination directory creation
print_test "Automatic destination directory creation"
echo "auto create test" > /tmp/cpdd_test_auto1.txt
echo "auto create test" > /tmp/cpdd_test_auto2.txt

# Try to copy to non-existent directory (should create it)
rm -rf /tmp/cpdd_test_new_dest
./cpdd /tmp/cpdd_test_auto1.txt /tmp/cpdd_test_auto2.txt /tmp/cpdd_test_new_dest

if [ -f "/tmp/cpdd_test_new_dest/cpdd_test_auto1.txt" ] && 
   [ -f "/tmp/cpdd_test_new_dest/cpdd_test_auto2.txt" ]; then
    print_pass "Destination directory created automatically"
else
    print_fail "Failed to create destination directory automatically"
fi
echo

# Test Summary
echo "=== Test Summary ==="
echo "Tests passed: $TESTS_PASSED"
echo "Tests failed: $TESTS_FAILED"
echo "Total tests: $((TESTS_PASSED + TESTS_FAILED))"
echo

if [ $TESTS_FAILED -eq 0 ]; then
    echo "All tests passed!"
    exit_code=0
else
    echo "Some tests failed."
    exit_code=1
fi

echo
echo "Cleaning up test files..."
cleanup
echo "Test suite completed."

exit $exit_code