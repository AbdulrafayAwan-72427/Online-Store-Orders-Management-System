# Phase 4B-4 — Activity Logs, Processing Queue, DSA Performance — Report

Date: 13 Nov 2026. Status: **DONE and tested against the live C++ backend.**
Phase 4B is now complete (4B-1 Dashboard + Orders, 4B-2, 4B-3 Planner + Trips, 4B-4 Logs + Queue + DSA).

## 1. Files created / modified

**Frontend only — no C++ file, test file, CSV, API.md, Dashboard or Orders page was touched,
and no API contract changed.**

Created:
- `frontend/src/pages/ActivityLogs.tsx` — route `/logs`, sidebar "Activity Logs".
- `frontend/src/pages/Queue.tsx` — route `/queue`, sidebar "Processing Queue".
- `frontend/src/pages/DSA.tsx` — route `/dsa`, sidebar "DSA Performance".
- `frontend/src/components/TreeFlow.tsx` — shared @xyflow/react BST renderer used by the
  Logs page and the DSA page (inorder-x / depth-y layout from the real parent/child links,
  edges only where the backend reports a child, first-40 cap with a visible label).

Modified:
- `frontend/src/lib/api.ts` — `getLogs` now accepts `from`/`to` (URLSearchParams, as before);
  new `getLog(id)`, `getLogTree()`, `getOrderTree()`, `getBenchmark()` + types `LogTreeNode`,
  `LogTree`, `OrderTreeNode`, `OrderTree`, `Benchmark`. Existing `getQueue` / `processNext` /
  `undo` / `getStats` / `getOrders` / `getPlan` / `postMultiStop` reused unchanged.
- `frontend/src/App.tsx` — all 7 pages now `React.lazy` + `Suspense` (route-based code splitting).
- `frontend/src/components/Layout.tsx` — sidebar gained Activity Logs (ScrollText),
  Processing Queue (ListOrdered), DSA Performance (GraduationCap).
- `frontend/src/pages/Trips.tsx` — removed the discarded `getStats()` fetch after mutations
  (and its now-unused import); re-fetch is trips + orders only.
- `frontend/src/pages/DeliveryPlanner.tsx` — client-side pre-check wording only (see §5).
- `frontend/README.md` — new pages, updated Trips/Planner notes, code-splitting note.

## 2. Backend counts (re-run, not copied)

- C++ suite: `g++ -std=c++17 -Wall -Wextra tests.cpp Store.cpp -o /tmp/dsa_tests && /tmp/dsa_tests`
  → **329 passed, 0 failed** (unchanged — no backend code was modified).
- API suite: `./test_api.sh` (server rebuilt first) → **77 passed, 0 failed**.
- Original CSVs md5-unchanged after all verification
  (`orders.csv` c2e3b6f5…, `delivery_routes.csv` 13749ce0…, `test_routes_intermediate.csv` 36c6e1fa…).
  No `server`/`tests` binary left in the project folder.

## 3. Frontend build + chunk sizes

`npm run build` (`tsc -b && vite build`) → **clean, zero TypeScript errors** (one fix during
the build: an unused `EmptyState` import in `DSA.tsx`, removed — no logic change).
Code splitting worked; the 945 kB single chunk from Phase 4B-3 is gone:

| Chunk | Size | Gzip |
|---|---|---|
| `index` (main, shared) | 284.66 kB | 89.74 kB |
| `BarChart` (Recharts, shared by Dashboard + DSA) | 384.38 kB | 103.86 kB |
| `style` (React Flow styles, shared) | 174.96 kB | 55.60 kB |
| Dashboard page | 46.94 kB | 12.30 kB |
| DeliveryPlanner page | 18.87 kB | 5.57 kB |
| Orders page | 15.18 kB | 4.15 kB |
| DSA page | 12.47 kB | 4.21 kB |
| ActivityLogs page | 8.96 kB | 3.02 kB |
| Trips page | 6.16 kB | 2.20 kB |
| Queue page | 5.12 kB | 1.86 kB |
| TreeFlow (shared tree renderer) | 1.95 kB | 1.01 kB |

## 4. Pages

