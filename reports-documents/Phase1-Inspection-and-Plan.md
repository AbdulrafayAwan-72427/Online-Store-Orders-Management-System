# Phase 1 — Inspection Report + Phase 2 Implementation Plan

**Project:** Online Store Orders — Order and Delivery Management System
**Date:** 13 November 2026
**Status:** PHASE 1 ONLY. No existing code was modified. No new code was written.
**Next step:** User approval required before Phase 3 (implementation).

---

## 1. What I actually read

| File | Size / lines | What it is |
|---|---|---|
| `main.cpp` | 705 lines, single file | The working C++ console program. All 5 original DSA classes live here. |
| `orders.csv` | 30 orders, 1001–1030 | Order data, verified again on 13 Nov. |
| `delivery_routes.csv` | 12 routes, 6 cities | City network. |
| `Project-Proposal-Online-Store-Orders.md` (+ .docx / .pdf) | Dated 5 Nov 2026 | Original proposal. No frontend, no multi-order routing, no Activity Log BST. |
| `README.md` | — | Run steps + tested results for the C++ program. |
| `app.py` | 1,200 lines, CustomTkinter | A Python GUI copy of the same system (8 Nov 2026). **Not** the C++ backend. |

No React, TypeScript, Tailwind, HTTP API, database, or activity-log file exists yet. Those are all new work.

---

## 2. What already works — verified, not assumed

C++ (`main.cpp`, compiled clean with `g++ -std=c++17 -Wall -Wextra`, tested 5 Nov):

- **Linked List** `OrderLinkedList` — `insertAtEnd` O(1) with tail pointer, `insertById`, `deleteById`, `searchById` (with step counting), `updateStatus`, `totalSales`, `countStatus`.
- **Queue** `OrderQueue` — real linked-list queue (`front`/`rear`), `enqueue`/`dequeue`/`peek`, O(1). On load, the 11 Pending/Processing orders are enqueued in file order. First processed is 1005.
- **Stack** `ActionStack` — real linked-list stack, `push`/`pop`/`peek`, O(1). Stores `orderId + oldStatus + newStatus`. Cancel 1006 → Undo restores Pending. Tested.
- **Order BST** `OrderBST` — `insert`, `searchById` (with step counting), `deleteById` (all 3 cases), `inorder`. Search 1024 in **5 steps**. 1,000-search test: Linked List **16,400 steps (avg 16.4)**, BST **4,600 steps (avg 4.6)**.
- **Graph** `DeliveryGraph` — adjacency matrix, max 20 cities, `addCity`/`addRoute`, Dijkstra O(V²) with parent array for path reconstruction. Lahore→Islamabad **260 min, direct**. Lahore→Karachi **900 min, direct**. Total sales (not cancelled) **Rs.182,300**.
- **CSV** — `loadOrders`, `loadRoutes`, `saveOrders` (rewrites `orders.csv`). Simple comma split; safe here because no field contains a comma.
- **Balanced-load fix (important for the viva):** IDs in the CSV are sorted 1001–1030. Inserting them in order would make the BST a straight line (O(n)). `insertBalancedBST()` inserts the middle order first, then the middles of the halves, so the loaded tree is balanced. New orders added later are inserted normally, so the tree can slowly unbalance again — that limit is already in the proposal and must stay honest in the new docs.

Python GUI (`app.py`, tested headless 8 Nov): same 5 structures re-implemented in Python, 8 pages, threading for load/compare/Dijkstra, validated forms, log panel. It proves the screens can work, but it is **not** a C++ backend and must not be presented as one.

### Data — re-checked 13 Nov

- Orders: **30**. Lahore 9, Karachi 7, Islamabad 4, Rawalpindi 4, Faisalabad 3, Multan 3.
- Status: Delivered 11, Shipped 6, Processing 6, Pending 5, Cancelled 2. Dates 1–15 Oct 2026.
- Undelivered orders (Pending + Processing + Shipped) by city: Islamabad 2 (1003, 1018), Rawalpindi 3 (1005, 1013, 1021), Faisalabad 2 (1006, 1022), Lahore 5, Karachi 4, Multan 1 (1026).
- Routes: 12 undirected edges, 6 cities. Every Lahore destination has a direct edge.

---

## 3. What is missing — the honest gap list

