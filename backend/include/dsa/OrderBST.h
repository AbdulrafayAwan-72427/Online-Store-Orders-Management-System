#pragma once
// OrderBST.h — Phase 3a: moved verbatim from main.cpp v1 (archive/main_v1.cpp).
// OrderBST. Unchanged.

#include "models/Order.h"
#include <iostream>

// 4. BST (Binary Search Tree) - fast search by order_id
//    insert, searchById, deleteById
//    Left child  = smaller ID, Right child = larger ID
//    Search is O(log n) on average, O(n) only if unbalanced.
// ============================================================
struct TreeNode {
    Order data;
    TreeNode *left = nullptr;
    TreeNode *right = nullptr;
    TreeNode(Order o) : data(o) {}
};

class OrderBST {
public:
    TreeNode *root = nullptr;

    void insert(Order o) { root = insertRec(root, o); }

    TreeNode* insertRec(TreeNode *node, Order o) {
        if (!node) return new TreeNode(o);
        if (o.id < node->data.id) node->left = insertRec(node->left, o);
        else if (o.id > node->data.id) node->right = insertRec(node->right, o);
        // if ID already exists, do nothing (IDs are unique)
        return node;
    }

    // Search by ID. steps counts nodes visited.
    Order* searchById(int id, int &steps) {
        steps = 0;
        TreeNode *cur = root;
        while (cur) {
            steps++;
            if (id == cur->data.id) return &cur->data;
            if (id < cur->data.id) cur = cur->left;
            else cur = cur->right;
        }
        return nullptr;
    }
    Order* searchById(int id) { int s; return searchById(id, s); }

    bool updateStatus(int id, const std::string &newStatus) {
        Order *o = searchById(id);
        if (!o) return false;
        o->status = newStatus;
        return true;
    }

    // Delete by ID - standard BST delete (3 cases)
    void deleteById(int id) { root = deleteRec(root, id); }

    TreeNode* findMin(TreeNode *node) {
        while (node && node->left) node = node->left;
        return node;
    }

    TreeNode* deleteRec(TreeNode *node, int id) {
        if (!node) return nullptr;
        if (id < node->data.id) node->left = deleteRec(node->left, id);
        else if (id > node->data.id) node->right = deleteRec(node->right, id);
        else {
            if (!node->left) { TreeNode *t = node->right; delete node; return t; }
            if (!node->right) { TreeNode *t = node->left; delete node; return t; }
            TreeNode *minNode = findMin(node->right);
            node->data = minNode->data;
            node->right = deleteRec(node->right, minNode->data.id);
        }
        return node;
    }

    void inorder(TreeNode *node) {
        if (!node) return;
        inorder(node->left);
        printOrder(node->data);
        inorder(node->right);
    }
    void displaySorted() {
        if (!root) { std::cout << "BST is empty.\n"; return; }
        std::cout << "\n--- Orders sorted by ID (BST inorder) ---\n";
        inorder(root);
    }
};
