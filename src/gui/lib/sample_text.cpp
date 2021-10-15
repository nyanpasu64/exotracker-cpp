#include "sample_text.h"

#include "gui/lib/format.h"

#include <QObject>

namespace gui::lib::sample_text {

using gui::lib::format::format_hex_2;

QString sample_text(doc::Samples const& samples, size_t sample_idx) {
    assert(sample_idx < samples.size());
    auto const& maybe_sample = samples[sample_idx];
    if (maybe_sample) {
        QString name = QString::fromStdString(maybe_sample->name);
        return QObject::tr("%1 - %2").arg(
            format_hex_2(sample_idx),
            name);
    } else {
        return QObject::tr("%1 (none)")
            .arg(format_hex_2(sample_idx));
    }
}

QString sample_title(doc::Samples const& samples, size_t sample_idx) {
    assert(sample_idx < samples.size());
    auto const& maybe_sample = samples[sample_idx];
    if (maybe_sample) {
        QString name = QString::fromStdString(maybe_sample->name);
        return QObject::tr("Sample %1 - %2").arg(
            format_hex_2(sample_idx),
            name);
    } else {
        return QObject::tr("Sample %1 (none)")
            .arg(format_hex_2(sample_idx));
    }
}

} // namespace
