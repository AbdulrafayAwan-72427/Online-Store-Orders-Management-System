// Store.cpp — Phase 3a (NEW file), Phase 3b logging + rollback added
//
// Implementation of Store (see Store.h for the design + queue invariant).
// The DSA classes themselves are untouched here; this file only
// coordinates them, validates, and persists.
//
// Phase 3b transaction rule (see commitWithLog):
//   order change + log entry are ONE unit. Save orders.csv first;
//   if that fails, roll the order back in memory and write NO log.
//   Then append the log; if that fails, roll the order back AND
//   re-save orders.csv, and write NO log entry into the tree.
//   Either both files agree, or the mutation is reported as failed
//   and nothing changed. There is no state where the order changed
//   but the log is missing, or vice versa.

#include "Store.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

// Forward declaration (defined just before saveAtomic, used by saveTripsAtomic too).
static bool replaceFileAtomic(const std::string &tmpName, const std::string &filename);

// ------------------------------------------------------------------
// Destructor / clearing
// ------------------------------------------------------------------
static void deleteTree(TreeNode *node) {
    if (!node) return;
    deleteTree(node->left);
    deleteTree(node->right);
    delete node;
}

Store::~Store() { clearAll(); }

void Store::clearOrdersOnly() {
    // Free List / BST / Queue only. Used by rollback (restoreSnapshot)
    // and by loadOrders. Does NOT touch the undo stack, the graph,
    // or the log tree — a rollback must not erase undo history or
    // audit history.
    ListNode *cur = orderList.head;
    while (cur) { ListNode *tmp = cur; cur = cur->next; delete tmp; }
    orderList.head = orderList.tail = nullptr;
    orderList.size = 0;
    deleteTree(orderTree.root);
    orderTree.root = nullptr;
    while (!processQueue.isEmpty()) processQueue.dequeue();
}

void Store::clearAll() {
    clearOrdersOnly();
    // Stack
    Action a;
    while (undoStack.pop(a)) { /* pop frees the node */ }
    undoCount = 0;
    ordersLoaded = false;
    // NOTE: graph is reset by loadRoutes() (graph = DeliveryGraph()).
    // NOTE: logTree is NOT cleared here — logs survive an orders reload.
}

// ------------------------------------------------------------------
// Loading
// ------------------------------------------------------------------
void Store::insertBalancedBST(std::vector<Order> &v, int lo, int hi) {
    // Same as v1: middle-first, so sorted IDs 1001-1030 do not make
    // a degenerate (straight-line) BST. Search stays O(log n) on load.
    if (lo > hi) return;
    int mid = (lo + hi) / 2;
    orderTree.insert(v[mid]);
    insertBalancedBST(v, lo, mid - 1);
    insertBalancedBST(v, mid + 1, hi);
}

bool Store::loadOrders(const std::string &filename) {
    std::ifstream file(filename);
    if (!file) return false;
    ordersPath = filename;
    clearAll();
    std::string line;
    std::getline(file, line); // skip header
    std::vector<Order> all;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        Order o;
        if (!parseOrder(line, o)) continue;
        // Duplicate ID in the file: keep the first, skip the rest.
        int steps = 0;
        bool duplicate = false;
        for (const Order &seen : all)
            if (seen.id == o.id) { duplicate = true; break; }
        if (duplicate) continue;
        (void)steps;
        all.push_back(o);
        orderList.insertAtEnd(o);
        // QUEUE INVARIANT (Phase 3a): Pending orders only.
        // v1 also enqueued Processing orders (11 in queue on load);
        // that caused duplicate processing of orders already in progress.
        if (o.status == "Pending") processQueue.enqueue(o.id);
    }
    insertBalancedBST(all, 0, (int)all.size() - 1);
    ordersLoaded = true;
    return true;
}

// Shared routes-file reader: fills the given graph, leaves the
// caller's paths alone. Used by loadRoutes (production) and
// loadTestRoutes (Phase 4B-3 demo graph).
static bool loadRoutesInto(const std::string &filename, DeliveryGraph &g) {
    std::ifstream file(filename);
    if (!file) return false;
    std::string line;
    std::getline(file, line); // skip header
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::vector<std::string> p = splitCSV(line);
        if (p.size() < 4) continue;
        try {
            g.addRoute(p[0], p[1], std::stoi(p[3]), std::stoi(p[2])); // time (col 4), distance_km (col 3)
        } catch (...) { continue; }
    }
    return true;
}

bool Store::loadRoutes(const std::string &filename) {
    routesPath = filename;
    graph = DeliveryGraph(); // reset: fresh matrix, 0 cities
    if (!loadRoutesInto(filename, graph)) return false;
    routesLoaded = true;
    return true;
}

// Phase 4B-3 — test-dataset demo graph. The file lives next to the
// production routes file; if it is missing, demo mode is simply
// unavailable (false, no error) and production is unaffected.
bool Store::loadTestRoutes() {
    std::string dir;
    size_t slash = routesPath.find_last_of("/\\");
    if (slash != std::string::npos) dir = routesPath.substr(0, slash + 1);
    testRoutesPath = dir + "test_routes_intermediate.csv";
    testGraph = DeliveryGraph();
    testGraphLoaded = false;
    if (!loadRoutesInto(testRoutesPath, testGraph)) return false;
    if (testGraph.n == 0) return false;
    testGraphLoaded = true;
    return true;
}

const DeliveryGraph *Store::graphForDataset(const std::string &dataset) const {
    if (dataset == "production") return &graph;
    if (dataset == "test") return testGraphLoaded ? &testGraph : nullptr;
    return nullptr;
}

bool Store::loadAll() {
    if (!loadRoutes(routesPath)) return false;
    // Best-effort: a missing test file only disables demo mode.
    loadTestRoutes();
    if (!loadOrders(ordersPath)) return false;
    // Logs are best-effort: a missing file is a first run, not an error.
    loadLogs(logsPath);
    loadTrips(tripsPath);
    return true;
}

// ------------------------------------------------------------------
// Queries
// ------------------------------------------------------------------
Order *Store::findOrder(int id, int &steps) {
    return orderTree.searchById(id, steps);
}
Order *Store::findOrder(int id) {
    int steps = 0;
    return findOrder(id, steps);
}

Report Store::report() {
    Report r;
    r.total = orderList.size;
    r.pending = orderList.countStatus("Pending");
    r.processing = orderList.countStatus("Processing");
    r.shipped = orderList.countStatus("Shipped");
    r.delivered = orderList.countStatus("Delivered");
    r.cancelled = orderList.countStatus("Cancelled");
    r.totalSales = orderList.totalSales();
    r.queueSize = processQueue.size;
    r.logSize = logTree.size;
    r.tripCount = (int)trips.size();
    r.activeTripCount = 0;
    for (const DeliveryTrip &tr : trips)
        if (tr.status == "Planned" || tr.status == "InProgress") r.activeTripCount++;
    // undoStack has no size field in v1; count by walking is not
    // possible without popping, so report it via a stored counter:
    // (kept simple — see undoStackSize below in processNext/undo).
    r.undoSize = undoCount;
    return r;
}

