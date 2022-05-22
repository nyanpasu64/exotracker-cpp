#pragma once

#include "validate_common.h"
#include "doc.h"
#include "util/copy_move.h"

#include <fmt/format.h>  // format.h contains fmt::memory_buffer, core.h doesn't
#include <fmt/compile.h>

#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>

namespace doc::validate {

// When the user attempts to load a document with warnings or errors,
// I think it's useful to explain what caused the warnings or errors.
// The message style I chose was to print the "path" to the invalid value
// (for example "samples[0].brr.size()").
//
// My code accomplishes this by having each function call "annotate" all messages
// produced by nested calls with the field the nested call was called on.
// For example, `load_impl` may call `load_samples(gen_doc.samples)`,
// which may in turn call `load_sample(gen_samples[0])`, which errors on ".brr.size()".
// I need to generate the path "samples[0].brr.size()".
//
// I decided to stringify each error's path as soon as it is produced,
// and push it into a mutable list of errors passed as a parameter by reference.
// Each caller writes its portion of the path to a fmt::memory_buffer
// (a preallocated 500-byte memory buffer used to avoid extra allocations).
// The buffer is read when logging warnings or errors, or when logging a fatal error
// after an exception is thrown and the stack is unwound.
//
// I decided against lazily computing the path only when encountering warnings,
// because it would result in awkward syntax, C++ lambda bloat,
// and it's risky to perform string formatting during stack unwinding.
//
// I decided against computing path suffixes when a leaf encounters a warning
// and generating path prefixes only when returning a warning from a nested call,
// because computing path prefixes during stack unwinding is necessary
// to generate an error message, but risky after a std::bad_alloc.

/// ErrorState is only constructed once on the top level,
/// and a reference is passed into load function calls.
///
/// `msg` tracks the current path within a document, which is used to label
/// warning/error messages with where in the document they occurred.
///
/// All errors encountered are logged in `err`.
///
/// TODO: Also print the byte address in the file that the error occurred on,
///     so users can hex-edit files to fix them.
///     (IDK how to achieve this in capnp, for value types and/or pointer types.
///     I'd also have to handle the "not present in file, using default value" case.)
struct ErrorState {
    /// All current error messages.
    Errors err;

    /// True if no errors have been pushed.
    bool ok = true;

    /// A memory buffer used for holding the current error message or prefix
    /// (eg. "timeline[1].chip_channel_cells").
    /// Gets pushed or popped whenever a load_*() function gets called or returns,
    /// or an error message is created to be copied into a std::string.
    ///
    /// Throwing an exception may cause dropped pop() calls, but this is not a problem
    /// because throwing an exception ~~ignores ErrorState~~
    /// and prevents all future non-exception messages (which rely on ErrorState)
    /// from being produced.
    fmt::memory_buffer msg;
};

/// ErrorPrefixer is constructed in each non-leaf loading function.
///
/// Whenever a non-leaf loading function calls another function
/// and passes in a portion of the document,
/// the caller uses ErrorPrefixer::push[_literal]() to append to the fmt::memory_buffer
/// the called's portion's path relative to the caller's portion's path.
/// For example, when `Samples load_samples(ErrorState & state, GenSamples gen_samples)`
/// calls `load_sample(state, gen_samples[0])`, it only uses ErrorPrefixer to push
/// "[0]" to `state.msg`.
///
/// After the called function returns, the caller uses ErrorPrefixer::pop()
/// to truncate the buffer to the original size (without the pushed component).
///
/// (One option is to use RAII to automate this pop() call,
/// but I explicitly *don't* want pop() to be called upon an exception,
/// so I can get a trace of where the exception happened.
/// It's possible to skip destructor calls when unwinding, but it's tricky
/// to get right, and adds an unnecessary destructor call to unwinding.)
class ErrorPrefixer {
    size_t _initial_prefix;

// impl
public:
    ErrorPrefixer(ErrorState const& state)
        : _initial_prefix(state.msg.size())
    {}

    DEFAULT_MOVE(ErrorPrefixer)
    DISABLE_COPY(ErrorPrefixer)

    // Calling fmt::format_to() on a non-empty fmt::memory_buffer appends to the buffer.
    // This makes it easy to generate a path within the document.

    /// See #define PUSH().
    template <typename... Args>
    void push(
        ErrorState & state, fmt::format_string<Args...> str, Args&&... args
    ) const {
        fmt::format_to(std::back_inserter(state.msg), str, std::forward<Args>(args)...);
    }

    /// See #define PUSH_LITERAL().
    void push_literal(ErrorState & state, std::string_view new_prefix) const {
        assert(state.msg.size() == _initial_prefix);

        // https://github.com/fmtlib/fmt/issues/2237 but why
        fmt::format_to(std::back_inserter(state.msg), FMT_COMPILE("{}"), new_prefix);
    }

