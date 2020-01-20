#pragma once

#include <vector>

namespace doc {

/// Semantic typedef around a runtime-sized vector.
/// K is an integer type type-erased without type-checking,
/// and exists to document the domain space.
/// It's semantically nonsensical to index a list of sound chips
/// with a pixel coordinate integer.
template<typename K, typename V>
using DenseMap = std::vector<V>;

}