1. **No web frontend, no API.** `main.cpp` is a console menu (12 options). No HTTP server, no JSON, no React/TypeScript/Tailwind files.
2. **No Activity Log at all.** The only history is the in-memory `ActionStack` (undo). It has no log ID, no timestamp, no actor, no action type, no persistence, and it is *erased* by design when you pop it. Undo currently leaves no audit trail — the new rule (undo must create a new log entry, never erase history) is the opposite of the current behaviour.
3. **No order history.** An `Order` stores only its *current* status. There is no list of past status changes to show on an order-detail page.
4. **No delivery assignment / trip concept.** No trip ID, no assigned orders, no delivery-completion status. A route is printed and forgotten.
5. **No filtering.** Console shows all orders only. No filter by status / city / date. `app.py` has a status filter only.
6. **No charts / analytics.** Report is counts + total sales only. No daily trend (dates exist, 1–15 Oct, so this is computable) and no status-distribution data endpoint.
7. **Graph stores time only, not distance.** `loadRoutes()` reads `travel_time_min` (column 4) and **discards `distance_km` (column 2)**. `DeliveryGraph` has only a `timeMin[][]` matrix. Total distance, extra distance for a detour, and a distance-based route display are impossible without changing this.
8. **`shortestPath()` prints, it does not return.** It returns `void`. A frontend cannot use it. It must return a result object (path, total time, total distance) before any route page can work.
9. **Status transitions are not validated.** Any status can be set from any other status in code. Cancel on an already-Delivered order, processing a Cancelled order from the queue, and double-processing are not blocked in one place. There is no single `changeStatus()` rule.
10. **Queue goes stale.** Cancelling or deleting an order does **not** remove it from `processQueue`. A cancelled order can still be dequeued and processed later. Delete has the same problem, and delete does not push an undo action or a log entry.
11. **No admin / user identity.** No login, no actor ID. Every new log entry needs an actor field, so this must be decided (see §7, Decision D1).
12. **Single file, globals, no tests.** `main.cpp` is one 705-line file with global objects (`orderList`, `orderTree`, `processQueue`, `undoStack`, `graph`). There is no test file. The timing test is a menu option, not a repeatable test suite.

---

## 4. Reuse / Modify / New — the decision table

### Reuse as-is (do not touch the algorithm)

| Component | Reuse |
|---|---|
| `Order` struct + `parseOrder` / `splitCSV` | Yes, as-is. Add no fields to `Order` itself; delivery assignment lives in a separate trip record (see §6). |
| `OrderLinkedList` | Yes, as-is. |
| `OrderQueue` (linked-list FIFO) | Yes, as-is, *plus* a `removeById()` helper in Phase 3 (see §5.2). |
| `ActionStack` | Yes, as-is for undo. |
| `OrderBST` insert / search / delete | Yes, as-is. Purpose unchanged: search orders by ID only. |
| `insertBalancedBST` | Yes, as-is. |
| `compareSearch()` | Yes, as-is; expose it as an API endpoint in Phase 4. |
| `orders.csv` format | Yes, unchanged. `delivery_routes.csv` format unchanged. |
| Proposal §1–§5 (problem, data-first analysis, justification) | Yes, keep; extend, do not rewrite. |

### Modify (same class, new capability — keep the old behaviour working)

| Component | Change | Why |
|---|---|---|
| `DeliveryGraph` | Store **both** `timeMin[][]` and `distanceKm[][]`; load both columns in `loadRoutes()` | Total distance + detour extra distance are required and currently impossible. |
| `DeliveryGraph::shortestPath()` | Change from `void` + `cout` to returning `RouteResult { vector<string> path; int totalTimeMin; int totalDistanceKm; }` | The frontend, the intermediate matcher, and the detour calculator all need the path as data, not printed text. Keep a small `printRoute()` wrapper for the console so the old menu still works. |
| `Order` status changes | Route **every** status change through one new `Store::changeStatus(id, newStatus, actor, reason)` that (a) checks the transition table, (b) updates Linked List **and** BST together, (c) pushes the undo action, (d) writes one Activity Log entry, (e) saves CSVs | Today the same update is copy-pasted in 4 places in `main()`. That is how the queue-stale and undo-missing bugs happened. One path fixes them all. |
| `OrderQueue` | Add `removeById(id)`; `processNext()` must skip Cancelled/Deleted IDs instead of processing them | Fixes §3.10. |
| `main.cpp` structure | Split the single file into headers + `.cpp` files (see §8), but keep `main.cpp` as a thin console menu calling the same `Store` object | The console must keep working as the fallback demo. The API server will use the same `Store`, not a copy. |

