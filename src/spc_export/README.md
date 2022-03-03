# SPC export README

To rebuild SPC export drivers on Linux:

- Run `git submodule update --init --recursive` to initialize the `AddMusicKFF` submodule.
- Enter the `AddMusicKFF/` folder.
  - Install your distribution's `asar` package (the SNES assembler, not the Electron archive tool), which should create `/usr/bin/asar`. If your distribution doesn't have `asar`, build it yourself and copy/symlink the binary to `AddMusicKFF/asar` (overwriting the existing symlink).
  - Run `make -j4` or your CPU thread count.
- Go back to the parent folder, and run `./generate-spc-driver.sh` to rebuild `driver/*.{bin,inc}`.
- Copy `mainLoopPos = 0x...` from the program's output and paste it into `driver.h` here.
