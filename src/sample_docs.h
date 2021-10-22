#pragma once

#include "doc.h"

#include <string>
#include <map>

namespace sample_docs {

/// The default document created when starting the program or pressing New.
doc::Document new_document();

extern std::map<std::string, doc::Document> const DOCUMENTS;

}
