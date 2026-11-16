#pragma once
// Store.h — Phase 3a (NEW file, no v1 equivalent)
//
// Store is the ONE owner of all six data structures and both CSV files.
// Every mutation in the program (console menu, tests, and later the
// HTTP API in Phase 4) must go through Store. Nothing else touches
// the Linked List, BST, Queue, Stack, Graph, or the CSVs directly.
//
// Why this exists (Phase 1 findings it fixes):
//  1. Status changes were copy-pasted in 4 places in main(). One place
//     forgot the BST, another forgot the Queue. Now there is one path:
//     Store::changeStatus().
//  2. Cancelling / deleting an order left it in the processing queue,
//     so it could be processed afterwards (stale-queue bug).
//  3. Undo restored the status in the List and BST but never restored
//     the queue, so an undone order was lost from processing.
//  4. saveOrders() wrote directly to orders.csv, so a crash mid-write
//     could leave a half-written file. Now saves are atomic (see
//     Store::saveAtomic() in Store.cpp).
//
// Queue invariant (Phase 3a, deliberate change from v1 — read this
// before you are surprised by a test number):
//   The processing queue contains EXACTLY the Pending orders, in the
//   order they arrived. Processing / Shipped / Delivered / Cancelled
//   orders are NEVER in the queue.
//   v1 enqueued Pending + Processing (11 orders on load). That meant
//   processNext() "processed" order 1005, which was already Processing
//   — duplicate processing. Now load enqueues Pending only (5 orders),
//   and the first order processed is 1006, the first Pending order.
//   This is the fix for "prevent duplicate processing".
//
// Status transition table (enforced by changeStatus()):
//   Pending    -> Processing, Cancelled
//   Processing -> Shipped, Cancelled
//   Shipped    -> Delivered
//   Delivered  -> (terminal, no further change except Undo)
//   Cancelled  -> (terminal, no further change except Undo)
// Undo bypasses this table on purpose: restoring an old status is a
// reversal, not a forward transition (e.g. Cancelled -> Pending).

#include <string>
#include <vector>

#include "ActionStack.h"
#include "ActivityLog.h"
#include "Delivery.h"
#include "DeliveryGraph.h"
#include "LinkedList.h"
#include "Order.h"
#include "OrderBST.h"
#include "OrderQueue.h"

struct Report {
    int total = 0;
    int pending = 0;
    int processing = 0;
    int shipped = 0;
    int delivered = 0;
    int cancelled = 0;
    int totalSales = 0; // Rs., not counting Cancelled orders
    int queueSize = 0;
    int undoSize = 0;
    int logSize = 0; // Phase 3b: entries in the Activity Log BST
    int tripCount = 0; // Phase 3c: confirmed delivery trips
    int activeTripCount = 0; // trips with status Planned/InProgress
};

// Phase 3b — snapshot of the ORDER state only (not logs, not the
// undo stack's history beyond its count). Used for rollback when a
// file operation fails mid-mutation: restore the snapshot and the
// in-memory state matches the CSV on disk again. See Store.cpp
// "Transaction / rollback" for the full rule.
struct OrderSnapshot {
    std::vector<Order> orders; // Linked List order
    std::vector<int> queueIds; // queue front -> rear
};

class Store {
public:
    // The structures. Public so the console/tests can read them
    // (display, report, benchmark). Mutations must still go through
    // the methods below — do not call orderList.updateStatus() etc.
    // directly from outside Store.
    // Phase 3b adds a SEVENTH structure: logTree, the Activity Log
    // BST (ActivityLog.h). It is separate from orderTree in every
    // way: different key, different purpose, different file.
    OrderLinkedList orderList;
    OrderBST orderTree;
    OrderQueue processQueue;
    ActionStack undoStack;
    // v1 ActionStack has no size() method, so Store counts pushes/pops.
    int undoCount = 0;
    DeliveryGraph graph;
    // Phase 4B-3 — second graph for the test-dataset demo mode.
    // Loaded from test_routes_intermediate.csv (same folder as the
    // production routes) when that file exists; production data and
    // behaviour are untouched. Trips are production-only.
    DeliveryGraph testGraph;
    bool testGraphLoaded = false;
    std::string testRoutesPath = "test_routes_intermediate.csv";
    ActivityLogBST logTree;
    // Phase 3c: confirmed delivery trips (in-memory list; the file
    // delivery_trips.csv is the persistent record).
    std::vector<DeliveryTrip> trips;

