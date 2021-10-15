#include "edit_sample_list.h"
#include "edit_impl.h"
#include "doc.h"
#include "util/compare.h"
#include "util/expr.h"
#include "util/release_assert.h"
#include "util/typeid_cast.h"

#include <limits>
#include <optional>
#include <utility>  // std::move

namespace edit::edit_sample_list {

using namespace doc;
using namespace edit_impl;

struct AddRemoveSample {
    SampleIndex index;
    std::optional<Sample> sample;

    void apply_swap(Document & doc) {
        if (sample.has_value()) {
            release_assert(!doc.samples[index].has_value());
        } else {
            release_assert(doc.samples[index].has_value());
        }
        std::swap(sample, doc.samples[index]);
    }

    static constexpr ModifiedFlags _modified = ModifiedFlags::SamplesEdited;
    using Impl = ImplEditCommand<AddRemoveSample, Override::None>;
};

static std::optional<SampleIndex> get_empty_idx(
    Samples const& samples, size_t begin_idx
) {
    for (size_t i = begin_idx; i < MAX_SAMPLES; i++) {
        if (!samples[i]) {
            return (SampleIndex) i;
        }
    }
    return {};
}

std::tuple<MaybeEditBox, SampleIndex> try_add_sample(
    Document const& doc, SampleIndex begin_idx, doc::Sample sample
) {
    auto maybe_empty_idx = get_empty_idx(doc.samples, begin_idx);
    if (!maybe_empty_idx) {
        return {nullptr, 0};
    }
    SampleIndex empty_idx = *maybe_empty_idx;

    return {
        make_command(AddRemoveSample {empty_idx, std::move(sample)}),
        empty_idx,
    };
}

struct ReplaceSample {
    SampleIndex index;
    std::optional<Sample> sample;

    void apply_swap(Document & doc) {
        std::swap(sample, doc.samples[index]);
    }

    static constexpr ModifiedFlags _modified = ModifiedFlags::SamplesEdited;
    using Impl = ImplEditCommand<ReplaceSample, Override::None>;
};

EditBox replace_sample(Document const& doc, SampleIndex idx, doc::Sample sample) {
    return make_command(ReplaceSample {idx, std::move(sample)});
}

std::tuple<MaybeEditBox, SampleIndex> try_clone_sample(
    Document const& doc, SampleIndex old_idx, SampleIndex begin_idx
) {
    static_assert(
        MAX_SAMPLES == 256,
        "Must add bounds check when decreasing sample limit");

    if (!doc.samples[old_idx]) {
        return {nullptr, 0};
    }

    auto maybe_empty_idx = get_empty_idx(doc.samples, begin_idx);
    if (!maybe_empty_idx) {
        return {nullptr, 0};
    }
    SampleIndex empty_idx = *maybe_empty_idx;

    return {
        // make a copy of doc.samples[old_idx]
        make_command(AddRemoveSample {empty_idx, doc.samples[old_idx]}),
        empty_idx,
    };
}

std::tuple<MaybeEditBox, SampleIndex> try_remove_sample(
    Document const& doc, SampleIndex sample_idx
) {
    static_assert(
        MAX_SAMPLES == 256,
        "Must add bounds check when decreasing sample limit");

    if (!doc.samples[sample_idx]) {
        return {nullptr, 0};
    }

    SampleIndex begin_idx = EXPR(
        // Find the next filled sample slot.
        for (size_t i = sample_idx + 1; i < MAX_SAMPLES; i++) {
            if (doc.samples[i].has_value()) {
                return (SampleIndex) i;
            }
        }
        // We're removing the last sample present. Find the new last sample.
        for (size_t i = sample_idx; i--; ) {
            if (doc.samples[i].has_value()) {
                return (SampleIndex) i;
            }
        }
        // There are no samples left. The return value doesn't really matter.
        return sample_idx;
    );

    return {
        make_command(AddRemoveSample {sample_idx, {}}),
        begin_idx,
    };
}

struct RenamePath {
    SampleIndex sample_idx;

    DEFAULT_EQUALABLE(RenamePath)
};

struct RenameSample {
    RenamePath path;
    std::string name;

    void apply_swap(Document & doc) {
        release_assert(doc.samples[path.sample_idx].has_value());
        std::swap(doc.samples[path.sample_idx]->name, name);
    }

    using Impl = ImplEditCommand<RenameSample, Override::CanMerge>;
    bool can_merge(BaseEditCommand & prev) const {
        if (auto * p = typeid_cast<Impl *>(&prev)) {
            return p->path == path;
        }
        return false;
    }

    // ModifiedFlags is currently only used by the audio thread,
    // and renaming samples doesn't affect the audio thread.
    static constexpr ModifiedFlags _modified = (ModifiedFlags) 0;
};

MaybeEditBox try_rename_sample(
    Document const& doc, SampleIndex sample_idx, std::string new_name
) {
    static_assert(
        MAX_SAMPLES == 256,
        "Must add bounds check when decreasing sample limit");

    if (!doc.samples[sample_idx]) {
        return nullptr;
    }
    return make_command(RenameSample{{sample_idx}, std::move(new_name)});
}


static void instrument_swap_samples(
    Instruments & instruments, SampleIndex a, SampleIndex b
) {
    for (MaybeInstrument & instr : instruments) {
        if (instr) {
            for (InstrumentPatch & patch : instr->keysplit) {
                if (patch.sample_idx == a) {
                    patch.sample_idx = b;
                } else if (patch.sample_idx == b) {
                    patch.sample_idx = a;
                }
            }
        }
    }
}

struct SwapSamples {
    SampleIndex a;
    SampleIndex b;

    void apply_swap(Document & doc) {
        if (a == b) {
            return;
        }

        // Not currently necessary to assert that a and b < MAX_SAMPLES
        // because MAX_SAMPLES is 256.
        static_assert(
            std::numeric_limits<SampleIndex>::max() < MAX_SAMPLES,
            "TODO add bounds checks");

        std::swap(doc.samples[a], doc.samples[b]);
        instrument_swap_samples(doc.instruments, a, b);
    }

    using Impl = ImplEditCommand<SwapSamples, Override::CloneForAudio>;
    EditBox clone_for_audio(doc::Document const& doc) const;

    static constexpr ModifiedFlags _modified = ModifiedFlags::SamplesEdited;
};

EditBox swap_samples(SampleIndex a, SampleIndex b) {
    return make_command(SwapSamples{a, b});
}

struct SwapSamplesCached {
    SampleIndex a;
    SampleIndex b;
    Instruments instruments;

    void apply_swap(Document & doc) {
        if (a == b) {
            return;
        }

        static_assert(
            std::numeric_limits<SampleIndex>::max() < MAX_SAMPLES,
            "TODO add bounds checks");

        std::swap(doc.samples[a], doc.samples[b]);
        std::swap(doc.instruments, instruments);
    }

    using Impl = ImplEditCommand<SwapSamplesCached, Override::None>;
    static constexpr ModifiedFlags _modified = ModifiedFlags::SamplesEdited;
};

EditBox SwapSamples::clone_for_audio(Document const& doc) const {
    Instruments instruments = doc.instruments;  // Make a copy
    instrument_swap_samples(instruments, a, b);
    return make_command(SwapSamplesCached{a, b, std::move(instruments)});
}

}
