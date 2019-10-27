## Building

This project is built using CMake.

At the moment, this project does not build on compilers other than MSVC 2019, because of the use of C++20 features. In particular, the spaceship operator `<=>` and `= default` are not implemented in other compilers. This issue should resolve itself when C++20 support lands in GCC and Clang.

### Build Dependencies

This depends on Qt. All other libraries are bundled and compiled.

## Credits

This program would not be possible without the assistance of:

- [Ethan McCue](https://github.com/bowbahdoe) and [arximboldi](https://github.com/arximboldi) on immutable data structures
- Saga Musix and manx (OpenMPT developers) on low-latency audio development
