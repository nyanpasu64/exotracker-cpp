# ExoTracker

[![Appveyor build status](https://ci.appveyor.com/api/projects/status/02g6qu9deawagent/branch/dev?svg=true)](https://ci.appveyor.com/project/nyanpasu64/exotracker-cpp/branch/dev)

exotracker is an in-development cross-platform music tracker targeting the SNES SPC700 sound chip.

exotracker will not exactly match the tracker paradigm. Like trackers, it will have 1 note per channel, vertical channels, and note/effect columns. Unlike trackers, there is no concept of a row duration. Notes will not "take up" rows, but will be triggered at points in time (consisting of beat fraction + tick offset). This makes it easier, more self-documenting, and less hacky (compared to other trackers) to achieve dense rhythms (over 1 note per row), triplets, and early notes.

I have a [Google Drive folder of exploratory design notes](https://drive.google.com/drive/u/0/folders/15A1Td92HofO7KQ62QtuEDSmd4X1KKPAZ). Project planning is in a [Trello board](https://trello.com/b/4Njmv9hz/exotracker).

## Branches

To get the most up-to-date code, checkout and follow the `dev` branch. Note that it may be force-pushed to fix bugs, which complicates `git pull`. `stable` will not be force-pushed, but it is pushed infrequently and lags 1-2 weeks behind `dev`.

At the moment, I primarily develop on local HEAD and branches, instead of using pull requests to update the GitHub/GitLab repository. I may switch to pull requests to prevent merge conflicts, if more people start contributing to this repository.

## Download

Pre-built development binaries are available at Appveyor ([stable](https://ci.appveyor.com/project/nyanpasu64/exotracker-cpp/branch/stable), [dev](https://ci.appveyor.com/project/nyanpasu64/exotracker-cpp/branch/dev)). On Windows, you will need the Universal C Runtime to run these programs. It is installed by default on some machines, or can be downloaded from [Microsoft](https://support.microsoft.com/en-us/help/2977003/the-latest-supported-visual-c-downloads#section-2).

## Building

This project is built using CMake. I recommend using Ninja as a target build system since it behaves consistently across OSes and supports parallelism and automatic core counting. Make will probably work too.

```sh
mkdir build...
cd build...
cmake .. -DCMAKE_BUILD_TYPE={Release, Debug, ...} -G Ninja
ninja
```

I use Qt Creator and CLion IDEs for this project. You can import it into Visual Studio 2019 as a CMake project, but profiling will be tricky-to-impossible to set up. Telling CMake to generate Visual Studio .sln projects will probably not work (switching build types in Visual Studio will not change flags, and I don't know how to use Clang with .vcxproj).

### Compilers

exotracker-cpp requires a compiler with C++20 support. MSVC and GCC 10 are supported. Clang currently fails to build unit tests (it errors out when  doctest's `CAPTURE()` lambdas capture structured bindings).

- On Linux, I've had good results with GCC 10-11 (Clang can't compile unit tests). Older compilers lack support for `operator<=>` and defaulting `operator==`.
	- You can speed up builds if you install lld or the Linux-only mold, and use it instead of ld.
	- On GCC, enable lld using `-DCMAKE_C_FLAGS=-fuse-ld=lld -DCMAKE_CXX_FLAGS=-fuse-ld=lld`
	- On Clang, enable lld using `-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld`
- On Windows, I recommend using MSVC (Clang can't compile unit tests) (not clang-cl). MinGW/GCC works, but is not recommended.
	- MinGW-w64 produces larger binaries, ships with much larger Qt DLLs (stripping might help), and requires either linking libc statically into the binary (even larger binaries) or bundling glibc DLLs (since unlike Universal CRT, most people don't have mingw-w64 in PATH).
		- Additionally, MinGW-w64 builds slowly due to a slow linker. To workaround this, you can install `mingw-w64-x86_64-lld` and use `lld` instead.
	- If you plan to use MinGW, I recommend using MSYS2 to install GCC and Qt (`pacman -Syu mingw-w64-x86_64-gcc mingw-w64-x86_64-gdb mingw-w64-x86_64-qt5`).
	- Using Qt's web installer to install MinGW Qt is discouraged, since installing MinGW Qt also installs MinGW GCC 8.1.0, which is too old to compile exotracker. (If you try uninstalling MinGW, it removes MinGW Qt as well.) You have to keep GCC 8.1.0 around (but avoid using it), then use MSYS2 to install mingw-w64 GCC separately, use MSYS2's compiler to build exotracker, and use MSYS2's DLLs to run exotracker. You're better off installing GCC and Qt through MSYS2, which works by default.
	- Do not use Win-builds to install MinGW! It ships GCC 4.8.3 and Qt 5.3.1, which were released in 2014 and are far too outdated for exotracker.
	- MSYS2 also offers Clang with MinGW ABI (`mingw-w64-x86_64-clang`), UCRT64, and CLANG64 ([link](https://www.msys2.org/docs/environments/)). These are untested, but I encourage you to test UCRT64.
- On Intel Mac, the program compiled at one point using XCode's Clang. Building on/for M1 Mac is unsupported until exotracker adds Qt 6 support.

### Build Dependencies

exotracker-cpp depends on Qt 5. All other libraries are bundled, compiled, and linked statically. On Linux, to obtain audio, you need to install ALSA/PulseAudio/JACK headers (whichever one you want to use). Most Linux distributions use PulseAudio, but RtAudio's PulseAudio backend may be more stuttery (due to mutexes) than ALSA.

## Usage

- **Press Space to enable note entry, and Enter to play.** Unfortunately, note preview is not supported yet.
	- Press `'` to play from the current row. (Recall Channel State is not supported yet.)
- ExoTracker uses a FamiTracker-style piano layout. Keybinds mostly resemble FamiTracker, but changing patterns is mapped to Ctrl+PageUp/Down instead of Ctrl+Left/Right.
	- Additionally, you can Ctrl+Up/Down to snap to the next beat, and Ctrl+Alt+Up/Down to snap to the next event.
	- Mouse input is not implemented yet.
- Try zooming with Ctrl-±. Unlike previous trackers, rows are merely used for editing, and the document stores an event stream with each event's timestamp as a beat fraction. This means you can use real triplets without hacking with delay effects, and more closely spaced notes when needed.
- Try passing in names of sample documents as command-line arguments. Listed in order from most to least useful:
	- Partial songs: `dream-fragments`
	- `all-channels` (default song) (sounds bad, tests that all audio channels play properly)
	- `empty` (add your own notes)
- Some sample documents have short and/or looped blocks (the gray rectangles to the left of each channel), intended as a DAW-inspired system of looping and reuse. But right now, users can only create full-grid blocks, and cannot delete blocks.
	- The block system is powerful, but unfortunately not editable through the UI yet, so you can't try it out to see useful it is.
	- Pattern reuse is not implemented.

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
- KungFuFurby on the N-SPC driver and S-SMP assembly
