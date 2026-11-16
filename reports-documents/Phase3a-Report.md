# Phase 3a Report — Modular Backend, Store, Central Status Changes, Queue Fix

**Date:** 13 November 2026
**Status:** COMPLETE and TESTED. 66/66 regression tests passed.
**Next step:** Phase 3b (Activity Log BST + `activity_logs.csv`) — **NOT started**, awaiting your go-ahead.

---

## 1. Files created or modified

| File | Change |
|---|---|
| `Order.h` | NEW. Order struct + `printOrder` / `splitCSV` / `parseOrder`, moved verbatim from v1 `main.cpp`. Functions marked `inline` so the header can be included by both `main.cpp` and `Store.cpp`. |
| `LinkedList.h` | NEW. `OrderLinkedList`, moved verbatim. |
| `OrderQueue.h` | NEW. `OrderQueue`, moved verbatim, **plus** `contains()`, `removeById()`, `enqueueFront()` (marked NEW). |
| `ActionStack.h` | NEW. `ActionStack`, moved verbatim, **plus** `Action.wasInQueue` and `Action.wasDequeued` (marked NEW). |
| `OrderBST.h` | NEW. `OrderBST`, moved verbatim. |
| `DeliveryGraph.h` | NEW. `DeliveryGraph`, moved verbatim, **plus** `shortestTime()` (marked NEW) — same Dijkstra, returns time + path instead of only printing. `shortestPath()` now calls it; printed output is unchanged. |
| `Store.h` / `Store.cpp` | NEW. The `Store` class — owns all 6 structures + both CSVs. Central `changeStatus()`, transition validation, queue invariant, atomic save. |
| `main.cpp` | REWRITTEN (705 lines → ~190 lines). Thin console menu only; all logic is in `Store`. Menu 1–12 behave as v1, plus new option 13 "Update order status". |
| `tests.cpp` | NEW. 66-check regression suite, exit 0 only if all pass. |
| `archive/main_v1.cpp` | NEW. Byte-identical copy of the original `main.cpp` (md5 `f73468b4d757c7fc8f4cf4d8f9b50e7d`). |
| `archive/orders_v1.csv`, `archive/delivery_routes_v1.csv` | NEW. Copies of the original datasets. |
| `README.md` | UPDATED. New build commands, queue change, transition table, atomic-save explanation. |
| `orders.csv`, `delivery_routes.csv` | **NOT modified.** md5 verified after all work: orders `c2e3b6f5f8250dc05b916f71557d639d`, routes `13749ce0933f67aabf204446cb3cf52e` — same as before Phase 3a. |
| `app.py` | **NOT touched.** Preserved as the separate Python demonstration (decision D5). |
| `Phase1-Inspection-and-Plan.md`, proposal files | **NOT touched.** |

Headers use `std::` prefixes (v1 relied on a global `using namespace std;` in `main.cpp`, which does not carry over to headers). No DSA algorithm was changed.

## 2. Architecture changes

**Before (v1):** one 705-line `main.cpp`, global objects, status changes copy-pasted in 4 places in `main()`, save wrote directly to `orders.csv`.

**After (Phase 3a):**
```
main.cpp (thin menu) ──┐
tests.cpp (regressions) ├─► Store (Store.h/.cpp) ─► Order.h / LinkedList.h /
                          │   owns List, BST, Queue, Stack, Graph, CSVs   OrderQueue.h / ActionStack.h /
                          │                                                OrderBST.h / DeliveryGraph.h
                          └─► orders.csv / delivery_routes.csv (atomic save)
```
Every mutation goes through `Store`: `addOrder`, `changeStatus`, `cancelOrder`, `deleteOrder`, `processNext`, `undo`. The console, the tests, and (in Phase 4) the HTTP API will all use the same `Store`, so they cannot disagree.

### Central status changes
`Store::changeStatus()` is the single path. It: (1) checks the transition table, (2) pushes the undo `Action`, (3) updates Linked List **and** BST together, (4) fixes the queue, (5) saves atomically. `cancelOrder()` is just `changeStatus(id, "Cancelled")`.

Transition table (enforced):
- Pending → Processing, Cancelled
- Processing → Shipped, Cancelled
- Shipped → Delivered
- Delivered / Cancelled → terminal (no forward change; Undo is the only way back)

### Queue fix — the stale-queue bug is fixed, and the invariant changed
**Invariant:** the queue contains *exactly* the Pending orders, in arrival order. Processing / Shipped / Delivered / Cancelled orders are never in it.

- Load enqueues Pending only: **5 orders** (1006, 1012, 1017, 1022, 1027). v1 enqueued Pending + Processing (**11**). This is deliberate: v1's first processed order was 1005, which was already Processing — duplicate processing.
- First processed order is now **1006**, not 1005.
- Cancel / delete remove the order from the queue immediately (`removeById`).
- `processNext()` skips any front order that is no longer Pending (defensive; under the invariant it should not happen).
- **Undo restores the queue:** undo of a cancel/status change puts the order back at the **rear**; undo of processNext puts it back at the **front**, so it is processed next again. The `Action` records `wasInQueue` / `wasDequeued` for exactly this.
- Undo bypasses the forward transition table (Cancelled → Pending is invalid forward, but is what undo of a cancel must do).

