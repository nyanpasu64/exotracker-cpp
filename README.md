# ExoTracker

[![Appveyor build status](https://ci.appveyor.com/api/projects/status/02g6qu9deawagent/branch/master?svg=true)](https://ci.appveyor.com/project/nyanpasu64/exotracker-cpp/branch/master)

exotracker is an in-development cross-platform tracker targeting the Famicom/NES with expansion audio chips, possibly with other consoles being added later.

exotracker will not exactly match the tracker paradigm. Like trackers, it will have 1 note per channel, vertical channels, and note/effect columns. Unlike trackers, there is no concept of a row duration. Notes will not "take up" rows, but will be triggered at points in time (consisting of beat fraction + tick offset). This makes it easier, more self-documenting, and less hacky (compared to other trackers) to achieve dense rhythms (over 1 note per row), triplets, and early notes.

I have a [Google Drive folder of exploratory design notes](https://drive.google.com/drive/u/0/folders/15A1Td92HofO7KQ62QtuEDSmd4X1KKPAZ). Project planning is in a [Trello board](https://trello.com/b/4Njmv9hz/exotracker).

## Branches

To get the most up-to-date code, checkout and follow the `dev` branch. Note that it may be force-pushed to fix bugs, which complicates `git pull`. `master` will not be force-pushed, but it is pushed infrequently and lags 1-2 weeks behind `dev`.

At the moment, I primarily develop on local HEAD and branches, instead of using pull requests to update the GitHub/GitLab repository. I may switch to pull requests if more users.

## Building

This project is built using CMake. I recommend using Ninja as a target build system since it behaves consistently across OSes and supports parallelism and automatic core counting.

```sh
mkdir build...
cd build...
cmake .. -DCMAKE_BUILD_TYPE={Release, Debug, ...} -G Ninja
ninja
```

### Build Dependencies

exotracker-cpp requires a compiler with C++20 support. On Linux, it compiles on GCC 10 and Clang 10 (not sure about older versions). On Windows, it compiles on mingw-w64 GCC and MSVC 2019. On Mac, it compiles using XCode's Clang.

exotracker-cpp depends on Qt. All other libraries are bundled, compiled, and linked statically. On Linux, you need to install ALSA/PulseAudio/JACK headers to obtain each backend.

## Documentation

See [src/DESIGN.md](src/DESIGN.md) for high-level design docs. There is no end-user help manual yet.

## Credits

This program would not be possible without the assistance of:

- [Ethan McCue](https://github.com/bowbahdoe) and [arximboldi](https://github.com/arximboldi) on immutable data structures (no longer used)
- Saga Musix and manx (OpenMPT developers) on low-latency audio development
- plgDavid on audio development and NES emulation
- ax6 for the idea of double-buffering/copying multiple copies of the document instead of immutable data structures (double-buffering no longer used)
- Discord servers: The PSG Cabal, The Fourth Modulator, and famitracker.org, for discussing tracker design
- Discord servers: The Fourth Modulator and Rust Audio for discussing audio development
- [bintracker (by irrlicht project)](https://bintracker.org/) for discussing tracker design
- konakonaa and [Persune](https://github.com/Gumball2415) ([Twitter](https://twitter.com/Gumball2415)) for discussing tracker design and commenting on prototypes

