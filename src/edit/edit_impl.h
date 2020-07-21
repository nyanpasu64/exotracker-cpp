#pragma once

#include "edit_common.h"

namespace edit::edit_impl {

/// Since BaseEditCommand has virtual member functions,
/// subclasses cannot be aggregate-initialized (requiring constructor boilerplate).
/// So instead make BaseEditCommand subclasses hold data (Body _inner),
/// which can be aggregate-initialized.
/// This approach also allows us to define cloning once,
/// instead of repeating the boilerplate in each command class.
template<typename Body>
struct ImplEditCommand : BaseEditCommand {
    Body _body;

    ImplEditCommand(Body body)
        : BaseEditCommand{}, _body{std::move(body)}
    {}

    EditBox box_clone() const override {
        return std::make_unique<ImplEditCommand>(_body);
    }

    void apply_swap(doc::Document & document) override {
        return _body.apply_swap(document);
    }

    bool can_coalesce(BaseEditCommand & prev) const override {
        return _body.can_coalesce(prev);
    }
};

template<typename Body>
static EditBox make_command(Body body) {
    return std::make_unique<ImplEditCommand<Body>>(std::move(body));
}

}