### New — C++ (all custom classes, no STL containers for the DSA itself)

| New class / file | Purpose |
|---|---|
| `ActivityLogEntry` + `ActivityLogBST` (`ActivityLog.h/.cpp`) | The second BST. Key = composite `(timestamp, logId)` so inorder traversal = chronological order. Methods exactly as specified: `insertLog()`, `searchLogById()`, `deleteLogById()` (admin maintenance only), `inorderTraversal()`, `searchLogsByTimeRange()`, `displayAllLogs()`. **Separate purpose from `OrderBST`:** Order BST is keyed by `order_id` for fast lookup; Log BST is keyed by time for a chronological audit trail. Same tree shape, different key, different question answered. |
| `ActivityLogStore` / `activity_logs.csv` | Persistent audit record. Every log entry is appended to `activity_logs.csv` on creation and reloaded into the Log BST at startup. The CSV is the truth; the BST is the index. Undo never deletes a row — it appends a new `UNDO` entry. |
| `DeliveryTrip` + `RoutePlanner` (`Delivery.h/.cpp`) | `RouteResult` + `findOrdersOnRoute()` + `calculateDetour()` (see §5). A trip record: `tripId, source, destination, path, totalTime, totalDistance, assignedOrderIds[], status (Planned / InProgress / Completed)`. Stored in `delivery_trips.csv`. |
| `Store` (`Store.h/.cpp`) | One owner object holding the 6 structures + the CSV files. All API/console actions go through it, so the Linked List, BST, Queue, Stack, Log BST and CSVs can never disagree. |
| `server.cpp` (zero-dependency, stdlib sockets only — same approach as the CivicFix C++ API, no new library) | Tiny HTTP API on `http://localhost:8080`. Serves JSON, handles CORS for the Vite dev server, single-threaded to start (same as the existing program). No database. |
| `tests.cpp` | Repeatable test runner (not a menu option): add/search, cancel, FIFO, undo, BST insert/search/delete, Dijkstra, intermediate matching, detour cost, log insert/search, restart persistence, invalid input. Each test prints PASS/FAIL and an exit code. |

### New — Frontend (React + TypeScript + Vite + Tailwind, greenfield)

No frontend files exist, so this is a new `frontend/` folder, not a conversion of `app.py`. `app.py` stays as a Python demo; the React app talks only to the C++ API.

| Page / component | Backend it uses |
|---|---|
| Collapsible sidebar + top bar + layout | — (shell) |
| Dashboard: 7 stat cards, status-distribution chart, daily-trend chart, Recent Orders, Delivery Activity, Recent Logs | `GET /api/stats`, `GET /api/analytics`, `GET /api/orders?limit=5`, `GET /api/logs?limit=5` |
| Orders: table, BST search by ID, filter by status / city / date, Add, Cancel, Update status, Order history drawer | `GET /api/orders`, `GET /api/orders/:id`, `POST /api/orders`, `PATCH /api/orders/:id/status`, `GET /api/orders/:id/history` |
| Processing Queue: queue list, Process Next, Undo | `GET /api/queue`, `POST /api/queue/process-next`, `POST /api/undo` |
| Delivery Planner: source/destination, route + graph view, intermediate orders, detour comparison, assign + complete trip | `POST /api/delivery/plan`, `POST /api/delivery/trips`, `POST /api/delivery/trips/:id/complete` |
| Activity Logs: timeline, search by log ID, date-range + action-type filters, optional BST visualisation | `GET /api/logs`, `GET /api/logs/:id`, `GET /api/logs/tree` |
| Reports & Analytics: status chart, daily trend, search-benchmark table | `GET /api/analytics`, `GET /api/benchmark` |
| Settings: warehouse city, API base URL | Local only, no backend call |

Every page needs loading / empty / error / success states. No hardcoded dashboard numbers — all from the API.

---

## 5. How the smart delivery algorithm will work

### 5.1 The one fact that changes the demo

