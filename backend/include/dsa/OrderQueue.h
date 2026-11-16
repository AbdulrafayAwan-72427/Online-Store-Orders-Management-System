#pragma once
// OrderQueue.h — Phase 3a: moved verbatim from main.cpp v1 (archive/main_v1.cpp).
// OrderQueue. Unchanged in Phase 3a except the added helpers at the end of the class (marked NEW).

#include <iostream>

// 2. QUEUE - process orders FIFO (first in, first out)
//    enqueue, dequeue, peek, isEmpty
//    Built on a simple linked list of order IDs.
// ============================================================
struct QueueNode {
    int orderId;
    QueueNode *next;
    QueueNode(int id) : orderId(id), next(nullptr) {}
};

class OrderQueue {
public:
    QueueNode *front = nullptr;
    QueueNode *rear = nullptr;
    int size = 0;

    bool isEmpty() { return front == nullptr; }

    void enqueue(int id) {           // push at rear - O(1)
        QueueNode *node = new QueueNode(id);
        if (!rear) { front = rear = node; }
        else { rear->next = node; rear = node; }
        size++;
    }

    int dequeue() {                  // pop from front - O(1)
        if (isEmpty()) return -1;
        int id = front->orderId;
        QueueNode *tmp = front; front = front->next;
        if (!front) rear = nullptr;
        delete tmp; size--;
        return id;
    }

    int peek() { return isEmpty() ? -1 : front->orderId; }

    // NEW in Phase 3a — queue-consistency helpers (all O(n) worst case,
    // O(1) for enqueueFront when the queue is non-empty; the queue is
    // small and these run once per cancel/delete/undo, not per search).
    bool contains(int id) const {
        QueueNode *cur = front;
        while (cur) { if (cur->orderId == id) return true; cur = cur->next; }
        return false;
    }

    // Remove one ID wherever it sits. Used when an order is cancelled
    // or deleted so it can never be processed afterwards (the stale-queue bug).
    bool removeById(int id) {
        if (!front) return false;
        if (front->orderId == id) {
            QueueNode *tmp = front; front = front->next;
            if (!front) rear = nullptr;
            delete tmp; size--; return true;
        }
        QueueNode *cur = front;
        while (cur->next && cur->next->orderId != id) cur = cur->next;
        if (!cur->next) return false;
        QueueNode *tmp = cur->next; cur->next = tmp->next;
        if (!cur->next) rear = cur;
        delete tmp; size--; return true;
    }

    // Put an ID back at the FRONT. Used only by Undo of processNext,
    // so the undone order is processed next again, not last.
    void enqueueFront(int id) {
        if (contains(id)) return; // never enqueue the same order twice
        QueueNode *node = new QueueNode(id);
        node->next = front; front = node;
        if (!rear) rear = node;
        size++;
    }

    // NEW in Phase 3b — queue-position helpers for Undo.
    // indexOf: 0-based position from the front, or -1 if absent.
    int indexOf(int id) const {
        QueueNode *cur = front;
        int idx = 0;
        while (cur) {
            if (cur->orderId == id) return idx;
            cur = cur->next; idx++;
        }
        return -1;
    }

    // Insert at an exact position, preserving FIFO order.
    // pos <= 0 -> front; pos >= size -> rear (clamped); otherwise
    // insert in the middle. Used by Undo to restore the order's
    // original place in the queue instead of always appending it.
    void insertAtPosition(int id, int pos) {
        if (contains(id)) return; // never enqueue the same order twice
        if (pos <= 0) { enqueueFront(id); return; }
        if (pos >= size) { enqueue(id); return; }
        QueueNode *node = new QueueNode(id);
        QueueNode *cur = front;
        for (int i = 0; i < pos - 1; i++) cur = cur->next;
        node->next = cur->next;
        cur->next = node;
        size++;
    }

    void display() {
        if (isEmpty()) { std::cout << "Queue is empty.\n"; return; }
        std::cout << "Queue (front -> rear): ";
        QueueNode *cur = front;
        while (cur) { std::cout << cur->orderId; if (cur->next) std::cout << " -> "; cur = cur->next; }
        std::cout << "\n";
    }
};
