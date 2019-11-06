Dependency graph of .h files. Top-level headers `#include` nested headers.

- audio
    - output.h (does NOT include synth.h, to reduce recompilation. Only output.cpp includes synth.h.)

- synth
    - synth/nes_2a03
        - synth_common
            - event_queue
            - make_blip_buffer
    - music_driver
        - chips
            - synth_common
        - music_driver/nes_2a03
            - music_driver_common
    - chips
        - synth_common
    - synth_common

chips.h contains a list of sound chips, etc. It can be used by synth.h and music_driver.h, but not synth/nes_2a03 or other sound chips. This reduces recompilation.
