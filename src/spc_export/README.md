# SPC export README

To rebuild drivers:

- Clone [my fork of AddMusicKFF](https://github.com/nyanpasu64/AddMusicKFF/tree/all-custom-instruments) to the `all-custom-instruments` branch, then enter the folder.
- Run `make -j4` or your CPU thread count.
- Run `./addmusick -norom`. This generates a driver image with zero global songs and one local song (even when passing in zero .txt files).
- Copy `mainLoopPos = ...` from the program's output and paste it into `driver.h` here.
- Copy `asm/SNES/SPCBase.bin`, `asm/SNES/SPCDSPBase.bin`, and `asm/SNES/bin/main.bin` to `driver/...`.
- Run `./generate-spc-driver.sh` to rebuild `driver/*.inc`.
