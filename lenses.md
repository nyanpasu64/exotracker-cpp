# Pseudo-lenses for copy-modifying 1 or more leaf in a nested object

One description of lenses at https://sebfisch.github.io/research/pub/Fischer+MPC15.pdf

This is inspired by functional-programming lenses, but is more clear and understandable for me, and only supports the "over" function.

## Why does Modify return None?

Global find-and-replace requires iterating over the entire document. Either I can iterate manually (and only use lenses to modify required paths), or I can ask the lens to iterate for me. In the latter case, I need to distinguish "I modified this pattern" from "I didn't modify this pattern, please reuse the existing array".

When `Modify(inner: Inner) -> Option<Inner>` returns `None`, that means means "this function does not modify the parameter", and the caller can reuse the `inner` argument, and potentially reuse the object holding `inner` as well. (Does Immer allow identity comparison of stack-allocated objects which are not references?)

Since the original data is a widely branching tree, returning None allows structural sharing of unmodified parts of the original data.

## Pseudo-Rust code

```rust
// Too abstract?
type Modify<T> = dyn Fn(T) -> Option<T>;

// All types are Copy (touches atomic refcounts).
// Alternatively, all ModifyFoo take &Foo (doesn't touch atomic refcounts).
Inner = ...;
// type ModifyInner = Modify<Inner>;
type ModifyInner = dyn Fn(Inner) -> Option<Inner>;

Outer = {inner: Inner, ...}
// type ModifyOuter = Modify<Outer>;
type ModifyOuter = dyn Fn(Outer) -> Option<Outer>;

/// Is this like a lens in functional programming?
fn outer_modify_inner(f: ModifyInner) -> ModifyOuter {
    return |outer: Outer| -> Option<Outer> {
        match f(outer.inner) {
            Some(inner) => Some(Outer{inner, ..outer}),
            None => None,
        }
    }
}

type Array = [Inner];
type ModifyArray = dyn Fn(Array) -> Option<Array>

fn array_foreach_modify(f: ModifyInner) -> ModifyArray {
    return |array| {
        let mut modified = false;
        let mut new_array = transient copy of array;

        for i0 in array.iter() {
            match f(i0) {
                Some(inner) => {
                    modified = true;
                    new_array[i] = inner;
                }
                None => {
                    new_array[i] = i0;
                }
            }
        }

        if modified {
            Some(new_array)
        } else {
            None
        }
    }
}


```

https://stevedonovan.github.io/rustifications/2018/08/18/rust-closures-are-hard.html

>[`Box<Fn(f64)->f64>`] is equivalent to the `std::function` type in C++, which also involves calling a virtual method. `Box<Fn(f64)->f64>` is a Rust trait object.
