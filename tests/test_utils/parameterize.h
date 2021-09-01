#pragma once

#include "util/release_assert.h"

#include <functional>
#include <string>
#include <unordered_set>
#include <utility>  // std::move

#include <ostream>
#include <doctest.h>

using SubcaseName = std::string;
using SubcaseNameMaybe = SubcaseName;

// private
template<typename Return>
using StackedFunc = std::function<Return(SubcaseName)>;

// public
// impl StackedFunc<SubcaseName>
inline SubcaseName noop(SubcaseName leaf_stack) {
    release_assert(!leaf_stack.empty());
    return leaf_stack;
}

// private
/// doctest::detail::Subcase arguments must be unique within each TEST_CASE,
/// otherwise subsequent cases will be skipped.
/// So use the entire subcase stack as a subcase name.
/// This enables us to generate the entire product set of
/// two orthogonal parameterizations.
///
/// If you have n calls to subcase_tree,
/// doctest will loop over this at least n times or more if there are nested subcases.
/// On each iteration, only one will return a non-empty result (SubcaseNameMaybe).
SubcaseNameMaybe subcase_tree(
    SubcaseName & /*'a*/ parent_stack,
    char const * /*'b*/ subcase,
    StackedFunc<SubcaseName> action
);

// public
/// Define a function "returning" multiple values to a doctest TEST_CASE.
/// Much like `all_foo = pytest.mark.parameterize("foo", [a, b, c])`.
///
/// fn(out parameter, [inner])
/// This will iterate over every value writable by inner,
/// before writing a different value.
/// If inner is omitted, every call to this function will return a different value.
///
/// (Unconfirmed) Do NOT early-exit on first match,
/// since doctest will only iterate if subsequent subcases fail.
#define PARAMETERIZE(func_name, T, parameter, OPTIONs) \
    [[nodiscard]] StackedFunc<SubcaseName> func_name( \
        T & parameter, StackedFunc<SubcaseName> inner = noop \
    ) { \
        return [&, inner](SubcaseName stack) { \
            SubcaseName leaf_stack; \
            OPTIONs \
            release_assert(!leaf_stack.empty()); \
            return leaf_stack; \
        }; \
    }

// public
/// One possible value which a PARAMETERIZE list can write.
#define OPTION(k, v) \
    leaf_stack += subcase_tree( \
        stack, #k " = " #v, [&](SubcaseName stack2) { \
            k = v; \
            return inner(std::move(stack2)); \
        } \
    )


// public
/// Pick one element from a PARAMETERIZE chain, and record its value.
/// Doctest will print the value of `leaf_stack` until the current scope ends.
#define PICK(parameter_func) \
    SubcaseName leaf_stack = parameter_func("PICK(): "); \
    INFO(leaf_stack)
