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
unique_ptr<WidgetOrLayout> create_element(QWidget * parent, QString name = "") {
    unique_ptr<WidgetOrLayout> item;
    if constexpr (std::is_base_of<QLayout, WidgetOrLayout>::value) {
        // new_widget_or_layout is used to add sublayouts, which do NOT have a parent.
        // Only widgets' root layouts have parents.
        item = make_unique<WidgetOrLayout>(nullptr);
    } else {
        item = make_unique<WidgetOrLayout>(parent);
    }

    // QObject.objectName defaults to empty string. So does our function default.
    item->setObjectName(name);

    return item;
}


unique_ptr<QLabel> create_element(QString label_text, QWidget * parent, QString name = "") {
    unique_ptr<QLabel> label = create_element<QLabel>(parent, name);
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
    StackRaii(LayoutStack * stack, StackFrame frame, WidgetOrLayout item)
    : item(item) {
        this->stack = stack;
        stack->frames.push(frame);
    }

    WidgetOrLayout* operator->() {
        return item;
    }

    ~StackRaii() {
        stack->frames.pop();
    }
};


template <class...> constexpr std::false_type always_false{};


class LayoutStack final {
    std::stack<StackFrame> frames;
    friend class StackRaii;

public:
    LayoutStack(QWidget * root) {
        frames.push(StackFrame(root));
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
        return StackRaii<WidgetOrLayout>(this, frame);
    }

    StackFrame peek() {
        return frames.top();
    }

    // don't use widget(), use StackRaii.
    // maybe don't use layout(), ^.
    // parent() is unused in corrscope too.
};


template<typename LayoutType>
unique_ptr<LayoutType> set_layout(LayoutStack &mut stack) {
    static_assert(std::is_base_of(QLayout, LayoutType));;

    auto layout = make_unique<LayoutType>(stack.peek().widget);
}