void Store::compareSearch() {
    // v1 code, moved here unchanged in behaviour.
    if (!ordersLoaded) {
        std::cout << "Load orders first.\n";
        return;
    }
    int testIds[] = {1001, 1010, 1024, 1030, 1017};
    const int REPEAT = 200; // 5 IDs x 200 = 1000 searches
    long listSteps = 0, treeSteps = 0;
    auto start = std::chrono::high_resolution_clock::now();
    for (int r = 0; r < REPEAT; r++)
        for (int id : testIds) { int s; orderList.searchById(id, s); listSteps += s; }
    auto mid = std::chrono::high_resolution_clock::now();
    for (int r = 0; r < REPEAT; r++)
        for (int id : testIds) { int s; orderTree.searchById(id, s); treeSteps += s; }
    auto end = std::chrono::high_resolution_clock::now();
    long listUs = std::chrono::duration_cast<std::chrono::microseconds>(mid - start).count();
    long treeUs = std::chrono::duration_cast<std::chrono::microseconds>(end - mid).count();
    std::cout << "\n--- Search Test: 1000 searches (IDs 1001, 1010, 1024, 1030, 1017) ---\n";
    std::cout << "Linked List: total steps = " << listSteps << ", avg steps/search = " << (listSteps / 1000.0)
              << ", time = " << listUs << " microseconds\n";
    std::cout << "BST:         total steps = " << treeSteps << ", avg steps/search = " << (treeSteps / 1000.0)
              << ", time = " << treeUs << " microseconds\n";
    std::cout << "Result: BST checked far fewer nodes. List = O(n), BST = O(log n).\n";
    std::cout << "Example - search 1030: List checks 30 nodes, BST checks about 5.\n";
}

// ------------------------------------------------------------------
// Validation
// ------------------------------------------------------------------
bool Store::isValidStatus(const std::string &s) {
    return s == "Pending" || s == "Processing" || s == "Shipped" ||
           s == "Delivered" || s == "Cancelled";
}

bool Store::isValidTransition(const std::string &from, const std::string &to) {
    if (from == to) return false;
    if (from == "Pending") return to == "Processing" || to == "Cancelled";
    if (from == "Processing") return to == "Shipped" || to == "Cancelled";
    if (from == "Shipped") return to == "Delivered";
    return false; // Delivered / Cancelled are terminal
}

// ------------------------------------------------------------------
// Internal helpers
// ------------------------------------------------------------------
bool Store::updateStores(int id, const std::string &newStatus) {
    // Update BOTH copies of the order (List and BST hold separate
    // Order objects). If they ever disagree, that is a bug — report it.
    bool inList = orderList.updateStatus(id, newStatus);
    bool inTree = orderTree.updateStatus(id, newStatus);
    return inList && inTree;
}

void Store::syncQueue(int id, const std::string &status, bool wasDequeued,
                      bool wasInQueue, int queuePosition) {
    if (status == "Pending") {
        if (processQueue.contains(id)) return;
        // Phase 3b: restore the ORIGINAL position, not just front/rear.
        // wasDequeued (processNext) means position 0, i.e. the front.
        if (wasDequeued) processQueue.enqueueFront(id);
        else if (wasInQueue) processQueue.insertAtPosition(id, queuePosition);
        // If it was never in the queue and was not dequeued, do NOT
        // enqueue it here: changeStatus() to Pending is the only path
        // that should add a Pending order, and it passes wasInQueue
        // from before the change. (addOrder() enqueues directly.)
    } else {
        processQueue.removeById(id);
    }
}

// ------------------------------------------------------------------
// Mutations
// ------------------------------------------------------------------
// Transaction / rollback (Phase 3b, requirement 1):
//   Every mutation below follows the same pattern:
//     1. Validate (no change yet).
//     2. Take a snapshot (orders in List order + queue IDs).
//     3. Apply the in-memory change (List + BST + Queue + Stack).
//     4. commitWithLog(snapshot, logEntry):
//          a. Save orders.csv atomically. On failure: restore the
//             snapshot in memory, pop the undo action if one was
//             pushed, write NO log, return false. The CSV on disk
//             was never changed (atomic save), so in-memory now
//             matches the disk again.
//          b. Append the log line to activity_logs.csv, then insert
//             it into logTree. On failure: restore the snapshot,
//             pop/push the undo action back as appropriate, RE-SAVE
//             orders.csv (it currently holds the mutated orders),
//             write NO log entry into the tree, return false.
//   If BOTH steps fail, the error message says so explicitly —
//   that state is reported, not hidden. On the next restart the
//   CSV on disk is the source of truth, so a double failure cannot
//   silently invent an order change.
//   The log file itself is append-only in normal operation; the
//   only rewrite of activity_logs.csv would be authorised
//   maintenance via deleteLogById(), which Store never calls.

OrderSnapshot Store::takeSnapshot() const {
    OrderSnapshot snap;
    ListNode *cur = orderList.head;
    while (cur) { snap.orders.push_back(cur->data); cur = cur->next; }
    QueueNode *q = processQueue.front;
    while (q) { snap.queueIds.push_back(q->orderId); q = q->next; }
    return snap;
}

void Store::restoreSnapshot(const OrderSnapshot &snap) {
    // Restore orders + queue from the snapshot. Does NOT touch the
    // undo stack or the log tree — callers fix the stack themselves
    // (pop a pushed action, or push back a popped one).
    clearOrdersOnly();
    std::vector<Order> v = snap.orders;
    for (const Order &o : v) orderList.insertAtEnd(o);
    if (!v.empty()) insertBalancedBST(v, 0, (int)v.size() - 1);
    for (int id : snap.queueIds) processQueue.enqueue(id);
    ordersLoaded = !v.empty();
}

bool Store::commitWithLog(const OrderSnapshot &snap, ActivityLogEntry &entry,
                          std::string &error) {
    // Step (a): orders.csv
    if (!persist()) {
        restoreSnapshot(snap);
        error = "Could not save to " + ordersPath +
                " — the change was rolled back and no log entry was written.";
        return false;
    }
    // Step (b): activity log
    if (!appendLogEntry(entry)) {
        restoreSnapshot(snap);
        // orders.csv currently holds the MUTATED orders; write the
        // rolled-back orders back over it.
        if (!persist()) {
            error = "CRITICAL: the activity log could not be written AND "
                    "the orders file could not be restored. In-memory "
                    "state matches the pre-change snapshot, but " +
                    ordersPath + " still holds the unlogged change. "
                    "Restart and check both files before continuing.";
            return false;
        }
        error = "Could not write to " + logsPath +
                " — the change was rolled back and no log entry was written.";
        return false;
    }
    return true;
}

