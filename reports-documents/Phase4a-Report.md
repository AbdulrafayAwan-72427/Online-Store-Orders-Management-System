# Phase 4A — C++ HTTP API (cpp-httplib) — Report

Date: 13 Nov 2026. Status: **DONE and tested. React frontend (Phase 4B) NOT started — awaiting approval.**

## 1. Library comparison and choice

Compared cpp-httplib, Crow, and Drogon (full table in `API.md` §1).

**Chosen: cpp-httplib 0.66.0** — single header (`httplib.h`, 843 KB, MIT), zero dependencies for plain HTTP, same `g++` command on Linux/Windows.

- Crow: header-only but needs Asio setup — extra setup on Windows for no benefit at this scale.
- Drogon: full async framework, needs CMake + trantor + jsoncpp — far too heavy for a thin wrapper over `Store`.

Honest limits: no built-in JSON (server uses ~150 lines of hand-rolled JSON read/write in `server.cpp`, handling only the shapes this API uses); no built-in auth (localhost-only by design, decision D1); thread safety comes from one `std::mutex` around every `Store` call, because `Store` is not thread-safe.

## 2. Files created / modified

- **NEW `server.cpp`** (~31 KB): `./server [port] [data_dir]`, defaults 8080 / `.`; binds **127.0.0.1 only**; CORS origin `http://localhost:5173`, preflight → 204; all endpoints are thin `Store` calls — no business logic duplicated.
- **NEW `httplib.h`** (0.66.0, vendored).
- **NEW `test_api.sh`**: automated integration test, starts the real server on port 18080 with data copied to `/tmp/api_test`, project folder never touched.
- **NEW `API.md`**: library comparison, architecture, endpoint table, trip-cancellation rules, JSON, CORS, recovery, testing, limitations.
- **Modified `Store.h` / `Store.cpp`**: `cancelTrip()`, `checkRecovery()`, `nextOrderId()`; `createTrip` validation reordered (see §6).
- **Modified `tests.cpp`**: groups 30–32, 33 new checks (see §4).
- No changes to any DSA header, to `main.cpp`, to `app.py`, or to the original CSVs.

## 3. Endpoint list (22 endpoints)

Base `http://127.0.0.1:8080/api`. Errors are `{"error": "..."}` with 400 / 404 / 409 / 500.

| Method + path | Purpose |
|---|---|
| `GET /api/health` | Up + recovery report (`recovery` object) |
| `GET /api/stats` | Dashboard counts + total sales |
| `GET /api/analytics` | Status counts + daily orders/sales |
| `GET /api/benchmark` | Live List-vs-BST 1,000-search steps |
| `GET /api/orders?status=&city=&date=` | List with filters |
| `GET /api/orders/:id` | One order (BST) + its history |
| `POST /api/orders` | Add order (201) |
| `PATCH /api/orders/:id/status` | Status change, transition table enforced |
| `POST /api/orders/:id/cancel` | Cancel (Pending/Processing only) |
| `DELETE /api/orders/:id` | Delete order |
| `GET /api/queue` | Pending queue, front → rear |
| `POST /api/queue/process` | Process next (FIFO), 409 if empty |
| `POST /api/undo` | Undo last action, 409 if none |
| `GET /api/delivery/plan?source=&destination=` | Route + intermediates + destination orders |
| `GET /api/delivery/detour?source=&destination=&via=` | Original vs via-route, extra time/distance |
| `POST /api/delivery/multi-stop` | Exact best route through stops |
| `GET /api/delivery/trips` | All trips |
| `POST /api/delivery/trips` | Create trip, assignment rules enforced (201) |
| `POST /api/delivery/trips/:id/complete` | Explicit completion only |
| `POST /api/delivery/trips/:id/cancel` | Cancel Planned trip, release orders |
| `GET /api/logs?order_id=&action_type=&from=&to=` | Chronological logs, filters |
| `GET /api/logs/:id` | One log entry (404 if missing) |

## 4. Build and test results (actually run, 13 Nov 2026)

**C++ regression suite — 299 passed, 0 failed, exit 0.**
```
g++ -std=c++17 -Wall -Wextra tests.cpp Store.cpp -o tests && ./tests
=== Results: 299 passed, 0 failed ===
```
266 old checks + 33 new (groups 30–32): trip cancellation, recovery reporting, cancellation rollback.

**Server build — clean, no warnings.**
```
g++ -std=c++17 -Wall -Wextra server.cpp Store.cpp -o server -pthread
```
(Compiling httplib is slow, ~15–55 s on this VM.)

**API integration test — 47 passed, 0 failed.**
```
./test_api.sh
=== API integration test: 47 passed, 0 failed ===
```
Phase 1 (production routes): health, stats, orders list/filter/get, add 1031, invalid status change, cancel, queue, process, undo, plan Lahore→Karachi (900 min / 1210 km), detour via Faisalabad (+30 min / +20 km), multi-stop Faisalabad+Multan (960 min), create trip, duplicate-assignment rejection, cancel trip, re-create after cancel, complete trip, cancel-Completed rejection, logs filter, missing log 404, benchmark (List 16,400 / BST 4,600 steps), analytics, CORS preflight, delete 1031.
Phase 2 (test dataset `test_routes_intermediate.csv`): Lahore→Islamabad 275 min via Rawalpindi, intermediate order 1005, create + complete intermediate trip.

## 5. Sample curl requests / responses (live run, 13 Nov 2026, port 18081, /tmp copy)

