#include "parameterize.h"

// doctest takes all subcase names as char const * with a long lifetime.
static std::unordered_set<SubcaseName> NAME_CACHE;

SubcaseNameMaybe subcase_tree(
    SubcaseName & /*'a*/ parent_stack,
    char const * /*'b*/ subcase,
    StackedFunc<SubcaseName> action
) {
    // sub_stack: &'static-ish str
    SubcaseName sub_full_str = parent_stack + subcase + ", ";

    auto [sub_stack, _] = NAME_CACHE.insert(sub_full_str);
    char const * sub_full_name = sub_stack->c_str();

    SUBCASE(sub_full_name) {
        // CAPTURE, INFO, DOCTEST_INFO_IMPL, MakeContextScope, ContextScope, ContextScopeBase, g_infoContexts
        // so CAPTURE messages will only be printed for the remainder of the RAII scope.
        // Which is incompatible with our design.

        return action(*sub_stack);
    }
    return "";
}