bool Store::addOrder(const Order &o, std::string &error) {
    if (o.id <= 0) { error = "Order ID must be a positive number."; return false; }
    if (o.customer.empty() || o.product.empty() || o.category.empty() ||
        o.city.empty() || o.date.empty()) {
        error = "Customer, product, category, city and date must not be empty.";
        return false;
    }
    if (o.price <= 0) { error = "Price must be greater than 0."; return false; }
    if (o.quantity < 1) { error = "Quantity must be 1 or more."; return false; }
    if (findOrder(o.id)) { error = "Order ID " + std::to_string(o.id) + " already exists."; return false; }
    OrderSnapshot snap = takeSnapshot();
    Order copy = o;
    copy.status = "Pending"; // new orders always start Pending
    orderList.insertAtEnd(copy);
    orderTree.insert(copy);
    processQueue.enqueue(copy.id);
    ordersLoaded = true;
    ActivityLogEntry entry;
    entry.actionType = "ORDER_CREATED";
    entry.orderId = copy.id;
    entry.oldStatus = "";
    entry.newStatus = "Pending";
    entry.description = "Order " + std::to_string(copy.id) + " created for " +
                        sanitizeDescription(copy.customer) + "; Pending";
    return commitWithLog(snap, entry, error);
}

bool Store::changeStatus(int id, const std::string &newStatus, std::string &error) {
    if (!isValidStatus(newStatus)) { error = "Unknown status: " + newStatus; return false; }
    Order *o = findOrder(id);
    if (!o) { error = "Order " + std::to_string(id) + " not found."; return false; }
    std::string oldStatus = o->status;
    if (oldStatus == newStatus) {
        error = "Order " + std::to_string(id) + " is already " + newStatus + ".";
        return false;
    }
    if (!isValidTransition(oldStatus, newStatus)) {
        error = "Invalid status change: " + oldStatus + " -> " + newStatus +
                ". Allowed: Pending->Processing/Cancelled, "
                "Processing->Shipped/Cancelled, Shipped->Delivered.";
        return false;
    }
    OrderSnapshot snap = takeSnapshot();
    bool wasInQueue = processQueue.contains(id);
    int queuePos = processQueue.indexOf(id); // Phase 3b: remember the exact place
    // Push undo BEFORE changing, so Undo can restore oldStatus.
    undoStack.push(Action{id, oldStatus, newStatus, wasInQueue, false, queuePos});
    undoCount++;
    if (!updateStores(id, newStatus)) {
        Action dummy;
        undoStack.pop(dummy); undoCount--;
        error = "Internal error: order found in BST but not in Linked List (or vice versa).";
        return false;
    }
    syncQueue(id, newStatus, false, wasInQueue, queuePos);
    ActivityLogEntry entry;
    entry.actionType = (newStatus == "Cancelled") ? "ORDER_CANCELLED" : "STATUS_UPDATED";
    entry.orderId = id;
    entry.oldStatus = oldStatus;
    entry.newStatus = newStatus;
    entry.description = "Order " + std::to_string(id) + " status " + oldStatus +
                        " to " + newStatus;
    if (!commitWithLog(snap, entry, error)) {
        // commitWithLog already restored the orders; undo the stack push.
        Action dummy;
        undoStack.pop(dummy); undoCount--;
        return false;
    }
    return true;
}

bool Store::cancelOrder(int id, std::string &error) {
    // Cancel is just a status change to Cancelled, so it is undoable
    // and goes through the same validation + queue fix + logging.
    return changeStatus(id, "Cancelled", error);
}

bool Store::deleteOrder(int id, std::string &error) {
    Order *o = findOrder(id);
    if (!o) { error = "Order " + std::to_string(id) + " not found."; return false; }
    std::string oldStatus = o->status;
    std::string customer = o->customer;
    OrderSnapshot snap = takeSnapshot();
    // Remove from the queue FIRST, so a deleted order can never be
    // processed afterwards (stale-queue bug, v1).
    processQueue.removeById(id);
    if (!orderList.deleteById(id)) {
        restoreSnapshot(snap);
        error = "Internal error: order found in BST but not in Linked List.";
        return false;
    }
    orderTree.deleteById(id);
    // Delete is permanent: no undo entry is pushed (as in v1).
    ActivityLogEntry entry;
    entry.actionType = "ORDER_DELETED";
    entry.orderId = id;
    entry.oldStatus = oldStatus;
    entry.newStatus = "";
    entry.description = "Order " + std::to_string(id) + " deleted; was " + oldStatus;
    (void)customer;
    return commitWithLog(snap, entry, error);
}

bool Store::processNext(Order &out, std::string &error) {
    // Skip (and drop) any front order that is no longer Pending.
    // Under the queue invariant this should not happen, but it makes
    // processNext safe even if an old queue was built by v1 code.
    // NOTE: dropping a stale front order is itself a queue fix, not
    // a status change — it is not logged as a separate action, and
    // it is not undoable. The dropped order's status does not change.
    while (!processQueue.isEmpty()) {
        int id = processQueue.peek();
        Order *o = findOrder(id);
        if (!o) { processQueue.dequeue(); continue; } // deleted order: drop it
        if (o->status != "Pending") { processQueue.dequeue(); continue; }
        // Found the next Pending order. Dequeue it, then mark Processing.
        OrderSnapshot snap = takeSnapshot();
        processQueue.dequeue();
        undoStack.push(Action{id, "Pending", "Processing", true, true, 0});
        undoCount++;
        if (!updateStores(id, "Processing")) {
            Action dummy;
            undoStack.pop(dummy); undoCount--;
            restoreSnapshot(snap);
            error = "Internal error updating order " + std::to_string(id) + ".";
            return false;
        }
        // Processing orders are not in the queue (invariant), and this
        // one was already dequeued, so no syncQueue call is needed.
        out = *findOrder(id);
        ActivityLogEntry entry;
        entry.actionType = "ORDER_PROCESSED";
        entry.orderId = id;
        entry.oldStatus = "Pending";
        entry.newStatus = "Processing";
        entry.description = "Order " + std::to_string(id) +
                            " processed from queue; Pending to Processing";
        if (!commitWithLog(snap, entry, error)) {
            Action dummy;
            undoStack.pop(dummy); undoCount--;
            return false;
        }
        return true;
    }
    error = "Queue is empty. No Pending order to process.";
    return false;
}

