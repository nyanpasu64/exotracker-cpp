#pragma once

#include "doc.h"
#include "doc/validate_common.h"  // too lazy to duplicate the types for export validation

namespace spc_export {

using doc::validate::Errors;
// Not used, but reexported.
using doc::validate::Error;
using doc::validate::ErrorType;

/// Exporting a SPC may result in:
///
/// - successfully exported
///     - {true, {}}
/// - successfully exported with warnings
///     - {true, {Warning...}}
/// - failed to export SPC, with errors (and possibly warnings)
///     - {false, {Error, Warning...}}
///
/// If `errors` is non-empty, the warnings and errors must be shown in a dialog box.
///
/// {false, {}} should never be returned.
struct ExportSpcResult {
    bool ok;
    // TODO add metadata about ARAM usage
    Errors errors;
};

[[nodiscard]] ExportSpcResult export_spc(doc::Document const& doc, char const* path);

}