**On the current CSV, a shortest path from Lahore never has an intermediate city.** Lahore has a direct edge to every other city (Islamabad 260, Rawalpindi 240, Faisalabad 150, Multan 280, Karachi 900), and every direct edge is faster than any two-hop alternative. I verified all 5 Lahore destinations with Dijkstra on 13 Nov: every path is `[Lahore, Destination]`, intermediates `[]`.

So the instructor's example — *Lahore → Islamabad passing through Faisalabad* — is **not** the shortest route on the current data. Via Faisalabad it is 150 + 290 = **440 min**, vs **260 min** direct. The system must never claim Faisalabad is "on the route" for that trip. It must show it as a **detour** (§5.3), with the honest extra cost.

That is not a failure of the algorithm. It is the correct answer for this dataset, and it is exactly what the two required test examples should show:

- **Example A (no intermediate orders):** Lahore → Islamabad, 260 min, path `[Lahore, Islamabad]`, intermediates `[]` → "No additional orders on this route." This is the current data, and it must be shown, not hidden.
- **Example B (intermediate orders, via a detour or a test route):** Lahore → Karachi via Faisalabad, **930 min** vs 900 direct (**+30 min, +? km** — distance must be computed from the new distance matrix), serves Faisalabad orders **1006** (Sana Iqbal, Handbag, Pending) and **1022** (Mahnoor Asif, Earrings, Pending). Or Lahore → Multan via Faisalabad, **300 min** vs 280 direct (**+20 min**), same two Faisalabad orders.

For a true on-route example (intermediates `≠ []` with no detour), Phase 3 will add a **separate test dataset** (e.g. a copy of the routes file with the Lahore–Islamabad direct edge removed, or a new city with no direct Lahore edge). The production CSV stays unchanged; the test proves the matcher works when a route genuinely passes through a city.

### 5.2 Intermediate-city matching (on-route, no detour)

```
1. Load graph from delivery_routes.csv (cities = vertices, roads = weighted
   edges, weight = travel_time_min; also keep distance_km per edge).
2. Run Dijkstra(Lahore, destination) -> RouteResult { path, totalTime, totalDistance }.
   Dijkstra is exact. Path is rebuilt from the parent array, as today.
3. intermediates = path[1 .. n-2]   // exclude source and destination.
   A city counts ONLY if it is in this list. Never add a city because it
   is "nearby" or "on the way" in plain English.
4. For each intermediate city C:
     eligible = orders where order.city == C
                AND order.status in {Pending, Processing}
                AND order.id is not already assigned to another active trip.
     Delivered / Cancelled / already-Shipped orders are NOT eligible.
     (Shipped is excluded: it is already on a vehicle. Pending/Processing
     is the admin's choice to confirm in Phase 3 — see Decision D3.)
5. Return, per intermediate city: city, its orders (id, customer, product,
   price), and the cumulative time/distance when the vehicle reaches it
   (sum of the segments up to that city, from the Dijkstra result).
6. Admin selects 0..n of those orders -> POST /api/delivery/trips.
   Only on confirmation: each selected order gets status Shipped,
   a DeliveryTrip record is created, and one Activity Log entry per
   order (DELIVERY_ASSIGNMENT) is written. Passing through a city does
   NOT mark anything Delivered.
```

Complexity: Dijkstra O(V²) as today (V = 6, trivial) + one Linked List scan per intermediate city O(n) to collect eligible orders, where n = number of orders. Total O(V² + k·n), k = intermediates (≤ 4 here).

### 5.3 Optional detours (off-route city)

For a candidate city X **not** on the shortest path, never guess. Calculate the full cost:

```
original = Dijkstra(source, destination)            // time T0, distance D0
via X    = Dijkstra(source, X) + Dijkstra(X, destination)
           // time T1 = time(source->X) + time(X->destination)
           // distance D1 = distance(source->X) + distance(X->destination)
extraTime = T1 - T0        // can be 0 only if X was already on a shortest path
extraDistance = D1 - D0
```

Show all four numbers: original route, proposed route, extra time, extra distance, plus the orders in X that would be served. The admin decides; the system never auto-adds a detour.

