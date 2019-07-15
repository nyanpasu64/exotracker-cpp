#include "gui/layout_stack.h"


template<typename WidgetOrLayout>
StackRaii<WidgetOrLayout>::StackRaii(LayoutStack *stack, StackFrame frame, WidgetOrLayout * item)
: item(item) {
    this->stack = stack;
    stack->frames.push(frame);
}


template<typename WidgetOrLayout>
WidgetOrLayout * StackRaii<WidgetOrLayout>::operator->() {
    return item;
}


template<typename WidgetOrLayout>
StackRaii<WidgetOrLayout>::~StackRaii() {
    stack->frames.pop();
}
