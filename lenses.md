One description of lenses at https://sebfisch.github.io/research/pub/Fischer+MPC15.pdf

Pseudo-Rust code:
```rust
// All types are Copy (touches atomic refcounts).
// Alternatively, all ModifyFoo take &Foo (doesn't touch atomic refcounts).
Inner = ...;
type ModifyInner = Fn(Inner) -> Inner;

Outer = {inner: Inner, ...}
type ModifyOuter = Fn(Outer) -> Outer;

/// Is this like a lens in functional programming?
fn outer_modify_inner(f: ModifyInner) -> ModifyOuter {
	return |outer: Outer| -> Outer {
		Outer{inner: f(outer.inner), ...outer}
	}
}
```

https://stevedonovan.github.io/rustifications/2018/08/18/rust-closures-are-hard.html

>[`Box<Fn(f64)->f64>`] is equivalent to the `std::function` type in C++, which also involves calling a virtual method. `Box<Fn(f64)->f64>` is a Rust trait object.
