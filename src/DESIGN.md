## Code architecture

I have a [Google Drive folder of design notes](https://drive.google.com/drive/u/0/folders/15A1Td92HofO7KQ62QtuEDSmd4X1KKPAZ).

Code is stored in src/, dependencies in 3rdparty/.

~~Some classes may have member variables prefixed with a single underscore, like `_var`, to distinguish from locals.~~ (to be decided)

src/gui/history.h has `gui::history::History`. There is only 1 instance, and it owns tracker pattern state. The pattern editor and audio thread read from `History`.

Tracker pattern state is immutable, using data structures supplied by the immer library. This allows the audio thread to read it without blocking. The UI thread creates edited copies of the old state, using structural sharing to avoid copying unmodified data, then atomically updates `History.current`.

## Audio architecture

Design notes at https://docs.google.com/document/d/17g5wqgpUPWItvHCY-0eCaqZSNdVKwouYoGbu-RTAjfo .

There is only 1 audio thread, which runs callbacks called by PortAudio, like OpenMPT.

The audio system (`OverallSynth`) is driven by sound synthesis callbacks. Every time the audio callback calls `OverallSynth.synthesize_overall()`, it synthesizes a fixed number of samples, using `EventQueue` to know when to trigger new ticks (frame or vblank).

Alternatives to this design:

- Synthesizing 1 tick of audio at a time from the callback thread is also an acceptable option.
- Synthesizing audio in a separate "synth thread", and splitting into fixed-size chunks queued up and read by the "output thread", is unacceptable since it generates latency. (This is how FamiTracker works.) Even with a length-0 queue, the synth thread can run 1 audio block ahead of the output thread.

### Audio components (unfinished)

The sequencer is implemented in C++, and converts pattern data from "events placed at beats" into "events separated by ticks". This is a complex task because events are located at beat fractions, not entries in a fixed array. So when performing NSF export, exotracker will likely use the C++ sequencer to compile patterns into a list of (delay, event), much like PPMCK. The NSF driver will only have a simplified sequencer.

The audio driver is called once per tick. It handles events from the sequencer, as well as volume envelopes and vibrato. It generates register writes once per tick (possibly with delays between channels). It will be implemented separately in C++ and NSF export.

The sound-chip emulator (`ChipSynth` subclasses) can be run for specific periods of time (nclocks). When suspended between emulation calls, they can accept register writes. Each sound chip (not channel) receives its own `ChipSynth`. Separate chips are mixed linearly (addition or weighted average), while each chip can perform nonlinear/arbitrary mixing of its channels. Sound chips can produce output by either writing to `OverallSynth.nes_blip` (type: `Blip_Buffer`), or writing to an audio buffer. This will not be implemented in NSF export (since it's handled by hardware or the NSF player/emulator).

- ~~Due to the design of emu2413, VRC7 may be run for specific number of samples, not clocks.~~ (unsure)

## Rules for avoiding circular header inclusion

C++ headers malfunction when there is a `#include` cycle. I have designed some rules to prevent inclusion cycles (ensure topological sortability).

Abstract files (in order from common to derived). Each file can include all files listed above.

- ../*_common.h
- ./general_common.h
    - External libraries: none
- ./specific_common.h
    - External libraries: specific -related libraries
- ./specific/nested_common.h, if used
- ./specific/nested.h
- ./specific.h (public interface of . folder)
- ./general.h, if used
- ../app.cpp will interface mostly with ./specific.h (a public interface). I should try C++ modules.

----

Concrete files in the "audio" folder (in order from common to derived). Each file can include all files listed above.

- audio_common.h (general audio types and functions, like Amplitude = s16)
- output.h
    - Depends on output-related libraries (portaudio).
- Application code depends on output.h.

and

- audio_common.h
- synth_common.h
    - Depends on synth-related libraries (blip_buf).
- synth/Nes2A03Synth.h
    - class audio::synth::Nes2A03Synth
- synth.h
- Application code depends on synth.h.

(There is an inconsistency where I place audio/audio_common.h within audio/, but audio/synth.h outside audio/synth/. This inconsistency is unfortunate, but I do not plan to change this.)

----

Includes will be ordered from most coupled to least coupled. This ensures that tightly coupled headers don't depend on incidental dependencies.

## Dependencies (3rdparty)

Dependencies may originate from Git repositories (either release/tag, or master), or release tarballs.

To import dependencies from Git repositories, follow the tutorial at https://www.atlassian.com/git/tutorials/git-subtree , under heading "Adding the sub-project as a remote".

```sh
git subtree [add|pull] --prefix folder_to_create \
    remote_or_url branch_or_commit \
    --squash
```
