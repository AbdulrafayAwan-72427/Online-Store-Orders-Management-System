# Phase 3b Report — Activity Log BST

**Date:** 13 November 2026
**Status:** COMPLETE and TESTED. 161/161 regression tests passed (66 old + 95 new).
**Next step:** Phase 3c (delivery upgrade: distance + RouteResult + intermediate matching + detours + trips) — **NOT started**, awaiting your approval.

---

## 1. Files created or modified

| File | Change |
|---|---|
| `ActivityLog.h` | NEW (249 lines). Separate custom BST — `ActivityLogEntry`, `LogNode`, `ActivityLogBST`. No `std::map`/`std::set` anywhere; nodes are linked by pointers, insert/search/delete are recursive, inorder is the timeline. |
| `Store.h` / `Store.cpp` | MODIFIED. Log tree + `logsPath`/`nextLogId`/`logsSkipped`, `loadLogs`, `appendLogEntry`, `nowIso`, snapshot/rollback (`takeSnapshot`, `restoreSnapshot`, `commitWithLog`), Windows atomic replace, queue-position sync. Store.cpp grew 17 KB → 29 KB. |
| `OrderQueue.h` | MODIFIED. Added `indexOf()` and `insertAtPosition()` (marked NEW, Phase 3b). |
| `ActionStack.h` | MODIFIED. `Action` gains `queuePosition` (default −1). |
| `main.cpp` | MODIFIED. Loads `activity_logs.csv` at startup; report shows log count; new menu option 14 "Show activity logs". |
| `tests.cpp` | MODIFIED. 66 old checks kept verbatim in intent; 95 new checks added (groups 11–18). 558 lines total. |
| `README.md` | UPDATED. Phase 3b section, log file format, rollback rule, Windows note. |
| `orders.csv`, `delivery_routes.csv` | **NOT modified.** md5 verified after all work: orders `c2e3b6f5f8250dc05b916f71557d639d`, routes `13749ce0933f67aabf204446cb3cf52e`. No `activity_logs.csv` was created in the project folder (tests use `test_work/`, the console smoke test ran in `/tmp`). |
| `app.py`, proposal files, Phase 1/3a reports | **NOT touched.** |

## 2. How the Activity Log BST works

**Key:** composite `(timestamp, logId)`. Timestamp is ISO `"YYYY-MM-DDTHH:MM:SS"`, so lexicographic order = chronological order, and inorder traversal *is* the timeline, oldest first. Duplicate timestamps are normal (several actions in one second) — the logId breaks the tie, so entries in the same second stay in the order they happened.

**The two BSTs, for the viva:**

| | Order BST (existing) | Activity Log BST (new) |
|---|---|---|
| Key | `order_id` | `(timestamp, logId)` |
| Answers | "Where is order 1024?" | "What happened, in order?" |
| Inorder gives | Orders sorted by ID | Log entries in chronological order |
| `searchLogById` | — | O(n) full traversal — the tree is ordered by *time*, not logId, so there is no shortcut to a logId. Say this, do not hide it. |
| Worst case | O(n) if unbalanced | O(n) if unbalanced (timestamps arrive in increasing order, so the tree leans right; it is not self-balancing — do not claim O(log n) is guaranteed) |

**Operations (all in `ActivityLog.h`):** `insertLog()` (rejects duplicate logId, logId ≤ 0, empty timestamp/action), `searchLogById()`, `deleteLogById()` (maintenance only — Store never calls it in normal operation), `inorderTraversal()`, `searchLogsByTimeRange(start, end)` (inclusive), `searchLogsByOrderId()`, `searchLogsByActionType()`, `maxLogId()`, `displayAllLogs()`.

**Log entry fields:** `logId` (unique, sequential, resumes from the file on restart, never reused), `timestamp`, `actor` = `"admin"` (decision D1), `actionType`, `orderId` (−1 if none), `oldStatus`, `newStatus`, `description` (commas replaced with `;` so the CSV stays parseable).

**Action types logged:** `ORDER_CREATED` (add), `ORDER_CANCELLED` (cancel, or any change to Cancelled), `STATUS_UPDATED` (other status changes), `ORDER_PROCESSED` (processNext), `ORDER_DELETED` (delete), `UNDO` (undo). Failed actions write **no** log entry — a log entry means the action actually happened.