For **multiple** optional stops, the exact problem is a shortest path visiting a set of cities (TSP-like, NP-hard in general). With 6 cities the exact method is feasible and honest: try every ordering of the chosen stops (at most 5! = 120 Dijkstra segment sums) and pick the cheapest that ends at the destination. That is the documented exact method for this dataset size. If the city count ever grows past ~8, switch to the documented heuristic (nearest-neighbour + 2-opt) and say so in the report — do not pretend the heuristic is exact.

Worked detour numbers on the current data (time only; distance needs the §4 change):

| Trip | Original | Via | Proposed | Extra |
|---|---|---|---|---|
| Lahore → Karachi | 900 min | Faisalabad | 150 + 780 = 930 | **+30 min**, serves 1006, 1022 |
| Lahore → Karachi | 900 min | Multan | 280 + 660 = 940 | **+40 min**, serves 1026 |
| Lahore → Multan | 280 min | Faisalabad | 150 + 150 = 300 | **+20 min**, serves 1006, 1022 |
| Lahore → Islamabad | 260 min | Faisalabad | 150 + 290 = 440 | **+180 min**, serves 1006, 1022 — expensive, show it and let the admin say no |

### 5.4 Delivery completion rule (must be explicit)

Passing through a city ≠ delivered. An order becomes Delivered **only** when the admin confirms `POST /api/delivery/trips/:id/complete` (or per-order completion). That writes a `DELIVERY_COMPLETION` log entry and updates the order status through the single `changeStatus()` path. A trip left Planned does not change any order status.

---

## 6. How the Activity Log BST will work

### 6.1 The two BSTs, side by side (for the viva)

| | Order BST (existing) | Activity Log BST (new) |
|---|---|---|
| **Key** | `order_id` (int) | Composite `(timestamp, logId)` — timestamp first, logId breaks ties |
| **Question it answers** | "Where is order 1024?" | "What happened, in order, between 10 Nov and 12 Nov?" |
| **Inorder traversal gives** | Orders sorted by ID | Log entries in chronological order |
| **Insert** | `insert(order)` | `insertLog(entry)` — logId is assigned by the store, never by the caller |
| **Search** | `searchById(order_id)` | `searchLogById(logId)` + `searchLogsByTimeRange(start, end)` (inorder walk, collect entries in range) |
| **Delete** | `deleteById()` (order deleted) | `deleteLogById()` — admin maintenance only, and it deletes from the **index only after** a deliberate admin action; normal Undo never calls it |
| **Worst case** | O(n) if unbalanced (same as today; balanced on load, not guaranteed after many inserts — say this, do not claim O(log n) always) | Same O(n) worst case. Same honesty rule. |
| **Persistence** | `orders.csv` | `activity_logs.csv` — the BST is rebuilt from this file at startup |

### 6.2 Log entry fields (exactly as specified)

```
logId        int, unique, assigned sequentially by Store (1, 2, 3, …; resumes from file on restart)
timestamp    string, ISO format "2026-11-13T16:49:00" (sortable, human-readable)
actor        string, e.g. "admin" for now (see Decision D1)
actionType   enum string: ORDER_CREATED | ORDER_CANCELLED | STATUS_UPDATED |
             ORDER_PROCESSED | UNDO | DELIVERY_ASSIGNMENT | ROUTE_GENERATED |
             DELIVERY_SELECTION | DELIVERY_COMPLETED
orderId      int, -1 if the action has no single order (e.g. ROUTE_GENERATED)
oldStatus    string, "" if none
newStatus    string, "" if none
description  string, e.g. "Order 1006 cancelled by admin; was Pending"
```

### 6.3 Rules that prevent the common mistakes

1. **Log first, then act — or act then log in the same function, never in the UI.** Every `Store::changeStatus`, `addOrder`, `processNext`, `undo`, `planRoute`, `assignTrip`, `completeTrip` writes its log entry inside the same C++ function. The React app never creates log entries.
2. **Undo creates a log, it never deletes one.** Undo pops the `ActionStack` and restores the old status, then appends a new entry with `actionType = UNDO`, `oldStatus = (the status being undone)`, `newStatus = (the restored status)`. The original cancellation entry stays in the tree and in the CSV.
3. **Persistence is append-only in normal use.** `activity_logs.csv` is appended to on every action and fully reloaded at startup. `deleteLogById()` is for authorised maintenance only and must itself be logged (a `LOG_DELETED` entry naming the deleted logId, or it is invisible — which defeats the audit).
4. **Time-range search is an inorder walk**, not a second index. Collect entries where `start <= timestamp <= end`. O(n) in the worst case, O(k + height) to find the start — state this plainly in the docs.

