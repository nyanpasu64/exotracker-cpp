@0xd64b51b6e793ded4;
using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("serialize::generated");

# See doc.h and doc/*.h for more documentation.
#
# All table fields should be written and not left as null (minus obsolete fields)
# unless otherwise specified.
# (If we change the file format structure, we may make "backwards compatibility"
# optional, and give users a choice whether to write old formats
# in parallel with new.)
#
# Some table fields may be null when loading files in a different schema version.
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
  const unknown :Version = 0;
  const minimum :Version = 2;
  const current :Version = 2;
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
  # A combination of the C++ types TimedRowEvent and RowEvent.
  # I merged them to simplify the binary format.

  anchorBeatNum @0 :Int32;
  anchorBeatDen @1 :Int32;

  note :union {
    none @2 :Void;
    some @3 :Int16;
    # [0..128) is a chromatic note.
    # -1 is a note cut. -2 is a release.
  }
  instr :union {
    none @4 :Void;
    some @5 :UInt8;
  }
  volume :union {
    none @6 :Void;
    some @7 :UInt8;
  }

  effects @8 :List(MaybeEffect);
  # If present, the vector is 1 through MAX_EFFECTS_PER_EVENT (8) items long.
  # exotracker always stores 8 effect slots in memory,
  # but skips saving empty slots after the last filled one.
}


### doc/timeline.h

struct Pattern {
  # The length of a pattern is determined by its entry in the timeline (TimelineBlock).
  # However a Pattern may specify a loop length.
  # If set, the first `loop_length` beats of the Pattern will loop
  # for the duration of the TimelineBlock.

  events @0 :List(TimedRowEvent);

  loopLength @1 :UInt32;
  # Loop length in beats. If length is zero, don't loop the pattern.
}

struct TimelineBlock {
  # Each pattern usage in the timeline has a begin and end time.
  # To match traditional trackers, these times can align with the global pattern grid.
  # But you can get creative and offset the pattern by a positive integer
  # number of beats.

  beginTime @0 :Int32;  # Any negative side effects for switching C++ to UInt32?
  endTime @1 :UInt32;
  # 0xffff_ffff means "end of TimelineItem".
  # Other values are treated as integer beat numbers.

  pattern @2 :Pattern;
}

struct TimelineCell {
  # One timeline item, one channel.
  # Can hold multiple blocks at non-overlapping increasing times.

  blocks @0 :List(TimelineBlock);
}

struct TimelineItem {
  # One timeline item, all channels. Stores duration of timeline item.

  nbeatsNum @0 :UInt32;
  nbeatsDen @1 :UInt32;
  chipChannelCells @2 :List(List(TimelineCell));
}


### doc/sample.h

struct SampleTuning {
  sampleRate @0 :UInt32;
  rootKey @1 :UInt8;
  detuneCents @2 :Int16;
}

# Native support for optional fields in FlatBuffers was only added(?) in 2020,
# and has not appeared in any stable release yet.
# See https://github.com/google/flatbuffers/issues/6014 for more details.

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
  # The target tempo to play the module at, in beats/minute.

  spcTimerPeriod @1 :UInt32;
  # Controls the period of the SPC timer, which controls when the engine advances.

  ticksPerBeat @2 :UInt32;
  # Controls the number of "sequencer ticks" per beat.
  # Note that this field is signed in C++,
  # even though it would be better off unsigned there too.
}

enum AccidentalMode {
  sharp @0;
  flat @1;
}

enum ChipKind {
  unknown @0;
  spc700 @1;
}

struct PerChannelSettings {
  nEffectCol @0 :UInt8 = 1;
}
# kinda regretting using flatbuffers.
# in cap'n proto, i can just say List(List(ChannelSettings) or a generic type,
# instead of defining monomorphized wrapper types.

struct Document {
  version @0 :Version;
  sequencerOptions @1 :SequencerOptions;
  frequencyTable @2 :List(Float64);
  # Length must be exactly CHROMATIC_COUNT (128).
  # Shorter vectors will be rejected, longer will be rejected or truncated.

  accidentalMode @3 :AccidentalMode;

  zoomLevel @4 :UInt16;
  # The number of rows shown per beat.
  # Note that the "beat" system may change eventually.

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

  chipChannelSettings @9 :List(List(PerChannelSettings));

  timeline @10 :List(TimelineItem);
}
