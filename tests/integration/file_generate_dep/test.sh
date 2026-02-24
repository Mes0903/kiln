#!/bin/bash
set -e

KILN="$1"
DIR="$(cd "$(dirname "$0")" && pwd)"

# Clean up
rm -rf "$DIR/build"

OUTFILE="$DIR/build/debug/generated.txt"
INFILE="$DIR/build/debug/generated.in"

# === Build 1: baseline ===
cd "$DIR"
cp "$DIR/CMakeLists.txt" "$DIR/CMakeLists.txt.bak"
"$KILN" --config debug -j 1

# Verify generated.in was created with correct content
if [ ! -f "$INFILE" ]; then
    echo "ERROR: generated.in not found"
    exit 1
fi

if grep -q "EXTRA_LINE" "$INFILE"; then
    echo "ERROR: generated.in should not contain EXTRA_LINE on first build"
    exit 1
fi

# Verify generated.txt was created (custom command ran)
if [ ! -f "$OUTFILE" ]; then
    echo "ERROR: generated.txt not found"
    exit 1
fi

# Verify content matches
if ! diff -q "$INFILE" "$OUTFILE" > /dev/null 2>&1; then
    echo "ERROR: generated.txt should match generated.in"
    exit 1
fi

echo "  PASS: baseline build"

# === Build 2: rebuild with no changes — generated.in mtime must NOT change ===
MTIME_BEFORE=$(stat -c %Y "$INFILE")
sleep 1  # ensure filesystem timestamp granularity
"$KILN" --config debug -j 1
MTIME_AFTER=$(stat -c %Y "$INFILE")

if [ "$MTIME_BEFORE" != "$MTIME_AFTER" ]; then
    echo "ERROR: generated.in mtime changed ($MTIME_BEFORE -> $MTIME_AFTER) on no-op rebuild (hash-based write avoidance failed)"
    exit 1
fi

echo "  PASS: no-op rebuild preserves generated.in mtime"

# === Build 3: change CMakeLists.txt to set the property — content changes ===
cp "$DIR/CMakeLists_v2.txt" "$DIR/CMakeLists.txt"
"$KILN" --config debug -j 1

# Verify generated.in mtime DID change (new content was written)
MTIME_AFTER_V2=$(stat -c %Y "$INFILE")
if [ "$MTIME_BEFORE" = "$MTIME_AFTER_V2" ]; then
    echo "ERROR: generated.in mtime should have changed after content change"
    exit 1
fi

# Verify generated.in now has the new content
if ! grep -q "EXTRA_LINE" "$INFILE"; then
    echo "ERROR: generated.in should contain EXTRA_LINE after property change"
    exit 1
fi

echo "  PASS: file(GENERATE) writes when content changes"

# === Verify implicit dependency propagation ===
# generated.txt should have been updated even though the only EXPLICIT
# dependency (stable_dep.txt) did NOT change. The custom command must have
# re-run because kiln detected the implicit dependency on generated.in.
if ! grep -q "EXTRA_LINE" "$OUTFILE"; then
    echo "ERROR: generated.txt should contain EXTRA_LINE (custom command should have re-run via implicit dep)"
    exit 1
fi

echo "  PASS: implicit dependency propagation"

# Restore original CMakeLists.txt
cp "$DIR/CMakeLists.txt.bak" "$DIR/CMakeLists.txt"
rm -f "$DIR/CMakeLists.txt.bak"

echo "All file_generate_dep tests passed!"
