# Online Store Orders — React Admin Frontend (Phase 4B-1 + 4B-2 + 4B-3 + 4B-4)

React + TypeScript + Vite + Tailwind CSS v4 + React Router + Recharts + lucide-react + @xyflow/react.
The C++17 backend (`server.cpp`, cpp-httplib + nlohmann/json) is the **only** backend:
every number, chart, table row and log entry comes from `http://127.0.0.1:8080/api`.
No business logic or DSA is implemented in this frontend — only fetch + types
(`src/lib/api.ts`).

## Stack / setup

- **Tailwind CSS v4** via the `@tailwindcss/vite` plugin (see `vite.config.ts`
  and the `@import "tailwindcss"` line in `src/index.css`). Dark mode uses the
  `.dark` class on `<html>` (`@custom-variant dark`) toggled from the top bar
  and remembered in `localStorage` (`oso-theme`).
- **React Router** — `/` Dashboard, `/orders` Orders, `/planner` Delivery Planner, `/trips` Delivery Trips,
  `/logs` Activity Logs, `/queue` Processing Queue, `/dsa` DSA Performance. All 7 pages are
  lazy-loaded (`React.lazy` + `Suspense` in `App.tsx`), so each page is its own JS chunk —
  the main bundle is ~285 kB instead of the single ~945 kB chunk of Phase 4B-3.
- **Recharts** for the status donut, daily sales bar chart and orders-per-day
  line chart on the Dashboard.
- **@xyflow/react (React Flow)** for the delivery-network map on the Delivery Planner page.

## How to run

Terminal 1 — backend (from the project folder, `DSA-Online-Store-Orders/`):

```bash
cmake -S . -B build
cmake --build build
./build/bin/server 8080 ./data/runtime
```

The server binds `127.0.0.1:8080` and allows CORS from `http://localhost:5173`.
The build is slow (~1–3 min on this VM) because `json.hpp` + `httplib.h` are
large headers; that is compile time only.

Terminal 2 — frontend (from `frontend/`):

```bash
npm install
npm run dev        # dev server on http://localhost:5173
# or: npm run build && npm run preview -- --port 5173
npm test           # Vitest unit tests for the API client (8 tests)
```

Open http://localhost:5173. The top bar shows **API connected** (green) when
`GET /api/health` succeeds, or **API offline** (red) when the backend is down.
If the backend is down, pages show the backend/network error instead of fake data.

## Windows (MinGW-w64 + g++, PowerShell) — execution PENDING

The target environment is Windows, but this VM has no Windows and no
MinGW cross-compiler, so **nothing below has been run on Windows yet — please
run these exact commands there** (from the project folder in PowerShell):

```powershell
# Console app + regression tests
g++ -std=c++17 -Wall -Wextra main.cpp Store.cpp -o store.exe
.\store.exe
g++ -std=c++17 -Wall -Wextra tests.cpp Store.cpp -o tests.exe
.\tests.exe

# C++ HTTP API server (note -lws2_32: httplib needs Winsock on Windows)
g++ -std=c++17 -Wall -Wextra server.cpp Store.cpp -o server.exe -lws2_32
.\server.exe 8080 .

# Frontend (needs Node.js + npm on Windows)
cd frontend
npm install
npm run dev        # http://localhost:5173
# or: npm run build; npm run preview -- --port 5173
npm test           # Vitest unit tests
```

Expected on Windows: `tests.exe` prints `ALL TESTS PASSED.` (347 checks);
`server.exe` prints `Loaded 30 orders, …` and `API listening on
http://127.0.0.1:8080`; the frontend shows **API connected**. If the server
build fails, the usual cause is a missing `-lws2_32` or an old MinGW without
C++17 `<filesystem>` support — use a current MinGW-w64 (UCRT) build.

## Pages (4B-1 + 4B-2 + 4B-3 + 4B-4)

- **Dashboard (`/`)** — stat cards from `GET /api/stats`; status donut + daily
  sales/orders charts from `GET /api/analytics`; recent orders (latest 8) from
  `GET /api/orders`; recent activity (latest 8) from `GET /api/logs`.
- **Orders (`/orders`)** — full table from `GET /api/orders` with status / city /
  date filters (server-side query params); search by ID calls `GET /api/orders/:id`
  (the backend BST search); row View opens the detail modal with history from
  the same response; Add Order posts to `POST /api/orders`; per-row status
  dropdown offers only the valid next statuses
  (Pending→Processing/Cancelled, Processing→Shipped/Cancelled,
  Shipped→Delivered); Cancel and Delete use confirmation dialogs. After every
  successful mutation the list is re-fetched; on failure the backend error is
  shown and the UI is **not** updated optimistically.
