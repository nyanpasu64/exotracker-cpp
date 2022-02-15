#include "gui/main_window.h"
#include "gui/history.h"
#include "gui/app.h"
#include "sample_docs.h"
#include "util/expr.h"

#include <fmt/core.h>

#include <QApplication>
#include <QCommandLineParser>

#include <variant>

using gui::main_window::MainWindow;


static std::string list_documents() {
    std::string out;

    for (auto && [doc_name, _] : sample_docs::DOCUMENTS) {
        out += fmt::format("  {}\n", doc_name);
    }

    return out;
}


struct ReturnCode {
    int value;
};

// TODO put in header
/// Translate a string in a global context, outside of a class.
static QString gtr(
    const char *context,
    const char *sourceText,
    const char *disambiguation = nullptr,
    int n = -1)
{
    return QCoreApplication::translate(context, sourceText, disambiguation, n);
}

static bool has(std::string const& s) {
    return !s.empty();
}

static bool has(QString const& s) {
    return !s.isEmpty();
}

/// Returns the default help text, followed by a list of sample document names.
static QString help_text(QCommandLineParser & parser) {
    auto text = parser.helpText();
    text += QStringLiteral("\n%1\n%2").arg(
        gtr("main", "Sample document names:"),
        QString::fromStdString(list_documents()));
    return text;
}

[[noreturn]] static void bail_only(QString error) {
    fprintf(stderr, "%s\n", error.toUtf8().data());
    exit(1);
}

[[noreturn]] static void bail_help(QCommandLineParser & parser, QString error) {
    fprintf(stderr, "%s\n\n%s",
        error.toUtf8().data(),
        help_text(parser).toUtf8().data());
    exit(1);
}

[[noreturn]] static void help_and_exit(QCommandLineParser & parser) {
    fputs(help_text(parser).toUtf8().data(), stdout);
    exit(0);
}

struct Arguments {
    QString filename;
    std::string sample_doc;

    /// May exit if invalid arguments, --help, or --version is passed.
    [[nodiscard]]
    static Arguments parse_or_exit(QStringList const& arguments) {
        using sample_docs::DOCUMENTS;

        QCommandLineParser parser;

        // Prepare the argument list.
        parser.addHelpOption();
        parser.addVersionOption();
        parser.addPositionalArgument("FILE", gtr("main", "Module file to open."));

        auto sample_doc = QCommandLineOption(
            "sample-doc",
            gtr("main", "Name of sample document to load."),
            gtr("main", "name"));

        parser.addOption(sample_doc);

        // Parse the arguments.
        // May exit if invalid arguments, --help, or --version is passed.
        if (!parser.parse(arguments)) {
            bail_help(parser, QStringLiteral("%1: %2").arg(
                gtr("main", "error"),
                parser.errorText()));
        }
        if (parser.isSet(QStringLiteral("version"))) {
            // Exits the program.
            parser.showVersion();
        }
        if (parser.isSet(QStringLiteral("help"))) {
            // Prints the default help text, followed by a list of sample document names.
            help_and_exit(parser);
        }
        if (parser.isSet(QStringLiteral("help-all"))) {
            // Prints the default app+Qt help text.
            parser.process(arguments);
            // This should be unreachable, since parser should exit upon seeing
            // --help-all.
            abort();
        }

        Arguments out;
        auto positional = parser.positionalArguments();
        if (0 < positional.size()) {
            out.filename = positional[0];
        }
        if (1 < positional.size()) {
            bail_help(parser, gtr("main", "Too many command-line arguments, expected FILE"));
        }

        if (parser.isSet(sample_doc)) {
            out.sample_doc = parser.value(sample_doc).toStdString();
        }

        // Validate the arguments.
        if (has(out.sample_doc) && has(out.filename)) {
            bail_only(gtr("main", "Cannot pass both --sample-doc <%1> and FILE.")
                .arg(sample_doc.valueName()));
        }

        return out;
    }
};


int main(int argc, char *argv[]) {
    using gui::app::GuiApp;

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor
    );
    GuiApp a(argc, argv);
    QCoreApplication::setApplicationName("exotracker");
    // Don't call QGuiApplication::setApplicationDisplayName()
    // to append the app name to windows.
    // It can't be turned off for instrument dialogs,
    // uses hyphens on Windows but en dashes on Linux (and you can't tell which),
    // and disappears when a file or instrument is named ExoTracker.

    // Parse command-line arguments.
    // May exit if invalid arguments, --help, or --version is passed.
    auto arg = Arguments::parse_or_exit(QCoreApplication::arguments());

    using sample_docs::DOCUMENTS;
    if (has(arg.sample_doc) && !DOCUMENTS.count(arg.sample_doc)) {
        fmt::print(stderr,
            "Invalid document name \"{}\". Valid names are:\n\n", arg.sample_doc
        );
        fmt::print(stderr, "{}", list_documents());
        return 1;
    }

    std::unique_ptr<MainWindow> w;
    if (has(arg.filename)) {
        w = MainWindow::new_with_path(std::move(arg.filename));
    } else {
        auto document = EXPR(
            if (has(arg.sample_doc)) {
                return sample_docs::DOCUMENTS.at(arg.sample_doc).clone();
            } else {
                return sample_docs::new_document();
            }
        );

        w = MainWindow::make(std::move(document));
    }
    w->show();

    return a.exec();
}