bool Store::undo(std::string &error) {
    Action a;
    if (!undoStack.pop(a)) {
        error = "Nothing to undo.";
        return false;
    }
    undoCount--;
    Order *o = findOrder(a.orderId);
    if (!o) {
        // The action is already popped; put it back so the user can
        // see it was not lost, and report the problem.
        undoStack.push(a); undoCount++;
        error = "Cannot undo: order " + std::to_string(a.orderId) +
                " no longer exists (it was deleted after the action).";
        return false;
    }
    OrderSnapshot snap = takeSnapshot();
    std::string currentStatus = o->status;
    // Restore the OLD status directly — do NOT run the forward
    // transition table here. Cancelled -> Pending is invalid forward,
    // but it is exactly what Undo of a cancel must do.
    if (!updateStores(a.orderId, a.oldStatus)) {
        undoStack.push(a); undoCount++;
        error = "Internal error restoring order " + std::to_string(a.orderId) + ".";
        return false;
    }
    // Restore the queue exactly (Phase 3b: original position):
    //  - Undo of processNext (wasDequeued, position 0): back to FRONT.
    //  - Undo of cancel/status change (wasInQueue): back to its
    //    ORIGINAL position (insertAtPosition, clamped if shrunk).
    //  - Otherwise (old status was not Pending): out of the queue.
    syncQueue(a.orderId, a.oldStatus, a.wasDequeued, a.wasInQueue, a.queuePosition);
    ActivityLogEntry entry;
    entry.actionType = "UNDO";
    entry.orderId = a.orderId;
    entry.oldStatus = currentStatus;
    entry.newStatus = a.oldStatus;
    entry.description = "Undo: order " + std::to_string(a.orderId) +
                        " restored from " + currentStatus + " to " + a.oldStatus;
    if (!commitWithLog(snap, entry, error)) {
        // commitWithLog restored the orders; push the action back so
        // the undo itself is not lost.
        undoStack.push(a); undoCount++;
        return false;
    }
    return true;
}

// ------------------------------------------------------------------
// Activity Log (Phase 3b)
// ------------------------------------------------------------------
std::string Store::nowIso() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
    return std::string(buf);
}

std::string Store::sanitizeDescription(const std::string &d) {
    // activity_logs.csv is comma-separated; a comma in a description
    // would break parsing. Replace commas with semicolons.
    std::string out = d;
    for (char &c : out)
        if (c == ',' || c == '\n' || c == '\r') c = ';';
    return out;
}

// File format, one line per entry:
//   log_id,timestamp,actor,action_type,order_id,old_status,new_status,description
// Description is the last field and may itself contain commas if a
// file was edited by hand — parseLogLine therefore joins any extra
// fields back into the description instead of rejecting the line.
bool Store::parseLogLine(const std::string &line, ActivityLogEntry &out) {
    std::vector<std::string> f = splitCSV(line);
    if (f.size() < 7) return false;
    try {
        out.logId = std::stoi(f[0]);
        out.timestamp = f[1];
        out.actor = f[2];
        out.actionType = f[3];
        out.orderId = std::stoi(f[4]);
        out.oldStatus = f[5];
        out.newStatus = f[6];
    } catch (...) { return false; }
    if (out.logId <= 0 || out.timestamp.empty() || out.actionType.empty())
        return false;
    out.description = "";
    for (size_t i = 7; i < f.size(); i++) {
        if (i > 7) out.description += ",";
        out.description += f[i];
    }
    return true;
}

bool Store::loadLogs(const std::string &filename) {
    logsPath = filename;
    logsSkipped = 0;
    // Rebuild the tree from scratch so a second load cannot duplicate entries.
    // ActivityLogBST has no clear() method; destroy and re-create it.
    logTree.~ActivityLogBST();
    new (&logTree) ActivityLogBST();
    std::ifstream file(filename);
    if (!file) {
        // Missing file = first run. Empty log, not an error.
        logsLoaded = false;
        nextLogId = 1;
        return true;
    }
    std::string line;
    std::getline(file, line); // skip header, if present
    // The header line starts with "log_id," — if the file has no
    // header, the first line will fail parseLogLine and be skipped,
    // which is safe. Detect the header properly:
    if (!line.empty() && line.rfind("log_id,", 0) != 0) {
        ActivityLogEntry e;
        if (parseLogLine(line, e)) {
            if (!logTree.insertLog(e)) logsSkipped++;
        } else logsSkipped++;
    }
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        ActivityLogEntry e;
        if (!parseLogLine(line, e)) { logsSkipped++; continue; }
        if (!logTree.insertLog(e)) logsSkipped++; // duplicate logId, etc.
    }
    nextLogId = logTree.maxLogId() + 1;
    logsLoaded = true;
    return true;
}

bool Store::appendLogEntry(ActivityLogEntry &entry) {
    // Assign the ID + timestamp here, so the file and the tree can
    // never disagree about them.
    entry.logId = nextLogId;
    entry.timestamp = nowIso();
    entry.actor = "admin";
    entry.description = sanitizeDescription(entry.description);
    // Append-only: open in append mode, write ONE line, flush, close.
    // If the open or the write fails, insert NOTHING into the tree.
    // A new (or empty) log file gets a header line first, so the
    // file always has the same format as orders.csv.
    bool needHeader = false;
    {
        std::error_code ec;
        if (!fs::exists(logsPath, ec) || fs::file_size(logsPath, ec) == 0)
            needHeader = true;
    }
    {
        std::ofstream out(logsPath, std::ios::app);
        if (!out) return false;
        if (needHeader)
            out << "log_id,timestamp,actor,action_type,order_id,old_status,new_status,description\n";
        out << entry.logId << "," << entry.timestamp << "," << entry.actor
            << "," << entry.actionType << "," << entry.orderId << ","
            << entry.oldStatus << "," << entry.newStatus << ","
            << entry.description << "\n";
        out.flush();
        if (!out.good()) { out.close(); return false; }
        out.close();
        if (!out.good()) return false;
    }
    if (!logTree.insertLog(entry)) return false; // should not happen (unique ID)
    nextLogId++;
    logsLoaded = true;
    return true;
}

// Phase 3c — batch append. One file open, all lines, one flush.
// If the open fails, NO lines are written and nothing is inserted
// into the tree (the common failure case in tests: a missing
// directory). If the write fails mid-batch, some lines may already
// be in the file — that is the documented append limitation; the
// caller rolls back the orders/trips, and the partial log lines
// describe an attempted transaction, not a completed one. The log
// is append-only, so they are not deleted.
bool Store::appendLogEntries(std::vector<ActivityLogEntry> &entries) {
    if (entries.empty()) return true;
    bool needHeader = false;
    {
        std::error_code ec;
        if (!fs::exists(logsPath, ec) || fs::file_size(logsPath, ec) == 0)
            needHeader = true;
    }
    {
        std::ofstream out(logsPath, std::ios::app);
        if (!out) return false;
        if (needHeader)
            out << "log_id,timestamp,actor,action_type,order_id,old_status,new_status,description\n";
        for (ActivityLogEntry &e : entries) {
            e.logId = nextLogId;
            e.timestamp = nowIso();
            e.actor = "admin";
            e.description = sanitizeDescription(e.description);
            out << e.logId << "," << e.timestamp << "," << e.actor
                << "," << e.actionType << "," << e.orderId << ","
                << e.oldStatus << "," << e.newStatus << ","
                << e.description << "\n";
            nextLogId++;
        }
        out.flush();
        if (!out.good()) { out.close(); return false; }
        out.close();
        if (!out.good()) return false;
    }
    for (const ActivityLogEntry &e : entries) {
        if (!logTree.insertLog(e)) return false; // should not happen
    }
    logsLoaded = true;
    return true;
}