### Atomic CSV save (no more partial writes)
`Store::saveAtomic()` writes the complete new file to `orders.csv.tmp` first (flush + close + check), then renames it over `orders.csv`. On POSIX the rename is atomic; on Windows the destination is removed and the rename retried once. Recovery, documented in `Store.cpp`:
- Crash during the write → original file untouched, only a `.tmp` file left (safe to delete).
- Crash during rename → either the old or the new file is present, never a half-written file; the complete new data still exists in the `.tmp` file.
- A leftover `.tmp` on startup means "save was interrupted before the rename" — the original CSV is the good copy; do not load from the `.tmp`.
Every mutation calls this save automatically. If a save fails, the method returns an error and the old CSV stays intact (in-memory state is kept and will be retried on the next save — documented in `Store.cpp`).

### Other
- `Store` destructor frees List / BST / Queue / Stack nodes (v1 leaked them on exit).
- `DeliveryGraph::shortestTime()` added for testability; `shortestPath()` printed output unchanged.
- New console option 13: update order status via `changeStatus()`.

## 3. Build and test results — ACTUAL, executed 13 Nov 2026

**Build (this VM, `g++ -std=c++17 -Wall -Wextra`, zero warnings):**
```
g++ -std=c++17 -Wall -Wextra main.cpp Store.cpp -o store   → OK
g++ -std=c++17 -Wall -Wextra tests.cpp Store.cpp -o tests  → OK
```
Two compile fixes were needed during the split and are recorded here honestly: (1) headers needed `std::` prefixes because v1 used a global `using namespace std;`; (2) free functions in `Order.h` needed `inline` to avoid multiple-definition link errors when included by two `.cpp` files.

**Regression tests (`./tests`, exit code 0):**
```
=== Results: 66 passed, 0 failed ===
ALL TESTS PASSED.
```
Covered, in 10 groups: load (30 orders, counts 11/6/6/5/2, sales Rs.182,300, queue 5, front 1006); BST search 1024 in **5 steps**; 1,000-search benchmark **List 16,400 vs BST 4,600**; add + duplicate/price validation; FIFO (1006 then 1012, undo puts 1012 back at front); cancel + undo (cancel removes from queue, undo restores Pending + queue); invalid transitions (Delivered→Processing, Pending→Delivered rejected; Shipped→Delivered allowed); delete (gone from List, BST and queue, total 29); Dijkstra (Lahore→Islamabad **260 min** direct, Lahore→Karachi **900 min** direct, unknown city −1); persistence after restart (cancel + add survive a fresh `Store` reload, no `.tmp` left); original datasets untouched (orders.csv 31 lines incl. header, routes 13 lines, no `.tmp`).

**First test run was 57 passed / 9 failed** — the failures were a bug in the *tests*, not in `Store`: all test groups shared one `test_work/orders.csv`, so mutations from one group leaked into the next. Fixed by resetting the test data at the start of every test group. The second run was 66/66. This is recorded because the CTO requirement is to report actual results, not to hide a failed run.

**Console smoke test (in `/tmp`, not on the real CSVs):** load → report (30 orders, Rs.182,300, queue 5) → search 1024 in 5 steps → process 1006 → cancel 1006 → undo → report; benchmark 16,400 vs 4,600; routes 260 / 900 min; invalid Pending→Shipped correctly rejected; save succeeded with no `.tmp` left. All as expected.

## 4. Regressions or unresolved problems

- **One deliberate behaviour change (not a regression, but flag it):** queue on load is 5, not 11, and the first processed order is 1006, not 1005. This is the duplicate-processing fix (see §2). If your teacher's notes say "queue has 11", the proposal's §4/§6 will need one line updated in Phase 6 to say "Pending only".
- **No unresolved bugs found in Phase 3a.** Two test-design bugs were found and fixed during testing (see §3).
- **Not yet done (by design, Phase 3a scope):** no Activity Log BST, no `activity_logs.csv`, no delivery trips / detours, no distance in the Graph (time only, as in v1), no HTTP API, no React frontend. The Graph still stores time only — distance is a Phase 3c change.
- **Known limit, carried over from v1:** `parseOrder` uses a simple comma split; fields containing commas would break it. The data has none. Documented in `Order.h`.
- **Windows note:** the atomic save's remove-then-rename path is for Windows; it was compiled and tested on Linux here. The rename fallback is standard C++17 `std::filesystem` and should work on Windows, but it has not been run on your Windows machine yet — test it there with `g++ main.cpp Store.cpp -o store.exe` then `.\store.exe`.

## 5. Next implementation step

**Phase 3b — Activity Log BST + persistent audit log** (do not start until you approve):
1. `ActivityLog.h/.cpp`: `ActivityLogEntry` (logId, timestamp, actor `"admin"`, actionType, orderId, oldStatus, newStatus, description) + `ActivityLogBST` keyed by `(timestamp, logId)` with `insertLog`, `searchLogById`, `deleteLogById` (admin maintenance only), `inorderTraversal`, `searchLogsByTimeRange`, `displayAllLogs`.
2. `activity_logs.csv`: append-only in normal operation; reloaded into the Log BST at startup. Wire logging into `Store` only (every mutation writes its log entry inside the same `Store` method).
3. Undo appends a new `UNDO` log entry — it never deletes the original entry.
4. Extend `tests.cpp`: log insert/search, time-range search, restart persistence, undo-does-not-erase-history.
5. Re-run the full 66-test suite plus the new log tests; all must pass before Phase 3c.