- **Activity Logs (`/logs`)** — both counts labelled: "entries in this list: N" (the filtered
  list) and "log entries (from /api/stats): M". Timeline oldest-first by default (API inorder =
  chronological) with a Newest-first toggle labelled as presentation-only reordering. Each entry
  shows timestamp, actor, action-type badge, order ID (plain text), old → new status, description.
  Filters are server-side via `GET /api/logs` query params (action-type dropdown with all 11 types,
  order-ID input, from/to date inputs with the hint "Compared lexicographically against timestamps,
  as the API does — a date alone matches that day's prefix"). Search-by-log-ID uses
  `GET /api/logs/:id` (single entry shown, API 404 shown as the error, clear button). The BST
  visualisation renders the REAL tree from `GET /api/logs/tree` (inorder-x, depth-y, real edges
  only; first-40 cap with label if size > 40) and states in plain words: "Ordered by (timestamp,
  logId) — inorder traversal IS the timeline. Searching by log ID alone is O(n): the tree is
  ordered by time, not by ID, so there is no shortcut to a log ID." When the log is empty the page
  shows an empty state, no fake data. The page also notes honestly when height == size that the
  tree is right-leaning, not balanced.
- **Processing Queue (`/queue`)** — real FIFO order front → rear from `GET /api/queue`; the front
  row is highlighted with a "NEXT" badge and a "Next to process" line. Each row: 1-based position,
  order ID, customer, city, amount (`formatPkr`), status badge. "Process next order" calls
  `POST /api/queue/process` and shows the processed order (id + new status Processing); "Undo last
  action" calls `POST /api/undo` and shows success; the backend's 409 is shown verbatim
  ("Nothing to undo."). Both buttons are disabled while a request is running; Process is disabled
  on an empty queue. After success the page re-fetches queue + orders + stats. A short explainer
  (labelled as explanation, not data) states: new Pending orders join at the rear, processing takes
  from the front, Undo restores the exact original position (Phase 3b rule).
- **DSA Performance (`/dsa`)** — seven sections (the brief says "six sections" but lists seven
  items; all seven are built). Each names its source:
  1. **Linked List vs BST** — live `GET /api/benchmark` (1,000 searches: List vs BST steps, BST
     search of 1024 steps) as numbers + a Recharts bar chart. "Observed on this dataset (30
     orders)" is kept separate from "Theory: List O(n), BST O(log n) average, O(n) worst case —
     this BST is NOT self-balancing; sorted IDs stay searchable because loading inserts
     middle-first (insertBalancedBST), not because the tree rebalances itself."
  2. **Queue** — live queue size + front order from `GET /api/queue`; FIFO explanation.
  3. **Stack** — no invented undo count: what Undo restores (status + exact queue position), Undo
     appends an UNDO log entry and never erases history, and the sentence "Undo depth is not
     exposed by the API."
  4. **Graph + Dijkstra** — live `GET /api/delivery/plan` Lahore→Karachi (900 min / 1210 km,
     direct) + test-dataset Lahore→Islamabad (275 min via Rawalpindi), link to `/planner`;
     Dijkstra O(V²) as implemented in `DeliveryGraph.h`.
  5. **Activity Log BST** — live size + height from `GET /api/logs/tree`; inorder = timeline;
     O(n) log-ID search note (same wording as the Logs page).
  6. **Order BST visualisation** — `GET /api/orders/tree` rendered with the same TreeFlow
     component (real parent/child edges only); root order id and height shown from the live
     response, never hardcoded.
  7. **Multi-stop** — exact permutation method, O(k! × Dijkstra), max 6 stops; live
     `POST /api/delivery/multi-stop` (Lahore→Karachi, stops Faisalabad+Multan) with the returned
     stop order/path/time/distance shown — 960 was NOT hardcoded.
  No invented measurements anywhere. Note on the page: loading it calls the planning endpoints,
  and each planning call appends one ROUTE_GENERATED log entry, so the log grows by a few entries
  per visit; no order status changes.

## 5. The two fixes (exact wording)

1. **Trips.tsx** — the post-mutation `await getStats().catch(() => undefined);` and its comment
   are removed, and the `getStats` import is removed (nothing else in the file used it).
   Re-fetch is now trips + orders only, via `load()`.