// ------------------------------------------------------------------
// Delivery (Phase 3c)
// ------------------------------------------------------------------
// Multi-file transaction rule (requirement 10):
//   A trip touches THREE files: orders.csv (statuses), 
//   delivery_trips.csv (the trip record), activity_logs.csv (audit).
//   Plain CSVs have no true atomic multi-file transaction, so the
//   order is defined and the failure behaviour is tested:
//     1. orders.csv (atomic save). On failure: roll back orders
//        + trips in memory, write NO log, return false.
//     2. delivery_trips.csv (atomic save). On failure: roll back
//        orders + trips, RE-SAVE orders.csv, write NO log.
//     3. Log batch (one file open, all lines). On failure at the
//        open: roll back orders + trips, RE-SAVE both files, write
//        NO log entries into the tree. On a mid-batch write failure
//        (disk full), some log lines may already be in the file —
//        that is the documented limitation (see appendLogEntries);
//        the orders/trips are still rolled back, so the log lines
//        describe an attempted trip, not a completed one.
//   If the RE-SAVE in step 2/3 also fails, the error says so
//   explicitly (CRITICAL). On the next restart the CSVs on disk
//   are the source of truth.

std::string Store::joinRoute(const std::vector<std::string> &route) {
    std::string out;
    for (size_t i = 0; i < route.size(); i++) {
        if (i) out += ">";
        out += route[i];
    }
    return out;
}

std::string Store::joinIds(const std::vector<int> &ids) {
    std::string out;
    for (size_t i = 0; i < ids.size(); i++) {
        if (i) out += ";";
        out += std::to_string(ids[i]);
    }
    return out;
}

static std::vector<std::string> splitByChar(const std::string &s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

bool Store::parseTripLine(const std::string &line, DeliveryTrip &out) {
    std::vector<std::string> f = splitCSV(line);
    if (f.size() < 7) return false;
    try {
        out.tripId = std::stoi(f[0]);
        out.source = f[1];
        out.destination = f[2];
        out.route = splitByChar(f[3], '>');
        out.assignedOrderIds.clear();
        if (!f[4].empty()) {
            for (const std::string &s : splitByChar(f[4], ';'))
                out.assignedOrderIds.push_back(std::stoi(s));
        }
        out.totalTimeMin = std::stoi(f[5]);
        out.totalDistanceKm = std::stoi(f[6]);
        out.status = f.size() > 7 ? f[7] : "Planned";
    } catch (...) { return false; }
    if (out.tripId <= 0 || out.source.empty() || out.destination.empty() ||
        out.route.empty() || out.status.empty())
        return false;
    return true;
}

bool Store::loadTrips(const std::string &filename) {
    tripsPath = filename;
    tripsSkipped = 0;
    trips.clear();
    std::ifstream file(filename);
    if (!file) {
        tripsLoaded = false;
        nextTripId = 1;
        return true; // no trips yet, not an error
    }
    std::string line;
    std::getline(file, line); // header, if present
    if (!line.empty() && line.rfind("trip_id,", 0) != 0) {
        DeliveryTrip tr;
        if (parseTripLine(line, tr)) trips.push_back(tr);
        else tripsSkipped++;
    }
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        DeliveryTrip tr;
        if (!parseTripLine(line, tr)) { tripsSkipped++; continue; }
        bool dup = false;
        for (const DeliveryTrip &seen : trips)
            if (seen.tripId == tr.tripId) { dup = true; break; }
        if (dup) { tripsSkipped++; continue; }
        trips.push_back(tr);
    }
    int m = 0;
    for (const DeliveryTrip &tr : trips)
        if (tr.tripId > m) m = tr.tripId;
    nextTripId = m + 1;
    tripsLoaded = true;
    return true;
}

bool Store::saveTrips() { return saveTripsTo(tripsPath); }

bool Store::saveTripsTo(const std::string &filename) {
    return saveTripsAtomic(filename);
}

bool Store::saveTripsAtomic(const std::string &filename) {
    // Trips are rewritten whole (not appended), because a trip's
    // status changes on completion. Same atomic rule as orders:
    // write a complete .tmp file, then replace the original.
    std::string tmpName = filename + ".tmp";
    {
        std::ofstream out(tmpName, std::ios::trunc);
        if (!out) return false;
        out << "trip_id,source,destination,route,assigned_order_ids,time_min,distance_km,status\n";
        for (const DeliveryTrip &tr : trips) {
            out << tr.tripId << "," << tr.source << "," << tr.destination << ","
                << joinRoute(tr.route) << "," << joinIds(tr.assignedOrderIds) << ","
                << tr.totalTimeMin << "," << tr.totalDistanceKm << "," << tr.status << "\n";
        }
        out.flush();
        if (!out.good()) { out.close(); fs::remove(tmpName); return false; }
        out.close();
        if (!out.good()) { fs::remove(tmpName); return false; }
    }
    if (!replaceFileAtomic(tmpName, filename)) return false;
    return fs::exists(filename);
}

std::vector<int> Store::activeAssignedOrderIds() const {
    std::vector<int> ids;
    for (const DeliveryTrip &tr : trips) {
        if (tr.status != "Planned" && tr.status != "InProgress") continue;
        for (int id : tr.assignedOrderIds) ids.push_back(id);
    }
    return ids;
}

bool Store::isOrderAssignedToActiveTrip(int orderId) const {
    std::vector<int> ids = activeAssignedOrderIds();
    return std::find(ids.begin(), ids.end(), orderId) != ids.end();
}

DeliveryTrip *Store::findTrip(int tripId) {
    for (DeliveryTrip &tr : trips)
        if (tr.tripId == tripId) return &tr;
    return nullptr;
}

static std::vector<Order> allOrdersFromList(const OrderLinkedList &list) {
    std::vector<Order> out;
    ListNode *cur = list.head;
    while (cur) { out.push_back(cur->data); cur = cur->next; }
    return out;
}

Store::PlanResult Store::planRoute(const std::string &source,
                                   const std::string &destination,
                                   std::string &error, bool audit) {
    return planRouteOn(source, destination, error, graph, audit);
}

