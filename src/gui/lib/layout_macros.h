#pragma once

#define require_semicolon do {} while (0)

// unexplored C++20 idea: `addWidget/addLayout(w __VA_OPT__(,) __VA_ARGS__)`.

// https://doc.qt.io/qt-5/layout.html#laying-out-widg(ets-in-code
// Apparently setting the parent of a layout
// recursively reparents all widgets the layout is managing (not owning),
// and causes the layout to set the parent of future widgets.


#define HIDE(var)  void * var = nullptr; Q_UNUSED(var)


/// Add toolbar to QMainWidget.
#define main__tb(qtoolbar) \
    auto * tb = new qtoolbar; \
    main->addToolBar(tb); \
    HIDE(main) \
    \
    require_semicolon


/// Add central leaf widget to QMainWidget.
#define main__central_w(qwidget_main) \
    auto * w = new qwidget_main; \
    parent->setCentralWidget(w); \
    HIDE(main) \
    \
    require_semicolon


/// Add central container and QBoxLayout to QMainWidget.
#define main__central_c_l(qwidget, qlayout) \
    auto * c = new qwidget; \
    main->setCentralWidget(c); \
    HIDE(main) \
    \
    auto * l = new qlayout; \
    c->setLayout(l); \
    \
    require_semicolon


/// Add container and QBoxLayout to QBoxLayout.
#define l__c_l(qwidget, qlayout) \
    auto * c = new qwidget; \
    l->addWidget(c); \
    \
    auto * l = new qlayout; \
    c->setLayout(l); \
    \
    require_semicolon
// l->addWidget(c) sets c.parent.


/// Add container and QFormLayout to QBoxLayout.
#define l__c_form(qwidget, qlayout) \
    auto * c = new qwidget; \
    l->addWidget(c); \
    HIDE(l) \
    \
    auto * form = new qlayout; \
    c->setLayout(form); \
    \
    require_semicolon


/// Add leaf widget to QBoxLayout.
#define l__w(qwidget) \
    auto * w = new qwidget; \
    l->addWidget(w); \
    HIDE(l) \
    \
    require_semicolon
// l->addWidget(w) sets w.parent.


/// Add leaf widget to QBoxLayout.
#define l__w_factory(qwidget_make) \
    auto * w = qwidget_make; \
    l->addWidget(w); \
    HIDE(l) \
    \
    require_semicolon

/// Add QBoxLayout to QBoxLayout.
#define l__l(qlayout) \
    auto * parentL = l; \
    auto * l = new qlayout; \
    parentL->addLayout(l); \
    \
    require_semicolon


/// Add left/right to QFormLayout.
#define form__left_right(_left, _right) \
    auto * left = new _left; \
    auto * right = new _right; \
    form->addRow(left, right); \
    HIDE(form) \
    \
    require_semicolon


/// Add wide leaf widget to QFormLayout.
#define form__w(qwidget) \
    auto * w = new qwidget; \
    form->addRow(w); \
    HIDE(form) \
    \
    require_semicolon


/// Add wide layout to QFormLayout.
#define form__l(qlayout) \
    auto * l = new qlayout; \
    form->addRow(l); \
    HIDE(form) \
    \
    require_semicolon


/// Add label and leaf widget to QFormLayout.
#define form__label_w(left_text, _right) \
    auto * w = new _right; \
    \
    form->addRow(left_text, w); \
    HIDE(form) \
    \
    require_semicolon


#define append_stretch() \
    l->addStretch()
