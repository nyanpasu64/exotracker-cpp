@0xd64b51b6e793ded4;
using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("serialize::generated");

# See doc.h and doc/*.h for more documentation.
#
# We reserve the right to break backwards compatibility upon format changes, until the
# format is explicitly stabilized. We may possibly add support for loading older
# incompatible formats, but this is unlikely because it requires multiple schemas(?).
#
# Some struct fields may be zero/null when loading files in a different schema version.
#
# Unrecognized table fields are *not* preserved when saving,
# to prevent old versions of fields from existing in parallel
# (and because my in-memory structs don't currently support it).
# Users will be warned when (opening? saving?) documents with with a newer version ID
# (or unknown fields?).

const magicNumber :Data = "EXO-MODU";
# Bytes which must be present at the start of the file,
# before data serialized by Cap'n Proto.

using Version = UInt32;
struct Versions {
  # Version history:
  #
  # - 1: initial
  # - 2 (incompatible): Change instrument format to only include min key, not max
  # - 3 (incompatible): Remove frames, replace beat fractions with ticks (amk-sequencer)

  const unknown :Version = 0;
  const minimum :Version = 3;
  const current :Version = 3;
}

### doc/events.h

struct MaybeEffect {
  # V1 of effect data structure. May be replaced in the future, creating V2 of
  # MaybeEffect, RowEvent, TimedRowEvent, Pattern, and perhaps more.

  name0 @0 :UInt8;
  name1 @1 :UInt8;
  # If both bytes are 0x00, then effect is absent and value is unspecified.
  # Otherwise, both bytes should be printable characters.
  #
  # This should be a 2-element array, but capnp doesn't support fixed-size arrays.

  value @2 :UInt8;
}


### doc/timed_events.h

struct TimedRowEvent {
  # A combination of the C++ types TimedRowEvent and RowEvent. I merged them to remove
  # an indirection in the binary format.

  anchorTick @0 :Int32;
  # Relative to pattern start. May be offset further through signed Gxx delay effects.

  note :union {
    none @1 :Void;
    some @2 :Int16;
    # [0..128) is a chromatic note.
    # -1 is a note cut. -2 is a release.
  }
  instr :union {
    none @3 :Void;
    some @4 :UInt8;
  }
  volume :union {
    none @5 :Void;
    some @6 :UInt8;
  }

  effects @7 :List(MaybeEffect);
  # If present, the vector is 1 through MAX_EFFECTS_PER_EVENT (8) items long.
  # exotracker always stores 8 effect slots in memory,
  # but skips saving empty slots after the last filled one.
}


### doc/timeline.h

struct Pattern {
  # A pattern holds a list of events. It also determines its own duration, while the
  # block holding it (or in the future each block referencing its ID) determines how
  # many times to loop it.

  lengthTicks @0 :Int32;
  events @1 :List(TimedRowEvent);
}

struct TrackBlock {
  # Each block (pattern usage) in a track has a begin time and loop count, and
  # references a pattern which stores its own length. Blocks can be placed at arbitrary
  # ticks, like AMK but unlike frame-based trackers.
  #
  # It is legal to have gaps between `TrackBlock` in a track where no events are
  # processed. It is illegal for `TrackBlock` to overlap in a track.

  beginTick @0 :Int32;  # Any negative side effects for switching C++ to UInt32?
  # Time in ticks.

  loopCount @1 :UInt32;

  pattern @2 :Pattern;
}

struct SequenceTrack {
  # One channel. Can hold multiple blocks at non-overlapping increasing times. Each
  # block *should* have nonzero length. Notes are cut upon each block end, to match AMK.

  blocks @0 :List(TrackBlock);

  # Contents of struct ChannelSettings:
  nEffectCol @1 :UInt8;
}


### doc/sample.h

struct SampleTuning {
  sampleRate @0 :UInt32;
  rootKey @1 :UInt8;
  detuneCents @2 :Int16;
}

