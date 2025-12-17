#!/bin/bash
# Script to test networkfs mounting and file listing

set -e

echo "=== NetworkFS Testing Script ==="
echo ""

# Step 1: Get a token
TOKEN="da3db72d-3f71-4160-a24c-33acc6451e85"



echo "✓ Got token: $TOKEN"
echo ""
rm -rf mnt
# Step 2: Build the project
echo "Step 2: Building the project..."
if [ ! -d "build" ]; then
    meson setup build
fi
meson compile -C build

if [ ! -f "build/networkfs" ]; then
    echo "ERROR: Build failed - networkfs binary not found"
    exit 1
fi

echo "✓ Build successful"
echo ""

# Step 3: Create mount point
echo "Step 3: Creating mount point..."
mkdir -p mnt
echo "✓ Mount point ready: ./mnt"
echo ""

# Step 4: Mount the filesystem
echo "Step 4: Mounting filesystem..."
echo "Note: You need to update the TOKEN in src/inode.cpp line 4 to: $TOKEN"
echo "      OR modify the code to use NETWORKFS_TOKEN from environment"
echo ""
echo "After updating, run:"
echo "  NETWORKFS_TOKEN=$TOKEN ./build/networkfs mnt"
echo ""
echo "Or in background:"
echo "  NETWORKFS_TOKEN=$TOKEN ./build/networkfs mnt &"
echo ""
echo "Then in another terminal, test with:"
echo "  ls -la mnt/"
echo "  stat mnt/"
echo ""

# Step 5: Instructions for testing
echo "=== Testing Instructions ==="
echo ""
echo "1. Update src/inode.cpp line 4 with your token, OR"
echo "   modify networkfs_http_call calls to use token from userdata"
echo ""
echo "2. Rebuild: meson compile -C build"
echo ""
echo "3. Mount: NETWORKFS_TOKEN=$TOKEN ./build/networkfs mnt"
echo ""
echo "4. In another terminal, test:"
echo "   - ls -la mnt/          # Should show files"
echo "   - stat mnt/            # Should show directory attributes"
echo "   - cat mnt/<filename>   # If files exist"
echo ""
echo "5. Unmount: fusermount3 -u mnt"
echo ""