2. **DeliveryPlanner.tsx** — the client-side pre-check changed from
   `Order ${id} is ${o.status} — process it first.`
   to
   `Order ${id} is ${o.status}, not Processing. Process it first, then assign it to a trip.`
   The backend wording in `Store::createTrip` (verified by reading `Store.cpp`) is:
   `Order <id> is <status>, not Processing. Process it first (option 7), then assign it to a trip.`
   The frontend paraphrase uses the backend wording **without the console-specific "(option 7)"
   parenthetical**, because "(option 7)" refers to the console menu and is wrong for the web UI.
   The backend message itself was NOT changed, and when its 400 fires it is still shown verbatim
   (already the case via `errorMessage`). No other Planner behaviour changed.

## 6. Live verification (real server on a /tmp data copy, never the project folder)

- Preview on 5173: `/`, `/logs`, `/queue`, `/dsa`, `/orders`, `/planner`, `/trips` all **200**;
  the main bundle and every lazy chunk (`DSA`, `ActivityLogs`, `Queue`, `TreeFlow`, `BarChart`)
  each returned **200**.
- `GET /api/benchmark`: List **16400** / BST **4600** steps, 1024 in 5 steps.
- `GET /api/orders/tree`: size **30**, height **5**, root order **1015** (matches the brief).
- `GET /api/logs/tree` on the fresh copy: size **0**, height **0**, root `null` — the empty state
  is real (no `activity_logs.csv` was copied, so no entries existed yet).
- Queue round-trip via curl: queue `[1006, 1012, 1017, 1022, 1027]` → process → order **1006
  Processing**, queue `[1012, 1017, 1022, 1027]` → undo → **1006 Pending again and back at the
  front**, queue `[1006, 1012, 1017, 1022, 1027]` — exact-position restore confirmed live.
- After the round-trip, `GET /api/logs/tree`: size **2**, height **2**, root log 1
  (ORDER_PROCESSED, then UNDO) — height == size, i.e. the right-leaning chain the pages describe.
- `GET /api/logs/999` → **404** `{"error":"Log entry 999 not found."}` (the error the Logs page
  search shows).
- Live `POST /api/delivery/multi-stop` Lahore→Karachi via Faisalabad+Multan: stop order
  Faisalabad → Multan, path Lahore → Faisalabad → Multan → Karachi, **960 min / 1240 km**.
- Live plans: production Lahore→Karachi **900 min / 1210 km** (direct); test Lahore→Islamabad
  **275 min** via Rawalpindi. `GET /api/logs?action_type=UNDO` returned 1 entry.
- Cleanup done: server and preview stopped, `/tmp` data copy + built binaries removed,
  `node_modules` + `dist/` kept. No `server`/`tests` binary in the project folder.

## 7. Remaining issues / honest gaps

- **No screenshot and no rendered-page check in a real browser.** No headless browser was
  available in this environment; the React Flow trees, charts, highlighting and page layouts were
  verified via build + HTTP 200s + per-chunk 200s + API round-trips only. The servers were
  deliberately not exposed publicly (API has no auth, localhost-only by design). **The user must
  open `/logs`, `/queue` and `/dsa` in a real browser before demoing** (same caveat as 4B-2/4B-3).
- The DSA page's planning calls grow the activity log by 3 ROUTE_GENERATED entries per load
  (2 plans + 1 multi-stop). That is the Phase 3c audit requirement working as designed, but it
  means the log-tree size/height shown on the Logs page changes every time the DSA page is visited.
- The backend's `Store::createTrip` 400 message still contains the console-menu "(option 7)"
  parenthetical; only the frontend pre-check paraphrase drops it (see §5). Changing the backend
  message was out of scope for this frontend phase.
- The log tree is right-leaning by construction (height == size for append-only sequences); the
  pages say so explicitly rather than presenting it as balanced.
- Windows server build still untested (`-lws2_32` needed), as in all previous phases.
- The brief's "six sections" for the DSA page actually lists seven items; all seven were built
  and are numbered 1–7 in the page.
