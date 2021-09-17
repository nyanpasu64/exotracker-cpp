#include "list_warnings.h"
#include "gui/lib/docs_palette.h"

#include <QTextCursor>
#include <QTextDocument>

namespace gui::lib::list_warnings {

QIcon warning_icon() {
    return QIcon(QStringLiteral("://icons/warning-sign.svg"));
}

namespace pal = gui::lib::docs_palette;

QColor warning_bg() {
    QColor color = pal::get_color(pal::Hue::Yellow, pal::Shade::Light1);
    color.setAlphaF(0.4);
    return color;
}

QString warning_tooltip(gsl::span<QString const> warnings) {
    if (!warnings.empty()) {
        QTextDocument document;
        auto cursor = QTextCursor(&document);
        cursor.beginEditBlock();
        cursor.insertText(QObject::tr("Warnings:"));

        // https://stackoverflow.com/a/51864380
        QTextList* bullets = nullptr;
        QTextBlockFormat non_list_format = cursor.blockFormat();
        for (auto const& w : warnings) {
            if (!bullets) {
                // create list with 1 item
                bullets = cursor.insertList(QTextListFormat::ListDisc);
            } else {
                // append item to list
                cursor.insertBlock();
            }

            cursor.insertText(w);
        }

        return document.toHtml();
    }

    return QString();
}

}