- **Delivery Planner (`/planner`)** — React Flow map of the delivery network from
  `GET /api/delivery/graph?dataset=` (cities, roads and weights all come from the
  API; only the node positions on screen are a fixed layout in the component).
  Source is fixed to Lahore (warehouse); the destination is picked from a dropdown
  or by clicking a city node, and the calculated shortest path is highlighted on
  the map. Dataset toggle: **Production routes** / **Test dataset demo** (test =
  production minus the Lahore–Islamabad direct edge, so Lahore→Islamabad goes via
  Rawalpindi); switching datasets clears every calculated result. **Calculate
  route** calls `GET /api/delivery/plan` and shows the path, total time/distance,
  and eligible orders in three groups — *On the shortest path* (per intermediate
  city, with time/distance to each), *At the destination*, and *Needs a detour*
  (only after a detour is calculated). **Detour** calls `GET /api/delivery/detour`
  for a chosen extra city and shows original vs via side by side with the extra
  cost in minutes/km (a detour is never called an improvement — the extra is
  always ≥ 0 here). **Multi-stop** calls `POST /api/delivery/multi-stop` with the
  ticked cities and shows the best stop order, full path, totals, and a comparison
  against the direct route; the page explains the exact O(k! × Dijkstra) method.
  **Create trip from selected orders** is enabled only with ≥ 1 order ticked, and
  only when every ticked order is Processing — otherwise the page names the
  offending order inline (e.g. "Order 1006 is Pending, not Processing. Process it first, then assign it to a trip");
  the backend's 400 is shown verbatim if it fires (note: the backend message still contains the console-menu
  "(option 7)" parenthetical; the frontend pre-check drops only that part). In test-dataset mode the button is
  disabled with an explanation (trips are production-only). On success the page
  shows the trip ID with a link to `/trips` and re-runs the plan so the order
  lists reflect the new (Shipped) statuses.
- **Delivery Trips (`/trips`)** — cards for every trip from `GET /api/delivery/trips`,
  filterable by status (All / Planned / Completed / Cancelled), with source →
  destination, full route, assigned order IDs resolved to customer/city/status via
  one `GET /api/orders` fetch, total time/distance and a status badge. Planned trips
  have **Complete** and **Cancel** buttons (confirmation dialogs); Completed and
  Cancelled trips show no actions. After every mutation trips + orders are
  re-fetched and backend errors are shown verbatim. The page states the rules:
  only Processing orders can be assigned, completion is explicit, and cancelling
  releases orders back to Processing.
- **Activity Logs (`/logs`)** — timeline from `GET /api/logs` (oldest-first by default, the API's
  inorder order; a Newest-first toggle reorders the display only). Both counts are shown:
  "entries in this list" (the filtered list) and "log entries (from /api/stats)". Filters are
  server-side query params: action type (all 11 types), order ID, date range from/to (compared
  lexicographically against timestamps, as the API does). Search by log ID calls `GET /api/logs/:id`
  (API 404 shown as the error). The Activity Log BST is drawn with React Flow from
  `GET /api/logs/tree` — real nodes and real parent/child edges only, laid out by inorder
  position (x) and depth (y), capped at the first 40 nodes by inorder with a visible label.
  The page states plainly that inorder IS the timeline and that searching by log ID alone is O(n).
- **Processing Queue (`/queue`)** — FIFO list from `GET /api/queue` (front → rear) with the front
  order highlighted as "Next to process" (NEXT badge). Each row: 1-based position, order ID,
  customer, city, amount, status badge. **Process next order** (`POST /api/queue/process`) and
  **Undo last action** (`POST /api/undo`) are both disabled while a request is running; Process is
  disabled on an empty queue; the backend's 409 ("Nothing to undo.") is shown verbatim. After
  success the page re-fetches queue + orders + stats. A short labelled explainer states the FIFO
  rule and the Phase 3b exact-position Undo rule.
- **DSA Performance (`/dsa`)** — seven sections, each naming its source, all numbers live from the
  API: (1) Linked List vs BST from `GET /api/benchmark` (live 1,000-search steps + bar chart,
  observed-vs-theory kept separate, including that the BST is NOT self-balancing); (2) Queue size
  + front order from `GET /api/queue`; (3) Stack/Undo — what it restores, no undo count invented
  ("Undo depth is not exposed by the API."); (4) Graph + Dijkstra from live `GET /api/delivery/plan`
  calls (production Lahore→Karachi, test Lahore→Islamabad via Rawalpindi) with a link to `/planner`;
  (5) Activity Log BST size + height from `GET /api/logs/tree` with the O(n) log-ID note;
  (6) Order BST visualisation from `GET /api/orders/tree` (same React Flow tree component as the
  Logs page, root id + height from the live response); (7) Multi-stop from a live
  `POST /api/delivery/multi-stop` (Lahore→Karachi via Faisalabad+Multan) with the exact O(k! × Dijkstra)
  method. Loading the page appends 3 ROUTE_GENERATED log entries (one per planning call) — the
  Phase 3c audit rule, not a bug.

## API client

`src/lib/api.ts` — typed `fetch` wrapper (`ApiError` carries the backend's
`{"error": "..."}` message and HTTP status). It also exports `getQueue`,
`processNext` and `undo`, now wired by the Queue page, plus the Phase 4B-4 additions:
`getLog` (`GET /api/logs/:id`), `getLogTree` / `getOrderTree` (tree endpoints), `getBenchmark`,
and `getLogs` extended with `from`/`to` query params.

Delivery endpoints used by the Planner and Trips pages (all typed in `api.ts`):

- `GET /api/delivery/graph?dataset=production|test` — `getGraph`
- `GET /api/delivery/plan?source=&destination=&dataset=` — `getPlan`
- `GET /api/delivery/detour?source=&destination=&via=&dataset=` — `getDetour`
- `POST /api/delivery/multi-stop` — `postMultiStop`
- `GET /api/delivery/trips` — `getTrips`
- `POST /api/delivery/trips` — `createTrip` (production only; the backend rejects `dataset: "test"` with 400)
- `POST /api/delivery/trips/:id/complete` — `completeTrip`
- `POST /api/delivery/trips/:id/cancel` — `cancelTrip`
