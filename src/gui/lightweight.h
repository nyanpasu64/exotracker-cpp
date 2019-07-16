#pragma once

#ifndef defer

// https://stackoverflow.com/a/42060129
struct DeferDummy {};

template <class Lambda>
struct Deferrer {
    Lambda f;
    ~Deferrer() { f(); }
};

template <class Lambda>
Deferrer<Lambda> operator<<(DeferDummy, Lambda f) {
    return {f};
}

#define DEFER_(LINE) zz_defer##LINE
#define DEFER(LINE) DEFER_(LINE)
#define defer \
    auto DEFER(__LINE__) = DeferDummy{} << [&]()

// Usage:
// defer {...};

#endif // defer


#define require_semicolon do {} while (0)

// Once C++20 rolls around, add (...) -> `addWidget/addLayout(w __VA_OPT__(,) __VA_ARGS__)`.


//#define set_layout(qlayout_w) \
//    auto * l = new qlayout_w; \


#define add_central_widget(qwidget_parent, qlayout_w) \
    auto * parent = w; \
    auto * w = new qwidget_parent; \
    \
    auto * l = new qlayout_w; \
    \
    defer { parent->setCentralWidget(w); }; \
    require_semicolon


#define append_container(qwidget_parent, qlayout_w) \
    auto * parent = w; \
    auto * w = new qwidget_parent; \
    \
    auto * parentL = l; \
    auto * l = new qlayout_w; \
    defer { parentL->addWidget(w); }; \
    \
    require_semicolon


#define append_layout(qlayout_nullptr) \
    auto * parentL = l; \
    auto * l = new qlayout_nullptr; \
    defer { parentL->addLayout(l); }; \
    \
    require_semicolon


#define append_widget(qwidget_parent) \
    auto * parent = w; \
    auto * w = new qwidget_parent; \
    \
    auto * parentL = l; \
    void * l = nullptr; (void)l; \
    defer { parentL->addWidget(w); }; \
    \
    require_semicolon
