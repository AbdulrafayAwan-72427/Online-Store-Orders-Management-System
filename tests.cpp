// tests.cpp — Phase 3a regression tests (NEW file)
//
// Repeatable, automated tests for the Store. Run:
//   g++ -std=c++17 -Wall -Wextra tests.cpp Store.cpp -o tests
//   ./tests        (on Windows: tests.exe)
//
// Every test prints PASS or FAIL and the program exits 0 only if
// ALL tests pass. No test writes to the real orders.csv or
// delivery_routes.csv: each test that mutates data works on a COPY
// in a temporary folder (test_work/), and the original files' hashes
// are checked at the end.
//
// Expected numbers come from the verified v1 data:
//   30 orders; Lahore 9, Karachi 7, Islamabad 4, Rawalpindi 4,
//   Faisalabad 3, Multan 3; Delivered 11, Shipped 6, Processing 6,
//   Pending 5, Cancelled 2; total sales Rs.182,300.
//   BST search 1024 in 5 steps; 1,000-search List 16,400 vs BST 4,600.
//   Dijkstra Lahore-Islamabad 260 min, Lahore-Karachi 900 min.
// Phase 3a deliberate change: the queue holds Pending orders only
// (5 on load, first = 1006), not Pending+Processing (11 in v1).

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Store.h"

namespace fs = std::filesystem;

static int passed = 0, failed = 0;
static std::string testDir = "test_work";

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) {                                                          \
            std::cout << "  PASS: " << msg << "\n";                         \
            passed++;                                                        \
        } else {                                                             \
            std::cout << "  FAIL: " << msg << "\n";                         \
            failed++;                                                        \
        }                                                                    \
    } while (0)

// Copy the real CSVs into test_work/ so tests never touch the originals.
static bool setupTestData() {
    std::error_code ec;
    fs::remove_all(testDir, ec);
    fs::create_directories(testDir, ec);
    fs::copy_file("orders.csv", testDir + "/orders.csv", ec);
    if (ec) return false;
    fs::copy_file("delivery_routes.csv", testDir + "/delivery_routes.csv", ec);
    return !ec;
}

static Store freshStore() {
    Store s;
    s.ordersPath = testDir + "/orders.csv";
    s.routesPath = testDir + "/delivery_routes.csv";
    s.logsPath = testDir + "/activity_logs.csv";
    s.tripsPath = testDir + "/delivery_trips.csv";
    return s;
}

static int countLines(const std::string &path) {
    std::ifstream f(path);
    int n = 0; std::string line;
    while (std::getline(f, line)) if (!line.empty()) n++;
    return n;
}
static std::vector<int> queueIds(Store &s) {
    std::vector<int> ids;
    QueueNode *cur = s.processQueue.front;
    while (cur) { ids.push_back(cur->orderId); cur = cur->next; }
    return ids;
}

static std::string statusOf(Store &s, int id) {
    Order *o = s.findOrder(id);
    return o ? o->status : "<not found>";
}
static std::string statusOfList(Store &s, int id) {
    Order *o = s.orderList.searchById(id);
    return o ? o->status : "<not found>";
}

// Phase 4B-4 — recursive tree walkers for the /api/logs/tree and
// /api/orders/tree endpoints (the same walks the server does).
static int logTreeHeight(LogNode *n) {
    if (!n) return 0;
    int l = logTreeHeight(n->left), r = logTreeHeight(n->right);
    return 1 + (l > r ? l : r);
}
static void logInorderIds(LogNode *n, std::vector<int> &out) {
    if (!n) return;
    logInorderIds(n->left, out);
    out.push_back(n->data.logId);
    logInorderIds(n->right, out);
}
static int orderTreeHeight(TreeNode *n) {
    if (!n) return 0;
    int l = orderTreeHeight(n->left), r = orderTreeHeight(n->right);
    return 1 + (l > r ? l : r);
}
static void orderInorderIds(TreeNode *n, std::vector<int> &out) {
    if (!n) return;
    orderInorderIds(n->left, out);
    out.push_back(n->data.id);
    orderInorderIds(n->right, out);
}
static bool makeProcessing(Store &s, int id, std::string &err) {
    Order *o = s.findOrder(id);
    if (!o) return false;
    if (o->status == "Processing") return true;
    return s.changeStatus(id, "Processing", err);
}

static bool bothAgree(Store &s, int id, const std::string &status) {
    return statusOf(s, id) == status && statusOfList(s, id) == status;
}

