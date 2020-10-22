#pragma once

/// This namespace is not included and re-exported by doc.h,
/// to prevent most code from recompiling when effects are added or removed.

#include "events.h"

namespace doc::effect_names {

using events::EffectName;

constexpr EffectName eff_name(char c) {
    return EffectName{'0', c};
}

// Currently unused.
//constexpr EffectName eff_name(char const* c) {
//    return EffectName{c[0], c[1]};
//}

constexpr EffectName DELAY = eff_name('G');

}