    /// See #define POP().
    void pop(ErrorState & state) const {
        // Does not reduce capacity, regardless of whether using heap allocation or not.
        state.msg.resize(_initial_prefix);
    }
};

// I chose to not feed the format string into FMT_COMPILE():
//
// > Format string compilation can generate more binary code compared to the default API
// > and is only recommended in places where formatting is a performance bottleneck.
//
// Additionally, calling PUSH(FORMAT, ...) with no extra arguments
// breaks on compilers other than MSVC,
// requiring __VA_OPT__ (C++20) or ## (GCC-only, ignored on MSVC) to compile.
#define PUSH(...) \
    prefix.push(state, __VA_ARGS__)

#define PUSH_LITERAL(str) \
    prefix.push_literal(state, str)

#define POP() \
    prefix.pop(state)

/// Insert a new `Error` into an `ErrorState`.
///
/// The error message is produced by concatenating the current path prefix
/// stored in `state.msg`, with the format string/args passed in `args`.
///
/// See #define PUSH_WARNING() and PUSH_ERROR().
template <ErrorType type, typename... Args>
void push_err_fmt(ErrorState & state, fmt::format_string<Args...> str, Args&&... args) {
    if constexpr (type == ErrorType::Error) {
        state.ok = false;
    }

    size_t prefix = state.msg.size();

    // push to end
    fmt::format_to(std::back_inserter(state.msg), str, std::forward<Args>(args)...);

    // read message
    state.err.push_back(Error{
        type, std::string(state.msg.begin(), state.msg.end())
    });

    // pop to original size
    state.msg.resize(prefix);
}

#define PUSH_WARNING(...) push_err_fmt<ErrorType::Warning>(__VA_ARGS__)
#define PUSH_ERROR(...) push_err_fmt<ErrorType::Error>(__VA_ARGS__)

// This could be turned into a function by forwarding variadic arguments into
// fmt::format.
#define ERR_FMT(...)  Error{ErrorType::Error, fmt::format(__VA_ARGS__)}

/// Used to continue on after fatal errors,
/// while tracking whether a fatal error has occurred (if so, return nullopt).
///
/// Constructed in fallible functions (which may return one or more fatal errors),
/// and controls whether they return a value or not.
///
/// You're not required to use it (it's semi-deprecated),
/// but it allows using PUSH_FATAL()
/// and simplifies producing multiple fatal errors from one function
/// (by continuing instead of returning immediately after the first),
/// and returning std::nullopt if one or more have occurred.
struct ErrorFrame {
    // TODO kill this variable and make PUSH_FATAL() use `state` directly.
    ErrorState & state;

    /// Must equal whether this frame has pushed a fatal error to `state.err`.
    ///
    /// See #define PUSH_FATAL().
    bool has_fatal = false;

// impl
    ErrorFrame(ErrorState & state_)
        : state(state_)
    {}
};

#define PUSH_FATAL(FRAME, ...) \
    PUSH_ERROR((FRAME).state, __VA_ARGS__); \
    (FRAME).has_fatal = true

// Validator functions

using std::optional;

[[nodiscard]] doc::SequencerOptions validate_sequencer_options(
    doc::SequencerOptions options, ErrorState & state
);

size_t truncate_frequency_table(ErrorState & state, size_t gen_size);
[[nodiscard]] doc::FrequenciesOwned validate_frequency_table(
    ErrorState & state, doc::FrequenciesRef freq_table, size_t valid_size
);

[[nodiscard]] doc::SampleTuning validate_tuning(
    ErrorState & state, doc::SampleTuning tuning
);

[[nodiscard]] doc::Sample validate_sample(ErrorState & state, doc::Sample sample);

size_t truncate_samples(ErrorState & state, size_t gen_size);

[[nodiscard]] InstrumentPatch validate_patch(ErrorState & state, InstrumentPatch patch);

size_t truncate_keysplits(ErrorState & state, size_t gen_nkeysplit);

size_t truncate_instruments(ErrorState & state, size_t gen_ninstr);

[[nodiscard]] optional<size_t> validate_nchip(ErrorState & state, size_t gen_nchip);

// TODO is it worth performing a bitwise copy of ChipKind from capnp to C++ enums,
// then checking that the values are valid?
// This reduces boilerplate and prevents capnp/C++'s values from desyncing,
// but also increases coupling and doesn't make sense when loading text to C++ enums.

[[nodiscard]] optional<size_t> validate_nchip_matches(
    ErrorState & state, size_t gen_nchip, size_t nchip
);

struct ChipMetadata {
    ChipKind chip_kind;
    uint32_t nchan;
};

// TODO replace with `using ChipToNchan = vector<uint32_t>`?
//
// ChipMetadatas will be unnecessary once we change the file format by replacing
// vector<ChipKind> with vector<(ChipKind, nchan)>. But this breaks file compatibility.
using ChipMetadatas = std::vector<ChipMetadata>;
using ChipMetadataRef = gsl::span<ChipMetadata const>;

// TODO store per-chip nchan in documents, rather than computing it from chip kind?
// Eventually unrecognized chips will be a warning and produce silence,
// rather than being an error.
ChipMetadatas compute_chip_metadata(gsl::span<const ChipKind> chips);

[[nodiscard]] optional<size_t> validate_nchan_matches(
    ErrorState & state,
    size_t gen_nchan,
    ChipMetadataRef chips_metadata,
    size_t chip_idx);

[[nodiscard]] doc::Effect validate_effect(ErrorState & state, doc::Effect effect);

[[nodiscard]] TickT validate_anchor_tick(ErrorState & state, TickT time);

[[nodiscard]] size_t truncate_effects(ErrorState & state, size_t gen_neffect);

[[nodiscard]] TimedRowEvent validate_event(
    ErrorState & state, TimedRowEvent timed_ev, TickT pattern_length
);

[[nodiscard]] size_t truncate_events(ErrorState & state, size_t gen_nevent);

[[nodiscard]] EventList validate_events(ErrorState & state, EventList events);

[[nodiscard]] optional<Pattern> validate_pattern(ErrorState & state, Pattern pattern);

[[nodiscard]]
optional<TrackBlock> validate_track_block(ErrorState & state, TrackBlock block);

[[nodiscard]] ChannelSettings validate_channel_settings(
    ErrorState & state, ChannelSettings settings
);

[[nodiscard]] size_t truncate_blocks(ErrorState & state, size_t gen_nblock);

[[nodiscard]] uint8_t validate_effect_name_chars(ErrorState & state, uint8_t gen_nchar);

}
