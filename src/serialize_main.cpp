// Simple program used to manually test file saving and loading.

#include "doc.h"
#include "serialize.h"
#include "doc/validate.h"

#include <fmt/core.h>

using namespace doc;

static Document empty_doc() {
    DocumentCopy doc{};
    doc.frequency_table = equal_temperament();
    return doc;
}

using namespace std::string_literals;
using namespace serialize;

int main() {
    auto d = empty_doc();

    static constexpr auto PATH = "empty_doc.etm";

    for (size_t i = 0; i < 3; i++) {
        {
            auto result = save_to_path(
                d, Metadata {}, PATH
            );
            if (result) {
                fmt::print("save error: {}\n", *result);
            } else {
                fmt::print("save successful\n");
            }
        }
        {
            LoadDocumentResult result = load_from_path(PATH);
            fmt::print("load success: {}\n", result.v.has_value());

            fmt::print("load errors:\n");
            for (auto const& err : result.errors) {
                fmt::print("- {}: {}\n",
                    (err.type == ErrorType::Error) ? "Error" : "Warning",
                    err.description);
            }
        }
        fmt::print("\n");
    }
}