```
$ curl http://127.0.0.1:18081/api/health
{"status":"ok","orders":30,"trips":0,"logs":0,"recovery":{"leftover_tmp_files":0,"corrupt_log_lines":0,"corrupt_trip_lines":0,"warnings":[]}}

$ curl http://127.0.0.1:18081/api/stats
{"total_orders":30,"pending":5,"processing":6,"shipped":6,"delivered":11,"cancelled":2,"total_sales_pkr":182300,"queue_size":5,"log_entries":0,"trips":0,"active_trips":0}

$ curl "http://127.0.0.1:18081/api/delivery/plan?source=Lahore&destination=Karachi"
{"source":"Lahore","destination":"Karachi","path":["Lahore","Karachi"],"total_time_min":900,"total_distance_km":1210,"intermediate":[],"destination_orders":[{"id":1011,...,"status":"Processing"},{"id":1016,...,"status":"Processing"}]}

$ curl -X POST http://127.0.0.1:18081/api/delivery/trips -d '{"source":"Lahore","destination":"Karachi","order_ids":[1011,1016]}'
{"trip_id":1,"source":"Lahore","destination":"Karachi","route":["Lahore","Karachi"],"assigned_order_ids":[1011,1016],"time_min":900,"distance_km":1210,"status":"Planned"}

$ curl -X POST http://127.0.0.1:18081/api/delivery/trips/1/cancel
{"trip_id":1,...,"status":"Cancelled"}

$ curl http://127.0.0.1:18081/api/orders/1011
{"order":{"id":1011,...,"status":"Processing"},"history":[{"action_type":"DELIVERY_ASSIGNMENT","old_status":"Processing","new_status":"Shipped",...},{"action_type":"TRIP_CANCELLED","old_status":"Shipped","new_status":"Processing",...}]}
```

## 6. Trip cancellation + the one API-test failure found and fixed

**Cancellation rules (`Store::cancelTrip`, `POST /api/delivery/trips/:id/cancel`):** only a Planned trip can be cancelled (Completed → 400 "already Completed", Cancelled → 400). Each assigned order is released **Shipped → Processing** — its exact pre-assignment status, a deliberate reversal like Undo (no forward Shipped→Processing transition exists). Logged as `TRIP_CANCELLED` once for the trip + once per order. A cancelled trip is not active, so released orders can be assigned to a new trip (tested: cancel trip 1, re-create as trip 2). Committed via the same `commitTripWithLogs` multi-file rule as creation/completion.

**Failure found in the first `test_api.sh` run (46 passed, 1 failed):** the duplicate-assignment check expected HTTP 400 with "already assigned", but got HTTP 400 with "Order 1011 is Shipped, not Processing. Process it first…". Cause: `Store::createTrip` checked order status **before** the duplicate-assignment check, so an already-assigned (Shipped) order hit the wrong error message. The rejection itself was correct; only the reason was wrong. **Fix (verified present in `Store.cpp`):** the duplicate-assignment check (`isOrderAssignedToActiveTrip`, "Requirement 9 first") now runs before the Processing-status check, then the on-route check. Re-ran: **47 passed, 0 failed.**

## 7. Crash recovery — what is and is not claimed

Implemented and tested (not full transaction safety — plain CSVs have no true multi-file atomic transaction, and this is stated, not hidden):

- `Store::checkRecovery()` reports, never auto-fixes: leftover `.tmp` files, corrupt log/trip lines skipped on load, trip/order inconsistencies (Planned trip whose order is not Shipped, Completed trip whose order is not Delivered, order on more than one active trip, trip listing a missing order). Exposed in `GET /api/health` under `recovery`, printed at server startup.
- Commit order orders → trips → logs, with rollback + re-save on any failure and an explicit `CRITICAL` error if the re-save also fails. All three failure directions tested in the C++ suite (groups 30–32) and in Phase 3c.
- Queue needs no recovery: rebuilt Pending-only from `orders.csv` on every load.
- Limitation: a mid-batch log-append failure (e.g. disk full) can leave partial log lines describing an attempted trip, not a completed one — documented in `Store.cpp` (`appendLogEntries`).

## 8. Unresolved issues / limitations

- **Windows server build NOT tested** (no Windows environment on this VM). Code uses only httplib + standard C++ + the same `MoveFileEx` path as Phases 3b/3c. Windows command: `g++ -std=c++17 -Wall -Wextra server.cpp Store.cpp -o server.exe -lws2_32` (httplib needs Winsock). **Please run the server + tests on Windows.**
- Hand-rolled JSON is a documented limitation (§1); it handles only the shapes this API uses.
- No auth, no HTTPS, no rate limiting — localhost only, by design (D1). Do not expose port 8080.
- `plan`/`detour`/`multi-stop` are GETs but append one `ROUTE_GENERATED` log entry each (Phase 3c audit rule). If that log write fails, the route is still returned with a `"log_warning"` field.
- Original CSVs verified unchanged after all work: `orders.csv` md5 `c2e3b6f5f8250dc05b916f71557d639d`, `delivery_routes.csv` md5 `13749ce0933f67aabf204446cb3cf52e`, `test_routes_intermediate.csv` md5 `36c6e1fa7b355498dc81f1e9923dd696`. No `delivery_trips.csv` / `activity_logs.csv` / binaries left in the project folder.

## 9. Next step — Phase 4B (needs approval)

React + TypeScript + Vite + Tailwind admin panel (Dashboard, Orders, Delivery Planner, Activity Logs first per D4), consuming this API only — no business logic in the frontend. **Do not start until approved.**
