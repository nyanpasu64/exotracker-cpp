#include "spc_export.h"
#include "spc_export/driver.h"
#include "spc_export/link.h"
#include "doc/gui_traits.h"
#include "doc/validate.h"
#include "util/enumerate.h"
#include "util/release_assert.h"

#include <gsl/span>
#include <kj/filesystem.h>

#include <algorithm>  // std::copy
#include <array>
#include <cmath>  // std::round
#include <cstdint>
#include <cstring>  // memcpy, strncpy
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace spc_export {

using link::Symbol;
using link::Object;
using link::Linker;

using namespace doc;
using doc::validate::ErrorState;
using doc::validate::ErrorPrefixer;

/// Finds the length to which you can truncate the array
/// while keeping all non-nullopt elements.
template<typename T>
[[nodiscard]] inline size_t leading_size(gsl::span<const std::optional<T>> data) {
    for (size_t i = data.size(); i--; ) {
        if (data[i].has_value()) {
            return i + 1;
        }
    }
    return 0;
}

namespace samples {
    using SamplesRef = gsl::span<const std::optional<Sample>>;

    struct BinSamples {
        /// no symbol
        Object sample_dir;

        /// Symbol::SampleBank
        Object sample_bank;
    };

    static constexpr size_t AMK_SAMPLE_MAX = 0x7F;

    // If we change the code to skip unused samples, we must also change the code to
    // create a sample map.
    [[nodiscard]] static std::optional<BinSamples> compile_samples(
        ErrorState & state, SamplesRef samples
    ) {
        // TODO deduplicate with audio/synth/spc700_driver.cpp#Spc700Driver::reload_samples()
        // TODO reduce MAX_SAMPLES to 0x80 or eventually 0xE0, and add all missing u8
        // bounds checks

        auto num_samples = leading_size(samples);
        if (num_samples > AMK_SAMPLE_MAX + 1) {
            PUSH_ERROR(state,
                "Highest sample {:02X} exceeds maximum export sample {:02X}",
                num_samples - 1, AMK_SAMPLE_MAX);
            return {};
        }

        samples = samples.subspan(0, num_samples);

        // List of pointers to samples.
        auto sample_dir = Object({});
        // Sample data.
        auto sample_bank = Object(Symbol::SampleBank);

        for (auto const& [i, sample] : enumerate<size_t>(samples)) {
            if (sample.has_value()) {
                auto sample_begin = sample_bank.size();
                auto sample_loop = sample_begin + sample->loop_byte;
                if (sample_begin + sample->brr.size() > 0xFFFF) {
                    PUSH_ERROR(state,
                        "Sample {:02X} with size {} exceeded 64 KB in samples alone!!!",
                        i, sample->brr.size());
                    return {};
                }

                // Push begin-playback address.
                sample_dir.push_reloc(Symbol::SampleBank, (uint16_t) sample_begin);
                // Push loop address.
                sample_dir.push_reloc(Symbol::SampleBank, (uint16_t) sample_loop);
                // Push sample data to bank at sample_begin.
                sample_bank.data().insert(
                    sample_bank.data().end(), sample->brr.begin(), sample->brr.end()
                );
            } else {
                sample_dir.push_reloc(Symbol::SampleBank, 0);
                sample_dir.push_reloc(Symbol::SampleBank, 0);
            }
        }

        return BinSamples {
            .sample_dir = std::move(sample_dir),
            .sample_bank = std::move(sample_bank),
        };
    }
}
using samples::BinSamples;
using samples::compile_samples;

namespace instr {
    constexpr double CENTS_PER_OCTAVE = 1200.;

    [[nodiscard]]
    static uint16_t calc_tuning(ErrorState & state, SampleTuning const& tuning) {
        // Input: insmp/s, outsmp/s, root key, detune cents
        // Output: n: 16samples/cycle as u8.8

        double smp_per_cyc =
            double(tuning.sample_rate)
            * exp2(
                double(tuning.detune_cents) / CENTS_PER_OCTAVE
                - double(int(tuning.root_key) - 69) / 12.
            )
            / 440.;
        double block_per_cyc = smp_per_cyc / 16.;

        double tmp = block_per_cyc * 256.;
        if (!(1. <= tmp)) {
            PUSH_WARNING(state,
                " computed tuning {:#.2f} too low, setting to $00 $01", tmp
            );
            tmp = 1.;
        }
        if (!(tmp <= double(0xFF'FF))) {
            PUSH_WARNING(state,
                " computed tuning {:#.2f} too high, setting to $FF $FF", tmp
            );
            tmp = double(0xFF'FF);
        }

        auto smw_tuning = (uint16_t) std::round(tmp);
        return smw_tuning;
    }

    static void compile_patch(
        ErrorState & state,
        std::vector<uint8_t> & data,
        InstrumentPatch const& patch,
        Samples const& samples)
    {
        auto prefix = ErrorPrefixer(state);

        // TODO write unit tests
        // See https://sneslab.net/wiki/N-SPC_Engine#Instrument_Format for data format.

        // Sample 0x80 and above is interpreted as noise in AMK.
        // This may eventually be changed to 0xE0 and up.
        // assert(patch.sample_idx < 0x80);
        data.push_back(patch.sample_idx);

        auto adsr_hex = patch.adsr.to_hex();

        // Write ADSR.
        data.push_back(adsr_hex[0]);
        data.push_back(adsr_hex[1]);

        // TODO figure out release GAIN with or without modifying the driver
        data.push_back(0);

        prefix.push(state, "sample {:02X}: ", patch.sample_idx);
        MaybeSample const& sample = samples[patch.sample_idx];

        SampleTuning tuning;
        if (sample.has_value()) {
            tuning = sample->tuning;
        } else {
            PUSH_WARNING(state,
                "Missing sample {}, will not play correctly", patch.sample_idx
            );
            // Default SMW samples have 48 samples per cycle. Use 48 as a placeholder.
            tuning = SampleTuning {
                .sample_rate = 440 * 48,
                .root_key = 69,
            };
        }

        auto smw_tuning = calc_tuning(state, tuning);
        prefix.pop(state);

        // Write coarse and fine tuning.
        data.push_back(uint8_t(smw_tuning >> 8));
        data.push_back(uint8_t(smw_tuning & 0xff));
    }

    static constexpr size_t AMK_INSTR_MAX = 0xFF;
    using InstrumentsRef = gsl::span<const std::optional<Instrument>>;

    // TODO write InstrumentMap/compile_instrs() unit tests
    class InstrumentMap {
        InstrumentsRef _instrs;

        /// vec[i: InstrumentIndex] the AMK instrument corresponding to
        /// Instruments[i]->keysplit[0].
        std::vector<uint8_t> _amk_begin;

    public:
        InstrumentMap(InstrumentsRef instrs, std::vector<uint8_t> arr)
            : _instrs(instrs)
            , _amk_begin(std::move(arr))
        {}

        [[nodiscard]] std::optional<uint8_t> amk_instrument(
            ErrorState & state, InstrumentIndex instr_idx, Chromatic note
        ) const {
            if (!(instr_idx < _instrs.size() && _instrs[instr_idx])) {
                PUSH_WARNING(state, "Playing invalid instrument {:02X}", instr_idx);
                return {};
            }

            auto amk_idx = _amk_begin[instr_idx];
            auto const& keysplit = _instrs[instr_idx]->keysplit;

            // NOTE: Keep in sync with audio/synth/spc700_driver.cpp#find_patch().
            int curr_min_note = -1;
            std::optional<uint8_t> matching = {};

            for (auto const& [patch_idx, patch] : enumerate<size_t>(keysplit)) {
                if ((int) patch.min_note <= curr_min_note) {
                    // Invalid out-of-order patch, skip it.
                    continue;
                }
                curr_min_note = patch.min_note;

                // If the current patch is above the playing note, the previous patch
                // (if any) wins.
                if (note < patch.min_note) {
                    break;
                } else {
                    matching = uint8_t(amk_idx + patch_idx);
                }
            }
            // If the playing note is above the final patch (if any), it wins.

            if (!matching.has_value()) {
                PUSH_WARNING(state,
                    "Instrument {:02X} has no keysplit for pitch {}", instr_idx, note
                );
            }
            return matching;
        }
    };

    struct InstrumentResult {
        /// No symbol.
        Object object;
        InstrumentMap amk_map;
    };

    [[nodiscard]] static std::optional<InstrumentResult> compile_instrs(
        ErrorState & state, doc::Document const& doc
    ) {
        auto prefix = ErrorPrefixer(state);

        auto instrs = doc.instruments.dyn_span();
        release_assert_equal(instrs.size(), MAX_INSTRUMENTS);

        size_t num_instr = leading_size(instrs);
        instrs = instrs.subspan(0, num_instr);

        Object object({});
        std::vector<uint8_t> amk_map_data;
        amk_map_data.reserve(instrs.size());

        size_t curr = 0;
        for (auto const& [instr_idx, maybe_instr] : enumerate<size_t>(instrs)) {
            amk_map_data.push_back((uint8_t) curr);
            if (maybe_instr) {
                auto const& keysplit = maybe_instr->keysplit;
                if (keysplit.empty()) {
                    PUSH_WARNING(state,
                        "instrument {:02X} has zero keysplits, ignoring", instr_idx
                    );
                }

                curr += keysplit.size();
                if (curr > AMK_INSTR_MAX) {
                    PUSH_ERROR(state,
                        "Cannot add instrument {:02X} with {} keysplits, global keysplit limit is {}",
                        instr_idx, keysplit.size(), AMK_INSTR_MAX);
                    return {};
                }

                int curr_min_note = -1;

                // TODO unify KeysplitWarningIter and PUSH_WARNING/compile_patch()
                for (auto const& [patch_idx, patch] : enumerate<size_t>(keysplit)) {
                    prefix.push(state, "instrument {:02X} patch {}: ", instr_idx, patch_idx);

                    if ((int) patch.min_note <= curr_min_note) {
                        PUSH_WARNING(state,
                            "Min key {} out of order; patch will not play",
                            patch.min_note);
                    } else {
                        curr_min_note = patch.min_note;
                    }

                    compile_patch(state, object.data(), patch, doc.samples);
                    prefix.pop(state);
                }
            }
        }
        release_assert(instrs.size() == amk_map_data.size());
        return InstrumentResult {
            .object = std::move(object),
            .amk_map = InstrumentMap(instrs, std::move(amk_map_data)),
        };
    }
}
using instr::InstrumentMap;
using instr::InstrumentResult;
using instr::compile_instrs;

namespace music {
    struct BinMusic {
        std::array<Object, 8> channels;
        // TODO loop points?
        // TODO add an Object holding contents of subroutines
    };

    [[nodiscard]] static
    std::optional<BinMusic> compile_music(
        ErrorState & state, Document const& doc, InstrumentMap const& instr_map
    ) {
        /*
        See https://sneslab.net/wiki/N-SPC_Engine/Prototype#Voice_Command_Format and
        AMKFF Music::parseNote(). AMK-specific notes:

        - $00 = "end song".
        - $01-$7F ($80-$FF) = note duration (ticks).
        - $01-$7F $00-$7F = note duration and quantization.
        - $80-$C5 = note at pitch.
        - $C6 = tie.
        - $C7 = rest.
        - $DA = instrument.
        - $DB = pan.
        - $E2 $xx = tempo.
        - $E6 $00 ... $E6 $xx = "inline loop xx+1 times" (not found in stock SMW!)
        - $E7 = volume.
        - $E9 $LL $HH $xx = "call subroutine $HHLL xx times".

        To be continued.
        */
        std::array<Object, 8> channels = {
            Object(Symbol::Channel0),
            Object(Symbol::Channel1),
            Object(Symbol::Channel2),
            Object(Symbol::Channel3),
            Object(Symbol::Channel4),
            Object(Symbol::Channel5),
            Object(Symbol::Channel6),
            Object(Symbol::Channel7),
        };

        // Set song tempo to 60 SMW units.
        channels[0].push_u8(0xE2);
        channels[0].push_u8(60);

        auto prefix = ErrorPrefixer(state);
        // Push track data for each channel.
        for (size_t chan = 0; chan < 8; chan++) {
            prefix.push(state,
                "{}: ", doc::gui_traits::channel_name(doc, 0, (ChannelIndex) chan)
            );

            std::optional<uint8_t> amk_instr;

            // Set volume to 192. (Volume 64 comes out to level 01 which is
            // near-silent.)
            channels[chan].push_u8(0xE7);
            channels[chan].push_u8(192);

            // Set note duration to 48 ticks and unquantized.
            // The quantization byte is necessary, otherwise notes don't play.
            channels[chan].push_u8(0x30);
            channels[chan].push_u8(0x7F);

            // Add one note per channel.
            for (size_t beat = 0; beat < 8; beat++) {
                if (beat == chan) {
                    auto note = Chromatic(60 + 2 * chan);

                    auto prev_instr = amk_instr;
                    amk_instr =
                        instr_map.amk_instrument(state, (InstrumentIndex) chan, note);
                    if (amk_instr && amk_instr != prev_instr) {
                        channels[chan].push_u8(0xDA);
                        channels[chan].push_u8(*amk_instr);
                    }

                    if (amk_instr) {
                        auto amk_note = int(note) - 60 + 0x80 + 36;
                        if (amk_note < 0x80 || amk_note > 0xC5) {
                            PUSH_WARNING(state,
                                "at time TODO, out of bounds pitch {}", note
                            );
                            amk_note = 0x80 + 36;
                        }

                        // Notes begin at $80.
                        channels[chan].push_u8((uint8_t) amk_note);
                    } else {
                        // If missing instrument or mapping, insert rests instead of
                        // notes.
                        channels[chan].push_u8(0xC7);
                    }
                } else {
                    // Rest is $C7.
                    channels[chan].push_u8(0xC7);
                }
            }
            // End track data.
            channels[chan].push_u8(0);
            prefix.pop(state);
        }

        return BinMusic {
            .channels = std::move(channels),
        };
    }
}
using music::BinMusic;
using music::compile_music;

// Depends on BinMusic and called after music compilation (because we need to hard-code
// the address of each channel's loop point). So put it after.
namespace frame {
    struct BinFrames {
        /// Symbol::Frames.
        Object frames;
        /// Symbol::Patterns.
        Object patterns;
    };

    // Writing to frames could be decoupled from patterns.curr_pos()
    // if objects could export multiple symbols.
    // But that introduces complexity in of itself.

    [[nodiscard]] static
    BinFrames compile_frames(ErrorState & state, BinMusic const& music) {
        auto frames = Object(Symbol::Frames);
        auto patterns = Object(Symbol::Patterns);
        // See https://sneslab.net/wiki/N-SPC_Engine/Prototype#Phrase_Format.

        link::Offset frame_loop_point = frames.curr_pos();

        // Play the pattern.
        frames.push_reloc(Symbol::Patterns, patterns.curr_pos());
        patterns.push_reloc(Symbol::Channel0);
        patterns.push_reloc(Symbol::Channel1);
        patterns.push_reloc(Symbol::Channel2);
        patterns.push_reloc(Symbol::Channel3);
        patterns.push_reloc(Symbol::Channel4);
        patterns.push_reloc(Symbol::Channel5);
        patterns.push_reloc(Symbol::Channel6);
        patterns.push_reloc(Symbol::Channel7);

        // TODO if song has loop point set, set frame_loop_point = frames.curr_pos(),
        // and write/call new pattern pointing to each track's loop point.

        if (true) {
            // Either loop unconditionally....
            frames.push_u16(0x00FF);
            frames.push_reloc(Symbol::Frames, frame_loop_point);
        } else {
            // Or insert a "stop playback" command.
            frames.push_u16(0);
        }

        return BinFrames {
            .frames = std::move(frames),
            .patterns = std::move(patterns),
        };
    }
}
using frame::BinFrames;
using frame::compile_frames;


#define FAIL(state) \
    assert(!state.ok); \
    state.ok = false; \
    return

static void build_spc(
    ErrorState & state, std::vector<uint8_t> & spc, Document const& doc
) {
    auto maybe_samples = compile_samples(state, doc.samples.dyn_span());
    if (!maybe_samples) {
        FAIL(state);
    }
    BinSamples & samples = *maybe_samples;

    auto maybe_instrs = compile_instrs(state, doc);
    if (!maybe_instrs) {
        FAIL(state);
    }
    // Linker::add_object() takes an Object const&.
    InstrumentResult const& instrs = *maybe_instrs;

    auto maybe_music = compile_music(state, doc, instrs.amk_map);
    if (!maybe_music) {
        FAIL(state);
    }
    BinMusic & music = *maybe_music;

    spc.clear();
    // Resize the SPC file to 0x1'0200 bytes (256 bytes for metadata/SMP header, 65536
    // bytes for ARAM, 256 bytes for DSP footer).
    spc.resize(0x1'0200);

    // Write SPC header and ARAM zero page.
    std::copy(driver::spc_header.begin(), driver::spc_header.end(), spc.begin());

    auto header = gsl::span<uint8_t, 0x100>(spc.data(), 0x100);
    auto aram = gsl::span<uint8_t, 0x1'0000>(spc.data() + 0x100, 0x1'0000);
    auto footer = gsl::span<uint8_t, 0x100>(spc.data() + 0x1'0100, 0x100);

    // Write DSP footer.
    std::copy(driver::dsp_footer.begin(), driver::dsp_footer.end(), footer.begin());

    // Begin laying out ARAM.
    auto linker = Linker(aram, 0x400);

    // Write driver.
    {
        std::vector<uint8_t> driver;
        driver.assign(driver::driver.begin(), driver::driver.end());

        auto err = linker.add_object(Object({}, std::move(driver)));
        if (!err.empty()) {
            PUSH_ERROR(state, "failed to write driver: {}", err);
            return;
        }
    }

    // Write frames, instruments, and patterns (holding channel pointers).
    {
        BinFrames frames = compile_frames(state, music);
        auto err = linker.add_object(frames.frames);
        if (!err.empty()) {
            PUSH_ERROR(state, "failed to write frame list: {}", err);
            return;
        }

        // Custom instruments come directly after the frame list.
        err = linker.add_object(instrs.object);
        if (!err.empty()) {
            PUSH_ERROR(state, "failed to write instruments: {}", err);
            return;
        }

        // Patterns can come anywhere. Put them after instruments.
        err = linker.add_object(frames.patterns);
        if (!err.empty()) {
            PUSH_ERROR(state, "failed to write pattern table: {}", err);
            return;
        }
    }

    // Write channel data (Symbol::Channel0-7). The patterns' pointers are relocated
    // here.
    for (auto const& [i, channel] : enumerate<size_t>(music.channels)) {
        auto err = linker.add_object(channel);
        if (!err.empty()) {
            PUSH_ERROR(state, "failed to write channel {}: {}", i, err);
            return;
        }
    }

    // Write sample table and data.
    linker.align_address();
    auto sample_dir_reg = (uint8_t) (linker.current_address() >> 8);
    {
        auto err = linker.add_object(samples.sample_dir);
        if (!err.empty()) {
            PUSH_ERROR(state, "failed to write sample directory: {}", err);
            return;
        }

        err = linker.add_object(samples.sample_bank);
        if (!err.empty()) {
            PUSH_ERROR(state, "failed to write sample data: {}", err);
            return;
        }
    }

    auto unresolved_syms = linker.finalize();
    if (!unresolved_syms.empty()) {
        PUSH_ERROR(state, "Internal error: {} (report this bug!)", unresolved_syms);
        return;
    }

    // Write SPC metadata and SMP registers.
    {
        strncpy((char *) &header[0x2E], "Title", 32);
        strncpy((char *) &header[0x4E], "Game", 32);
        strncpy((char *) &header[0x7E], "Comment", 32);
        strncpy((char *) &header[0xB1], "Author", 32);

        // Playback duration (seconds)
        int duration_s = 300;
        header[0xA9] = uint8_t((duration_s / 100 % 10) + '0');
        header[0xAA] = uint8_t((duration_s / 10 % 10) + '0');
        header[0xAB] = uint8_t((duration_s / 1 % 10) + '0');

        // Fadeout duration (ms)
        header[0xAC] = '1';
        header[0xAD] = '0';
        header[0xAE] = '0';
        header[0xAF] = '0';
        header[0xB0] = '0';

        // Date SPC was dumped (MM/DD/YYYY)
        memcpy(&header[0x9E], "01/01/1970", 10);

        // Set the S-SMP program counter to the driver's main loop.
        header[0x25] = driver::mainLoopPos & 0xFF;
        header[0x26] = driver::mainLoopPos >> 8;

        // > The values of the [SMP] registers (besides stack which is in the file)
        // > don't matter.  They're 0 in the base file.
    }

    // Patch ARAM.
    {
        // 0x5F = FLG DSP register mirror (including the noise clock frequency).
        aram[0x5F] = 0x20;

        // Write to SPC IO ports to simulate CPU communication.
        // SPC IO ports are located from $00F4 to $00F7 (https://problemkaputt.de/fullsnes.htm#snesapuspc700ioports).
        // CPU IO ports are located from $2140 to $2143 (https://problemkaputt.de/fullsnes.htm#snesapumaincpucommunicationport).
        // Tell SPC to play song 1.
        aram[0xF6] = 1;

        // In the AMK driver, sending $02 to $00F5 enables Yoshi drums.
        // ExoTracker currently does not support Yoshi drum tracks.
        // We may eventually either remove Yoshi drums from the driver (to save space),
        // or expose it in the tracker (and allow toggling upon SPC export).
    }

    // Write DSP registers.
    {
        // 0x5D = sample directory.
        footer[0x5D] = sample_dir_reg;
    }
}

namespace {
// TODO deduplicate kj/filesystem utility functions
std::string_view string_view(kj::StringPtr str) {
    return std::string_view(str.begin(), str.size());
}

std::string_view format_file_type(kj::FsNode::Type type) {
    // I hope kj will *never* reorder the Type enum, append elements,
    // or construct invalid enum values.
    // The vendored kj will never change,
    // but updating capnp/kj may require changing this array.
    static std::string_view errors[] = {
        "FILE",
        "DIRECTORY",
        "SYMLINK",
        "BLOCK_DEVICE",
        "CHARACTER_DEVICE",
        "NAMED_PIPE",
        "SOCKET",
        "OTHER",
    };
    return errors[(size_t) type];
}

inline kj::ArrayPtr<const uint8_t> array_ptr(gsl::span<const uint8_t> span) {
    return kj::ArrayPtr<const uint8_t>(span.data(), span.size());
}

void save_file(ErrorState & state, gsl::span<uint8_t> data, char const* path) {
    kj::Own<kj::Filesystem> fs = kj::newDiskFilesystem();
    kj::Path abs_path = fs->getCurrentPath().evalNative(path);

    // Holds a reference to abs_path.
    kj::PathPtr parent = abs_path.parent();
    kj::PathPtr basename = abs_path.basename();

    auto dir_open = fs->getRoot().openSubdir(parent, kj::WriteMode::MODIFY);

    // If the input path is a directory, fail instead of replacing it with a file.
    // (This is a best-effort attempt, vulnerable to TOCTTOU race conditions,
    // but I don't care).
    auto maybe_metadata = dir_open->tryLstat(basename);
    KJ_IF_MAYBE(m, maybe_metadata) {
        // We replace files with new files (OK), and write through symlinks.
        // Other special file types are probably wrong.
        if (m->type != kj::FsNode::Type::FILE && m->type != kj::FsNode::Type::SYMLINK) {
            PUSH_ERROR(state, "cannot overwrite path \"{}\", has type {}",
                string_view(abs_path.toString(true)), format_file_type(m->type)
            );
            return;
        }
    }

    kj::Own<const kj::File> file =
        dir_open->openFile(basename, kj::WriteMode::CREATE | kj::WriteMode::MODIFY);

    file->writeAll(array_ptr(data));
}

// Too lazy to std::move on each call. So take a regular reference and move from it.
[[nodiscard]] ExportSpcResult result(ErrorState & state) {
    return ExportSpcResult {
        .ok = state.ok,
        .errors = std::move(state.err),
    };
}
}

/// See comments for serialize::save_to_path, for overview of path encoding.
ExportSpcResult export_spc(Document const& doc, char const* path) {
    ErrorState state;
    std::vector<uint8_t> spc;

    // Generate SPC file data.
    build_spc(state, spc, doc);
    if (!state.ok) {
        return result(state);
    }

    // Write SPC file to disk.
    auto maybe_exception = kj::runCatchingExceptions([&]() {
        save_file(state, spc, path);
    });
    KJ_IF_MAYBE(e, maybe_exception) {
        PUSH_ERROR(state, "Error saving file: {}", string_view(e->getDescription()));
        return result(state);
    }

    return result(state);
}

}

#ifdef UNITTEST

#include "chip_kinds.h"
#include "doc_util/sample_instrs.h"

#include <doctest.h>

namespace spc_export {

TEST_CASE("Test converting tuning into AMK") {
    ErrorState state;

    SUBCASE("Regular tuning") {
        auto tuning = SampleTuning {
            .sample_rate = 440 * 48,
            .root_key = 69,
            .detune_cents = 0,
        };

        auto amk_tuning = instr::calc_tuning(state, tuning);
        CHECK(amk_tuning == 0x0300);
        CHECK(state.err.empty());
    }

    SUBCASE("Higher sampling rate") {
        auto tuning = SampleTuning {
            .sample_rate = 440 * 80,
            .root_key = 69,
            .detune_cents = 0,
        };

        auto amk_tuning = instr::calc_tuning(state, tuning);
        CHECK(amk_tuning == 0x0500);
        CHECK(state.err.empty());
    }

    SUBCASE("Higher root key") {
        auto tuning = SampleTuning {
            .sample_rate = 440 * 80,
            .root_key = 69 + 12,
            .detune_cents = 0,
        };

        auto amk_tuning = instr::calc_tuning(state, tuning);
        CHECK(amk_tuning == 0x0280);
        CHECK(state.err.empty());
    }

    SUBCASE("Positive detune increases frequency") {
        auto tuning = SampleTuning {
            .sample_rate = 440 * 48,
            .root_key = 69,
            .detune_cents = 50,
        };

        auto amk_tuning = instr::calc_tuning(state, tuning);
        CHECK(amk_tuning > 0x0300);
        CHECK(state.err.empty());
    }

    SUBCASE("Negative detune decreases frequency") {
        auto tuning = SampleTuning {
            .sample_rate = 440 * 48,
            .root_key = 69,
            .detune_cents = -50,
        };

        auto amk_tuning = instr::calc_tuning(state, tuning);
        CHECK(amk_tuning < 0x0300);
        CHECK(state.err.empty());
    }

    SUBCASE("Test sampling rate underflow") {
        auto tuning = SampleTuning {
            .sample_rate = 1,
            .root_key = 69,
            .detune_cents = 0,
        };

        auto amk_tuning = instr::calc_tuning(state, tuning);
        (void) amk_tuning;
        CHECK(!state.err.empty());
    }
}

using doc_util::sample_instrs::spc_chip_channel_settings;

// Keep in sync with instrument_test().
constexpr size_t PATCH_COUNT = 4;

static Document instrument_test() {
    SequencerOptions sequencer_options{.target_tempo = 150, .ticks_per_beat = 48};

    Samples samples;
    samples[0] = Sample{};
    samples[1] = Sample{};
    samples[2] = Sample{};
    samples[3] = Sample{};

    Instruments instruments;
    instruments[1] = Instrument{
        .name = "",
        .keysplit = {
            InstrumentPatch { .min_note = 72, .sample_idx = 0 },
            InstrumentPatch { .min_note = 60, .sample_idx = 0 },
        },
    };
    instruments[2] = Instrument{
        .name = "",
        .keysplit = {
            InstrumentPatch { .min_note = 0, .sample_idx = 0 },
            InstrumentPatch { .min_note = 72, .sample_idx = 0 },
        },
    };

    ChipList chips{ChipKind::Spc700};

    ChipChannelSettings chip_channel_settings = spc_chip_channel_settings();

    Timeline timeline;

    timeline.push_back(TimelineFrame{
        .nbeats = 16,
        .chip_channel_cells = {{{}, {}, {}, {}, {}, {}, {}, {}}},
    });

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = move(samples),
        .instruments = move(instruments),
        .chips = move(chips),
        .chip_channel_settings = move(chip_channel_settings),
        .timeline = move(timeline),
    };
}

TEST_CASE("Test compile_instrs() and InstrumentMap mapping") {
    auto doc = instrument_test();

    SUBCASE("Successful") {
        ErrorState state;
        auto maybe_instrs = compile_instrs(state, doc);
        REQUIRE(maybe_instrs.has_value());
        // Should we check the contents of state.err?

        InstrumentResult const& instrs = *maybe_instrs;

        // Each AMK instrument is 6 bytes long.
        CHECK(instrs.object.size() == PATCH_COUNT * 6);

        // Instrument 0 is missing.
        state.err.clear();
        CHECK(instrs.amk_map.amk_instrument(state, 0, 60) == std::nullopt);
        CHECK(!state.err.empty());

        // In instrument 1, patch 0 (note 72+) hides patch 1.
        state.err.clear();
        CHECK(instrs.amk_map.amk_instrument(state, 1, 60) == std::nullopt);
        CHECK(!state.err.empty());

        state.err.clear();
        CHECK(instrs.amk_map.amk_instrument(state, 1, 72) == 0);
        CHECK(state.err.empty());

        // In instrument 2, patch 0 (AMK 2) precedes patch 1 (AMK 3).
        state.err.clear();
        CHECK(instrs.amk_map.amk_instrument(state, 2, 60) == 2);
        CHECK(state.err.empty());

        state.err.clear();
        CHECK(instrs.amk_map.amk_instrument(state, 2, 72) == 3);
        CHECK(state.err.empty());

        // Instrument 3 is missing.
        state.err.clear();
        CHECK(instrs.amk_map.amk_instrument(state, 3, 60) == std::nullopt);
        CHECK(!state.err.empty());
    }
}

}

#endif
