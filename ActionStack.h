#pragma once
// ActionStack.h — Phase 3a: moved verbatim from main.cpp v1 (archive/main_v1.cpp).
// ActionStack. Unchanged except Action gains two queue fields (marked NEW).

#include <string>

// 3. STACK - Undo last action (LIFO)
//    push, pop, peek, isEmpty
// ============================================================
struct Action {
    int orderId;
    std::string oldStatus;
    std::string newStatus;
    // NEW in Phase 3a — needed so Undo can restore the queue exactly:
    // wasInQueue: the order was in the processing queue before the action.
    // wasDequeued: the action removed it from the FRONT of the queue
    //   (processNext). Undo must put it back at the FRONT, not the rear.
    bool wasInQueue = false;
    bool wasDequeued = false;
    // NEW in Phase 3b — original queue position (0 = front, -1 = not
    // in the queue). Phase 3a only remembered front vs rear, so
    // undo of a cancel in the MIDDLE of the queue put the order at
    // the rear and lost its place. Now Undo re-inserts at this
    // exact position (clamped to the queue size if the queue has
    // shrunk since). See OrderQueue::insertAtPosition().
    int queuePosition = -1;
};

struct StackNode {
    Action data;
    StackNode *next;
    StackNode(Action a) : data(a), next(nullptr) {}
};

class ActionStack {
public:
    StackNode *top = nullptr;

    bool isEmpty() { return top == nullptr; }

    void push(Action a) {            // O(1)
        StackNode *node = new StackNode(a);
        node->next = top; top = node;
    }

    bool pop(Action &a) {            // O(1)
        if (isEmpty()) return false;
        a = top->data;
        StackNode *tmp = top; top = top->next;
        delete tmp;
        return true;
    }

    bool peek(Action &a) {
        if (isEmpty()) return false;
        a = top->data; return true;
    }
};
