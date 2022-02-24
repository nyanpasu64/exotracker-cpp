#!/usr/bin/env bash

cd driver

# Trim off first 4 bytes (size header), keep bytes 4+. But tail counts bytes from 1.
tail -c +5 main.bin | xxd -i - main.inc
cat SPCBase.bin | xxd -i - SPCBase.inc
cat SPCDSPBase.bin | xxd -i - SPCDSPBase.inc
