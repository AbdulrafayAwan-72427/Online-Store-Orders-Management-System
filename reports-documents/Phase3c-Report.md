# Phase 3c Report — Advanced Delivery Route Optimization

**Date:** 13 November 2026
**Status:** COMPLETE and TESTED. 266/266 regression tests passed (161 old + 105 new), first run.
**Next step:** Phase 4 (HTTP API with an established C++ library + React frontend) — **NOT started**, awaiting your approval.

---

## 1. Files created or modified

| File | Change |
|---|---|
| `Delivery.h` | NEW (13 KB). `DeliveryTrip`, `IntermediateMatch`, `DetourResult`, `MultiStopResult`, `RoutePlanner` (shortest route, intermediate matching, detour, exact multi-stop). RoutePlanner only computes — it never changes an order, writes a file, or touches the log. |
| `DeliveryGraph.h` | MODIFIED. New `distanceKm[][]` matrix (v1 threw `distance_km` away); `addRoute(from, to, minutes, km)`; `edgeTime`/`edgeDistance`; new `RouteResult {ok, source, destination, path, totalTimeMin, totalDistanceKm, error}` and `shortestRoute()` that returns it. `shortestTime()`/`shortestPath()` kept for compatibility. |
| `Store.h` / `Store.cpp` | MODIFIED. Trips list + `tripsPath`/`nextTripId`, `loadTrips`/`saveTrips`, `planRoute`/`planDetour`/`planMultiStop`, `createTrip`, `completeTrip`, `activeAssignedOrderIds`, batch log append `appendLogEntries`, multi-file commit `commitTripWithLogs`, shared `replaceFileAtomic()` (used by both orders and trips). Store.cpp grew 29 KB → 50 KB. |
| `ActivityLog.h` | MODIFIED (comment only). Delivery action types are now generated, not just named as future types. |
| `main.cpp` | MODIFIED. New options 15 Plan delivery route, 16 Create delivery trip, 17 Complete delivery trip, 18 Show delivery trips. Report shows trip counts. |
| `test_routes_intermediate.csv` | NEW. 11 routes — the production routes **minus the Lahore–Islamabad direct edge**. See §3. |
| `tests.cpp` | MODIFIED. 105 new checks (groups 19–29). 47 KB total. |
| `README.md` | UPDATED. Phase 3c section, trip workflow, test dataset note. |
| `orders.csv`, `delivery_routes.csv` | **NOT modified.** md5 verified after all work: orders `c2e3b6f5f8250dc05b916f71557d639d`, routes `13749ce0933f67aabf204446cb3cf52e`. No `delivery_trips.csv` or `activity_logs.csv` in the project folder (tests ran in `test_work/`, console checks in `/tmp`). |
| `app.py`, proposal files, Phase 1/3a/3b reports | **NOT touched.** |

## 2. How it works

### Graph + RouteResult (requirement 1)
Every road now stores **both** numbers from the CSV: `timeMin` and `distanceKm`. `shortestRoute()` runs the same O(V²) Dijkstra as v1 (weight = time) and then sums the distance edge-by-edge along the chosen path. Example, verified: Lahore→Islamabad **260 min, 320 km**; Lahore→Karachi **900 min, 1210 km**; Lahore→Multan **280 min, 340 km**; Lahore→Faisalabad **150 min, 180 km**.

### Intermediate matching (requirement 2)
`RoutePlanner::findIntermediateOrders(route, orders, excluded)`:
- Intermediates = `route.path[1..n-2]` only. A city counts **only** if it actually appears in the calculated path.
- Eligible = status **Pending or Processing**, city == intermediate city, order ID **not** in `excluded` (orders already assigned to an active trip).
- Returns, per intermediate city, its orders plus the cumulative time/distance from the source to that city.