### 6.4 What the log page shows

Chronological timeline (inorder traversal), search by log ID (`searchLogById`), date-range filter (`searchLogsByTimeRange`), action-type filter (in-memory filter on the returned list, or a tree walk with a type predicate — say which one in code), related order details (click a log → `GET /api/orders/:id`), and an optional BST visualisation (the tree structure from `GET /api/logs/tree`, rendered as nested nodes in React — small dataset, so a simple indented tree is enough; do not build a full graph-layout library for 6–60 nodes).

---

## 7. API design (Phase 3/4, for approval — not built yet)

Base: `http://localhost:8080`. JSON everywhere. Single-threaded server to start. CORS enabled for `http://localhost:5173` (Vite).

| Endpoint | Method | What it does (C++ side) |
|---|---|---|
| `/api/health`, `/api/ready` | GET | Server up, CSVs loaded |
| `/api/stats` | GET | Total / Pending / Processing / Shipped / Delivered / Cancelled / total sales |
| `/api/analytics` | GET | Status distribution + daily order counts (1–15 Oct) for the charts |
| `/api/orders` | GET | List, with `?status=&city=&date=` filters |
| `/api/orders/:id` | GET | One order + its history (from the Log BST, filtered by orderId) |
| `/api/orders` | POST | Add order (validated: unique ID, price > 0, known city) |
| `/api/orders/:id/status` | PATCH | `changeStatus()` — validated transition table |
| `/api/orders/:id/cancel` | POST | Cancel (undoable) |
| `/api/orders/:id` | DELETE | Delete (removes from List + BST + Queue) |
| `/api/queue` | GET | Queue contents in FIFO order |
| `/api/queue/process-next` | POST | Dequeue + mark Processing |
| `/api/undo` | POST | Pop Stack, restore status, append UNDO log |
| `/api/delivery/plan` | POST | `{source, destination, detourCity?}` → RouteResult + intermediate orders + detour comparison |
| `/api/delivery/trips` | POST | Create trip from selected orders (confirmation only) |
| `/api/delivery/trips/:id/complete` | POST | Mark trip + its orders Delivered |
| `/api/logs` | GET | Logs, with `?from=&to=&type=&orderId=` |
| `/api/logs/:id` | GET | One log entry |
| `/api/logs/tree` | GET | Log BST structure for visualisation |
| `/api/benchmark` | GET | The 1,000-search List vs BST result |

Status transition table (to be confirmed in Phase 3): Pending → Processing → Shipped → Delivered; Pending/Processing → Cancelled; Delivered/Cancelled are terminal (no further change except Undo). Invalid transitions return HTTP 400 with a message, never a silent change.

### Decisions I need from you before Phase 3 (not blockers for approval, but they change code)

- **D1 — Actor / login:** No login exists today. For the log's `actor` field, is `actor = "admin"` (single admin, no login) acceptable for the semester project, or do you want a simple login first? My recommendation: `"admin"` for now, login as a stated future improvement — do not build auth before the DSA work is done.
- **D2 — Warehouse:** Keep Lahore as the single fixed warehouse (as today), or make it selectable in Settings? Recommendation: keep Lahore fixed in Phase 3, make it a setting in Phase 4 if time allows.
- **D3 — Eligible intermediate orders:** Pending + Processing only, or also Shipped? Recommendation: Pending + Processing only. Shipped is already on a vehicle.
- **D4 — Frontend scope:** Full 8-page React app as specified, or Dashboard + Orders + Delivery + Logs first, Reports/Settings later if time is short? Recommendation: build the 4 core pages first, then Reports/Settings.
- **D5 — `app.py`:** Keep it as a Python demo, or retire it once React is live? Recommendation: keep it, clearly labelled as a demo, so the teacher sees the Python GUI work is not lost.

---

## 8. Proposed new files (Phase 3/4)