**Persistence:** `activity_logs.csv` is the truth; the BST is the index. Header: `log_id,timestamp,actor,action_type,order_id,old_status,new_status,description`. Normal operation is **append-only**: one line appended per action, never a rewrite. At startup the tree is rebuilt from the file; `nextLogId = maxLogId + 1`. Missing file = first run, empty log, not an error. Corrupt lines are skipped and counted in `logsSkipped` (bad ID, too few fields, duplicate logId) — the good lines still load.

**Undo never erases history.** Undo pops the ActionStack and restores the old status, then appends a *new* `UNDO` entry (`oldStatus` = the status being undone, `newStatus` = the restored status). The original action's entry stays in the tree and in the file. Verified in tests (group 13) and in the console smoke test: cancel 1017 → log #1 `ORDER_CANCELLED`; undo → log #2 `UNDO`; both entries remain.

## 3. Consistency when file operations fail (requirement 1)

**Rule, implemented in `Store::commitWithLog()` and tested:** an order change and its log entry are ONE unit.

1. Validate, take a snapshot (orders in List order + queue IDs), apply the change in memory.
2. Save `orders.csv` atomically. **If it fails:** restore the snapshot in memory, undo the stack push if one was made, write **no** log, return an error. The CSV on disk was never changed (atomic save), so memory matches disk again.
3. Append the log line to `activity_logs.csv`, then insert it into the tree. **If it fails:** restore the snapshot, fix the stack (pop a pushed action / push back a popped one), **re-save** `orders.csv` with the restored orders, write **no** log entry into the tree, return an error.
4. Only if both steps succeed does the method return true.

If *both* steps fail, the error message says so explicitly (`CRITICAL: ...`) — that state is reported, not hidden. On the next restart the CSV on disk is the source of truth.

**Recovery on startup:** a leftover `orders.csv.tmp` means a save was interrupted before the replace — the original CSV is the good copy, delete the `.tmp`, do not load from it. A corrupt line in `activity_logs.csv` is skipped on load (`logsSkipped`), the rest load, and `nextLogId` resumes from the highest good ID.

## 4. Windows CSV replacement (requirement 2)

Phase 3a used `std::filesystem::rename()` with a remove-then-rename fallback. That is **wrong on Windows**: POSIX `rename()` atomically replaces the destination, but Windows `rename()` *fails* if the destination exists, so the fallback removed the destination first — a window in which the file did not exist at all.

Phase 3b fixes it in `Store::saveAtomic()`:
- **Windows (`_WIN32`):** `MoveFileExA(tmp, dest, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` — atomic replace, no gap, no remove step.
- **POSIX:** `std::filesystem::rename()` — already atomic.
- **No fallback on either platform.** If the replace fails, the method returns false and **leaves the `.tmp` file in place** for manual recovery (rename it back by hand). It never deletes the destination to retry.

**Honest limit:** this Windows path has **not** been run on Windows yet (this VM is Linux). It uses only the documented Win32 API. You must run the tests on your Windows machine (`g++ -std=c++17 -Wall -Wextra tests.cpp Store.cpp -o tests.exe` then `.\tests.exe`) to confirm it there.

## 5. Queue restoration after Undo (requirement 3)

Phase 3a only remembered front vs rear, so undo of a cancel in the *middle* of the queue put the order at the rear and lost its place. Phase 3b records the exact position:

- `Action` now stores `queuePosition` (0 = front, −1 = not in the queue), captured by `changeStatus()` / `processNext()` before the change.
- `OrderQueue::insertAtPosition(id, pos)` re-inserts at that exact position; `pos` is clamped to the queue size if the queue has shrunk since (e.g. other orders were processed meanwhile).
- Undo of processNext: position 0 → front. Undo of cancel/status change: original position. If the old status was not Pending, the order stays out of the queue.

Verified in tests (group 14) and the console smoke test: queue `[1006,1012,1017,1022,1027]` → cancel 1017 → `[1006,1012,1022,1027]` → undo → `[1006,1012,1017,1022,1027]` again. Processing two orders then undoing twice also restores the original queue order.

## 6. Build and test results — ACTUAL, executed 13 Nov 2026

**Build (this VM, `g++ -std=c++17 -Wall -Wextra`, zero warnings):**
```
g++ -std=c++17 -Wall -Wextra main.cpp Store.cpp -o store   → OK
g++ -std=c++17 -Wall -Wextra tests.cpp Store.cpp -o tests  → OK
```