### Detours (requirement 3)
`calculateDetour(source, destination, detourCity)`:
- `original` = shortest route. `via` = best route that visits the detour city (exact, via the multi-stop method with one stop). `extraTime = via − original`, `extraDistance = via − original`, plus the eligible orders in the detour city.
- Worked numbers on the production data: Lahore→Karachi via Faisalabad **930 min, 1230 km** (extra **+30 min, +20 km**, serves 1006, 1022); via Multan **940 min** (extra +40 min, serves 1026); Lahore→Multan via Faisalabad **300 min** (extra +20 min); Lahore→Islamabad via Faisalabad **440 min** (extra **+180 min** — expensive, shown honestly, not hidden).

### Multiple stops — exact method (requirement 4)
`bestRouteWithStops()` tries **every permutation** of the stops, adds up the shortest segment times/distances for each ordering (`source → stop1 → … → destination`), and picks the smallest total **time**, tie-broken by smallest total **distance**, then lexicographically smallest stop order (deterministic).
- Worked example: Lahore→Karachi with stops {Faisalabad, Multan}: Faisalabad-then-Multan **960 min, 1240 km** beats Multan-then-Faisalabad **1210 min**. Input order does not change the answer.
- Complexity: **O(k! × (k+1) × Dijkstra)**, Dijkstra O(V²). For this dataset V = 6, so k ≤ 4 (every city except source and destination): at most **24 permutations**, trivial. Practical limit documented in `Delivery.h`: k = 6 → 720 permutations (fine); k = 8 → 40,320 (slow); k ≥ 10 is impractical — a real system would switch to a heuristic (nearest neighbour + 2-opt) and say so. The code refuses k > 6 with an error instead of silently running for hours.

### Trips (requirement 5)
`DeliveryTrip {tripId, source, destination, route, assignedOrderIds, totalTimeMin, totalDistanceKm, status}`. Status: **Planned** on creation, **Completed** on explicit completion. (`InProgress` exists as a value for a future phase; Phase 3c does not use it.)
`delivery_trips.csv` format: `trip_id,source,destination,route,assigned_order_ids,time_min,distance_km,status` — route as `Lahore>Faisalabad>Karachi`, order IDs as `1006;1022`. The file is **rewritten atomically** (temp + replace, same rule as orders) because a trip's status changes on completion — append-only would leave two rows for one trip.

### Delivery logging (requirement 6)
Through the existing Activity Log BST, no new log system:
- `ROUTE_GENERATED` — once per `planRoute` / `planDetour` / `planMultiStop` (orderId −1).
- `DELIVERY_SELECTION` — once per trip, listing the selected order IDs.
- `DELIVERY_ASSIGNMENT` — once per assigned order (Processing → Shipped).
- `DELIVERY_COMPLETED` — once per order on completion (Shipped → Delivered).

### Status rules (requirements 7–9)
- **Planning changes nothing.** `planRoute` only reads orders. Verified in tests: status counts unchanged after planning.
- **Assignment requires Processing.** A Pending order is **rejected** with "process it first (option 7)". It is *not* silently moved Pending → Shipped, because that transition does not exist in the transition table. This is the explicit workflow the brief allows ("must first transition through Processing, or the workflow must explicitly require processing before assignment") — Phase 3c takes the second option and says so in the error message, the code, and this report.
- **Assignment is not undoable via the ActionStack.** It is a multi-order transaction; undoing one order's status would leave the trip record listing an order that is no longer Shipped. The trip record, not the undo stack, is the record of the assignment. Documented in `Store.h` and in the tests (`undoCount == 0` after assignment).
- **Completion is explicit only.** Only `completeTrip()` may mark assigned orders Delivered (Shipped → Delivered). Passing through a city during planning or assignment never delivers anything — verified in tests: after assignment the orders are Shipped, not Delivered.
- **No duplicate assignment.** `activeAssignedOrderIds()` collects the order IDs of all Planned/InProgress trips. Matching excludes them, and `createTrip` rejects any selected order already assigned to an active trip. After a trip Completes, its orders are free again (the trip is no longer active).