    bool ordersLoaded = false;
    bool routesLoaded = false;
    bool logsLoaded = false;
    std::string ordersPath = "orders.csv";
    std::string routesPath = "delivery_routes.csv";
    std::string logsPath = "activity_logs.csv";
    int nextLogId = 1;    // assigned to the next log entry; resumes from file
    int logsSkipped = 0;  // corrupt lines skipped while loading logs
    std::string tripsPath = "delivery_trips.csv";
    int nextTripId = 1;   // assigned to the next trip; resumes from file
    int tripsSkipped = 0; // corrupt lines skipped while loading trips
    bool tripsLoaded = false;

    Store() = default;
    ~Store(); // frees List / BST / Queue / Stack nodes (v1 leaked them)

    // -- Loading ------------------------------------------------------
    bool loadOrders(const std::string &filename);
    bool loadRoutes(const std::string &filename);
    // Phase 4B-3: load test_routes_intermediate.csv from the same
    // folder as routesPath into testGraph. Missing file = demo mode
    // unavailable, NOT an error (returns false, testGraphLoaded stays
    // false). loadAll() calls this best-effort after loadRoutes().
    bool loadTestRoutes();
    bool testGraphAvailable() const { return testGraphLoaded; }
    // Phase 4B-3: pick the graph for a dataset name. Returns nullptr
    // for an unknown dataset, or for "test" when no test graph loaded.
    const DeliveryGraph *graphForDataset(const std::string &dataset) const;
    // Phase 3b: rebuild the Log BST from activity_logs.csv.
    // Missing file = first run, empty log, returns true.
    // Corrupt lines are skipped (counted in logsSkipped), the rest load.
    bool loadLogs(const std::string &filename);
    // Phase 3c: rebuild the trips list from delivery_trips.csv.
    // Missing file = no trips yet, returns true.
    bool loadTrips(const std::string &filename);
    bool loadAll(); // loadRoutes + loadOrders + loadLogs + loadTrips

    // -- Queries ------------------------------------------------------
    // BST search (fast). steps = nodes visited, for the benchmark.
    Order *findOrder(int id, int &steps);
    Order *findOrder(int id);
    Report report();
    // Phase 4A: next free order ID (max existing ID + 1). Used by
    // the API when the client does not supply an order_id.
    int nextOrderId() const;
    void compareSearch(); // prints the 1,000-search List vs BST test (v1)

    // -- Mutations — the ONLY way to change an order -----------------
    // Every one of these validates first, updates List + BST together,
    // fixes the queue, persists orders.csv atomically, and appends
    // ONE Activity Log entry. See "Transaction / rollback" in
    // Store.cpp: if EITHER file step fails, the in-memory change is
    // rolled back and NO log entry is created, so order + log can
    // never disagree about what happened.
    // Log types: ORDER_CREATED (add), ORDER_CANCELLED (cancel / any
    // change to Cancelled), STATUS_UPDATED (other status changes),
    // ORDER_PROCESSED (processNext), ORDER_DELETED (delete),
    // UNDO (undo — appends a new entry, never deletes history).
    bool addOrder(const Order &o, std::string &error);
    bool changeStatus(int id, const std::string &newStatus, std::string &error);
    bool cancelOrder(int id, std::string &error); // = changeStatus(Cancelled)
    bool deleteOrder(int id, std::string &error); // permanent, not undoable
    // Dequeue the next Pending order and mark it Processing.
    // `out` receives a copy of the order after the change.
    bool processNext(Order &out, std::string &error);
    // Pop the ActionStack and restore the old status + queue position.
    // Appends a new UNDO log entry. The original action's log entry
    // stays in the tree and in the file — Undo never erases history.
    bool undo(std::string &error);

