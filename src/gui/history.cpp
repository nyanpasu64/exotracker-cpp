#include "history.h"

namespace gui {
namespace history {

DocumentStore::DocumentStore(doc::Document document) : _documents{{
    LockedDoc{document.clone()}, LockedDoc{std::move(document)}
}} {}

DocumentStore::ReadPtr DocumentStore::gui_get_document() const {
    return _documents[_front_index.load(std::memory_order_relaxed)].gui_read();
}

DocumentStore::ReadGuard DocumentStore::get_document() const {
    // Attempted proof at https://docs.google.com/document/d/1Ipi7vfFXDV2TQIqgDqfuz9YHQvSMsCLa1zrJ_NQs6Rk/edit
    // Based on a paper: https://www.hpl.hp.com/techreports/2012/HPL-2012-68.pdf
    // Godbolt compilation testing at https://gist.github.com/nyanpasu64/77af76dd491d6047a0a661e2aaa1e573.

    // Note that I don't fully understand C++11 atomics, nor the x86 it compiles to.
    // The below comments may not be fully correct?

    while (true) {
        auto front_index = _front_index.load(std::memory_order_acquire);
        auto maybe_doc = _documents[front_index].try_read();
        if (maybe_doc) {
            return std::move(*maybe_doc);
        }

        // Prevent the next _front_index.load() from being reordered
        // before the mutex try_read().
        atomic_thread_fence(std::memory_order_acquire);
    }
}

void DocumentStore::history_apply_change(command::Command command) {
    auto gui_write_document = [&]() -> LockedDoc::WriteGuard {
        return _documents[_front_index.load(std::memory_order_relaxed) ^ 1].gui_write();
    };

    {
        // Should not block.
        LockedDoc::WriteGuard back_lock = gui_write_document();
        // TODO apply command.redo(*back) or something
    }

    // swap docs
    _front_index.fetch_xor(1, std::memory_order_acq_rel);

    {
        // Blocks if audio thread is using the current back buffer
        // (previous front buffer).
        LockedDoc::WriteGuard back_lock = gui_write_document();
        // TODO apply command.redo(back) or something
    }
}

// namespaces
}
}