Store::PlanResult Store::planRouteOn(const std::string &source,
                                     const std::string &destination,
                                     std::string &error, const DeliveryGraph &g, bool audit) {
    PlanResult pr;
    RoutePlanner planner(g);
    pr.route = planner.shortestRoute(source, destination);
    if (!pr.route.ok) {
        error = pr.route.error;
        return pr;
    }
    std::vector<Order> orders = allOrdersFromList(orderList);
    std::vector<int> excluded = activeAssignedOrderIds();
    pr.intermediate = planner.findIntermediateOrders(pr.route, orders, excluded);
    // Destination-city eligible orders (same rule, city == destination).
    pr.destinationOrders = planner.eligibleOrdersInCity(destination, orders, excluded);
    // Planning must not change any order status — it only reads.
    // It does log one ROUTE_GENERATED entry (audit trail, as
    // specified) — unless audit=false (Phase 4B-5: read-only
    // demonstrations must not grow the log just by being viewed).
    // If that log write fails, the route is still returned; the
    // error string reports the log failure.
    if (!audit) return pr;
    ActivityLogEntry entry;
    entry.actionType = "ROUTE_GENERATED";
    entry.orderId = -1;
    entry.oldStatus = "";
    entry.newStatus = "";
    entry.description = "Route " + source + " to " + destination + "; " +
                        std::to_string(pr.route.totalTimeMin) + " min; " +
                        std::to_string(pr.route.totalDistanceKm) + " km; path " +
                        joinRoute(pr.route.path);
    if (!appendLogEntry(entry)) {
        error = "Route calculated, but the ROUTE_GENERATED log could not be written to " + logsPath + ".";
    }
    return pr;
}

DetourResult Store::planDetour(const std::string &source,
                               const std::string &destination,
                               const std::string &detourCity,
                               std::string &error, bool audit) {
    return planDetourOn(source, destination, detourCity, error, graph, audit);
}

DetourResult Store::planDetourOn(const std::string &source,
                                 const std::string &destination,
                                 const std::string &detourCity,
                                 std::string &error, const DeliveryGraph &g, bool audit) {
    RoutePlanner planner(g);
    std::vector<Order> orders = allOrdersFromList(orderList);
    std::vector<int> excluded = activeAssignedOrderIds();
    DetourResult d = planner.calculateDetour(source, destination, detourCity, orders, excluded);
    if (!d.ok) {
        error = d.error;
        return d;
    }
    if (!audit) return d; // Phase 4B-5: read-only demonstration, no log entry.
    ActivityLogEntry entry;
    entry.actionType = "ROUTE_GENERATED";
    entry.orderId = -1;
    entry.description = "Detour route " + source + " to " + destination + " via " + detourCity +
                        "; " + std::to_string(d.via.totalTimeMin) + " min; " +
                        std::to_string(d.via.totalDistanceKm) + " km; extra " +
                        std::to_string(d.extraTimeMin) + " min; " +
                        std::to_string(d.extraDistanceKm) + " km";
    if (!appendLogEntry(entry)) {
        error = "Detour calculated, but the ROUTE_GENERATED log could not be written to " + logsPath + ".";
    }
    return d;
}

MultiStopResult Store::planMultiStop(const std::string &source,
                                     const std::string &destination,
                                     const std::vector<std::string> &stops,
                                     std::string &error, bool audit) {
    return planMultiStopOn(source, destination, stops, error, graph, audit);
}

MultiStopResult Store::planMultiStopOn(const std::string &source,
                                       const std::string &destination,
                                       const std::vector<std::string> &stops,
                                       std::string &error, const DeliveryGraph &g, bool audit) {
    RoutePlanner planner(g);
    MultiStopResult r = planner.bestRouteWithStops(source, destination, stops);
    if (!r.ok) {
        error = r.error;
        return r;
    }
    if (!audit) return r; // Phase 4B-5: read-only demonstration, no log entry.
    ActivityLogEntry entry;
    entry.actionType = "ROUTE_GENERATED";
    entry.orderId = -1;
    entry.description = "Multi-stop route " + source + " to " + destination + " via " +
                        joinRoute(r.stopOrder) + "; " +
                        std::to_string(r.totalTimeMin) + " min; " +
                        std::to_string(r.totalDistanceKm) + " km";
    if (!appendLogEntry(entry)) {
        error = "Multi-stop route calculated, but the ROUTE_GENERATED log could not be written to " + logsPath + ".";
    }
    return r;
}

bool Store::commitTripWithLogs(const OrderSnapshot &orderSnap,
                               const std::vector<DeliveryTrip> &tripSnap,
                               std::vector<ActivityLogEntry> &entries,
                               std::string &error) {
    if (!persist()) {
        restoreSnapshot(orderSnap);
        trips = tripSnap;
        error = "Could not save to " + ordersPath +
                " — the trip was rolled back and no log entry was written.";
        return false;
    }
    if (!saveTrips()) {
        restoreSnapshot(orderSnap);
        trips = tripSnap;
        if (!persist()) {
            error = "CRITICAL: delivery_trips.csv could not be saved AND orders.csv could not be restored. "
                    "In-memory state matches the pre-trip snapshot, but the files on disk may disagree. "
                    "Restart and check both files before continuing.";
            return false;
        }
        error = "Could not save to " + tripsPath +
                " — the trip was rolled back and no log entry was written.";
        return false;
    }
    if (!appendLogEntries(entries)) {
        restoreSnapshot(orderSnap);
        trips = tripSnap;
        bool okOrders = persist();
        bool okTrips = saveTrips();
        if (!okOrders || !okTrips) {
            error = "CRITICAL: the activity log could not be written AND the orders/trips files could not be restored. "
                    "In-memory state matches the pre-trip snapshot, but the files on disk may disagree. "
                    "Restart and check all three files before continuing.";
            return false;
        }
        error = "Could not write to " + logsPath +
                " — the trip was rolled back and no log entry was written.";
        return false;
    }
    return true;
}

