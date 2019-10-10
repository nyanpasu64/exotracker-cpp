# Code Architecture

Code is stored in src/, dependencies in 3rdparty/.

src/gui/history.h has `gui::history::History`. There is only 1 instance, and it owns tracker pattern state. The pattern editor and audio thread read from `History`.

Tracker pattern state is immutable, using data structures supplied by the immer library. This allows the audio thread to read it without blocking. The UI thread creates edited copies of the old state, using structural sharing to avoid copying unmodified data, then atomically updates `History.current`.

There is only 1 audio thread, which runs callbacks called by PortAudio, like OpenMPT. Unlike FamiTracker, this program does not have a separate synth and output thread.
