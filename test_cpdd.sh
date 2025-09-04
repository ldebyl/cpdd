#!/bin/bash
#
# test_cpdd.sh - Simple test script for cpdd functionality
# Creates synthetic data and tests various copy scenarios
#
# Copyright (c) 2025 Lee de Byl <lee@32kb.net>
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# set -e  # Disabled to see all test results

SEED=42
SUCCESS=0
FAILED=0
FILES=1000
VERBOSE=""
STATS=""

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -f|--files)
            FILES="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE="-v"
            shift
            ;;
        -vv)
            VERBOSE="-vv"
            shift
            ;;
        -vvv)
            VERBOSE="-vvv"
            shift
            ;;
        --stats)
            STATS="--stats"
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  -f, --files COUNT    Number of files to generate (default: 1000)"
            echo "  -v, --verbose        Enable verbose output for cpdd operations"
            echo "  -vv                  Enable more verbose output"
            echo "  -vvv                 Enable debug output"
            echo "  --stats              Show statistics after operations"
            echo "  -h, --help           Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "🧪 === CPDD Test Script ==="
echo "🎲 Using seed: $SEED"
echo "📁 Files to create: $FILES"
[[ -n "$VERBOSE" ]] && echo "🔍 Verbosity: $VERBOSE"
[[ -n "$STATS" ]] && echo "📊 Statistics enabled"
echo

# Get system temp directory
TEMP_DIR=$(mktemp -d)
echo "📂 Using temp directory: $TEMP_DIR"

make cpdd
make syndir

# Function to count hard links
count_hard_links() {
    find "$1" -type f -exec stat -c "%h %n" {} \; | awk '$1 > 1' | wc -l
}

# Function to count soft links
count_soft_links() {
    find "$1" -type l | wc -l
}

# Function to test case
test_case() {
    local name="$1"
    local cmd="$2"
    local expected_outcome="$3"
    
    echo -n "Testing $name... "
    
    if eval "$cmd" >/dev/null 2>&1; then
        if [[ "$expected_outcome" == "pass" ]]; then
            echo "PASS"
            ((SUCCESS++))
        else
            echo "FAIL (expected to fail)"
            ((FAILED++))
        fi
    else
        if [[ "$expected_outcome" == "fail" ]]; then
            echo "PASS (expected failure)"
            ((SUCCESS++))
        else
            echo "FAIL"
            ((FAILED++))
        fi
    fi
}

# Cleanup function
cleanup() {
    rm -rf "$TEMP_DIR"
}
#trap cleanup EXIT


# Create directories
REF_DIR="$TEMP_DIR/reference"
SRC_DIR="$TEMP_DIR/source"
DEST1="$TEMP_DIR/dest1"
DEST2="$TEMP_DIR/dest2"

echo "🏗️  Creating synthetic test data..."
./syndir --files $FILES --size-p50 5120 --size-p95 100000 --size-max 1048576 --seed $SEED "$REF_DIR" "$SRC_DIR"

echo
echo "🧪 === Test Results ==="

# Test 1: Single file copy (no reference)
mkdir -p "$DEST1"
FIRST_FILE=$(find "$SRC_DIR" -type f | head -1)
test_case "single file copy" \
    "./cpdd $VERBOSE $STATS '$FIRST_FILE' '$DEST1/single_file.txt'" \
    "pass"

# Test 2: Single file copy with reference directory
mkdir -p "$DEST2"
test_case "single file with reference" \
    "./cpdd $VERBOSE $STATS -r '$REF_DIR' '$FIRST_FILE' '$DEST2/single_ref.txt'" \
    "pass"

# Test 3: Recursive copy without reference
DEST3="$TEMP_DIR/dest3"
test_case "recursive copy (no reference)" \
    "./cpdd $VERBOSE $STATS -R '$SRC_DIR' '$DEST3'" \
    "pass"

# Test 4: Recursive copy with reference (hard links)
DEST4="$TEMP_DIR/dest4"
test_case "recursive copy with hard links" \
    "./cpdd $VERBOSE $STATS -r '$REF_DIR' -L -R '$SRC_DIR' '$DEST4'" \
    "pass"

# Test 5: Recursive copy with reference (soft links)
DEST5="$TEMP_DIR/dest5"
test_case "recursive copy with soft links" \
    "./cpdd $VERBOSE $STATS -r '$REF_DIR' -s -R '$SRC_DIR' '$DEST5'" \
    "pass"

echo

# Validation tests
if [[ -d "$DEST3" && -d "$DEST4" ]]; then
    echo "✅ === Validation ==="
    
    # Compare file structures
    echo -n "Comparing file structures... "
    if diff <(find "$DEST3" -type f | sort) <(find "$DEST4" -type f | sed "s|$DEST4|$DEST3|g" | sort) >/dev/null; then
        echo "PASS"
        ((SUCCESS++))
    else
        echo "FAIL"
        ((FAILED++))
    fi
    
    # Count links in hard link directory
    HARD_LINKS=$(count_hard_links "$DEST4")
    echo "🔗 Hard links created: $HARD_LINKS"
    if [[ $HARD_LINKS -gt 0 ]]; then
        echo "Hard link validation: PASS"
        ((SUCCESS++))
    else
        echo "Hard link validation: FAIL (no hard links found)"
        ((FAILED++))
    fi
fi

if [[ -d "$DEST5" ]]; then
    # Count links in soft link directory
    SOFT_LINKS=$(count_soft_links "$DEST5")
    echo "🔗 Soft links created: $SOFT_LINKS"
    if [[ $SOFT_LINKS -gt 0 ]]; then
        echo "Soft link validation: PASS"
        ((SUCCESS++))
    else
        echo "Soft link validation: FAIL (no soft links found)"
        ((FAILED++))
    fi
fi

# Test hard and soft link creation explicitly
echo
echo "🔗 === Link-specific Tests ==="

# Use the first file for link tests
TEST_FILE="$FIRST_FILE"

DEST6="$TEMP_DIR/dest6"
mkdir -p "$DEST6"

# Test hard link creation
test_case "hard link creation" \
    "./cpdd $VERBOSE $STATS -r '$REF_DIR' -L '$TEST_FILE' '$DEST6/hard_linked.txt'" \
    "pass"

# Test soft link creation  
DEST7="$TEMP_DIR/dest7"
mkdir -p "$DEST7"
test_case "soft link creation" \
    "./cpdd $VERBOSE $STATS -r '$REF_DIR' -s '$TEST_FILE' '$DEST7/soft_linked.txt'" \
    "pass"

echo
echo "📋 === Final Results ==="
echo "✅ Tests passed: $SUCCESS"
echo "❌ Tests failed: $FAILED"
echo "📊 Total tests: $((SUCCESS + FAILED))"

if [[ $FAILED -eq 0 ]]; then
    echo "🎉 Overall result: SUCCESS"
    exit 0
else
    echo "💥 Overall result: FAILURE"
    exit 1
fi