    // -- Delivery (Phase 3c) ------------------------------------------
    // Planning NEVER changes an order status. By default planRoute
    // logs one ROUTE_GENERATED entry (a read with an audit trail, as
    // specified); if that log write fails, the route is still
    // returned and the error string reports the log failure.
    // Phase 4B-5: audit=false skips the log entry entirely, for
    // read-only demonstrations (e.g. the DSA page) that must not
    // change the audit trail just by being viewed.
    // PlanResult bundles the route + intermediate matches +
    // destination-city eligible orders.
    struct PlanResult {
        RouteResult route;
        std::vector<IntermediateMatch> intermediate; // cities on the route
        std::vector<Order> destinationOrders; // eligible in destination city
    };
    PlanResult planRoute(const std::string &source, const std::string &destination,
                         std::string &error, bool audit = true);
    // Phase 4B-3 — same planning, on an explicit graph (production or
    // test). The 3-argument versions above delegate to these with
    // `graph`, so existing callers (tests, console) are unchanged.
    PlanResult planRouteOn(const std::string &source, const std::string &destination,
                           std::string &error, const DeliveryGraph &g, bool audit = true);
    DetourResult planDetour(const std::string &source, const std::string &destination,
                            const std::string &detourCity, std::string &error, bool audit = true);
    DetourResult planDetourOn(const std::string &source, const std::string &destination,
                              const std::string &detourCity, std::string &error,
                              const DeliveryGraph &g, bool audit = true);
    MultiStopResult planMultiStop(const std::string &source, const std::string &destination,
                                  const std::vector<std::string> &stops,
                                  std::string &error, bool audit = true);
    MultiStopResult planMultiStopOn(const std::string &source, const std::string &destination,
                                    const std::vector<std::string> &stops,
                                    std::string &error, const DeliveryGraph &g, bool audit = true);
    // Create a confirmed trip. Rules (requirement 7 + 9):
    //  * Every selected order must be Processing. A Pending order
    //    is rejected with "process it first" — it is NOT silently
    //    moved Pending -> Shipped, because that transition does not
    //    exist in the transition table.
    //  * Every selected order must be in a city on the planned
    //    route (intermediate, detour stop, or destination).
    //  * No selected order may already be assigned to an active
    //    trip (Planned/InProgress).
    //  * Assignment is NOT undoable via the ActionStack (it is a
    //    multi-order transaction; see Store.cpp). On success the
    //    orders become Shipped, the trip is Planned, and the logs
    //    are DELIVERY_SELECTION (once) + DELIVERY_ASSIGNMENT (once
    //    per order). outTripId receives the new trip ID.
    bool createTrip(const std::string &source, const std::string &destination,
                    const std::vector<std::string> &stops,
                    const std::vector<int> &selectedOrderIds,
                    std::string &error, int &outTripId);
    // Explicit completion only (requirement 8): Planned -> Completed,
    // and each assigned order Shipped -> Delivered. Passing through
    // a city never delivers anything by itself.
    bool completeTrip(int tripId, std::string &error);
    // Phase 4A — trip cancellation. Only a Planned trip can be
    // cancelled (a Completed trip is history, not reversible).
    // Rules: the trip becomes Cancelled; each assigned order is
    // RELEASED back to Processing (the exact status it had before
    // assignment — assignment required Processing, so this is
    // deterministic). This is a deliberate reversal, like Undo: it
    // bypasses the forward transition table, because there is no
    // forward Shipped -> Processing transition. The cancellation
    // is logged as TRIP_CANCELLED (once per order + once for the
    // trip). A cancelled trip is not active, so its orders may be
    // assigned to a new trip afterwards.
    bool cancelTrip(int tripId, std::string &error);
    // Phase 4A — recovery. checkRecovery() reports what a restart
    // found: leftover .tmp files, corrupt lines skipped, and any
    // trip/order inconsistency (e.g. a Planned trip whose order is
    // not Shipped — that can only happen if the files were edited
    // by hand or a crash hit between writes). It never auto-fixes:
    // the files on disk are the source of truth, and a warning in
    // the report (and in /api/health) is safer than a silent fix.
    struct RecoveryReport {
        int leftoverTmpFiles = 0;
        std::vector<std::string> leftoverTmpNames;
        int corruptLogLines = 0;
        int corruptTripLines = 0;
        std::vector<std::string> warnings;
    };
    RecoveryReport checkRecovery();
    // All orders in List order (for the API listing). The List is
    // the source; the BST is the search index.
    std::vector<Order> allOrders() const;
    DeliveryTrip *findTrip(int tripId);
    std::vector<int> activeAssignedOrderIds() const;
    bool isOrderAssignedToActiveTrip(int orderId) const;
    // Trips persistence (atomic rewrite, same replace rule as orders).
    bool saveTrips();
    bool saveTripsTo(const std::string &filename);

