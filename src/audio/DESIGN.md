*Note that this document is probably out-of-date and no longer useful.*

Dependency graph of .h files. Top-level headers `#include` nested headers.

All paths are relative to src/audio/. Paths starting with / are relative to src.

The world includes /audio.h.

- /audio.h
    - output.h (does NOT include synth.h, to reduce recompilation.)

output.cpp includes synth.h.

- synth.h
    - synth_common.h
        - audio_common.h
        - event_queue.h
        - make_blip_buffer.h
        - /chip_common.h
        - /doc.h
            - /chip_common.h
    - audio_common.h
    - synth/nes_2a03.h (impl ChipInstance)
        - synth_common.h

synth/nes_2a03.cpp includes synth/nes_2a03_driver.h.

- synth/nes_2a03_driver.h
    - synth/music_driver_common.h (nearly empty)
        - synth_common.h
        - sequencer.h
            - doc.h
            - chip_common.h
    - chip_kinds.h (rebuilt whenever chips added or modified)

Dependencies:

- synth/music_driver_common.h cares about Address and Byte and ClockT.
- synth/nes_2a03.cpp cares about RegisterWrite{Address, Byte}, ClockT, and ChipKind.

Currently, all of the above are defined in synth_common.h. This means that editing the ChipKind enum (to add more chips) will rebuild

chip_kinds.h contains a list of sound chips, etc. It can be used by synth.h and music_driver.h. synth/nes_2a03_driver.h has no reason to view the overall list of sound chips, and the channels available to other sound chips. But it's simpler than moving Apu1ChannelID to an APU1-specific file.
