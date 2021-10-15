#pragma once

#include "doc.h"

#include <cstdint>

#include <QString>

namespace gui::lib::sample_text {

QString sample_text(doc::Samples const& samples, size_t sample_idx);

QString sample_title(doc::Samples const& samples, size_t sample_idx);

} // namespace
