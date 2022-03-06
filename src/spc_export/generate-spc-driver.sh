#!/usr/bin/env bash
set -e
set -o pipefail

## Clean compiled driver binaries and headers.
rm driver/SPCBase.bin driver/SPCDSPBase.bin driver/main.bin || true
rm driver/SPCBase.inc driver/SPCDSPBase.inc driver/main.inc || true

DRIVER="$PWD/driver"

## Build and install driver binaries.
cd AddMusicKFF

# Prints mainLoopPos.
./addmusick -norom
cp asm/SNES/SPCBase.bin asm/SNES/SPCDSPBase.bin asm/SNES/bin/main.bin "$DRIVER/"

## Convert driver binaries into headers.

cd "$DRIVER"
# Trim off first 4 bytes (size header), keep bytes 4+. But tail counts bytes from 1.
tail -c +5 main.bin | xxd -i - main.inc
cat SPCBase.bin | xxd -i - SPCBase.inc
cat SPCDSPBase.bin | xxd -i - SPCDSPBase.inc
