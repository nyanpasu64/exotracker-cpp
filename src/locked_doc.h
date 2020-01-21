#pragma once

#if !defined(EXO_BIN) && !defined(UNITTEST)
#error Locked documents should only be used within exotracker-bin or exotracker-tests \
(only the GUI supports document mutation and needs synchronization).
#endif

#include "doc.h"
#include "util/sync.h"

namespace locked_doc {

using LockedDoc = sync::FakeRwLock<doc::Document>;
using ReadPtr = LockedDoc::ReadPtr;
using ReadGuard = LockedDoc::ReadGuard;

/// get_document() must be thread-safe in implementations.
/// For example, if implemented by DocumentHistory,
/// get_document() must not return invalid states while undoing/redoing.
class GetDocument {
public:
    virtual ~GetDocument() = default;
    virtual ReadGuard get_document() const = 0;
};

}
