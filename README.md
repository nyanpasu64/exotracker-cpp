# ExoTracker (or Exotracker or exotracker)

exotracker is an in-development cross-platform tracker which targets the Famicom/NES with expansion audio chips, possibly with other consoles being added later.

exotracker will not exactly match the tracker paradigm. Like trackers, it will have 1 note per channel, vertical channels, and note/effect columns. Unlike trackers, there is no concept of a row duration. Notes will not "take up" rows, but will be triggered at points in time (consisting of beat fraction + tick offset). This makes it easier, more self-documenting, and less hacky (compared to other trackers) to achieve dense rhythms (over 1 note per row), triplets, and early notes.

I have a [Google Drive folder of design notes](https://drive.google.com/drive/u/0/folders/15A1Td92HofO7KQ62QtuEDSmd4X1KKPAZ).

## Building

This project is built using CMake.

At the moment, this project does not build on compilers other than MSVC 2019, because of the use of C++20 features. In particular, the spaceship operator `<=>` and `= default` are not implemented in other compilers. This issue should resolve itself when C++20 support lands in GCC and Clang.

### Build Dependencies

This depends on Qt. All other libraries are bundled and compiled.

## Documentation

See [src/DESIGN.md](src/DESIGN.md) for design notes.

## Credits

This program would not be possible without the assistance of:

- [Ethan McCue](https://github.com/bowbahdoe) and [arximboldi](https://github.com/arximboldi) on immutable data structures
- Saga Musix and manx (OpenMPT developers) on low-latency audio development
- plgDavid on audio development and NES emulation