int main() {
    std::cout << "=== Phase 3a + 3b + 3c + 4a + 4B-4 regression tests ===\n";
    if (!setupTestData()) {
        std::cout << "FAIL: could not copy orders.csv / delivery_routes.csv "
                     "into test_work/. Run from the project folder.\n";
        return 1;
    }

    // -- Test 1: load -------------------------------------------------
    {
        std::cout << "\n[1] Load orders + routes\n";
        setupTestData();
        Store s = freshStore();
        std::string err;
        CHECK(s.loadRoutes(s.routesPath), "loadRoutes succeeds");
        CHECK(s.loadOrders(s.ordersPath), "loadOrders succeeds");
        Report r = s.report();
        CHECK(r.total == 30, "30 orders loaded");
        CHECK(r.delivered == 11 && r.shipped == 6 && r.processing == 6 &&
                  r.pending == 5 && r.cancelled == 2,
              "status counts 11/6/6/5/2");
        CHECK(r.totalSales == 182300, "total sales Rs.182,300");
        CHECK(r.queueSize == 5, "queue holds Pending only: 5 (v1 had 11)");
        CHECK(s.processQueue.peek() == 1006, "queue front is 1006 (first Pending)");
        CHECK(s.graph.n == 6, "graph has 6 cities");
    }

    // -- Test 2: BST search + benchmark -------------------------------
    {
        std::cout << "\n[2] BST search + List vs BST benchmark\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        int steps = 0;
        Order *o = s.findOrder(1024, steps);
        CHECK(o && o->customer == "Waqas Ahmed", "BST finds 1024 (Waqas Ahmed)");
        CHECK(steps == 5, "BST search 1024 in 5 steps");
        int listSteps = 0, treeSteps = 0;
        int ids[] = {1001, 1010, 1024, 1030, 1017};
        for (int rep = 0; rep < 200; rep++)
            for (int id : ids) { int st; s.orderList.searchById(id, st); listSteps += st; }
        for (int rep = 0; rep < 200; rep++)
            for (int id : ids) { int st; s.orderTree.searchById(id, st); treeSteps += st; }
        CHECK(listSteps == 16400, "1,000 searches: Linked List 16,400 steps");
        CHECK(treeSteps == 4600, "1,000 searches: BST 4,600 steps");
        CHECK(s.findOrder(9999) == nullptr, "search for missing ID 9999 returns null");
    }

    // -- Test 3: add order --------------------------------------------
    {
        std::cout << "\n[3] Add order\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::string err;
        Order o{1031, "Test User", "Test Product", "Electronics", 1000, 1,
                "2026-10-16", "Lahore", "Pending"};
        CHECK(s.addOrder(o, err), "add order 1031 succeeds");
        CHECK(s.report().total == 31, "total is 31 after add");
        CHECK(s.processQueue.contains(1031), "new Pending order is in the queue");
        CHECK(bothAgree(s, 1031, "Pending"), "List and BST agree: 1031 Pending");
        Order dup{1001, "X", "Y", "Electronics", 100, 1, "2026-10-16", "Lahore", "Pending"};
        CHECK(!s.addOrder(dup, err), "duplicate ID 1001 is rejected");
        Order bad{1032, "X", "Y", "Electronics", 0, 1, "2026-10-16", "Lahore", "Pending"};
        CHECK(!s.addOrder(bad, err), "price 0 is rejected");
    }

    // -- Test 4: FIFO processing + undo -------------------------------
    {
        std::cout << "\n[4] FIFO queue processing + undo\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::string err;
        Order out;
        CHECK(s.processNext(out, err) && out.id == 1006, "first processed is 1006 (first Pending)");
        CHECK(bothAgree(s, 1006, "Processing"), "1006 is Processing in List and BST");
        CHECK(s.processQueue.size == 4, "queue size 4 after one process");
        CHECK(s.processNext(out, err) && out.id == 1012, "second processed is 1012");
        CHECK(s.undo(err), "undo succeeds");
        CHECK(bothAgree(s, 1012, "Pending"), "undo restores 1012 to Pending in List and BST");
        CHECK(s.processQueue.peek() == 1012, "undo of process puts 1012 back at FRONT of queue");
        CHECK(s.processNext(out, err) && out.id == 1012, "next process is 1012 again");
    }

    // -- Test 5: cancel + undo (queue consistency) --------------------
    {
        std::cout << "\n[5] Cancel + undo restores queue\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::string err;
        CHECK(s.cancelOrder(1006, err), "cancel Pending order 1006 succeeds");
        CHECK(bothAgree(s, 1006, "Cancelled"), "1006 Cancelled in List and BST");
        CHECK(!s.processQueue.contains(1006), "cancelled order is REMOVED from queue (stale-queue fix)");
        CHECK(s.undo(err), "undo succeeds");
        CHECK(bothAgree(s, 1006, "Pending"), "undo restores 1006 to Pending in List and BST");
        CHECK(s.processQueue.contains(1006), "undo puts 1006 back in the queue");
        // Cancel a Delivered order must fail (transition table).
        CHECK(!s.cancelOrder(1001, err), "cancel Delivered order 1001 is rejected");
        CHECK(bothAgree(s, 1001, "Delivered"), "1001 is still Delivered after rejected cancel");
    }

    // -- Test 6: invalid transitions ----------------------------------
    {
        std::cout << "\n[6] Invalid status transitions are rejected\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::string err;
        CHECK(!s.changeStatus(1001, "Processing", err), "Delivered -> Processing rejected");
        CHECK(s.changeStatus(1003, "Delivered", err), "Shipped -> Delivered is allowed");
        CHECK(bothAgree(s, 1003, "Delivered"), "1003 Delivered in List and BST");
        CHECK(!s.changeStatus(1006, "Delivered", err), "Pending -> Delivered rejected (must go via Processing/Shipped)");
        CHECK(!s.changeStatus(9999, "Shipped", err), "changeStatus on missing order rejected");
        CHECK(s.changeStatus(1006, "Processing", err), "Pending -> Processing is allowed");
        CHECK(s.changeStatus(1006, "Shipped", err), "Processing -> Shipped is allowed");
        CHECK(s.changeStatus(1006, "Delivered", err), "Shipped -> Delivered is allowed");
        CHECK(bothAgree(s, 1006, "Delivered"), "1006 Delivered in List and BST");
    }

    // -- Test 7: delete removes from List, BST and Queue --------------
    {
        std::cout << "\n[7] Delete order\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::string err;
        CHECK(s.deleteOrder(1027, err), "delete Pending order 1027 succeeds");
        CHECK(s.findOrder(1027) == nullptr, "1027 gone from BST");
        CHECK(s.orderList.searchById(1027) == nullptr, "1027 gone from Linked List");
        CHECK(!s.processQueue.contains(1027), "1027 gone from queue");
        CHECK(!s.deleteOrder(1027, err), "deleting 1027 twice is rejected");
        CHECK(s.report().total == 29, "total is 29 after delete");
    }

    // -- Test 8: Dijkstra ----------------------------------------------
    {
        std::cout << "\n[8] Graph / Dijkstra\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::vector<std::string> path;
        int t = s.graph.shortestTime("Lahore", "Islamabad", path);
        CHECK(t == 260, "Lahore->Islamabad 260 min");
        CHECK(path.size() == 2 && path[0] == "Lahore" && path[1] == "Islamabad",
              "Lahore->Islamabad path is direct, no intermediate city");
        t = s.graph.shortestTime("Lahore", "Karachi", path);
        CHECK(t == 900, "Lahore->Karachi 900 min");
        CHECK(path.size() == 2, "Lahore->Karachi path is direct");
        t = s.graph.shortestTime("Lahore", "Nowhere", path);
        CHECK(t == -1, "unknown city returns -1");
    }

    // -- Test 9: persistence + atomic save -----------------------------
    {
        std::cout << "\n[9] Persistence after restart + atomic save\n";
        setupTestData();
        std::string err;
        {
            Store s = freshStore();
            s.loadAll();
            CHECK(s.cancelOrder(1006, err), "cancel 1006 in session 1");
            CHECK(s.addOrder(Order{1031, "Test User", "Test Product", "Electronics",
                                   1000, 1, "2026-10-16", "Lahore", "Pending"}, err),
                  "add 1031 in session 1");
            // saveOrders() is called inside every mutation; check no .tmp is left.
            CHECK(!fs::exists(testDir + "/orders.csv.tmp"), "no leftover .tmp file after saves");
        }
        {
            Store s2 = freshStore(); // "restart": fresh Store, reload from CSV
            s2.loadAll();
            CHECK(s2.report().total == 31, "after restart: 31 orders (30 + 1031)");
            CHECK(bothAgree(s2, 1006, "Cancelled"), "after restart: 1006 still Cancelled");
            CHECK(s2.findOrder(1031) != nullptr, "after restart: 1031 still present");
            CHECK(s2.processQueue.contains(1031), "after restart: 1031 still in queue");
            CHECK(!s2.processQueue.contains(1006), "after restart: cancelled 1006 still out of queue");
        }
    }

    // -- Test 10: original datasets untouched --------------------------
    {
        std::cout << "\n[10] Original datasets preserved\n";
        setupTestData();
        // tests.cpp only ever wrote to test_work/. Verify the real files
        // still have 30 orders / 12 routes by counting lines.
        auto countLines = [](const std::string &path) {
            std::ifstream f(path);
            int n = 0; std::string line;
            while (std::getline(f, line)) if (!line.empty()) n++;
            return n;
        };
        CHECK(countLines("orders.csv") == 31, "orders.csv still has 30 orders + header (31 lines)");
        CHECK(countLines("delivery_routes.csv") == 13, "delivery_routes.csv still has 12 routes + header (13 lines)");
        CHECK(!fs::exists("orders.csv.tmp"), "no .tmp file left next to the real orders.csv");
    }


    // -- Test 11: Activity Log BST unit tests -------------------------
    {
        std::cout << "\n[11] Activity Log BST (unit, direct tree)\n";
        setupTestData();
        ActivityLogBST tree;
        auto mk = [](int id, const std::string &ts, const std::string &type, int oid) {
            ActivityLogEntry e; e.logId = id; e.timestamp = ts;
            e.actionType = type; e.orderId = oid; e.description = "test";
            return e;
        };
        // Insert out of chronological order: tree must sort them.
        CHECK(tree.insertLog(mk(3, "2026-11-13T10:00:03", "STATUS_UPDATED", 1006)), "insert log 3");
        CHECK(tree.insertLog(mk(1, "2026-11-13T10:00:01", "ORDER_CREATED", 1006)), "insert log 1");
        CHECK(tree.insertLog(mk(2, "2026-11-13T10:00:02", "ORDER_CANCELLED", 1006)), "insert log 2");
        CHECK(tree.size == 3, "tree size 3 after 3 inserts");
        std::vector<ActivityLogEntry> all;
        tree.inorderTraversal(all);
        CHECK(all.size() == 3 && all[0].logId == 1 && all[1].logId == 2 && all[2].logId == 3,
              "inorder traversal is chronological (1,2,3) despite insert order");
        // Duplicate TIMESTAMP, different logId: both kept, ordered by logId.
        CHECK(tree.insertLog(mk(4, "2026-11-13T10:00:02", "UNDO", 1006)), "duplicate timestamp insert succeeds");
        tree.inorderTraversal(all);
        CHECK(all.size() == 4 && all[1].logId == 2 && all[2].logId == 4,
              "duplicate timestamp ordered by logId (2 before 4)");
        // Duplicate logId rejected, original kept.
        CHECK(!tree.insertLog(mk(2, "2026-11-13T10:00:09", "UNDO", 1006)), "duplicate logId 2 rejected");
        CHECK(tree.size == 4, "tree size still 4 after duplicate logId");
        ActivityLogEntry bad; bad.logId = 0; bad.timestamp = "2026-11-13T10:00:10"; bad.actionType = "UNDO";
        CHECK(!tree.insertLog(bad), "logId 0 rejected");
        ActivityLogEntry bad2; bad2.logId = 9; bad2.timestamp = ""; bad2.actionType = "UNDO";
        CHECK(!tree.insertLog(bad2), "empty timestamp rejected");
        // searchLogById
        ActivityLogEntry *f = tree.searchLogById(3);
        CHECK(f && f->actionType == "STATUS_UPDATED", "searchLogById(3) finds STATUS_UPDATED");
        CHECK(tree.searchLogById(9999) == nullptr, "searchLogById(9999) returns null");
        CHECK(tree.searchLogById(-1) == nullptr, "searchLogById(-1) returns null");
        // Time-range queries
        auto range = tree.searchLogsByTimeRange("2026-11-13T10:00:01", "2026-11-13T10:00:02");
        CHECK(range.size() == 3, "time range 10:00:01-10:00:02 returns 3 entries (incl. duplicate ts)");
        CHECK(tree.searchLogsByTimeRange("2026-11-14T00:00:00", "2026-11-14T23:59:59").empty(),
              "time range with no entries returns empty");
        CHECK(tree.searchLogsByTimeRange("2026-11-13T10:00:03", "2026-11-13T10:00:01").empty(),
              "start > end returns empty");
        CHECK(tree.maxLogId() == 4, "maxLogId is 4");
        // deleteLogById (maintenance only)
        CHECK(tree.deleteLogById(2), "deleteLogById(2) succeeds");
        CHECK(tree.size == 3 && tree.searchLogById(2) == nullptr, "log 2 gone after delete");
        CHECK(!tree.deleteLogById(2), "deleting log 2 twice is rejected");
        CHECK(!tree.deleteLogById(9999), "deleting missing logId is rejected");
        tree.inorderTraversal(all);
        CHECK(all.size() == 3 && all[0].logId == 1 && all[1].logId == 4 && all[2].logId == 3,
              "inorder still chronological after delete (1,4,3)");
    }

    // -- Test 12: Store logging for every action ----------------------
    {
        std::cout << "\n[12] Store logs every action\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::string err;
        CHECK(s.report().logSize == 0, "log starts empty");
        Order o{1031, "Test User", "Test Product", "Electronics", 1000, 1,
                "2026-10-16", "Lahore", "Pending"};
        CHECK(s.addOrder(o, err), "add 1031 succeeds");
        CHECK(s.report().logSize == 1, "1 log entry after add");
        ActivityLogEntry *e1 = s.logTree.searchLogById(1);
        CHECK(e1 && e1->actionType == "ORDER_CREATED" && e1->orderId == 1031,
              "log 1 is ORDER_CREATED for 1031");
        CHECK(e1 && e1->actor == "admin", "log actor is admin");
        CHECK(s.cancelOrder(1006, err), "cancel 1006 succeeds");
        ActivityLogEntry *e2 = s.logTree.searchLogById(2);
        CHECK(e2 && e2->actionType == "ORDER_CANCELLED" && e2->orderId == 1006,
              "log 2 is ORDER_CANCELLED for 1006");
        CHECK(s.changeStatus(1012, "Processing", err), "change 1012 to Processing succeeds");
        ActivityLogEntry *e3 = s.logTree.searchLogById(3);
        CHECK(e3 && e3->actionType == "STATUS_UPDATED" && e3->oldStatus == "Pending",
              "log 3 is STATUS_UPDATED Pending->Processing");
        Order out;
        CHECK(s.processNext(out, err) && out.id == 1017, "processNext returns 1017");
        ActivityLogEntry *e4 = s.logTree.searchLogById(4);
        CHECK(e4 && e4->actionType == "ORDER_PROCESSED" && e4->orderId == 1017,
              "log 4 is ORDER_PROCESSED for 1017");
        CHECK(s.deleteOrder(1022, err), "delete 1022 succeeds");
        ActivityLogEntry *e5 = s.logTree.searchLogById(5);
        CHECK(e5 && e5->actionType == "ORDER_DELETED" && e5->orderId == 1022,
              "log 5 is ORDER_DELETED for 1022");
        CHECK(s.report().logSize == 5, "5 log entries total");
        // File matches the tree: 5 entries + header = 6 lines.
        CHECK(countLines(testDir + "/activity_logs.csv") == 6,
              "activity_logs.csv has 5 entries + header");
        // Order history: all logs for 1006 are found.
        auto hist = s.logTree.searchLogsByOrderId(1006);
        CHECK(hist.size() == 1 && hist[0].actionType == "ORDER_CANCELLED",
              "searchLogsByOrderId(1006) returns the cancel entry");
        CHECK(s.logTree.searchLogsByOrderId(9999).empty(),
              "searchLogsByOrderId(9999) returns empty");
        // Failed actions write NO log.
        int before = s.report().logSize;
        CHECK(!s.cancelOrder(1001, err), "cancel Delivered 1001 fails");
        CHECK(s.report().logSize == before, "failed action writes no log entry");
        CHECK(!s.addOrder(o, err), "duplicate add fails");
        CHECK(s.report().logSize == before, "failed add writes no log entry");
    }

    // -- Test 13: Undo appends UNDO, never erases history -------------
    {
        std::cout << "\n[13] Undo appends UNDO, history kept\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::string err;
        CHECK(s.cancelOrder(1006, err), "cancel 1006 succeeds");
        int cancelLogId = s.nextLogId - 1;
        CHECK(s.undo(err), "undo succeeds");
        ActivityLogEntry *cancelEntry = s.logTree.searchLogById(cancelLogId);
        CHECK(cancelEntry && cancelEntry->actionType == "ORDER_CANCELLED",
              "original ORDER_CANCELLED entry still in tree after undo");
        ActivityLogEntry *undoEntry = s.logTree.searchLogById(cancelLogId + 1);
        CHECK(undoEntry && undoEntry->actionType == "UNDO" && undoEntry->orderId == 1006,
              "undo appends a new UNDO entry with the next logId");
        CHECK(undoEntry && undoEntry->oldStatus == "Cancelled" && undoEntry->newStatus == "Pending",
              "UNDO entry records Cancelled -> Pending");
        CHECK(s.report().logSize == 2, "log size grew (2), nothing was deleted");
        CHECK(countLines(testDir + "/activity_logs.csv") == 3,
              "log file has 2 entries + header after undo");
    }

    // -- Test 14: Queue position preserved on Undo --------------------
    {
        std::cout << "\n[14] Queue position preserved on Undo\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::string err;
        std::vector<int> orig = queueIds(s);
        CHECK(orig == std::vector<int>({1006, 1012, 1017, 1022, 1027}),
              "queue starts [1006,1012,1017,1022,1027]");
        // Cancel the MIDDLE order (1017, position 2), then undo.
        CHECK(s.cancelOrder(1017, err), "cancel middle order 1017 succeeds");
        CHECK(queueIds(s) == std::vector<int>({1006, 1012, 1022, 1027}),
              "queue after cancel is [1006,1012,1022,1027]");
        CHECK(s.undo(err), "undo succeeds");
        CHECK(queueIds(s) == orig,
              "undo restores 1017 to its ORIGINAL position (index 2), not the rear");
        // Cancel the FRONT order, then undo.
        CHECK(s.cancelOrder(1006, err), "cancel front order 1006 succeeds");
        CHECK(s.undo(err), "undo succeeds");
        CHECK(queueIds(s) == orig && s.processQueue.peek() == 1006,
              "undo restores front order 1006 to the front");
        // Process two, undo twice: queue back to the original order.
        Order out;
        CHECK(s.processNext(out, err) && out.id == 1006, "process 1006");
        CHECK(s.processNext(out, err) && out.id == 1012, "process 1012");
        CHECK(s.undo(err), "undo 1012");
        CHECK(s.processQueue.peek() == 1012, "undo of process puts 1012 at front");
        CHECK(s.undo(err), "undo 1006");
        CHECK(queueIds(s) == orig, "undoing both processes restores the original queue");
    }

    // -- Test 15: Log persistence after restart -----------------------
    {
        std::cout << "\n[15] Log persistence after restart\n";
        setupTestData();
        std::string err;
        {
            Store s = freshStore();
            s.loadAll();
            CHECK(s.addOrder(Order{1031, "Test User", "Test Product", "Electronics",
                                   1000, 1, "2026-10-16", "Lahore", "Pending"}, err),
                  "add 1031 in session 1");
            CHECK(s.cancelOrder(1006, err), "cancel 1006 in session 1");
            CHECK(s.report().logSize == 2, "2 log entries in session 1");
        }
        {
            Store s2 = freshStore();
            s2.loadAll();
            CHECK(s2.report().logSize == 2, "after restart: 2 log entries reloaded");
            CHECK(s2.logTree.searchLogById(1) != nullptr &&
                  s2.logTree.searchLogById(1)->actionType == "ORDER_CREATED",
                  "after restart: log 1 is ORDER_CREATED");
            CHECK(s2.logTree.searchLogById(2) != nullptr &&
                  s2.logTree.searchLogById(2)->actionType == "ORDER_CANCELLED",
                  "after restart: log 2 is ORDER_CANCELLED");
            CHECK(s2.nextLogId == 3, "after restart: nextLogId resumes at 3, not 1");
            CHECK(s2.changeStatus(1012, "Processing", err), "new action after restart succeeds");
            ActivityLogEntry *e3 = s2.logTree.searchLogById(3);
            CHECK(e3 && e3->actionType == "STATUS_UPDATED",
                  "new action after restart gets logId 3 (no reuse)");
        }
    }

    // -- Test 16: Failure rollback — log append fails -----------------
    {
        std::cout << "\n[16] Rollback when the log file cannot be written\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::string err;
        // Point the log file at a directory that does not exist.
        s.logsPath = testDir + "/no_such_dir/activity_logs.csv";
        int logsBefore = s.report().logSize;
        CHECK(!s.cancelOrder(1006, err), "cancel fails when the log file cannot be written");
        CHECK(bothAgree(s, 1006, "Pending"), "after failed cancel: 1006 still Pending in List and BST");
        CHECK(s.processQueue.contains(1006), "after failed cancel: 1006 still in the queue");
        CHECK(s.report().logSize == logsBefore, "after failed cancel: no log entry was created");
        // The orders file on disk must also be unchanged (30 orders, 1006 Pending).
        Store s2 = freshStore();
        s2.loadAll();
        CHECK(bothAgree(s2, 1006, "Pending"), "after reload: 1006 still Pending on disk");
        CHECK(s2.report().logSize == 0, "after reload: no log entries on disk either");
        // Same for add: it must fail cleanly and leave no trace.
        s.logsPath = testDir + "/no_such_dir/activity_logs.csv";
        Order o{1031, "Test User", "Test Product", "Electronics", 1000, 1,
                "2026-10-16", "Lahore", "Pending"};
        CHECK(!s.addOrder(o, err), "add fails when the log file cannot be written");
        CHECK(s.findOrder(1031) == nullptr, "after failed add: 1031 not in memory");
        CHECK(s.report().total == 30, "after failed add: total still 30");
    }

    // -- Test 17: Failure rollback — orders save fails ----------------
    {
        std::cout << "\n[17] Rollback when orders.csv cannot be saved\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::string err;
        // Point orders at a directory that does not exist.
        s.ordersPath = testDir + "/no_such_dir/orders.csv";
        int logsBefore = s.report().logSize;
        CHECK(!s.cancelOrder(1006, err), "cancel fails when orders.csv cannot be saved");
        CHECK(bothAgree(s, 1006, "Pending"), "after failed cancel: 1006 still Pending in memory");
        CHECK(s.report().logSize == logsBefore, "after failed orders save: no log entry was created");
        // Undo failure path: log file broken, undo must not change anything.
        s.ordersPath = testDir + "/orders.csv"; // restore orders path
        s.logsPath = testDir + "/activity_logs.csv"; // restore log path
        CHECK(s.cancelOrder(1006, err), "cancel succeeds again with good paths");
        s.logsPath = testDir + "/no_such_dir/activity_logs.csv";
        CHECK(!s.undo(err), "undo fails when the log file cannot be written");
        CHECK(bothAgree(s, 1006, "Cancelled"), "after failed undo: 1006 still Cancelled");
        CHECK(s.undoCount == 1, "after failed undo: the undo action is still on the stack");
    }

    // -- Test 18: Corrupt log lines are skipped on load --------------
    {
        std::cout << "\n[18] Corrupt log lines skipped on load\n";
        setupTestData();
        {
            std::ofstream f(testDir + "/activity_logs.csv");
            f << "log_id,timestamp,actor,action_type,order_id,old_status,new_status,description\n";
            f << "1,2026-11-13T10:00:01,admin,ORDER_CREATED,1031,,Pending,Order 1031 created\n";
            f << "this is not a log line at all\n";
            f << "2,2026-11-13T10:00:02,admin,ORDER_CANCELLED,1006,Pending,Cancelled,Order 1006 cancelled\n";
            f << "99,not-a-timestamp\n";
            f << "1,2026-11-13T10:00:03,admin,UNDO,1006,Cancelled,Pending,duplicate logId 1\n";
            f << "3,2026-11-13T10:00:03,admin,UNDO,1006,Cancelled,Pending,Order 1006 restored\n";
        }
        Store s = freshStore();
        s.loadAll();
        CHECK(s.report().logSize == 3, "3 good entries loaded, corrupt/duplicate lines skipped");
        CHECK(s.logsSkipped == 3, "logsSkipped counts 3 bad lines (garbage, short, duplicate)");
        CHECK(s.nextLogId == 4, "nextLogId resumes at 4 after corrupt load");
        CHECK(s.logTree.searchLogById(3) != nullptr, "good entry 3 loaded");
        CHECK(s.logTree.searchLogById(99) == nullptr, "short corrupt line did not create log 99");
    }


    // -- Test 19: Graph distance + RouteResult -------------------------
    {
        std::cout << "\n[19] Graph distance + RouteResult\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        RouteResult r = s.graph.shortestRoute("Lahore", "Islamabad");
        CHECK(r.ok, "Lahore->Islamabad route ok");
        CHECK(r.totalTimeMin == 260 && r.totalDistanceKm == 320,
              "Lahore->Islamabad 260 min, 320 km");
        CHECK(r.path.size() == 2 && r.path[0] == "Lahore" && r.path[1] == "Islamabad",
              "Lahore->Islamabad path is direct");
        r = s.graph.shortestRoute("Lahore", "Karachi");
        CHECK(r.ok && r.totalTimeMin == 900 && r.totalDistanceKm == 1210,
              "Lahore->Karachi 900 min, 1210 km");
        r = s.graph.shortestRoute("Lahore", "Multan");
        CHECK(r.ok && r.totalTimeMin == 280 && r.totalDistanceKm == 340,
              "Lahore->Multan 280 min, 340 km");
        r = s.graph.shortestRoute("Lahore", "Faisalabad");
        CHECK(r.ok && r.totalTimeMin == 150 && r.totalDistanceKm == 180,
              "Lahore->Faisalabad 150 min, 180 km");
        r = s.graph.shortestRoute("Lahore", "Nowhere");
        CHECK(!r.ok, "unknown destination returns ok=false");
        r = s.graph.shortestRoute("Lahore", "Lahore");
        CHECK(r.ok && r.totalTimeMin == 0 && r.totalDistanceKm == 0 && r.path.size() == 1,
              "same source and destination returns 0 min, 0 km");
        CHECK(s.graph.edgeDistance("Lahore", "Karachi") == 1210, "edgeDistance Lahore-Karachi 1210 km");
        CHECK(s.graph.edgeTime("Faisalabad", "Multan") == 150, "edgeTime Faisalabad-Multan 150 min");
    }

    // -- Test 20: Intermediate matching on production data (Example A) -
    {
        std::cout << "\n[20] Intermediate matching — production data, no intermediates\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::string err;
        Store::PlanResult pr = s.planRoute("Lahore", "Islamabad", err);
        CHECK(pr.route.ok && pr.route.totalTimeMin == 260, "plan Lahore->Islamabad 260 min");
        CHECK(pr.intermediate.empty(), "Example A: no intermediate cities on Lahore->Islamabad");
        CHECK(pr.destinationOrders.empty(), "Example A: no eligible orders in Islamabad (all Shipped/Delivered)");
        pr = s.planRoute("Lahore", "Karachi", err);
        CHECK(pr.route.ok && pr.intermediate.empty(), "Lahore->Karachi also has no intermediates");
        CHECK(pr.destinationOrders.size() == 2, "Karachi destination eligible orders: 1011, 1016 (Processing)");
        // Planning must not change any order status.
        CHECK(s.report().pending == 5 && s.report().processing == 6,
              "planning changed no order statuses");
        // Planning logs ROUTE_GENERATED (one per plan call).
        CHECK(s.logTree.searchLogsByActionType("ROUTE_GENERATED").size() == 2,
              "2 ROUTE_GENERATED log entries after 2 plans");
    }

    // -- Test 21: Test dataset — genuine intermediate city -------------
    {
        std::cout << "\n[21] Test dataset — genuine intermediate city\n";
        setupTestData();
        Store s = freshStore();
        s.routesPath = "test_routes_intermediate.csv";
        s.loadAll();
        std::string err;
        Store::PlanResult pr = s.planRoute("Lahore", "Islamabad", err);
        CHECK(pr.route.ok, "test-dataset route ok");
        CHECK(pr.route.totalTimeMin == 275 && pr.route.totalDistanceKm == 315,
              "Lahore->Islamabad via Rawalpindi 275 min, 315 km (no direct edge)");
        CHECK(pr.route.path.size() == 3 && pr.route.path[1] == "Rawalpindi",
              "path is Lahore -> Rawalpindi -> Islamabad");
        CHECK(pr.intermediate.size() == 1 && pr.intermediate[0].city == "Rawalpindi",
              "one intermediate city: Rawalpindi");
        CHECK(pr.intermediate[0].timeToCityMin == 240 && pr.intermediate[0].distanceToCityKm == 290,
              "Rawalpindi reached in 240 min, 290 km");
        CHECK(pr.intermediate[0].orders.size() == 2,
              "Rawalpindi eligible orders: 2 (1005, 1021, both Processing)");
        bool has1005 = false, has1021 = false;
        for (const Order &o : pr.intermediate[0].orders) {
            if (o.id == 1005) has1005 = true;
            if (o.id == 1021) has1021 = true;
        }
        CHECK(has1005 && has1021, "intermediate orders are 1005 and 1021");
        // Production CSV is untouched by using the test dataset.
        CHECK(countLines("delivery_routes.csv") == 13, "production delivery_routes.csv still has 12 routes + header");
    }

    // -- Test 22: Detour ------------------------------------------------
    {
        std::cout << "\n[22] Detour calculation\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::string err;
        DetourResult d = s.planDetour("Lahore", "Karachi", "Faisalabad", err);
        CHECK(d.ok, "detour Lahore->Karachi via Faisalabad ok");
        CHECK(d.original.totalTimeMin == 900 && d.original.totalDistanceKm == 1210,
              "detour: original 900 min, 1210 km");
        CHECK(d.via.totalTimeMin == 930 && d.via.totalDistanceKm == 1230,
              "detour via Faisalabad 930 min, 1230 km");
        CHECK(d.extraTimeMin == 30 && d.extraDistanceKm == 20,
              "detour extra cost +30 min, +20 km");
        CHECK(d.eligibleOrders.size() == 2, "detour eligible orders in Faisalabad: 2 (1006, 1022)");
        DetourResult d2 = s.planDetour("Lahore", "Multan", "Faisalabad", err);
        CHECK(d2.ok && d2.extraTimeMin == 20, "Lahore->Multan via Faisalabad extra +20 min");
        DetourResult d3 = s.planDetour("Lahore", "Islamabad", "Faisalabad", err);
        CHECK(d3.ok && d3.extraTimeMin == 180, "Lahore->Islamabad via Faisalabad extra +180 min (expensive, shown honestly)");
        DetourResult bad = s.planDetour("Lahore", "Karachi", "Nowhere", err);
        CHECK(!bad.ok, "detour to unknown city fails");
        DetourResult bad2 = s.planDetour("Lahore", "Karachi", "Lahore", err);
        CHECK(!bad2.ok, "detour city = source fails");
        DetourResult bad3 = s.planDetour("Lahore", "Karachi", "Karachi", err);
        CHECK(!bad3.ok, "detour city = destination fails");
    }

    // -- Test 23: Multiple stops, exact method --------------------------
    {
        std::cout << "\n[23] Multiple stops — exact method\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::string err;
        MultiStopResult ms = s.planMultiStop("Lahore", "Karachi", {"Faisalabad", "Multan"}, err);
        CHECK(ms.ok, "multi-stop Lahore->Karachi via Faisalabad+Multan ok");
        CHECK(ms.stopOrder.size() == 2 && ms.stopOrder[0] == "Faisalabad" && ms.stopOrder[1] == "Multan",
              "best stop order is Faisalabad then Multan (not the reverse)");
        CHECK(ms.totalTimeMin == 960 && ms.totalDistanceKm == 1240,
              "best multi-stop route 960 min, 1240 km (reverse order would be 1210 min)");
        CHECK(ms.bestRoute.path.size() == 4, "multi-stop path has 4 cities");
        // Reverse input order must give the same best result.
        MultiStopResult ms2 = s.planMultiStop("Lahore", "Karachi", {"Multan", "Faisalabad"}, err);
        CHECK(ms2.ok && ms2.totalTimeMin == 960 && ms2.stopOrder[0] == "Faisalabad",
              "input order does not change the best route");
        // Single stop = detour; empty stops = shortest route.
        MultiStopResult ms3 = s.planMultiStop("Lahore", "Karachi", {"Faisalabad"}, err);
        CHECK(ms3.ok && ms3.totalTimeMin == 930, "single stop = detour route, 930 min");
        MultiStopResult ms4 = s.planMultiStop("Lahore", "Karachi", {}, err);
        CHECK(ms4.ok && ms4.totalTimeMin == 900, "empty stops = direct route, 900 min");
        // Invalid inputs.
        CHECK(!s.planMultiStop("Lahore", "Karachi", {"Faisalabad", "Faisalabad"}, err).ok,
              "duplicate stops rejected");
        CHECK(!s.planMultiStop("Lahore", "Karachi", {"Nowhere"}, err).ok,
              "unknown stop rejected");
        CHECK(!s.planMultiStop("Lahore", "Karachi", {"Lahore"}, err).ok,
              "stop = source rejected");
    }

    // -- Test 24: Trip assignment ---------------------------------------
    {
        std::cout << "\n[24] Trip assignment\n";
        setupTestData();
        Store s = freshStore();
        s.routesPath = "test_routes_intermediate.csv";
        s.loadAll();
        std::string err;
        int tripId = 0;
        // 1005 and 1021 are already Processing in Rawalpindi — assignable.
        CHECK(s.createTrip("Lahore", "Islamabad", {}, {1005, 1021}, err, tripId),
              "create trip Lahore->Islamabad with 1005,1021 succeeds");
        CHECK(tripId == 1, "first trip gets ID 1");
        CHECK(bothAgree(s, 1005, "Shipped") && bothAgree(s, 1021, "Shipped"),
              "assigned orders are Shipped in List and BST");
        DeliveryTrip *tr = s.findTrip(1);
        CHECK(tr && tr->status == "Planned", "trip 1 is Planned");
        CHECK(tr && tr->totalTimeMin == 275 && tr->totalDistanceKm == 315,
              "trip 1 route 275 min, 315 km");
        CHECK(tr && tr->route.size() == 3 && tr->route[1] == "Rawalpindi",
              "trip 1 route goes via Rawalpindi");
        CHECK(countLines(testDir + "/delivery_trips.csv") == 2,
              "delivery_trips.csv has 1 trip + header");
        CHECK(s.logTree.searchLogsByActionType("DELIVERY_ASSIGNMENT").size() == 2,
              "2 DELIVERY_ASSIGNMENT log entries (one per order)");
        CHECK(s.logTree.searchLogsByActionType("DELIVERY_SELECTION").size() == 1,
              "1 DELIVERY_SELECTION log entry for the trip");
        CHECK(!s.processQueue.contains(1005) && !s.processQueue.contains(1021),
              "assigned (Shipped) orders are not in the processing queue");
        // Undo stack was NOT touched by the multi-order assignment.
        CHECK(s.undoCount == 0, "trip assignment pushes no single-order undo action");
    }

    // -- Test 25: Assignment requires Processing + no duplicates --------
    {
        std::cout << "\n[25] Assignment rules — Processing only, no duplicates\n";
        setupTestData();
        Store s = freshStore();
        s.loadAll();
        std::string err;
        int tripId = 0;
        // 1006 is Pending in Faisalabad — must be processed first.
        CHECK(!s.createTrip("Lahore", "Karachi", {"Faisalabad"}, {1006}, err, tripId),
              "assigning a Pending order (1006) is rejected");
        CHECK(bothAgree(s, 1006, "Pending"), "rejected Pending order stays Pending");
        CHECK(s.trips.empty(), "no trip created after rejected assignment");
        // Process it, then assignment succeeds.
        CHECK(makeProcessing(s, 1006, err), "process 1006 to Processing first");
        CHECK(s.createTrip("Lahore", "Karachi", {"Faisalabad"}, {1006}, err, tripId),
              "assigning a Processing order succeeds");
        CHECK(tripId == 1, "trip 1 created");
        // Duplicate assignment to a second active trip is rejected.
        int tripId2 = 0;
        CHECK(!s.createTrip("Lahore", "Karachi", {"Faisalabad"}, {1006}, err, tripId2),
              "assigning 1006 to a second active trip is rejected");
        CHECK(s.trips.size() == 1, "still only 1 trip after duplicate attempt");
        CHECK(bothAgree(s, 1006, "Shipped"), "1006 still Shipped (first trip only)");
        // Matching now excludes the assigned order.
        Store::PlanResult pr = s.planRoute("Lahore", "Karachi", err);
        bool found1006 = false;
        for (const Order &o : pr.destinationOrders) if (o.id == 1006) found1006 = true;
        CHECK(!found1006, "assigned order 1006 no longer appears in matching");
        // Other invalid selections.
        CHECK(!s.createTrip("Lahore", "Karachi", {}, {}, err, tripId2),
              "empty selection rejected");
        CHECK(!s.createTrip("Lahore", "Karachi", {}, {9999}, err, tripId2),
              "missing order ID rejected");
        CHECK(!s.createTrip("Lahore", "Karachi", {}, {1006, 1006}, err, tripId2),
              "same order selected twice rejected");
        CHECK(!s.createTrip("Lahore", "Karachi", {}, {1001}, err, tripId2),
              "Delivered order 1001 cannot be assigned");
        CHECK(!s.createTrip("Lahore", "Karachi", {}, {1022}, err, tripId2),
              "order 1022 (Faisalabad) is not on the direct Lahore->Karachi route");
    }

    // -- Test 26: Trip completion ---------------------------------------
    {
        std::cout << "\n[26] Trip completion — explicit only\n";
        setupTestData();
        Store s = freshStore();
        s.routesPath = "test_routes_intermediate.csv";
        s.loadAll();
        std::string err;
        int tripId = 0;
        CHECK(s.createTrip("Lahore", "Islamabad", {}, {1005, 1021}, err, tripId),
              "create trip succeeds");
        // Passing through Rawalpindi did NOT deliver anything.
        CHECK(bothAgree(s, 1005, "Shipped") && bothAgree(s, 1021, "Shipped"),
              "after assignment (and passing through Rawalpindi), orders are Shipped, not Delivered");
        CHECK(s.completeTrip(tripId, err), "completeTrip succeeds");
        CHECK(bothAgree(s, 1005, "Delivered") && bothAgree(s, 1021, "Delivered"),
              "after explicit completion, orders are Delivered");
        DeliveryTrip *tr = s.findTrip(tripId);
        CHECK(tr && tr->status == "Completed", "trip status is Completed");
        CHECK(s.logTree.searchLogsByActionType("DELIVERY_COMPLETED").size() == 2,
              "2 DELIVERY_COMPLETED log entries");
        CHECK(!s.completeTrip(tripId, err), "completing an already-Completed trip fails");
        CHECK(!s.completeTrip(9999, err), "completing a missing trip fails");
        // After completion, the orders are free again (trip no longer active).
        CHECK(!s.isOrderAssignedToActiveTrip(1005), "completed trip no longer blocks its orders");
    }

    // -- Test 27: Trip persistence after restart ------------------------
    {
        std::cout << "\n[27] Trip persistence after restart\n";
        setupTestData();
        std::string err;
        {
            Store s = freshStore();
            s.routesPath = "test_routes_intermediate.csv";
            s.loadAll();
            int tripId = 0;
            CHECK(s.createTrip("Lahore", "Islamabad", {}, {1005}, err, tripId),
                  "create trip in session 1");
        }
        {
            Store s2 = freshStore();
            s2.routesPath = "test_routes_intermediate.csv";
            s2.loadAll();
            CHECK(s2.trips.size() == 1, "after restart: 1 trip reloaded");
            DeliveryTrip *tr = s2.findTrip(1);
            CHECK(tr && tr->status == "Planned" && tr->totalTimeMin == 275,
                  "after restart: trip 1 Planned, 275 min");
            CHECK(bothAgree(s2, 1005, "Shipped"), "after restart: 1005 still Shipped");
            CHECK(s2.nextTripId == 2, "after restart: nextTripId resumes at 2");
            CHECK(s2.completeTrip(1, err), "complete trip after restart succeeds");
            CHECK(bothAgree(s2, 1005, "Delivered"), "after restart + completion: 1005 Delivered");
        }
        {
            Store s3 = freshStore();
            s3.routesPath = "test_routes_intermediate.csv";
            s3.loadAll();
            CHECK(s3.findTrip(1) && s3.findTrip(1)->status == "Completed",
                  "after second restart: trip 1 still Completed");
        }
    }

    // -- Test 28: Multi-file rollback on trip failure -------------------
    {
        std::cout << "\n[28] Multi-file rollback on trip failure\n";
        setupTestData();
        Store s = freshStore();
        s.routesPath = "test_routes_intermediate.csv";
        s.loadAll();
        std::string err;
        int tripId = 0;
        // (a) Trips file cannot be saved.
        s.tripsPath = testDir + "/no_such_dir/delivery_trips.csv";
        CHECK(!s.createTrip("Lahore", "Islamabad", {}, {1005}, err, tripId),
              "createTrip fails when delivery_trips.csv cannot be saved");
        CHECK(bothAgree(s, 1005, "Processing"), "after trips-save failure: 1005 still Processing");
        CHECK(s.trips.empty(), "after trips-save failure: no trip in memory");
        CHECK(s.logTree.searchLogsByActionType("DELIVERY_ASSIGNMENT").empty(),
              "after trips-save failure: no assignment log");
        // (b) Log file cannot be written.
        s.tripsPath = testDir + "/delivery_trips.csv";
        s.logsPath = testDir + "/no_such_dir/activity_logs.csv";
        CHECK(!s.createTrip("Lahore", "Islamabad", {}, {1005}, err, tripId),
              "createTrip fails when the log cannot be written");
        CHECK(bothAgree(s, 1005, "Processing"), "after log failure: 1005 still Processing");
        CHECK(s.trips.empty(), "after log failure: no trip in memory");
        // (c) Orders file cannot be saved.
        s.logsPath = testDir + "/activity_logs.csv";
        s.ordersPath = testDir + "/no_such_dir/orders.csv";
        CHECK(!s.createTrip("Lahore", "Islamabad", {}, {1005}, err, tripId),
              "createTrip fails when orders.csv cannot be saved");
        CHECK(bothAgree(s, 1005, "Processing"), "after orders-save failure: 1005 still Processing");
        CHECK(s.trips.empty(), "after orders-save failure: no trip in memory");
        // Files on disk are unchanged.
        Store s2 = freshStore();
        s2.routesPath = "test_routes_intermediate.csv";
        s2.loadAll();
        CHECK(bothAgree(s2, 1005, "Processing"), "after reload: 1005 still Processing on disk");
        CHECK(s2.trips.empty(), "after reload: no trip on disk");
    }

    // -- Test 29: Original datasets + test dataset separation -----------
    {
        std::cout << "\n[29] Original datasets preserved\n";
        setupTestData();
        CHECK(countLines("orders.csv") == 31, "orders.csv still 30 orders + header");
        CHECK(countLines("delivery_routes.csv") == 13, "delivery_routes.csv still 12 routes + header");
        CHECK(countLines("test_routes_intermediate.csv") == 12,
              "test_routes_intermediate.csv has 11 routes + header (Lahore-Islamabad removed)");
        CHECK(!fs::exists("delivery_trips.csv"), "no delivery_trips.csv in the project folder (tests use test_work/)");
        CHECK(!fs::exists("activity_logs.csv"), "no activity_logs.csv in the project folder (tests use test_work/)");
    }


    // -- Test 30: Trip cancellation ------------------------------------
    {
        std::cout << "\n[30] Trip cancellation\n";
        setupTestData();
        Store s = freshStore();
        s.routesPath = "test_routes_intermediate.csv";
        s.loadAll();
        std::string err;
        int tripId = 0;
        CHECK(s.createTrip("Lahore", "Islamabad", {}, {1005, 1021}, err, tripId),
              "create trip for cancellation test");
        CHECK(s.cancelTrip(tripId, err), "cancel a Planned trip succeeds");
        DeliveryTrip *tr = s.findTrip(tripId);
        CHECK(tr && tr->status == "Cancelled", "trip status is Cancelled");
        CHECK(bothAgree(s, 1005, "Processing") && bothAgree(s, 1021, "Processing"),
              "cancelled trip releases orders back to Processing (not Shipped, not Pending)");
        CHECK(!s.isOrderAssignedToActiveTrip(1005), "released order is no longer assigned to an active trip");
        CHECK(s.logTree.searchLogsByActionType("TRIP_CANCELLED").size() == 3,
              "TRIP_CANCELLED logged once for the trip + once per order (3 total)");
        // Released orders can be assigned to a NEW trip.
        int tripId2 = 0;
        CHECK(s.createTrip("Lahore", "Islamabad", {}, {1005}, err, tripId2),
              "released order can be assigned to a new trip");
        CHECK(tripId2 == 2, "new trip gets the next ID (2)");
        CHECK(bothAgree(s, 1005, "Shipped"), "re-assigned order is Shipped again");
        // Cancelling twice / cancelling a Completed trip fails.
        CHECK(!s.cancelTrip(tripId, err), "cancelling an already-Cancelled trip fails");
        CHECK(s.completeTrip(tripId2, err), "complete trip 2 succeeds");
        CHECK(!s.cancelTrip(tripId2, err), "cancelling a Completed trip fails");
        CHECK(!s.cancelTrip(9999, err), "cancelling a missing trip fails");
        // Cancelled trip stays Cancelled in the file after restart.
        {
            Store s2 = freshStore();
            s2.routesPath = "test_routes_intermediate.csv";
            s2.loadAll();
            CHECK(s2.findTrip(1) && s2.findTrip(1)->status == "Cancelled",
                  "after restart: trip 1 still Cancelled");
            CHECK(s2.findTrip(2) && s2.findTrip(2)->status == "Completed",
                  "after restart: trip 2 still Completed");
        }
    }

    // -- Test 31: Recovery report --------------------------------------
    {
        std::cout << "\n[31] Recovery report\n";
        setupTestData();
        Store s = freshStore();
        s.routesPath = "test_routes_intermediate.csv";
        s.loadAll();
        Store::RecoveryReport rec = s.checkRecovery();
        CHECK(rec.warnings.empty() && rec.leftoverTmpFiles == 0,
              "clean load: no recovery warnings");
        // (a) Leftover .tmp file is reported, and the original is untouched.
        {
            std::ofstream tmp(testDir + "/orders.csv.tmp");
            tmp << "this is a stale temp file\n";
        }
        rec = s.checkRecovery();
        CHECK(rec.leftoverTmpFiles == 1, "leftover orders.csv.tmp is reported");
        CHECK(!rec.warnings.empty(), "leftover .tmp produces a warning");
        // (b) Corrupt log/trip lines are counted.
        {
            std::ofstream f(testDir + "/activity_logs.csv", std::ios::app);
            f << "garbage line\n";
        }
        {
            std::ofstream f(testDir + "/delivery_trips.csv");
            f << "trip_id,source,destination,route,assigned_order_ids,time_min,distance_km,status\n";
            f << "not a trip line\n";
            f << "1,Lahore,Islamabad,Lahore>Rawalpindi>Islamabad,1005,275,315,Planned\n";
        }
        Store s2 = freshStore();
        s2.routesPath = "test_routes_intermediate.csv";
        s2.loadAll();
        rec = s2.checkRecovery();
        CHECK(rec.corruptLogLines == 1, "1 corrupt log line counted");
        CHECK(rec.corruptTripLines == 1, "1 corrupt trip line counted");
        CHECK(s2.trips.size() == 1, "good trip line still loads");
        // (c) Trip/order inconsistency is reported, not silently fixed.
        // Trip 1 is Planned but order 1005 is Processing (not Shipped).
        bool warned = false;
        for (const std::string &w : rec.warnings)
            if (w.find("1005") != std::string::npos) warned = true;
        CHECK(warned, "Planned trip with a non-Shipped order is reported as a warning");
        CHECK(s2.findTrip(1) && s2.findTrip(1)->status == "Planned",
              "inconsistent trip is reported, not auto-fixed");
        // (d) Duplicate assignment across active trips is reported.
        {
            std::ofstream f(testDir + "/delivery_trips.csv", std::ios::app);
            f << "2,Lahore,Islamabad,Lahore>Rawalpindi>Islamabad,1005,275,315,Planned\n";
        }
        Store s3 = freshStore();
        s3.routesPath = "test_routes_intermediate.csv";
        s3.loadAll();
        rec = s3.checkRecovery();
        bool dupWarned = false;
        for (const std::string &w : rec.warnings)
            if (w.find("more than one active trip") != std::string::npos) dupWarned = true;
        CHECK(dupWarned, "order assigned to two active trips is reported");
    }

    // -- Test 32: Cancellation rollback on file failure ----------------
    {
        std::cout << "\n[32] Cancellation rollback on file failure\n";
        setupTestData();
        Store s = freshStore();
        s.routesPath = "test_routes_intermediate.csv";
        s.loadAll();
        std::string err;
        int tripId = 0;
        CHECK(s.createTrip("Lahore", "Islamabad", {}, {1005}, err, tripId),
              "create trip for rollback test");
        s.tripsPath = testDir + "/no_such_dir/delivery_trips.csv";
        CHECK(!s.cancelTrip(tripId, err), "cancel fails when delivery_trips.csv cannot be saved");
        CHECK(bothAgree(s, 1005, "Shipped"), "after failed cancel: 1005 still Shipped");
        CHECK(s.findTrip(tripId) && s.findTrip(tripId)->status == "Planned",
              "after failed cancel: trip still Planned");
        s.tripsPath = testDir + "/delivery_trips.csv";
        s.logsPath = testDir + "/no_such_dir/activity_logs.csv";
        CHECK(!s.cancelTrip(tripId, err), "cancel fails when the log cannot be written");
        CHECK(bothAgree(s, 1005, "Shipped"), "after log failure: 1005 still Shipped");
        CHECK(s.findTrip(tripId) && s.findTrip(tripId)->status == "Planned",
              "after log failure: trip still Planned");
        // With good paths, the same cancel succeeds.
        s.logsPath = testDir + "/activity_logs.csv";
        CHECK(s.cancelTrip(tripId, err), "cancel succeeds with good paths");
        CHECK(bothAgree(s, 1005, "Processing"), "after successful cancel: 1005 Processing");
    }

    // -- Test 33: Test-graph demo mode (Phase 4B-3) ---------------------
    {
        std::cout << "\n[33] Test-graph demo mode\n";
        setupTestData();
        // Put the test routes file next to the production routes file.
        {
            std::error_code ec;
            fs::copy_file("test_routes_intermediate.csv",
                          testDir + "/test_routes_intermediate.csv", ec);
            CHECK(!ec, "test routes file copied into test_work");
        }
        Store s = freshStore();
        CHECK(s.loadAll(), "loadAll succeeds with test file present");
        CHECK(s.testGraphAvailable(), "test graph loaded by loadAll");
        const DeliveryGraph *tg = s.graphForDataset("test");
        CHECK(tg != nullptr, "graphForDataset('test') returns the test graph");
        CHECK(s.graphForDataset("production") == &s.graph,
              "graphForDataset('production') returns the production graph");
        CHECK(s.graphForDataset("bogus") == nullptr,
              "graphForDataset('bogus') returns nullptr");
        if (tg) {
            RouteResult r = tg->shortestRoute("Lahore", "Islamabad");
            CHECK(r.ok && r.totalTimeMin == 275 && r.totalDistanceKm == 315,
                  "test graph: Lahore->Islamabad 275 min / 315 km");
            CHECK(r.path.size() == 3 && r.path[1] == "Rawalpindi",
                  "test graph: route goes via Rawalpindi");
        }
        // Planning through the test graph uses it for route AND matching.
        std::string err;
        Store::PlanResult pr = s.planRouteOn("Lahore", "Islamabad", err, *tg);
        CHECK(pr.route.ok && pr.route.totalTimeMin == 275,
              "planRouteOn(test): Lahore->Islamabad 275 min");
        bool serves1005 = false, serves1021 = false;
        for (const IntermediateMatch &m : pr.intermediate) {
            if (m.city != "Rawalpindi") continue;
            for (const Order &o : m.orders) {
                if (o.id == 1005) serves1005 = true;
                if (o.id == 1021) serves1021 = true;
            }
        }
        CHECK(serves1005 && serves1021,
              "planRouteOn(test): intermediate Rawalpindi serves 1005 + 1021");
        // Production planning is unchanged by the test graph existing.
        Store::PlanResult pp = s.planRoute("Lahore", "Islamabad", err);
        CHECK(pp.route.ok && pp.route.totalTimeMin == 260 && pp.intermediate.empty(),
              "production plan still direct 260 min, no intermediates");
        // No test file -> demo mode unavailable, production unaffected.
        {
            std::error_code ec;
            fs::remove(testDir + "/test_routes_intermediate.csv", ec);
            Store s2 = freshStore();
            CHECK(s2.loadAll(), "loadAll succeeds with no test file");
            CHECK(!s2.testGraphAvailable(), "no test file: test graph unavailable");
            CHECK(s2.graphForDataset("test") == nullptr,
                  "no test file: graphForDataset('test') is nullptr");
        }
    }

    // -- Test 34: Tree shape — log BST + order BST (Phase 4B-4) --------
    {
        std::cout << "\n[34] Tree shape (log BST + order BST)\n";
        setupTestData();
        Store s = freshStore();
        CHECK(s.loadAll(), "loadAll succeeds");
        // Log tree starts empty on a fresh copy (no activity_logs.csv).
        CHECK(s.logTree.size == 0, "log tree starts empty");
        CHECK(logTreeHeight(s.logTree.root) == 0, "empty log tree height is 0");
        // Order BST: balanced load, inorder is the sorted IDs.
        CHECK(s.orderTree.root != nullptr, "order tree root exists");
        std::vector<int> ids;
        orderInorderIds(s.orderTree.root, ids);
        bool sorted = ids.size() == 30;
        for (size_t i = 0; i < ids.size(); i++)
            if (ids[i] != 1001 + (int)i) sorted = false;
        CHECK(sorted, "order tree inorder is 1001..1030 sorted");
        CHECK(orderTreeHeight(s.orderTree.root) >= 1, "order tree height >= 1");
        std::cout << "  (order tree root id " << s.orderTree.root->data.id
                  << ", height " << orderTreeHeight(s.orderTree.root) << ")\n";
        // Five logged actions, one after another.
        std::string err;
        Order o{1031, "Test User", "Test Product", "Electronics", 1000, 1,
                "2026-10-16", "Lahore", "Pending"};
        CHECK(s.addOrder(o, err), "add 1031 succeeds");
        CHECK(s.changeStatus(1012, "Processing", err), "change 1012 to Processing succeeds");
        Order out;
        CHECK(s.processNext(out, err) && out.id == 1006, "processNext returns 1006");
        CHECK(s.undo(err), "undo succeeds");
        CHECK(s.cancelOrder(1006, err), "cancel 1006 succeeds");
        CHECK(s.logTree.size == 5, "5 log entries after 5 actions");
        // (a) inorder logIds are ascending / chronological.
        std::vector<int> logIds;
        logInorderIds(s.logTree.root, logIds);
        bool ascending = logIds.size() == 5;
        for (size_t i = 1; i < logIds.size(); i++)
            if (logIds[i] <= logIds[i - 1]) ascending = false;
        CHECK(ascending, "log tree inorder logIds are ascending");
        CHECK(logIds.size() == 5 && logIds[0] == 1 && logIds[4] == 5,
              "log tree inorder logIds are 1..5");
        // (b) height == size: timestamps arrive in non-decreasing
        // order and logIds increase, so every new entry compares
        // greater than every existing node and is inserted as the
        // rightmost leaf — the tree is a right-leaning chain, not
        // a balanced tree. (See ActivityLog.h: the tree is NOT
        // self-balancing.)
        CHECK(logTreeHeight(s.logTree.root) == s.logTree.size,
              "log tree height == size (right-leaning chain, timestamps increase)");
        CHECK(logTreeHeight(s.logTree.root) == 5, "log tree height is 5");
    }

    // -- Test 35: audit=false planning + error wording (Phase 4B-5) --------
    {
        std::cout << "\n[35] audit=false planning + error wording\n";
        setupTestData();
        Store s = freshStore();
        CHECK(s.loadAll(), "loadAll succeeds");
        CHECK(s.logTree.size == 0, "log tree starts empty");
        std::string err;
        // Audited (default) plan appends one ROUTE_GENERATED entry.
        Store::PlanResult pr = s.planRoute("Lahore", "Karachi", err);
        CHECK(pr.route.ok && pr.route.totalTimeMin == 900, "audited plan Lahore-Karachi 900 min");
        CHECK(s.logTree.size == 1, "audited plan appends 1 log entry");
        // audit=false: same route, no log entry, no error.
        err.clear();
        Store::PlanResult pr2 = s.planRoute("Lahore", "Karachi", err, false);
        CHECK(pr2.route.ok && pr2.route.totalTimeMin == 900, "unaudited plan same route 900 min");
        CHECK(s.logTree.size == 1, "unaudited plan appends no log entry");
        CHECK(err.empty(), "unaudited plan sets no log_warning error");
        // Detour + multi-stop, both modes.
        DetourResult d1 = s.planDetour("Lahore", "Karachi", "Faisalabad", err);
        CHECK(d1.ok && d1.extraTimeMin == 30, "audited detour extra +30 min");
        CHECK(s.logTree.size == 2, "audited detour appends 1 log entry");
        DetourResult d2 = s.planDetour("Lahore", "Karachi", "Faisalabad", err, false);
        CHECK(d2.ok && d2.extraTimeMin == 30, "unaudited detour same result");
        CHECK(s.logTree.size == 2, "unaudited detour appends no log entry");
        MultiStopResult m1 = s.planMultiStop("Lahore", "Karachi", {"Faisalabad", "Multan"}, err);
        CHECK(m1.ok && m1.totalTimeMin == 960, "audited multi-stop 960 min");
        CHECK(s.logTree.size == 3, "audited multi-stop appends 1 log entry");
        MultiStopResult m2 = s.planMultiStop("Lahore", "Karachi", {"Faisalabad", "Multan"}, err, false);
        CHECK(m2.ok && m2.totalTimeMin == 960, "unaudited multi-stop same result");
        CHECK(s.logTree.size == 3, "unaudited multi-stop appends no log entry");
        // Error wording: no console-menu text in the createTrip message.
        std::string cerr;
        int tripId = 0;
        CHECK(!s.createTrip("Lahore", "Karachi", {}, {1006}, cerr, tripId),
              "assigning Pending order 1006 is rejected");
        CHECK(cerr.find("option") == std::string::npos,
              "createTrip error has no console-menu wording");
        CHECK(cerr == "Order 1006 is Pending, not Processing. Process it first, then assign it to a trip.",
              "createTrip error wording is the new message");
    }

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
    if (failed == 0) std::cout << "ALL TESTS PASSED.\n";
    else std::cout << "SOME TESTS FAILED — see FAIL lines above.\n";
    return failed == 0 ? 0 : 1;
}
