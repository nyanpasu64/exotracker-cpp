#pragma once

#include <vector>
#include <string>

namespace doc::validate {

enum class ErrorType {
    Warning,
    Error,
};

struct Error {
    ErrorType type;
    std::string description;
};

using Errors = std::vector<Error>;

}
