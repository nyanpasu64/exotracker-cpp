## Code architecture

I have a [Google Drive folder of design notes](https://drive.google.com/drive/u/0/folders/15A1Td92HofO7KQ62QtuEDSmd4X1KKPAZ).

Code is stored in src/, dependencies in 3rdparty/.

Classes have member variables prefixed with a single underscore, like `_var`, to distinguish from locals. Not all classes follow this convention yet (but they should eventually).

src/gui/history.h has `gui::history::History`. There is only 1 instance, and it owns tracker pattern state. The pattern editor and audio thread read from `History`.

## Document architecture

The History class stores two copies of the current document, each located behind its own mutex. This operates analogously to double-buffered displays. Each can be designated as the front or back document. The "front document" is shown to the GUI and read by the audio thread, and never modified. The "back document" is not shown to the GUI or read by the audio thread, but mutated in response to user input.

The interesting part is in `DocumentStore::history_apply_change()`. When the user edits the document, the program runs the edit function on the back document, swaps the roles of the documents (page flip), then runs the edit function on the new back document (which the audio thread may still be holding its mutex, so the GUI thread may block). This results in O(2*edit size) edit time instead of O(entire document) per edit. One risk of this design is that the two copies may desync; one way that may happen is if `command.redo()` is not deterministic.

Each document is protected by a pseudo-rwlock (backed by a mutex) for two threads. The GUI does not acquire a lock when reading, but acquires a lock (exclusive reference) when writing. The audio thread acquires a lock when reading from the document; it cannot safely write, since the GUI can read simultaneously without locking the mutex.

Since Document is mutable, it should almost never be copied (except when initializing the double-buffer), since a deep copy would be very slow. As a result, you need to explicitly call `Document.clone()` when you make a copy. By contrast, copying an immutable persistent data structure (my previous design) is merely a pointer copy and atomic refcount increase.

## Code style

I am following some Rust-inspired rules in the codebase.

- Polymorphic classes are non-copyable. Maybe non-polymorphic classes (not value structs) too.
- Mutable aliasing is disallowed, except when mandated by third-party libraries (Blip_Synth points to Blip_Buffer, nsfplay APU2 points to APU1).
- Self-referential structs are disallowed, except when mandated by third-party libraries (blip_buffer and rtaudio), in which case the copy and move constructors are disabled (or move constructor is manually fixed, in the case of Blip_Buffer).
- Inheritance and storing data in base classes is discouraged.

In audio code, "out parameters" are common since they don't require allocations. Heap allocations can take a long time, pool allocations (I don't know much) may be faster, writing into an existing buffer is free.