bool Store::createTrip(const std::string &source, const std::string &destination,
                       const std::vector<std::string> &stops,
                       const std::vector<int> &selectedOrderIds,
                       std::string &error, int &outTripId) {
    if (selectedOrderIds.empty()) {
        error = "No orders selected for the trip.";
        return false;
    }
    // Work out the planned route (with stops, or the shortest route).
    RouteResult route;
    if (stops.empty()) {
        RoutePlanner planner(graph);
        route = planner.shortestRoute(source, destination);
    } else {
        MultiStopResult ms = RoutePlanner(graph).bestRouteWithStops(source, destination, stops);
        if (!ms.ok) { error = ms.error; return false; }
        route = ms.bestRoute;
    }
    if (!route.ok) { error = route.error; return false; }
    // Cities on the route (source, intermediates, destination).
    std::vector<std::string> routeCities = route.path;
    // Validate every selected order BEFORE changing anything.
    // Duplicate IDs in the selection are rejected.
    std::vector<int> seen;
    for (int id : selectedOrderIds) {
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
            error = "Order " + std::to_string(id) + " is selected twice.";
            return false;
        }
        seen.push_back(id);
        Order *o = findOrder(id);
        if (!o) { error = "Order " + std::to_string(id) + " not found."; return false; }
        // Requirement 9 first: an order already on an active trip
        // gets the duplicate-assignment error, not the status error
        // (an assigned order is Shipped, so the status check below
        // would also reject it — but the admin should hear the real
        // reason). The status check is still the second layer.
        if (isOrderAssignedToActiveTrip(id)) {
            error = "Order " + std::to_string(id) + " is already assigned to an active trip.";
            return false;
        }
        // Requirement 7: only Processing orders may be assigned.
        // A Pending order must first be processed (changeStatus to
        // Processing). It is NOT silently moved Pending -> Shipped,
        // because that transition does not exist.
        if (o->status != "Processing") {
            error = "Order " + std::to_string(id) + " is " + o->status +
                    ", not Processing. Process it first, then assign it to a trip.";
            return false;
        }
        if (std::find(routeCities.begin(), routeCities.end(), o->city) == routeCities.end()) {
            error = "Order " + std::to_string(id) + " is in " + o->city +
                    ", which is not on the route " + joinRoute(route.path) + ".";
            return false;
        }
    }
    OrderSnapshot orderSnap = takeSnapshot();
    std::vector<DeliveryTrip> tripSnap = trips;
    // Apply in memory: Processing -> Shipped, no undo push (see
    // Store.h: a multi-order assignment is not undoable via the
    // single-order ActionStack).
    for (int id : selectedOrderIds) {
        if (!updateStores(id, "Shipped")) {
            restoreSnapshot(orderSnap);
            trips = tripSnap;
            error = "Internal error updating order " + std::to_string(id) + ".";
            return false;
        }
        syncQueue(id, "Shipped", false, false, -1);
    }
    DeliveryTrip trip;
    trip.tripId = nextTripId;
    trip.source = source;
    trip.destination = destination;
    trip.route = route.path;
    trip.assignedOrderIds = selectedOrderIds;
    trip.totalTimeMin = route.totalTimeMin;
    trip.totalDistanceKm = route.totalDistanceKm;
    trip.status = "Planned";
    trips.push_back(trip);
    nextTripId++;
    // Logs: one DELIVERY_SELECTION for the trip + one
    // DELIVERY_ASSIGNMENT per order, in a single batch append.
    std::vector<ActivityLogEntry> entries;
    {
        ActivityLogEntry sel;
        sel.actionType = "DELIVERY_SELECTION";
        sel.orderId = -1;
        sel.description = "Selected orders " + joinIds(selectedOrderIds) +
                          " for trip " + std::to_string(trip.tripId) +
                          " (" + source + " to " + destination + ")";
        entries.push_back(sel);
        for (int id : selectedOrderIds) {
            ActivityLogEntry a;
            a.actionType = "DELIVERY_ASSIGNMENT";
            a.orderId = id;
            a.oldStatus = "Processing";
            a.newStatus = "Shipped";
            a.description = "Order " + std::to_string(id) + " assigned to trip " +
                            std::to_string(trip.tripId) + "; Processing to Shipped";
            entries.push_back(a);
        }
    }
    if (!commitTripWithLogs(orderSnap, tripSnap, entries, error)) {
        nextTripId--; // the trip ID was never confirmed; reuse it next time
        return false;
    }
    outTripId = trip.tripId;
    return true;
}

bool Store::completeTrip(int tripId, std::string &error) {
    DeliveryTrip *trip = findTrip(tripId);
    if (!trip) { error = "Trip " + std::to_string(tripId) + " not found."; return false; }
    if (trip->status == "Completed") {
        error = "Trip " + std::to_string(tripId) + " is already Completed.";
        return false;
    }
    // Requirement 8: completion is EXPLICIT. Only this method may
    // mark the assigned orders Delivered — passing through a city
    // during planning or assignment never does.
    OrderSnapshot orderSnap = takeSnapshot();
    std::vector<DeliveryTrip> tripSnap = trips;
    for (int id : trip->assignedOrderIds) {
        Order *o = findOrder(id);
        if (!o) {
            restoreSnapshot(orderSnap);
            trips = tripSnap;
            error = "Cannot complete trip: assigned order " + std::to_string(id) + " no longer exists.";
            return false;
        }
        if (o->status != "Shipped") {
            restoreSnapshot(orderSnap);
            trips = tripSnap;
            error = "Cannot complete trip: order " + std::to_string(id) + " is " + o->status +
                    ", not Shipped.";
            return false;
        }
    }
    for (int id : trip->assignedOrderIds) {
        if (!updateStores(id, "Delivered")) {
            restoreSnapshot(orderSnap);
            trips = tripSnap;
            error = "Internal error updating order " + std::to_string(id) + ".";
            return false;
        }
        syncQueue(id, "Delivered", false, false, -1);
    }
    trip->status = "Completed";
    std::vector<ActivityLogEntry> entries;
    for (int id : trip->assignedOrderIds) {
        ActivityLogEntry e;
        e.actionType = "DELIVERY_COMPLETED";
        e.orderId = id;
        e.oldStatus = "Shipped";
        e.newStatus = "Delivered";
        e.description = "Order " + std::to_string(id) + " delivered on trip " +
                        std::to_string(tripId) + "; Shipped to Delivered";
        entries.push_back(e);
    }
    if (!commitTripWithLogs(orderSnap, tripSnap, entries, error)) return false;
    return true;
}

// Phase 4A — trip cancellation. See Store.h for the rules.
bool Store::cancelTrip(int tripId, std::string &error) {
    DeliveryTrip *trip = findTrip(tripId);
    if (!trip) { error = "Trip " + std::to_string(tripId) + " not found."; return false; }
    if (trip->status == "Completed") {
        error = "Trip " + std::to_string(tripId) + " is already Completed and cannot be cancelled.";
        return false;
    }
    if (trip->status == "Cancelled") {
        error = "Trip " + std::to_string(tripId) + " is already Cancelled.";
        return false;
    }
    OrderSnapshot orderSnap = takeSnapshot();
    std::vector<DeliveryTrip> tripSnap = trips;
    for (int id : trip->assignedOrderIds) {
        Order *o = findOrder(id);
        if (!o) {
            restoreSnapshot(orderSnap);
            trips = tripSnap;
            error = "Cannot cancel trip: assigned order " + std::to_string(id) + " no longer exists.";
            return false;
        }
        if (o->status != "Shipped") {
            restoreSnapshot(orderSnap);
            trips = tripSnap;
            error = "Cannot cancel trip: order " + std::to_string(id) + " is " + o->status +
                    ", not Shipped. It may already have been delivered or changed by hand.";
            return false;
        }
    }
    // Release: Shipped -> Processing (the pre-assignment status).
    // Deliberate reversal, like Undo — not a forward transition.
    for (int id : trip->assignedOrderIds) {
        if (!updateStores(id, "Processing")) {
            restoreSnapshot(orderSnap);
            trips = tripSnap;
            error = "Internal error updating order " + std::to_string(id) + ".";
            return false;
        }
        syncQueue(id, "Processing", false, false, -1);
    }
    trip->status = "Cancelled";
    std::vector<ActivityLogEntry> entries;
    {
        ActivityLogEntry tripEntry;
        tripEntry.actionType = "TRIP_CANCELLED";
        tripEntry.orderId = -1;
        tripEntry.description = "Trip " + std::to_string(tripId) + " cancelled; assigned orders released to Processing";
        entries.push_back(tripEntry);
        for (int id : trip->assignedOrderIds) {
            ActivityLogEntry e;
            e.actionType = "TRIP_CANCELLED";
            e.orderId = id;
            e.oldStatus = "Shipped";
            e.newStatus = "Processing";
            e.description = "Order " + std::to_string(id) + " released from cancelled trip " +
                            std::to_string(tripId) + "; Shipped to Processing";
            entries.push_back(e);
        }
    }
    if (!commitTripWithLogs(orderSnap, tripSnap, entries, error)) return false;
    return true;
}

