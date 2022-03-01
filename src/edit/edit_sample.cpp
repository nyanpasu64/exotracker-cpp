#include "edit_sample.h"
#include "edit_impl.h"
#include "util/typeid_cast.h"
#include "util/release_assert.h"

namespace edit::edit_sample {

using namespace doc;
using namespace edit_impl;

// Sample edits. All replace the entire sample,
// and merge with other edits of the same sample index.

static Sample const& get_sample(doc::Document const& doc, size_t sample_idx) {
    release_assert(sample_idx < doc.samples.size());
    auto & maybe_sample = doc.samples[sample_idx];
    release_assert(maybe_sample);
    return *maybe_sample;
}

static Sample & get_sample_mut(doc::Document & doc, size_t sample_idx) {
    return const_cast<Sample &>(get_sample(doc, sample_idx));
}

class SetSampleMetadata {
    SampleIndex _path;
    // .brr is empty to conserve RAM.
    // Ideally I'd only store the single field being edited,
    // but that's difficult to achieve with C++ templates.
    Sample _value;

protected:
    ModifiedFlags _modified;

public:
    SetSampleMetadata(size_t sample_idx, Sample value, ModifiedFlags modified) {
        release_assert(sample_idx < MAX_SAMPLES);
        _path = (SampleIndex) sample_idx;
        _value = std::move(value);

        // Deallocate the sample data to save RAM in the undo history.
        _value.brr.clear();
        _value.brr.shrink_to_fit();

        _modified = modified;
    }

    void apply_swap(doc::Document & doc) {
        auto & patch = get_sample_mut(doc, _path);

        // Swap metadata but keep sample data in place.
        std::swap(patch, _value);
        std::swap(patch.brr, _value.brr);
    }

    using Impl = ImplEditCommand<SetSampleMetadata, Override::SkipHistory>;
};

EditBox set_loop_byte(doc::Document const& doc, size_t sample_idx, uint16_t loop_byte) {
    auto sample = get_sample(doc, sample_idx);
    sample.loop_byte = loop_byte;
    return make_command(SetSampleMetadata(
        sample_idx, std::move(sample), ModifiedFlags::SampleMetadataEdited
    ));
}

EditBox set_sample_rate(
    doc::Document const& doc, size_t sample_idx, uint32_t sample_rate
) {
    auto sample = get_sample(doc, sample_idx);
    sample.tuning.sample_rate = sample_rate;
    return make_command(SetSampleMetadata(
        sample_idx, std::move(sample), ModifiedFlags::SampleMetadataEdited
    ));
}

EditBox set_root_key(doc::Document const& doc, size_t sample_idx, Chromatic root_key) {
    auto sample = get_sample(doc, sample_idx);
    sample.tuning.root_key = root_key;
    return make_command(SetSampleMetadata(
        sample_idx, std::move(sample), ModifiedFlags::SampleMetadataEdited
    ));
}

EditBox set_detune_cents(
    doc::Document const& doc, size_t sample_idx, int16_t detune_cents
) {
    auto sample = get_sample(doc, sample_idx);
    sample.tuning.detune_cents = detune_cents;
    return make_command(SetSampleMetadata(
        sample_idx, std::move(sample), ModifiedFlags::SampleMetadataEdited
    ));
}

}
