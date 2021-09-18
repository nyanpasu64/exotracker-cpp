#pragma once

#include "edit_common.h"
#include "util/enum_flags.h"

namespace edit::edit_impl {

namespace Override_ {
enum Override {
    None = 0,
    CloneForAudio = 0x1,
    CanMerge = 0x2,
};
}
using Override_::Override;
DECLARE_OPERATORS_FOR_FLAGS(Override)

/// Since BaseEditCommand has virtual member functions,
/// subclasses cannot be aggregate-initialized (requiring constructor boilerplate).
/// So instead make BaseEditCommand subclasses hold data (Body _inner),
/// which can be aggregate-initialized.
/// This approach also allows us to define cloning once,
/// instead of repeating the boilerplate in each command class.
template<typename Body, Override OVERRIDE>
struct ImplEditCommand : BaseEditCommand, Body {
    explicit ImplEditCommand(Body body)
        : BaseEditCommand{}, Body{std::move(body)}
    {}

    [[nodiscard]] EditBox clone_for_audio(doc::Document const& doc) const override {
        if constexpr ((OVERRIDE & Override::CloneForAudio) != 0) {
            return Body::clone_for_audio(doc);
        } else {
            return std::make_unique<ImplEditCommand>(*(Body *)this);
        }
    }

    void apply_swap(doc::Document & document) override {
        return Body::apply_swap(document);
    }

    bool can_merge(BaseEditCommand & prev) const override {
        if constexpr ((OVERRIDE & Override::CanMerge) != 0) {
            return Body::can_merge(prev);
        } else {
            return false;
        }
    }

    [[nodiscard]] ModifiedFlags modified() const override {
        return Body::_modified;
    }
};

template<typename Body>
inline EditBox make_command(Body body) {
    return std::make_unique<typename Body::Impl>(std::move(body));
}

/// When pushed into undo history, remembers cursor position,
/// but doesn't modify the document.
struct NullEditCommand {
    void apply_swap(doc::Document &) {
    }

    using Impl = ImplEditCommand<NullEditCommand, Override::None>;
    constexpr static ModifiedFlags _modified = (ModifiedFlags) 0;
};

}
