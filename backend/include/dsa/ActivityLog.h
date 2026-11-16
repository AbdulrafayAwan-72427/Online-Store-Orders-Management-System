#pragma once
// ActivityLog.h — Phase 3b (NEW file)
//
// The SECOND BST in this project. It is a completely separate,
// hand-written tree — NOT std::map, NOT std::set, and NOT the
// OrderBST. The two trees answer different questions:
//
//   OrderBST       — keyed by order_id.
//                    "Where is order 1024?"  (fast lookup by ID)
//   ActivityLogBST — keyed by (timestamp, logId).
//                    "What happened, in order?" (chronological audit trail)
//
// Key design:
//   * Timestamp is ISO format "YYYY-MM-DDTHH:MM:SS". Lexicographic
//     order == chronological order, so an inorder traversal of the
//     tree IS the timeline, oldest first.
//   * Two entries can share a timestamp (several actions in one
//     second). The logId breaks the tie, so the key is unique and
//     the order inside one second is the order the actions happened.
//   * logId is assigned by Store, sequentially (1, 2, 3, ...),
//     never reused, and resumes from the highest ID in
//     activity_logs.csv after a restart.
//
// Honest complexity (same rule as the Order BST):
//   * insert / inorder / time-range walk: O(log n) average IF the
//     tree stays balanced, O(n) worst case. Timestamps arrive in
//     increasing order, so inserting them in time order makes the
//     tree lean right — like the sorted order IDs in v1. The tree
//     is NOT self-balancing (no AVL/red-black), so do NOT claim
//     O(log n) is guaranteed. For a semester audit log (tens to
//     hundreds of entries) this is fine, and the inorder walk is
//     O(n) anyway because it visits every entry.
//   * searchLogById(logId): O(n) — full traversal. The tree is
//     ordered by TIME, not by logId, so there is no way to walk
//     straight to a logId. If logId lookup were the main query,
//     the key would be logId. Say this in the viva, do not hide it.
//   * searchLogsByTimeRange(start, end): inorder walk, collect
//     entries with start <= timestamp <= end. O(n) worst case.
//
// Persistence: activity_logs.csv is the truth; this tree is the
// index. Store appends one line per action (append-only in normal
// operation) and rebuilds the tree from the file at startup.
// Undo NEVER deletes a log entry — it appends a new UNDO entry.
// deleteLogById() exists only for authorised maintenance, and
// using it is itself a loggable event (Store does not call it in
// normal operation).

#include <iostream>
#include <string>
#include <vector>

// One audit record. Fields exactly as specified in the Phase 2 plan.
struct ActivityLogEntry {
    int logId = 0;              // unique, assigned by Store (0 = not yet assigned)
    std::string timestamp;      // "YYYY-MM-DDTHH:MM:SS"
    std::string actor = "admin"; // single admin actor (decision D1)
    std::string actionType;     // ORDER_CREATED | ORDER_CANCELLED | STATUS_UPDATED |
                                // ORDER_PROCESSED | ORDER_DELETED | UNDO |
                                // ROUTE_GENERATED | DELIVERY_SELECTION |
                                // DELIVERY_ASSIGNMENT | DELIVERY_COMPLETED (Phase 3c) |
                                // TRIP_CANCELLED (Phase 4A)
    int orderId = -1;           // -1 if the action has no single order
    std::string oldStatus;      // "" if none
    std::string newStatus;      // "" if none
    std::string description;    // human-readable, no commas (CSV-safe)
};

struct LogNode {
    ActivityLogEntry data;
    LogNode *left = nullptr;
    LogNode *right = nullptr;
    explicit LogNode(const ActivityLogEntry &e) : data(e) {}
};

class ActivityLogBST {
public:
    LogNode *root = nullptr;
    int size = 0;

    ActivityLogBST() = default;
    ~ActivityLogBST() { clear(root); root = nullptr; size = 0; }

    // Insert one entry. Returns false (and changes nothing) if:
    //  - logId <= 0, timestamp empty, or actionType empty, or
    //  - an entry with the same logId already exists.
    // A duplicate logId is a data error, not a new entry — the
    // existing entry is kept. Duplicate TIMESTAMPS are fine: the
    // logId breaks the tie (see compareKey).
    bool insertLog(const ActivityLogEntry &e) {
        if (e.logId <= 0 || e.timestamp.empty() || e.actionType.empty())
            return false;
        if (containsLogId(root, e.logId)) return false;
        bool inserted = false;
        root = insertNode(root, e, inserted);
        if (inserted) size++;
        return inserted;
    }

    // Find one entry by logId. O(n) — see the header comment above.
    // Returns nullptr if no such logId exists.
    ActivityLogEntry *searchLogById(int logId) {
        return searchByIdNode(root, logId);
    }

    // Delete one entry by logId. AUTHORISED MAINTENANCE ONLY.
    // Normal operation (including Undo) must NEVER call this.
    // Returns false if the logId does not exist.
    bool deleteLogById(int logId) {
        ActivityLogEntry *found = searchLogById(logId);
        if (!found) return false;
        bool deleted = false;
        root = deleteNode(root, found->timestamp, logId, deleted);
        if (deleted) size--;
        return deleted;
    }

