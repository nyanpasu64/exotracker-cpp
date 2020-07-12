#include "gui/main_window.h"
#include "gui/history.h"
#include "gui_common.h"
#include "sample_docs.h"

#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <QApplication>

#include <variant>

#include "win32_fonts.h"

using std::unique_ptr;
using gui::main_window::MainWindow;


struct ReturnCode {
    int value;
};

struct Arguments {
    std::string doc_name;

    [[nodiscard]]
    static std::variant<Arguments, ReturnCode> parse(int argc, char *argv[]) {
        CLI::App app;

        std::string doc_name = sample_docs::DEFAULT_DOC;

        app.add_option("doc_name", /*mut*/ doc_name, "Name of sample document to load");
        app.failure_message(CLI::FailureMessage::help);

        try {
            app.parse(argc, argv);
        } catch (const CLI::ParseError &e) {
            return ReturnCode{app.exit(e)};
        }

        return Arguments{.doc_name=doc_name};
    }
};


int main(int argc, char *argv[])
{
    gui::app::GuiApp a(argc, argv);
    win32_set_font();

    // Parse command-line arguments.
    auto maybe_arg = Arguments::parse(argc, argv);
    if (auto return_code = std::get_if<ReturnCode>(&maybe_arg)) {
        return return_code->value;
    }
    auto arg = std::get<Arguments>(std::move(maybe_arg));

    using sample_docs::DOCUMENTS;
    if (!DOCUMENTS.count(arg.doc_name)) {
        fmt::print(
            stderr, "Invalid document name \"{}\". Valid names are:\n\n", arg.doc_name
        );

        std::vector<std::string> keys;
        keys.reserve(DOCUMENTS.size());
        for (auto && [doc_name, _] : DOCUMENTS) {
            fmt::print(stderr, "  {}\n", doc_name);
        }

        return 1;
    }
    auto const & document = sample_docs::DOCUMENTS.at(arg.doc_name);
    unique_ptr<MainWindow> w = MainWindow::make(document.clone());
    w->show();

    return a.exec();
}
