#pragma once

#define require_semicolon do {} while (0)

// Once C++20 rolls around, add (...) -> `addWidget/addLayout(w __VA_OPT__(,) __VA_ARGS__)`.


//#define set_layout(qlayout_w) \
//    auto * l = new qlayout_w; \


#define add_central_widget_no_layout(qwidget_parent) \
    auto * parent = w; \
    auto * w = new qwidget_parent; \
    \
    parent->setCentralWidget(w); \
    require_semicolon


#define add_central_widget(qwidget_parent, qlayout_w) \
    auto * parent = w; \
    auto * w = new qwidget_parent; \
    \
    auto * l = new qlayout_w; \
    \
    parent->setCentralWidget(w); \
    require_semicolon


#define append_container(qwidget_parent, qlayout_w) \
    auto * parent = w; \
    auto * w = new qwidget_parent; \
    \
    auto * parentL = l; \
    auto * l = new qlayout_w; \
    parentL->addWidget(w); \
    \
    require_semicolon


#define append_widget(qwidget_parent) \
    auto * parent = w; \
    auto * w = new qwidget_parent; \
    \
    auto * parentL = l; \
    void * l = nullptr; (void)l; \
    parentL->addWidget(w); \
    \
    require_semicolon


#define append_layout(qlayout_nullptr) \
    auto * parentL = l; \
    auto * l = new qlayout_nullptr; \
    parentL->addLayout(l); \
    \
    require_semicolon


#define add_row(_left, _right) \
    auto * left = new _left; \
    auto * right = new _right; \
    \
    l->addRow(left, right); \
    require_semicolon


#define label_row(left_label, _right) \
    auto * right = new _right; \
    \
    l->addRow(left_label, right); \
    require_semicolon


#define append_stretch() \
    l->addStretch()
