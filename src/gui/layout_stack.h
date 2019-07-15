/*
child = new QWidget(parent): creates child owned by parent.

layout->addItem does not own.
QTabWidget->addTab(new QWidget(nullptr)): QTabWidget owns new tab.
*/
#pragma once

#include <QString>
#include <QWidget>
#include <QLabel>
#include <QLayout>

#include <stack>
#include <memory>
#include <type_traits>


using std::unique_ptr;
using std::make_unique;
#define mut


/*!
Like HTML document.createElement().

Creates a widget or layout, for insertion into an existing layout.
Do NOT use for filling a widget with a layout!
*/
template <typename WidgetOrLayout>
WidgetOrLayout * create_element(QWidget * parent, QString name = "") {
    WidgetOrLayout * item;
    if constexpr (std::is_base_of<QLayout, WidgetOrLayout>::value) {
        // new_widget_or_layout is used to add sublayouts, which do NOT have a parent.
        // Only widgets' root layouts have parents.
        item = new WidgetOrLayout(nullptr);
    } else {
        item = new WidgetOrLayout(parent);
    }

    // QObject.objectName defaults to empty string. So does our function default.
    item->setObjectName(name);

    return item;
}


QLabel * create_element(QString label_text, QWidget * parent, QString name = "") {
    QLabel * label = create_element<QLabel>(parent, name);
    label->setText(label_text);
    return label;
}


/*!
All fields are nullable.
Does not take ownership of anything pushed.
*/
struct StackFrame final {
    QWidget * widget;
    QLayout * layout = nullptr;
    StackFrame * parent = nullptr;

    StackFrame with_layout(QLayout * layout) {
        return {this->widget, layout, this->parent};
    }
};


class LayoutStack;

/*!
Handles LayoutStack pushing and popping. We don't need to insert items into parents,
since QWidget/QLayout insert themselves into their parents.
*/
template<typename WidgetOrLayout>
class StackRaii final {
    LayoutStack * stack;

public:
    WidgetOrLayout * item;

public:
    StackRaii(LayoutStack * stack, StackFrame frame, WidgetOrLayout * item);
    StackRaii(StackRaii copyFrom) = delete;

    WidgetOrLayout* operator->();

    ~StackRaii();
};


template <class...> constexpr std::false_type always_false{};


class LayoutStack final {
    std::stack<StackFrame> frames;
    template<typename T> friend class StackRaii;

public:
    LayoutStack(QWidget * root) {
        frames.push(StackFrame{root});
    }

    template<typename WidgetOrLayout>
    StackRaii<WidgetOrLayout> push(WidgetOrLayout * item) {
        StackFrame frame;
        // if constexpr (std::is_same<StackFrame, WidgetOrLayout>::value) {
        //     frame = item;
        // } else
        if constexpr (std::is_base_of<QWidget, WidgetOrLayout>::value) {
            QWidget * widget = item;
            frame = StackFrame{widget};
        } else
        if constexpr (std::is_base_of<QLayout, WidgetOrLayout>::value) {
            QLayout * layout = item;
            frame = peek().with_layout(layout);
        } else
            static_assert(always_false<WidgetOrLayout>, "Invalid type passed in");

        frame.parent = peek();

        // StackRaii is only constructed once, because copy elision.
        // So we push only once.
        return StackRaii<WidgetOrLayout>(this, frame, item);
    }

    StackFrame peek() {
        return frames.top();
    }

    // don't use widget(), use StackRaii.
    // maybe don't use layout(), ^.
    // parent() is unused in corrscope too.
};


// QLayout is deleted by its owner widget, not by smart pointer.
// Most of the time straight from the documentation. In general, you can assume that every time you pass a pointer to an object which is meant to be "hold" somehow, there's a ownership transfer involved. A notable exception is QLayout::addWidget (which does NOT reparent the widget to the layout), and probably there are some others (documented or not; of course, the source code has the final word).

/*!
Non-RAII, sets root layout of current widget.
Returned layout is owned by widget, and should not be deleted.
*/
template<typename LayoutType>
LayoutType * set_layout(LayoutStack &mut stack) {
    static_assert(std::is_base_of<QLayout, LayoutType>::value);;

    auto layout = new LayoutType(stack.peek().widget);
    stack.peek().layout = layout.get();
    return layout;
}


class _new_widget {
    /*!
    Constructs item_type using parent.
    Yields item_type.
    */
    template<typename item_type>
    _new_widget(
        LayoutStack * stack,
        bool orphan = false
    ) {
        QWidget * parent;
        if (!orphan) {
            parent = stack->peek().widget;
        } else {
            parent = nullptr;
        }
        {
            stack->push(create_element<item_type>(parent));
        }

//        with stack.push(create_element(item_type, parent, kwargs)) as item:
//            if layout:
//                set_layout(stack, layout)
//            yield item

//        real_parent = stack.widget
//        if callable(exit_action):
//            exit_action(real_parent, item)
//        elif exit_action:
//            getattr(real_parent, exit_action)(item)
    }

    ~_new_widget() {}
};