    // Chronological order, oldest first. This is just inorder.
    void inorderTraversal(std::vector<ActivityLogEntry> &out) const {
        out.clear();
        inorderNode(root, out);
    }

    // All entries with start <= timestamp <= end (inclusive).
    // Empty result is a valid answer, not an error. If start > end,
    // the result is empty.
    std::vector<ActivityLogEntry> searchLogsByTimeRange(
        const std::string &start, const std::string &end) const {
        std::vector<ActivityLogEntry> all, result;
        inorderTraversal(all);
        for (const auto &e : all)
            if (e.timestamp >= start && e.timestamp <= end)
                result.push_back(e);
        return result;
    }

    // Helpers for the Order history view and the Logs page.
    // (Inorder walk + filter; O(n). The dataset is small.)
    std::vector<ActivityLogEntry> searchLogsByOrderId(int orderId) const {
        std::vector<ActivityLogEntry> all, result;
        inorderTraversal(all);
        for (const auto &e : all)
            if (e.orderId == orderId) result.push_back(e);
        return result;
    }
    std::vector<ActivityLogEntry> searchLogsByActionType(
        const std::string &type) const {
        std::vector<ActivityLogEntry> all, result;
        inorderTraversal(all);
        for (const auto &e : all)
            if (e.actionType == type) result.push_back(e);
        return result;
    }

    // Largest logId in the tree (0 if empty). Store uses this after
    // loading activity_logs.csv to resume the nextLogId counter.
    int maxLogId() const {
        std::vector<ActivityLogEntry> all;
        inorderTraversal(all);
        int m = 0;
        for (const auto &e : all)
            if (e.logId > m) m = e.logId;
        return m;
    }

    void displayAllLogs() const {
        std::vector<ActivityLogEntry> all;
        inorderTraversal(all);
        if (all.empty()) { std::cout << "No activity logs yet.\n"; return; }
        std::cout << "\n--- Activity Logs (oldest first, " << all.size() << ") ---\n";
        for (const auto &e : all) printLog(e);
    }

    static void printLog(const ActivityLogEntry &e) {
        std::cout << "#" << e.logId << " | " << e.timestamp << " | " << e.actor
                  << " | " << e.actionType << " | order "
                  << (e.orderId == -1 ? std::string("-") : std::to_string(e.orderId))
                  << " | " << e.oldStatus << " -> " << e.newStatus
                  << " | " << e.description << "\n";
    }

private:
    // Compare by (timestamp, logId). Returns <0 / 0 / >0.
    static int compareKey(const ActivityLogEntry &a, const ActivityLogEntry &b) {
        if (a.timestamp != b.timestamp)
            return a.timestamp < b.timestamp ? -1 : 1;
        if (a.logId != b.logId)
            return a.logId < b.logId ? -1 : 1;
        return 0;
    }

    LogNode *insertNode(LogNode *node, const ActivityLogEntry &e, bool &inserted) {
        if (!node) { inserted = true; return new LogNode(e); }
        int cmp = compareKey(e, node->data);
        if (cmp < 0) node->left = insertNode(node->left, e, inserted);
        else if (cmp > 0) node->right = insertNode(node->right, e, inserted);
        else inserted = false; // same (timestamp, logId) — should not happen
        return node;
    }

    LogNode *findMinNode(LogNode *node) const {
        while (node && node->left) node = node->left;
        return node;
    }

    LogNode *deleteNode(LogNode *node, const std::string &ts, int logId,
                        bool &deleted) {
        if (!node) return nullptr;
        ActivityLogEntry key;
        key.timestamp = ts; key.logId = logId;
        int cmp = compareKey(key, node->data);
        if (cmp < 0) node->left = deleteNode(node->left, ts, logId, deleted);
        else if (cmp > 0) node->right = deleteNode(node->right, ts, logId, deleted);
        else {
            deleted = true;
            if (!node->left) { LogNode *r = node->right; delete node; return r; }
            if (!node->right) { LogNode *l = node->left; delete node; return l; }
            LogNode *succ = findMinNode(node->right);
            node->data = succ->data;
            bool dummy = false;
            node->right = deleteNode(node->right, succ->data.timestamp,
                                     succ->data.logId, dummy);
        }
        return node;
    }

    void inorderNode(LogNode *node, std::vector<ActivityLogEntry> &out) const {
        if (!node) return;
        inorderNode(node->left, out);
        out.push_back(node->data);
        inorderNode(node->right, out);
    }

    ActivityLogEntry *searchByIdNode(LogNode *node, int logId) const {
        if (!node) return nullptr;
        if (node->data.logId == logId) return &node->data;
        ActivityLogEntry *left = searchByIdNode(node->left, logId);
        if (left) return left;
        return searchByIdNode(node->right, logId);
    }

    bool containsLogId(LogNode *node, int logId) const {
        return searchByIdNode(node, logId) != nullptr;
    }

    void clear(LogNode *node) {
        if (!node) return;
        clear(node->left);
        clear(node->right);
        delete node;
    }
};