std::vector<Order> Store::allOrders() const {
    return allOrdersFromList(orderList);
}

int Store::nextOrderId() const {
    int m = 1000;
    ListNode *cur = orderList.head;
    while (cur) {
        if (cur->data.id > m) m = cur->data.id;
        cur = cur->next;
    }
    return m + 1;
}

// Phase 4A — recovery report. Read-only; never auto-fixes.
Store::RecoveryReport Store::checkRecovery() {
    RecoveryReport r;
    r.corruptLogLines = logsSkipped;
    r.corruptTripLines = tripsSkipped;
    for (const std::string &tmp : {ordersPath + ".tmp", tripsPath + ".tmp"}) {
        if (fs::exists(tmp)) {
            r.leftoverTmpFiles++;
            r.leftoverTmpNames.push_back(tmp);
        }
    }
    if (r.leftoverTmpFiles > 0)
        r.warnings.push_back("Leftover .tmp file(s) found — a save was interrupted before the replace. "
                             "The original CSV is the good copy; delete the .tmp file, do not load from it.");
    if (r.corruptLogLines > 0)
        r.warnings.push_back(std::to_string(r.corruptLogLines) +
                             " corrupt line(s) in activity_logs.csv were skipped on load.");
    if (r.corruptTripLines > 0)
        r.warnings.push_back(std::to_string(r.corruptTripLines) +
                             " corrupt line(s) in delivery_trips.csv were skipped on load.");
    // Trip/order consistency: a Planned trip's orders must be Shipped;
    // a Completed trip's orders should be Delivered. Anything else
    // means the files were edited by hand or a crash hit between
    // the orders save and the trips save.
    std::vector<int> activeIds = activeAssignedOrderIds();
    std::sort(activeIds.begin(), activeIds.end());
    for (size_t i = 1; i < activeIds.size(); i++)
        if (activeIds[i] == activeIds[i - 1])
            r.warnings.push_back("Order " + std::to_string(activeIds[i]) +
                                 " is assigned to more than one active trip.");
    for (const DeliveryTrip &tr : trips) {
        for (int id : tr.assignedOrderIds) {
            const Order *o = findOrder(id);
            if (!o) {
                r.warnings.push_back("Trip " + std::to_string(tr.tripId) + " lists order " +
                                     std::to_string(id) + ", which does not exist in orders.csv.");
                continue;
            }
            if ((tr.status == "Planned" || tr.status == "InProgress") && o->status != "Shipped")
                r.warnings.push_back("Trip " + std::to_string(tr.tripId) + " is " + tr.status +
                                     " but order " + std::to_string(id) + " is " + o->status +
                                     " (expected Shipped).");
            if (tr.status == "Completed" && o->status != "Delivered")
                r.warnings.push_back("Trip " + std::to_string(tr.tripId) + " is Completed but order " +
                                     std::to_string(id) + " is " + o->status + " (expected Delivered).");
        }
    }
    return r;
}

// ------------------------------------------------------------------
// Persistence — atomic save (Phase 3a, Windows fix in Phase 3b)
// ------------------------------------------------------------------
bool Store::saveOrders() { return saveOrdersTo(ordersPath); }

bool Store::saveOrdersTo(const std::string &filename) {
    return saveAtomic(filename);
}

bool Store::persist() { return saveOrdersTo(ordersPath); }

// Phase 3c — shared atomic replace, used by orders.csv AND
// delivery_trips.csv. Windows: MoveFileEx REPLACE_EXISTING.
// POSIX: rename(). No remove-then-rename fallback on either
// platform; on failure the .tmp file is left for manual recovery.
static bool replaceFileAtomic(const std::string &tmpName, const std::string &filename) {
#ifdef _WIN32
    return MoveFileExA(tmpName.c_str(), filename.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    fs::rename(tmpName, filename, ec);
    return !ec;
#endif
}

bool Store::saveAtomic(const std::string &filename) {
    // Crash-safety, documented:
    //  1. Write the COMPLETE new file to "<filename>.tmp" first.
    //     If the program crashes, runs out of disk, or the write
    //     fails, the original file is untouched — only a .tmp file
    //     is left behind, and it is safe to delete.
    //  2. Only after the temp file is fully written, flushed and
    //     closed do we replace the original with it.
    //
    //  Phase 3b Windows fix (requirement 2):
    //  std::filesystem::rename() does NOT behave the same on
    //  Windows as on Linux. On POSIX, rename() atomically replaces
    //  the destination. On Windows, rename() FAILS if the
    //  destination already exists, so the Phase 3a code removed
    //  the destination and retried — a window in which the file
    //  did not exist at all, and a crash there lost the data.
    //  The correct Windows call is MoveFileEx with
    //  MOVEFILE_REPLACE_EXISTING (atomic replace, no gap) plus
    //  MOVEFILE_WRITE_THROUGH. We now use it on Windows and
    //  std::filesystem::rename() on POSIX, with NO remove-then-
    //  rename fallback on either platform. If the replace fails,
    //  we return false and LEAVE the .tmp file in place — recovery
    //  is: rename "<filename>.tmp" back to "<filename>" by hand.
    //  This Windows path has NOT been run on Windows yet (this VM
    //  is Linux); it uses only the documented Win32 API, and the
    //  student must run the tests on Windows to confirm it.
    //  3. A leftover "<filename>.tmp" on startup means "a save was
    //     interrupted before the replace" — the original CSV is the
    //     good copy. Delete the .tmp file; do NOT load from it.
    std::string tmpName = filename + ".tmp";
    {
        std::ofstream out(tmpName, std::ios::trunc);
        if (!out) return false;
        out << "order_id,customer_name,product,category,price_pkr,quantity,order_date,city,status\n";
        ListNode *cur = orderList.head;
        while (cur) {
            const Order &o = cur->data;
            out << o.id << "," << o.customer << "," << o.product << ","
                << o.category << "," << o.price << "," << o.quantity << ","
                << o.date << "," << o.city << "," << o.status << "\n";
            cur = cur->next;
        }
        out.flush();
        if (!out.good()) { out.close(); fs::remove(tmpName); return false; }
        out.close();
        if (!out.good()) { fs::remove(tmpName); return false; }
    }
    if (!replaceFileAtomic(tmpName, filename)) return false; // .tmp left for recovery
    return fs::exists(filename);
}