### Multi-file consistency (requirement 10)
A trip touches three files. Plain CSVs have no true atomic multi-file transaction, so the order is defined in `commitTripWithLogs()` and tested:
1. `orders.csv` (atomic save). On failure: roll back orders + trips in memory, write NO log.
2. `delivery_trips.csv` (atomic save). On failure: roll back orders + trips, **re-save** `orders.csv`, write NO log.
3. Log batch (one file open, all lines). On failure at the open: roll back orders + trips, **re-save both files**, write NO log entries into the tree.

**Crash-recovery limitations (documented, not hidden):**
- If the re-save in step 2/3 also fails, the error says `CRITICAL` explicitly — in-memory state matches the pre-trip snapshot, but the files on disk may disagree. On the next restart the CSVs on disk are the source of truth, so the user must check them.
- If a log batch fails **mid-write** (disk full after the open), some log lines may already be in the file. The log is append-only, so they are not deleted; the orders/trips are still rolled back, so those lines describe an *attempted* trip, not a completed one. The common failure case (missing directory) fails at the open with zero lines written, and that is what the tests simulate.
- A leftover `*.tmp` file means a save was interrupted before the replace — the original CSV is the good copy; delete the `.tmp`, do not load from it.

## 3. Test dataset — genuine intermediate city (requirement 11)

`test_routes_intermediate.csv` is the production routes **minus the Lahore–Islamabad direct edge** (11 routes, not 12). Nothing else changed. On it:
- Lahore→Islamabad is **275 min, 315 km** via Rawalpindi: `Lahore -> Rawalpindi -> Islamabad`.
- Intermediate: **Rawalpindi**, reached in **240 min, 290 km**, with eligible orders **1005** (Usman Tariq, Processing) and **1021** (Talha Iqbal, Processing).
- The production `delivery_routes.csv` is untouched (md5 checked; tests assert it still has 12 routes + header).

The production data remains the required **Example A** (no intermediates): Lahore→Islamabad 260 min direct, Lahore→Karachi 900 min direct, both with `intermediate == []`. The test dataset is **Example B** (genuine intermediate).

## 4. Build and test results — ACTUAL, executed 13 Nov 2026

**Build (this VM, `g++ -std=c++17 -Wall -Wextra`, zero warnings):**
```
g++ -std=c++17 -Wall -Wextra main.cpp Store.cpp -o store   → OK
g++ -std=c++17 -Wall -Wextra tests.cpp Store.cpp -o tests  → OK
```

**Regression tests (`./tests`, exit code 0):**
```
=== Results: 266 passed, 0 failed ===
ALL TESTS PASSED.
```
Old 161 checks all still pass; **105 new checks** in groups 19–29: graph distance + `RouteResult` (incl. unknown city, same-city 0/0); intermediate matching on production data (Example A, destination orders, planning changes nothing, `ROUTE_GENERATED` count); test dataset (275/315, Rawalpindi, 1005/1021, production CSV untouched); detours (+30/+20, +20, +180, invalid detours); multi-stop exact (960/1240, input order irrelevant, single/empty stops, duplicate/unknown/source stops rejected); trip assignment (IDs, route, file lines, log counts, queue, no undo push); assignment rules (Pending rejected, then succeeds after processing, duplicate assignment rejected, empty/missing/duplicate/Delivered/off-route selections rejected); completion (Shipped not Delivered after assignment, Delivered after explicit completion, double-completion and missing-trip rejected); trip persistence across two restarts (`nextTripId` resumes); multi-file rollback (trips-save, log, and orders-save failures each leave order + trip + log unchanged, verified in memory *and* after reload); original datasets preserved.

**First run of the extended suite was 266/266** — no failures to fix.

