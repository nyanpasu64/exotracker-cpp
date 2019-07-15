/*
child = new QWidget(parent): creates child owned by parent.

layout->addItem does not own.
QTabWidget->addTab(new QWidget(nullptr)): QTabWidget owns new tab.

Do NOT use smart pointers for holding QClasses.
You WILL get a double-free (parent + unique_ptr).
*/
#pragma once

#include <QString>
#include <QWidget>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>

#include <stack>
#include <memory>
#include <type_traits>


namespace layout_stack {

// Utility.
using std::unique_ptr;
using std::make_unique;
#define mut
template <class...> constexpr std::false_type always_false{};


// Non-RAII "widget constructors". Does not need to know about LayoutStack.

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


QLabel * create_label(QString label_text, QWidget * parent, QString name = "");


// LayoutStack.

/*! Non-copyable visitor supporting nested construction/destruction. */
class VisitorBase {
protected:
    VisitorBase(VisitorBase const & copyFrom) = delete;

    virtual ~VisitorBase() {}
};


class LayoutStack final {
public:
    /*!
    All fields are nullable.
    Does not take ownership of anything pushed.

    SHOULD ONLY BE INITIALIZED IN LayoutStack!!!!
    */
    struct Frame final {
        QWidget * widget;
        QLayout * layout = nullptr;
        Frame * parent = nullptr;

        Frame with_layout(QLayout * layout) {
            return {this->widget, layout, this->parent};
        }
    };

private:
    std::stack<LayoutStack::Frame> frames;
    template<typename T> friend class LayoutRaii;

public:
    LayoutStack(QWidget * root) {
        frames.push(LayoutStack::Frame{root});
    }

    /*!
    Handles LayoutStack pushing and popping. We don't need to insert items into parents,
    since QWidget/QLayout insert themselves into their parents.
    */
    template<typename WidgetOrLayout>
    class Raii : public VisitorBase {
        LayoutStack * stack;

    public:
        WidgetOrLayout * item;

        Raii(LayoutStack * stack, Frame frame, WidgetOrLayout * item)
        : item(item) {
            this->stack = stack;
            stack->frames.push(frame);
        }

        Raii(Raii const & copyFrom) = delete;

        ~Raii() {
            stack->frames.pop();
        }
    };

    template<typename WidgetOrLayout>
    Raii<WidgetOrLayout> _push_existing_object(WidgetOrLayout * item) {
        Frame frame;

        if constexpr (std::is_base_of<QWidget, WidgetOrLayout>::value) {
            QWidget * widget = item;
            frame = Frame{widget};
        } else
        if constexpr (std::is_base_of<QLayout, WidgetOrLayout>::value) {
            QLayout * layout = item;
            frame = peek().with_layout(layout);
        } else
        {
            static_assert(always_false<WidgetOrLayout>, "Invalid type passed in");
        }

        frame.parent = &peek();

        // Layout::Raii is only constructed once, because copy elision.
        // So we push only once.
        return Raii<WidgetOrLayout>(this, frame, item);
    }

    Frame & peek() {
        return frames.top();
    }

    // don't use widget(), use Layout::Raii.
    // maybe don't use layout(), ^.
    // parent() is unused in corrscope too.
};


template<typename WidgetOrLayout>
LayoutStack::Raii<WidgetOrLayout> append_widget(LayoutStack & stack, bool orphan = false) {
    QWidget * parent;
    if (!orphan) {
        parent = stack.peek().widget;
    } else {
        parent = nullptr;
    }

    return stack._push_existing_object(create_element<WidgetOrLayout>(parent));
}



template<typename SomeQW>
class CentralWidgetRaii : public LayoutStack::Raii<SomeQW> {
    using super = LayoutStack::Raii<SomeQW>;

    static_assert(std::is_base_of<QWidget, SomeQW>::value);

    QMainWindow * window;

public:
    CentralWidgetRaii(LayoutStack * stack, LayoutStack::Frame frame, SomeQW * item, QMainWindow * window)
    : super(stack, frame, item), window(window) {}

    ~CentralWidgetRaii() {
        window->setCentralWidget(inner.item);
        // Now C++ calls Visitor.~LayoutRaii() to destroy LayoutRaii.
    }
};


template<typename SomeQW>
auto central_widget(LayoutStack stack, QMainWindow * window) -> CentralWidgetRaii<SomeQW> {
    create_element
    return CentralWidgetRaii(stack, )
}


// Non-RAII operations

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



} // namespace