    // -- Activity Log (Phase 3b) --------------------------------------
    // Append one entry: assigns logId + current timestamp, writes
    // the line to activity_logs.csv (append mode), then inserts it
    // into logTree. Returns false if the file write fails (in which
    // case nothing is inserted into the tree).
    // `entry` is filled in (logId, timestamp) on success.
    bool appendLogEntry(ActivityLogEntry &entry);
    // Phase 3c: append several entries in ONE file open, so a
    // multi-entry transaction (e.g. a trip assignment) either
    // writes all its log lines or fails at the open with none
    // written. Each entry is assigned its logId + timestamp here.
    bool appendLogEntries(std::vector<ActivityLogEntry> &entries);
    static std::string nowIso(); // current time, "YYYY-MM-DDTHH:MM:SS"
    static std::string sanitizeDescription(const std::string &d);

    // -- Persistence --------------------------------------------------
    // Atomic save to ordersPath. Also used by every mutation above.
    bool saveOrders();
    bool saveOrdersTo(const std::string &filename);

    // -- Validation ---------------------------------------------------
    static bool isValidTransition(const std::string &from,
                                  const std::string &to);
    static bool isValidStatus(const std::string &status);

private:
    // Helpers — these do ONE job each, so changeStatus/processNext/undo
    // cannot drift apart again.
    void clearAll(); // free all nodes (used on reload + destructor)
    void clearOrdersOnly(); // free List/BST/Queue only (rollback helper)
    void insertBalancedBST(std::vector<Order> &v, int lo, int hi);
    // Update List + BST only. No queue, no undo, no validation, no save.
    bool updateStores(int id, const std::string &newStatus);
    // Make the queue match a status: Pending -> in queue at its
    // ORIGINAL position (queuePosition), anything else -> out of
    // the queue. `wasDequeued` means "put it at the FRONT" (Undo of
    // processNext only, position 0). Phase 3b: position is preserved
    // wherever possible, clamped to the queue size if it shrank.
    void syncQueue(int id, const std::string &status, bool wasDequeued,
                   bool wasInQueue, int queuePosition);
    bool persist(); // saveOrdersTo(ordersPath)
    // Atomic write: temp file -> rename. See Store.cpp for the
    // crash-recovery explanation. Windows uses MoveFileEx with
    // MOVEFILE_REPLACE_EXISTING (atomic replace); POSIX uses rename().
    bool saveAtomic(const std::string &filename);
    // Phase 3b transaction helpers.
    OrderSnapshot takeSnapshot() const;
    void restoreSnapshot(const OrderSnapshot &snap);
    // Commit one mutation: save orders.csv, then append its log entry.
    // On orders-save failure: restore snapshot, no log, return false.
    // On log-append failure: restore snapshot, re-save the restored
    // orders, no log entry in the tree, return false.
    // Returns true only when BOTH files are safely written.
    bool commitWithLog(const OrderSnapshot &snap, ActivityLogEntry &entry,
                       std::string &error);
    // Phase 3c: commit a trip (orders + trips + logs together).
    // Order of writes: orders.csv, then delivery_trips.csv, then
    // the log batch. On any failure, restore both snapshots and
    // re-save the files that were already written. See Store.cpp.
    bool commitTripWithLogs(const OrderSnapshot &orderSnap,
                            const std::vector<DeliveryTrip> &tripSnap,
                            std::vector<ActivityLogEntry> &entries,
                            std::string &error);
    bool saveTripsAtomic(const std::string &filename);
    static bool parseLogLine(const std::string &line, ActivityLogEntry &out);
    static bool parseTripLine(const std::string &line, DeliveryTrip &out);
    static std::string joinRoute(const std::vector<std::string> &route);
    static std::string joinIds(const std::vector<int> &ids);
};
