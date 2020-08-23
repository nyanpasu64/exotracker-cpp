# ExoTracker

[![Appveyor build status](https://ci.appveyor.com/api/projects/status/02g6qu9deawagent/branch/master?svg=true)](https://ci.appveyor.com/project/nyanpasu64/exotracker-cpp/branch/master)

exotracker is an in-development cross-platform tracker targeting the Famicom/NES with expansion audio chips, possibly with other consoles being added later.

exotracker will not exactly match the tracker paradigm. Like trackers, it will have 1 note per channel, vertical channels, and note/effect columns. Unlike trackers, there is no concept of a row duration. Notes will not "take up" rows, but will be triggered at points in time (consisting of beat fraction + tick offset). This makes it easier, more self-documenting, and less hacky (compared to other trackers) to achieve dense rhythms (over 1 note per row), triplets, and early notes.

I have a [Google Drive folder of exploratory design notes](https://drive.google.com/drive/u/0/folders/15A1Td92HofO7KQ62QtuEDSmd4X1KKPAZ). Project planning is in a [Trello board](https://trello.com/b/4Njmv9hz/exotracker).

## Branches

To get the most up-to-date code, checkout and follow the `dev` branch. Note that it may be force-pushed to fix bugs, which complicates `git pull`. `master` will not be force-pushed, but it is pushed infrequently and lags 1-2 weeks behind `dev`.

At the moment, I primarily develop on local HEAD and branches, instead of using pull requests to update the GitHub/GitLab repository. I may switch to pull requests if more users.

## Download

Pre-built development binaries are available at Appveyor ([master](https://ci.appveyor.com/project/nyanpasu64/exotracker-cpp/branch/master), [dev](https://ci.appveyor.com/project/nyanpasu64/exotracker-cpp/branch/dev)). On Windows, you will need the Universal C Runtime to run these programs. It is installed by default on some machines, or can be downloaded from [Microsoft](https://support.microsoft.com/en-us/help/2977003/the-latest-supported-visual-c-downloads#section-2).

## Building

This project is built using CMake. I recommend using Ninja as a target build system since it behaves consistently across OSes and supports parallelism and automatic core counting. Make will probably work too.

```sh
mkdir build...
cd build...
cmake .. -DCMAKE_BUILD_TYPE={Release, Debug, ...} -G Ninja
ninja
```

I use Qt Creator and CLion IDEs for this project. You can import it into Visual Studio 2019 as a CMake project, but profiling will be tricky-to-impossible to set up. Telling CMake to generate Visual Studio .sln projects will probably not work (switching build types in Visual Studio will not change flags, and I don't know how to use Clang with .vcxproj).

exotracker-cpp requires a compiler with C++20 support and GCC's "statement expressions" extension. MSVC is not supported, GCC and Clang is.

- On Linux, I've had good results with GCC 10 and Clang 10 (not sure about older versions).
- On Windows, I recommend using Clang (not clang-cl); mingw-w64 produces larger binaries and requires either static libc or DLLs most people don't have. MSVC is not supported and the code will not compile.
- On Mac, it compiles using XCode's Clang. I haven't tried other compilers.

### Build Dependencies

exotracker-cpp depends on Qt. All other libraries are bundled, compiled, and linked statically. On Linux, to obtain audio, you need to install ALSA/PulseAudio/JACK headers (whichever one you want to use). Most Linux distributions use PulseAudio, but RtAudio's PulseAudio backend may be more stuttery (due to mutexes) than ALSA.

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