```
DSA-Online-Store-Orders/
  main.cpp                  # MODIFY: thin console menu, calls Store (was 705 lines, all-in-one)
  Store.h / Store.cpp       # NEW: owns all 6 structures + CSVs, changeStatus(), validation
  Order.h                   # NEW: Order struct + parse (moved out of main.cpp, unchanged logic)
  LinkedList.h              # NEW: OrderLinkedList (moved, unchanged)
  OrderQueue.h              # NEW: + removeById()
  ActionStack.h             # NEW: unchanged
  OrderBST.h                # NEW: unchanged, purpose unchanged
  ActivityLog.h/.cpp        # NEW: ActivityLogEntry + ActivityLogBST
  DeliveryGraph.h           # MODIFY: store time + distance, return RouteResult
  Delivery.h/.cpp           # NEW: RoutePlanner, DeliveryTrip, intermediate matching, detour
  server.cpp                # NEW: zero-dependency HTTP API (stdlib sockets)
  tests.cpp                 # NEW: repeatable PASS/FAIL test runner
  activity_logs.csv         # NEW: persistent audit log (created on first run)
  delivery_trips.csv        # NEW: trip records
  frontend/                 # NEW: Vite + React + TypeScript + Tailwind
    src/pages/ Dashboard, Orders, Queue, Delivery, Logs, Reports, Settings
    src/components/ Sidebar, TopBar, StatCard, OrderTable, RouteMap, LogTimeline, Charts
    src/api/ client.ts      # fetch wrappers, one per endpoint above
  Phase1-Inspection-and-Plan.md   # THIS FILE (Phase 1/2, no code changed)
```

Dependencies to install (Phase 4 only): Node.js + npm, then `npm create vite@latest frontend -- --template react-ts` and `npm install -D tailwindcss`. No database, no new C++ library (the HTTP server uses only the C++ standard library + sockets, as in the earlier CivicFix C++ API). Python / `customtkinter` is **not** needed for the new system.

Estimated complexity (honest, for a semester project): Backend extensions (Log BST + trips + detours + `Store` + API) is the large piece — roughly 2× the current `main.cpp`. Frontend is the second large piece — 8 pages, but each page is a thin table/form over one or two endpoints. Testing + docs is a full phase, not an afterthought. If time is short, cut Settings and Reports first, never cut the Log BST or the detour calculation — those are the instructor's two new requirements.

---

## 9. Safest implementation order (do not skip steps)

1. **Phase 3a — Backend core, no API yet.** Split `main.cpp` into headers, add `Store` + `changeStatus()` + queue `removeById()`. Re-run the old console tests after every step: 30 orders load, search 1024 in 5 steps, 1,000-search 16,400 vs 4,600, routes 260/900 min, sales Rs.182,300. If any number changes, stop — the refactor broke something.
2. **Phase 3b — Activity Log BST + `activity_logs.csv`.** Wire logging into `Store` only. Test: create/cancel/undo an order, restart the program, logs are still there, undo did not delete the cancel entry.
3. **Phase 3c — Delivery upgrade.** Graph stores distance, `shortestPath()` returns `RouteResult`, intermediate matching, detour calculation, `DeliveryTrip` + `delivery_trips.csv`. Test both required examples: A (Lahore→Islamabad, no intermediates) and B (Lahore→Karachi via Faisalabad, +30 min, orders 1006/1022).
4. **Phase 3d — `tests.cpp`.** Turn the manual checks into repeatable PASS/FAIL tests, including restart persistence and invalid input.
5. **Phase 4 — API + React.** `server.cpp` first (test every endpoint with `curl`), then Dashboard + Orders, then Delivery + Logs, then Queue + Reports + Settings. Never build a page before its endpoint works in `curl`.
6. **Phase 5 — Integration testing.** Frontend + backend together, all 12 test areas from the brief, plus the benchmark comparison.
7. **Phase 6 — Documentation.** Revised proposal (new problem statement, architecture, DSA justification with honest O(n) worst cases, detour + log explanations), final report, and a viva sheet: one line per structure saying where it lives in code and what it does.

**What I have NOT done:** No code was changed, no file was created except this report, no React app was started, no API was built. The existing `main.cpp`, `app.py`, CSVs and proposal are exactly as they were.

---

## 10. Approval

Please confirm one of the options below. I will not start Phase 3 until you approve.

- **Approve as written** — I start with Phase 3a (backend split + `Store`), in the order in §9.
- **Change something first** — tell me what: e.g. actor/login (D1), warehouse selectable (D2), eligible statuses (D3), frontend scope (D4), or keep/retire `app.py` (D5).
