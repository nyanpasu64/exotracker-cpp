#pragma once

#include "doc.h"
#include "doc/validate_common.h"

#include <gsl/span>

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>
#include <optional>

#ifdef UNITTEST
#include "util/compare.h"
#endif

namespace serialize {

/// A serialized document.
using ByteBuffer = gsl::span<uint8_t const>;

inline constexpr char const* MODULE_EXT = ".etm";

// TODO struct ExportConfig { compatibility settings, etc. }
struct Metadata {
    uint16_t zoom_level;

// impl
#ifdef UNITTEST
    DEFAULT_EQUALABLE(Metadata)
#endif
};

/// If saving fails, returns a string error message (currently not localized).
[[nodiscard]] std::optional<std::string> save_to_path(
    doc::Document const& doc, Metadata metadata, char const* path
);


using doc::validate::Errors;
using doc::validate::Error;
using doc::validate::ErrorType;

/// Loading a document may result in:
///
/// - successfully loaded document
///     - {present, {}}
/// - successfully loaded document with warnings
///     - {present, {Warning...}}
/// - failure to load document, with errors (and possibly warnings)
///     - {nullopt, {Error, Warning...}}
///
/// If `errors` is non-empty, the warnings and errors must be shown in a dialog box.
/// If `v` is present, the document should be loaded even if `errors` is non-empty.
///
/// {nullopt, {}} should never be returned.
struct LoadDocumentResult {
    std::optional<std::tuple<doc::Document, Metadata>> v;
    Errors errors;

// impl
    static LoadDocumentResult ok(doc::Document doc, Metadata metadata, Errors errors);

    static LoadDocumentResult err(Errors errors);

private:
    LoadDocumentResult(
        std::optional<std::tuple<doc::Document, Metadata>> v, Errors errors
    );
};

[[nodiscard]] LoadDocumentResult load_from_path(char const* path);

}