**Regression tests (`./tests`, exit code 0):**
```
=== Results: 161 passed, 0 failed ===
ALL TESTS PASSED.
```
Old 66 checks all still pass; 95 new checks added in groups 11–18: Log BST unit (inorder, duplicate timestamp, duplicate logId, invalid IDs, time-range incl. empty and start>end, deleteLogById); Store logging for every action type (add/cancel/status/process/delete, actor `admin`, sequential IDs, file line count matches tree); Undo appends UNDO without erasing history; queue-position preservation (middle, front, double process-undo); log persistence after restart (entries reloaded, `nextLogId` resumes at 3, no ID reuse); rollback when the log file cannot be written (cancel/add/undo fail cleanly, order unchanged in memory *and* on disk, no log created); rollback when `orders.csv` cannot be saved (same, plus the undo action stays on the stack); corrupt log lines skipped on load (`logsSkipped` = 3, `nextLogId` resumes correctly).

**First run of the extended suite was 159 passed / 2 failed** — both failures were the same bug in *my* code, not the tests: `appendLogEntry()` never wrote a header line to a new log file, so the file had 5 entries but 5 lines instead of 6. Fixed by writing the header when the file is new/empty. Second run: 161/161.

**Console smoke test (in `/tmp`, not on the real CSVs):** load → cancel middle order 1017 → undo (queue restored to original order) → process 1006 → show logs. Log output was:
```
#1 | 2026-11-13T17:17:51 | admin | ORDER_CANCELLED | order 1017 | Pending -> Cancelled
#2 | 2026-11-13T17:17:51 | admin | UNDO | order 1017 | Cancelled -> Pending
#3 | 2026-11-13T17:17:51 | admin | ORDER_PROCESSED | order 1006 | Pending -> Processing
```
All three share one timestamp — duplicate-timestamp handling works in the real program, not just in the unit test. `activity_logs.csv` in `/tmp` had the header + 3 lines, no `.tmp` left.

## 7. Changes to existing behaviour

- **Queue position on Undo is now exact** (was front/rear only). No other Phase 3a behaviour changed: queue still Pending-only (5 on load), transition table unchanged, benchmark numbers unchanged (List 16,400 vs BST 4,600, search 1024 in 5 steps, routes 260/900 min — all re-verified by the old 66 checks).
- **Every mutation now also writes `activity_logs.csv`.** If you run the console in the project folder, an `activity_logs.csv` file will appear next to `orders.csv`. That is intended — it is the audit file. Tests and the smoke test ran in `test_work/` / `/tmp` so the project folder has no log file yet.
- **Atomic save no longer has a remove-then-rename fallback** (see §4). On Linux the behaviour is identical (rename); on Windows it is now correct instead of gapped.

## 8. Regressions / unresolved problems

- **No unresolved bugs found in Phase 3b.** One bug was found and fixed during testing (missing log-file header, §6).
- **Not yet done (by design, Phase 3b scope):** no delivery trips / detours, no distance in the Graph (time only, as in v1), no HTTP API, no React frontend. Delivery action types (`DELIVERY_ASSIGNMENT`, `ROUTE_GENERATED`, etc.) are named in `ActivityLog.h` as future types but are not generated yet.
- **Known limits, carried over:** log timestamps have 1-second resolution, so several actions in one second share a timestamp (ordered by logId — by design, not a bug). `searchLogById` is O(n) and the Log BST is not self-balancing (both documented in `ActivityLog.h`). The Windows `MoveFileEx` path is untested on Windows (see §4).
- **A leftover `activity_logs.csv.tmp` does not exist** — log appends write directly, not via a temp file. A crash mid-append could leave a partial last line; on the next load that line is skipped as corrupt (`logsSkipped`) and the earlier entries are intact. That is the documented recovery for the append path.

## 9. Next implementation step

**Phase 3c — Delivery upgrade** (do not start until you approve):
1. `DeliveryGraph`: store `distanceKm` alongside `timeMin`; `loadRoutes()` reads both columns; `shortestPath()` returns a `RouteResult {path, totalTimeMin, totalDistanceKm}`.
2. `Delivery.h/.cpp`: `RoutePlanner` — intermediate-city matching (only cities actually on the path), detour costing (full `source→X` + `X→destination` vs direct, extra time *and* distance), `DeliveryTrip` + `delivery_trips.csv`.
3. Wire delivery actions into the Activity Log (`ROUTE_GENERATED`, `DELIVERY_ASSIGNMENT`, `DELIVERY_SELECTION`, `DELIVERY_COMPLETED`).
4. Test both required examples: A (Lahore→Islamabad, no intermediates) and B (Lahore→Karachi via Faisalabad, +30 min, orders 1006/1022).
5. Re-run the full 161-test suite plus the new delivery tests; all must pass before Phase 4.