**Console smoke tests (in `/tmp`, not on the real CSVs):**
- *Production, detour trip:* plan Lahore→Karachi (900 min, 1210 km, no intermediates, Karachi eligible 1011/1016) → detour via Faisalabad (930 min, 1230 km, extra +30/+20, eligible 1006/1022 Pending) → process 1006 → create trip 1 (`Lahore>Faisalabad>Karachi`, 930/1230, Planned) → complete trip 1 (Completed). `delivery_trips.csv` ended as `1,Lahore,Karachi,Lahore>Faisalabad>Karachi,1006,930,1230,Completed`; the log had `ROUTE_GENERATED` ×2, `ORDER_PROCESSED`, `DELIVERY_SELECTION`, `DELIVERY_ASSIGNMENT`, `DELIVERY_COMPLETED`.
- *Test dataset, intermediate trip:* plan Lahore→Islamabad (275 min, 315 km, intermediate Rawalpindi with 1005/1021) → create trip 1 with 1005,1021 → complete. `delivery_trips.csv`: `1,Lahore,Islamabad,Lahore>Rawalpindi>Islamabad,1005;1021,275,315,Completed`.

## 5. Sample delivery outputs

```
Route: Lahore -> Karachi
Time: 900 min, Distance: 1210 km
No intermediate cities on this route.
Destination Karachi eligible orders: 1011 (Hassan Ali, Processing), 1016 (Amna Yousuf, Processing)
Detour via Faisalabad: 930 min, 1230 km (extra 30 min, 20 km)
Eligible in Faisalabad: 1006 (Pending), 1022 (Pending)

Route: Lahore -> Rawalpindi -> Islamabad   [test dataset]
Time: 275 min, Distance: 315 km
Intermediate Rawalpindi (reached in 240 min, 290 km): 1005 (Usman Tariq, Processing), 1021 (Talha Iqbal, Processing)

Trip 1 | Lahore -> Karachi | route Lahore>Faisalabad>Karachi | orders 1006 | 930 min, 1230 km | Planned -> Completed
Trip 1 | Lahore -> Islamabad | route Lahore>Rawalpindi>Islamabad | orders 1005;1021 | 275 min, 315 km | Planned -> Completed
```

## 6. Changes to existing behaviour

- **Graph now stores distance.** `loadRoutes()` reads `distance_km` (column 3) as well as time. No route time changed — Dijkstra still weights by time, so all old route numbers are identical (re-verified by the old tests).
- **Planning now writes a log entry.** `planRoute`/`planDetour`/`planMultiStop` each append one `ROUTE_GENERATED` entry. Planning still changes no order status, but it is no longer a pure read — that is what the brief requires (record route generation).
- **Trip assignment is not undoable** via option 8 (see §2). Single-order undo (cancel/process/status) is unchanged.
- No other Phase 3a/3b behaviour changed: queue still Pending-only, transition table unchanged, benchmark numbers unchanged.

## 7. Regressions / unresolved problems

- **No unresolved bugs found in Phase 3c.** The extended suite passed first run.
- **Not yet done (by design, Phase 3c scope):** no HTTP API, no React frontend. Trip statuses `Planned`/`Completed` only — there is no "start trip" / `InProgress` transition and no trip cancellation in Phase 3c. If a trip is created by mistake, there is no console way to cancel it yet (its orders stay Shipped until completion) — that is a deliberate gap to raise in Phase 4, not a hidden one.
- **Known limits:** multi-stop exact method refuses k > 6 (see §2); log batch mid-write partial lines are possible on disk-full (see §2); the Windows `MoveFileEx` path is still untested on Windows (carried over from Phase 3b — please run the tests there).

## 8. Next implementation step

**Phase 4 — HTTP API + React frontend** (do not start until you approve):
1. Choose an established C++ HTTP library (per your requirement: NOT a hand-rolled parser) — candidates to compare before adopting: `cpp-httplib` (single header, MIT) vs `Crow` vs `Drogon`. Document the choice, then wire the existing `Store` behind it (no new business logic in the server).
2. Endpoints from the Phase 2 plan (`/api/stats`, `/api/orders`, `/api/queue`, `/api/delivery/plan`, `/api/delivery/trips`, `/api/logs`, `/api/benchmark`, …), each verified with `curl` before its React page is built.
3. React + TypeScript + Vite + Tailwind: Dashboard, Orders, Delivery Planner, Activity Logs first (decision D4), then Queue, Reports, Settings.
4. Re-run the full 266-test suite plus new API integration tests; all must pass before Phase 5.
