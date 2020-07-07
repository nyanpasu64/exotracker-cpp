# ExoTracker (or exotracker)

exotracker is an in-development cross-platform tracker which targets the Famicom/NES with expansion audio chips, possibly with other consoles being added later.

exotracker will not exactly match the tracker paradigm. Like trackers, it will have 1 note per channel, vertical channels, and note/effect columns. Unlike trackers, there is no concept of a row duration. Notes will not "take up" rows, but will be triggered at points in time (consisting of beat fraction + tick offset). This makes it easier, more self-documenting, and less hacky (compared to other trackers) to achieve dense rhythms (over 1 note per row), triplets, and early notes.

I have a [Google Drive folder of design notes](https://drive.google.com/drive/u/0/folders/15A1Td92HofO7KQ62QtuEDSmd4X1KKPAZ).

## Building

This project is built using CMake.

exotracker-cpp depends on C++17 support. On Linux, it compiles on GCC and Clang (not sure which versions). On Windows, it compiles on mingw-w64 GCC and MSVC 2019.

### Build Dependencies

This depends on Qt. All other libraries are bundled and compiled.

## Documentation

See [src/DESIGN.md](src/DESIGN.md) for design notes.

## Credits

This program would not be possible without the assistance of:

- [Ethan McCue](https://github.com/bowbahdoe) and [arximboldi](https://github.com/arximboldi) on immutable data structures (no longer used)
- Saga Musix and manx (OpenMPT developers) on low-latency audio development
- plgDavid on audio development and NES emulation
- ax6 for the idea of double-buffering/copying multiple copies of the document instead of immutable data structures (double-buffering no longer used)
- Discord servers: The PSG Cabal, The Fourth Modulator, and famitracker.org, for discussing tracker design
- Discord servers: The Fourth Modulator and Rust Audio for discussing audio development
- [bintracker (by irrlicht project)](https://bintracker.org/) for discussing tracker design
- konakonaa for discussing tracker design and commenting on prototypes
