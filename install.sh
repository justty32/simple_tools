#!/usr/bin/env sh
# Build dcap and install it to /usr/local/bin (UNIX / macOS).
# Windows users: run `mingw32-make` and add the bin/ dir to PATH instead.
set -e

make
DEST="${1:-/usr/local/bin}"

if [ -w "$DEST" ]; then
    cp bin/dcap "$DEST/"
else
    echo "Need elevated permission to write $DEST; using sudo..."
    sudo cp bin/dcap "$DEST/"
fi

echo "dcap installed to $DEST/dcap"
echo "Try: dcap help"
