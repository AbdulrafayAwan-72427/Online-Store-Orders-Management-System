#pragma once
// LinkedList.h — Phase 3a: moved verbatim from main.cpp v1 (archive/main_v1.cpp).
// OrderLinkedList. Unchanged.

#include "Order.h"
#include <iostream>

// 1. LINKED LIST - store all orders
//    insertAtEnd, insertById, deleteById, searchById
// ============================================================
struct ListNode {
    Order data;
    ListNode *next;
    ListNode(Order o) : data(o), next(nullptr) {}
};

class OrderLinkedList {
public:
    ListNode *head = nullptr;
    ListNode *tail = nullptr;
    int size = 0;

    // Insert at end - O(1) because we keep a tail pointer
    void insertAtEnd(Order o) {
        ListNode *node = new ListNode(o);
        if (!head) { head = tail = node; }
        else { tail->next = node; tail = node; }
        size++;
    }

    // Insert by ID (keeps list sorted by ID) - simple version
    void insertById(Order o) {
        ListNode *node = new ListNode(o);
        if (!head || o.id < head->data.id) {
            node->next = head; head = node;
            if (!tail) tail = node;
            size++; return;
        }
        ListNode *cur = head;
        while (cur->next && cur->next->data.id < o.id) cur = cur->next;
        node->next = cur->next; cur->next = node;
        if (!node->next) tail = node;
        size++;
    }

    // Search by ID - O(n). Returns pointer to Order or nullptr.
    // steps counts how many nodes we checked (for the timing test).
    Order* searchById(int id, int &steps) {
        steps = 0;
        ListNode *cur = head;
        while (cur) {
            steps++;
            if (cur->data.id == id) return &cur->data;
            cur = cur->next;
        }
        return nullptr;
    }
    Order* searchById(int id) { int s; return searchById(id, s); }

    // Delete by ID - O(n) to find, O(1) to remove once found
    bool deleteById(int id) {
        if (!head) return false;
        if (head->data.id == id) {
            ListNode *tmp = head; head = head->next;
            if (!head) tail = nullptr;
            delete tmp; size--; return true;
        }
        ListNode *cur = head;
        while (cur->next && cur->next->data.id != id) cur = cur->next;
        if (!cur->next) return false;
        ListNode *tmp = cur->next; cur->next = tmp->next;
        if (!cur->next) tail = cur;
        delete tmp; size--; return true;
    }

    // Change status in the list (used by Cancel / Undo)
    bool updateStatus(int id, const std::string &newStatus) {
        Order *o = searchById(id);
        if (!o) return false;
        o->status = newStatus;
        return true;
    }

    void displayAll() {
        if (!head) { std::cout << "No orders yet. Load orders.csv first (option 1).\n"; return; }
        std::cout << "\n--- All Orders (" << size << ") ---\n";
        ListNode *cur = head;
        while (cur) { printOrder(cur->data); cur = cur->next; }
    }

    int totalSales() {
        int total = 0;
        ListNode *cur = head;
        while (cur) {
            if (cur->data.status != "Cancelled")
                total += cur->data.price * cur->data.quantity;
            cur = cur->next;
        }
        return total;
    }
    int countStatus(const std::string &st) {
        int c = 0; ListNode *cur = head;
        while (cur) { if (cur->data.status == st) c++; cur = cur->next; }
        return c;
    }
};