In function signatures, out parameters (mutable references primarily written to, not read from) are placed at the end of the argument list. This follows the [Google Style Guide](https://google.github.io/styleguide/cppguide.html#Output_Parameters).

## Document format (unfinished, subject to change)

In FamiTracker, the list of valid patterns is not exposed to the user. You can often dredge up old patterns by manually changing pattern IDs in the sequence table. You can ask FT to "Remove unused patterns".

In Exotracker, PatternID will be an opaque identifier not visible to the user. Non-shared patterns are shown on the GUI as a dash. Shared pattern are shown on the GUI as the SequenceIndex of its first usage. This will make "dredging up old patterns" impossible.

I have never had a use for FT's design. However apparently other people use this feature.

----

File-size efficiency (deleting unused patterns when saving to disk) was never my goal. The mental model is that patterns belong to frames, aka sequence entries.

There's no reason i couldn't implement both ft-style "sequence to pattern mapping" and filesystem-style "pattern IDs are hidden from the user" GUIs, on the same data format. And "delete unused patterns" would be a toggle option.

## Audio architecture

Design notes at https://docs.google.com/document/d/17g5wqgpUPWItvHCY-0eCaqZSNdVKwouYoGbu-RTAjfo .

There is only 1 audio thread, spawned by RtAudio and periodically running our callback. This is how OpenMPT works as well.

The audio system (`OverallSynth`) is driven by sound synthesis callbacks (operates on a pull model). Every time RtAudio calls the audio callback which calls `OverallSynth.synthesize_overall()`, OverallSynth synthesizes a fixed number of samples, using `EventQueue` to know when to trigger new ticks (frame or vblank).

By contrast, FamiTracker's synth thread pushes to a queue with backpressure.

Alternatives to this design:

- Synthesizing 1 tick of audio at a time from the callback thread is also an acceptable option.
- Synthesizing audio in a separate "synth thread", and splitting into fixed-size chunks queued up and read by the "output thread", is unacceptable since it generates latency. (This is how FamiTracker works.) Even with a length-0 queue, the synth thread can run 1 audio block ahead of the output thread.

### RtAudio audio output

AudioThreadHandle sends audio to computer speakers, and is intended for GUI mode with concurrent editing of the document during playback. AudioThreadHandle uses doc::GetDocument to handles audio/GUI locking and sends a raw `Document const &` to OverallSynth.

In the absence of concurrent editing, you can use OverallSynth directly and avoid locking and unlocking std::mutex.

This has precedent: I believe libopenmpt does not talk directly to an output device, but merely exposes a callback api with no knowledge of locks or an audio library (here, RtAudio). (OpenMPT allows simple edits to patterns without locks! Complex edits require locking though.) libopenmpt can be called via ffmpeg or foobar2000, which have their own non-speaker output mechanisms.

### Switching from PortAudio to RtAudio

In PortAudio, there's 1 RAII object to initialize PortAudio (`AutoSystem`), one object representing the session (`System`), one object per stream (`Stream`).

In RtAudio, the `RtAudio` object is both the interface to a list of devices, and stream connected to one device.

Example of RtAudio usage in OpenMPT: https://github.com/OpenMPT/openmpt/blob/07f8c29b37f2bf9696e2f5ffb6eef0152e7fd4cf/sounddev/SoundDeviceRtAudio.cpp#L139

### Audio components

`OverallSynth` owns a list of sound chips: `vector<ChipIndex -> unique_ptr<ChipInstance subclass>>`. Which chips are loaded, and in what order, is determined by the current document's properties. Each `ChipInstance` subclass can appear more than once, allowing you to use the same chip multiple times (not possible on 0CC-FamiTracker).

Each `ChipInstance` handles the channels/drivers/synthesis internally, so `OverallSynth` doesn't need to know how many channels each chip has. This reduces the potential for logic errors and broken invariants (different parts of the program disagreeing about chip/channel layout).

`ChipInstance` subclasses (sound chip objects) are defined in `synth/nes_2a03.cpp` etc. The header (`synth/nes_2a03.h` etc.) exposes factory functions returning `unique_ptr<ChipInstance>` base-class pointers. All methods except `ChipInstance::run_chip_for()` are pure virtual (implemented in subclasses).

----

Each `ChipInstance` subclass `(Chip)Instance` has an associated `(Chip)ChannelID` enum (or enum class), also found as `(Chip)Instance::ChannelID`, specifying which channels that chip has. `(Chip)Instance` owns a sequencer (`ChipSequencer<(Chip)Instance::ChannelID>`), driver `(Chip)Driver`, and sound chip emulator (from nsfplay).

Each sound chip module (`ChipInstance` subclass) can be run for a specific period of time (nclock: `ClockT`). The sound chip's synthesis callback (`ChipInstance::run_chip_for()`) is called by `OverallSynth`, and alternates running the synthesizer to generate audio for a duration in clock cycles (`ChipInstance::synth_run_clocks()`), and handling externally-imposed ticks or internally-timed register writes. On every tick (60 ticks/second), the synthesis callback returns, `OverallSynth` calls `ChipInstance::driver_tick()`, `ChipInstance`'s sound driver fetches event data from the sequencer and determines what registers to write to the chip (for new notes, instruments, vibrato, etc), and `OverallSynth` calls the synthesis callback again. On every register write, the synthesis callback writes to the registers of its sound chip (`ChipInstance::synth_write_memory()`) before resuming synthesis.

Audio generated from separate emulated sound chips is mixed linearly (addition or weighted average). Each sound chip returns premixed audio from all channels, instead of returning each channel's audio level. (Why? I use nsfplay to emulate sound chips; each nsfplay chip returns premixed audio from all channels. Also many Famicom chips mix their channels using nonlinear or time-division mixing.) Each sound chip can output audio by either writing to `OverallSynth._nes_blip` (type: `Blip_Buffer`), or writing to an temporary buffer passed into the sound chip callback.

Sound chip emulation (audio synthesis) will *not* be implemented in my future NSF driver (since it's handled by hardware or the NSF player/emulator).

----

The sequencer (`ChipSequencer<ChannelID>` holding many `ChannelSequencer`) is implemented in C++, and converts pattern data from "events placed at beats" into "events separated by ticks". This is a complex task because events are located at beat fractions, not entries in a fixed array (which other trackers use). So when performing NSF export, exotracker will likely use the C++ sequencer to compile patterns into a list of (delay, event), much like PPMCK or GEMS. The NSF driver will only have a simplified sequencer.

In the remainder of this section, I ignore NSF export and focus on PC playback. Each `ChannelSequencer` remembers its position in a document, but not what document it's playing (except as a performance optimization). Every call to `ChannelSequencer` will pass the latest document available as an argument (which changes every time the user edits the document's patterns or global options). This is not implemented yet, though.

`ChipSequencer<ChannelID>` is defined in `audio/synth/sequencer.h`. It owns `EnumMap<ChannelID, ChannelSequencer>` (one `ChannelSequencer` for every channel the current chip has).

----

Each chip's sound driver (`(Chip)Driver`) is called once per tick. It handles events from the sequencer, as well as volume envelopes and vibrato. It generates register writes once per tick (possibly with delays between channels). It will be implemented separately in C++ and NSF export.

Each `(Chip)Instance` subclass owns a `(Chip)Driver` owning several `(Chip)(Channel)Driver`, neither of which inherits from global interfaces. Drivers are only exposed to their owning instance, not to `OverallSynth` or beyond.

My current API provides no way for drivers to *read* from sound chips' address spaces. Channel drivers can use custom APIs to tell their owning chip driver how to handle chip-wide register writes: after all channel drivers are done running (and generating register writes), the chip driver knows the desired state of each channel and can generate more register writes. (For example, the 5B chip has a single 6-bit register telling the chip to enable/disable tone/noise for each of the 3 channels.)

j0CC handles this differently. CAPU (hardware chip synth) Read() method, but it's completely unused. The 5B uses static globals, rather than a ChipDriver class, to write chip-wide registers.

## Rules for avoiding circular header inclusion

C++ headers malfunction when there is a `#include` cycle. I have designed some rules to prevent inclusion cycles (ensure topological sortability).

Abstract files (in order from common to derived). Each file can include all files listed above.

- ../*_common.h
- ./general_common.h
    - External libraries: none
- ./specific_common.h
    - External libraries: specific-related libraries
- ./specific/nested_common.h, if used
- ./specific/nested.h
- ./specific.h (public interface of . folder)
- ./general.h, if used
- ../app.cpp will interface mostly with ./specific.h (a public interface). I should try C++ modules.

----

Concrete files in the "audio" folder (in order from common to derived). Each file can include all files listed above.

- audio_common.h (general audio types and functions, like Amplitude = s16)
- output.h
    - Depends on output-related libraries (RtAudio).
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

To import dependencies from Git repositories:

```sh
git subrepo clone <remote-url> [<subdir>]
```

*Older discussion about Git dependencies:*

`git subtree` is useful in theory. In practice, it squashes the remote repo into a single commit, then performs a subtree merge (the remote repo becomes a subdir in exotracker's filesystem).

The issue is that rebasing and reapplying the squashed commit will write that commit into / instead of /3rdparty/repo/, whether I use plain rebase or `git rebase --rebase-merges`. Since [Git 2.24.0 (commit 917a319)](https://github.com/git/git/commit/917a319ea59c130a14cff7656537ba14f593568b), you can specify `git rebase --rebase-merges --strategy subtree`, which works, but is a complex and ugly incantation.

My new preference is to just squash the subtree merge commit out of existence, which simplifies the commit graph too. I lose the ability to `git subtree pull`, but that's unimportant since the upstreams I'm using barely move at all (except for gsl and immer).