struct MaybeSample { union {
  none @0 :Void;
  some :group {
    name @1 :Text;
    brr @2 :List(UInt8);
    loopByte @3 :UInt16;
    tuning @4 :SampleTuning;
  }
}}


### doc/instr.h

struct Adsr {
  attack @0 :UInt8;
  decay @1 :UInt8;
  sustain @2 :UInt8;
  release @3 :UInt8;
}

struct InstrumentPatch {
  minNote @0 :UInt8 = 0;
  # Do not use this patch for pitches below this value.

  sampleIdx @1 :UInt8;
  # The sample to play. If sample missing, acts as a key-off(???).

  adsr @2 :Adsr;
  # If I add GAIN support (either global GAIN, or upon instrument release?),
  # I may add more fields or turn adsr into a union.
}

struct MaybeInstrument { union {
  none @0 :Void;
  some :group {
    name @1 :Text;
    keysplit @2 :List(InstrumentPatch);
  }
}}


### doc.h

struct SequencerOptions {
  targetTempo @0 :Float64;
  # The target tempo to play the module at, in beats/minute. Controls the percentage of
  # timer ticks that trigger sequencer ticks. Note that the actual playback tempo will
  # not match this value exactly (due to rounding), and note times will jitter slightly
  # as well (increasing spcTimerPeriod increases note jitter).

  noteGapTicks @1 :UInt16;
  # How many sequencer ticks before a new note/rest to release the previous note. This
  # creates a gap between notes, but allows the previous note to fade to silence instead
  # of being interrupted by the next note creating a pop.
  #
  # Increasing targetTempo or ticksPerBeat reduces the duration of each sequencer tick.
  # Increasing spcTimerPeriod increases the jitter of each sequencer tick.

  ticksPerBeat @2 :UInt16;
  # The scaling factor used to convert *all* BPM tempos into sequencer tempos; also
  # determines *initial* visual beat length in the pattern editor.

  beatsPerMeasure @3 :UInt16;
  # Purely cosmetic; determines the initial visual measure length in the pattern editor.

  spcTimerPeriod @4 :UInt16;
  # Controls the period of the SPC timer, which controls when the engine advances.
  # Increasing this value causes the driver to run less often. This increases the amount
  # of note timing jitter, but decreases the likelihood of driver slowdown (taking too
  # long to run and falling behind).
  #
  # Valid values range from [1 .. 256] inclusive. The value will be written into the
  # SNES S-SMP timer divisor address ($00fa), except 256 (0x100) will be written as 0
  # instead (which acts as 256).
}

enum AccidentalMode {
  sharp @0;
  flat @1;
}

enum ChipKind {
  unknown @0;
  spc700 @1;
}

struct Document {
  version @0 :Version;
  sequencerOptions @1 :SequencerOptions;
  frequencyTable @2 :List(Float64);
  # Length must be exactly CHROMATIC_COUNT (128).
  # Shorter vectors will be rejected, longer will be rejected or truncated.

  accidentalMode @3 :AccidentalMode;

  ticksPerRow @4 :UInt16;
  # The number of ticks per row (row height, zoom out factor) to show on the pattern
  # editor. Purely visual.

  effectNameChars @5 :UInt8 = 1;
  # Whether effect names are 1 or 2 characters wide.
  # When set to 1, the first digit is hidden if it's 0,
  # and typing character c will write effect 0c immediately.

  samples @6 :List(MaybeSample);
  # Length 0 through MAX_SAMPLES (currently 256).
  # To save space, exotracker skips saving empty slots after the last filled one.

  instruments @7 :List(MaybeInstrument);
  # Length 0 through MAX_INSTRUMENTS (currently 256).
  # To save space, exotracker skips saving empty slots after the last filled one.

  chips @8 :List(ChipKind);
  # vector<ChipIndex -> ChipKind>
  # chips.size() in [1..MAX_NCHIP] inclusive (not enforced yet).

  sequence @9 :List(List(SequenceTrack));
}
