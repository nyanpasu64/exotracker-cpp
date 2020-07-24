#include "edit_doc.h"
#include "edit_impl.h"
#include "doc.h"
#include "util/typeid_cast.h"

#include <functional>

namespace edit::edit_doc {

using edit_impl::make_command;

template<typename T>
using GetMutBare = T & (*)(doc::Document &);


template<typename T>
struct Setter {
    GetMutBare<T> _field;
    int _value;

    void apply_swap(doc::Document & document) {
        std::swap(_field(document), _value);
    }

    bool can_coalesce(BaseEditCommand & prev) const {
        if (auto p = typeid_cast<edit_impl::ImplEditCommand<Setter> *>(&prev)) {
            return _field == p->_body._field;
        }
        return false;
    }
};

doc::TickT & mut_ticks_per_beat(doc::Document & document) {
    return document.sequencer_options.ticks_per_beat;
}

EditBox set_ticks_per_beat(int ticks_per_beat) {
    return make_command(Setter<doc::TickT> {
        mut_ticks_per_beat,
        ticks_per_beat,
    });
}

}
