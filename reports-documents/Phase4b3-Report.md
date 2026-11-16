# Phase 4B-3 — Interactive Delivery Planner + Trip Management — Report

Date: 13 Nov 2026. Status: **DONE and tested against the live C++ backend. Phase 4B-4 (Activity Logs, Queue, DSA Performance) NOT started — awaiting approval.**

## 1. Files created / modified

**Backend (Part A)**
- Modified `Store.h` / `Store.cpp`: second `DeliveryGraph testGraph` loaded at startup from `test_routes_intermediate.csv` (same folder as the production routes, if present); `loadTestRoutes()`, `graphForDataset()`, and `planRouteOn` / `planDetourOn` / `planMultiStopOn` (graph-parameterised planning). Existing 3-argument planning signatures delegate to these with the production graph, so `tests.cpp` and `main.cpp` are unchanged in behaviour. Production routes file never modified.
- Modified `server.cpp`: new `GET /api/delivery/graph?dataset=`; `dataset` support on plan / detour / multi-stop; `POST /api/delivery/trips` rejects `dataset=test` with 400.
- Modified `tests.cpp`: new group [33] (14 checks). Modified `test_api.sh`: 16 new checks (graph + dataset mode).
- Modified `API.md`: graph endpoint, dataset parameter, production-only trip creation.

**Frontend (Part B)**
- New `frontend/src/pages/DeliveryPlanner.tsx` (route `/planner`), `frontend/src/pages/Trips.tsx` (route `/trips`).
- Modified `frontend/src/lib/api.ts` (GraphData, PlanResult, DetourResult, MultiStopResult, Trip types + `getGraph`, `getPlan`, `getDetour`, `postMultiStop`, `getTrips`, `createTrip`, `completeTrip`, `cancelTrip`), `frontend/src/App.tsx`, `frontend/src/components/Layout.tsx` (sidebar: Delivery Planner, Delivery Trips), `frontend/package.json` / `package-lock.json` (`@xyflow/react` 12.12.1), `frontend/README.md`.
- No changes to `main.cpp`, `app.py`, any CSV, Dashboard, or Orders page behaviour.

## 2. Graph-endpoint contract

`GET /api/delivery/graph?dataset=production|test` →
`{"dataset":"production","cities":["Lahore",...],"edges":[{"from":"Lahore","to":"Karachi","time_min":900,"distance_km":1210}, ...]}` — one edge per undirected road (i < j), built from the loaded graph matrices. Production: 6 cities, 12 edges. Test: 6 cities, 11 edges (no Lahore–Islamabad direct edge). Invalid dataset → 400 "Unknown dataset: … Use 'production' or 'test'." Test dataset missing → 400 "Test dataset not available".

`dataset` (default `production`) is accepted by the read-only planning endpoints only. Test-mode planning uses the test graph for route AND intermediate/detour/multi-stop matching and still logs one `ROUTE_GENERATED`. **Trip creation is production-only** (`dataset=test` → 400 "Trips can only be created on the production routes.").

## 3. Build results (exact)

- C++ tests: `g++ -std=c++17 -Wall -Wextra tests.cpp Store.cpp -o tests && ./tests` → **313 passed, 0 failed** (299 old + 14 new).
- Server build: `g++ -std=c++17 -Wall -Wextra server.cpp Store.cpp -o server -pthread` → clean, no warnings.
- API suite: `./test_api.sh` → **63 passed, 0 failed** (47 old + 16 new).
- Frontend: `npm run build` (`tsc -b && vite build`) → **clean, zero TypeScript errors**. One cosmetic Vite warning: JS chunk 945 kB > 500 kB (React Flow added bulk) — code-splitting the planner route is an easy follow-up.

## 4. Pages

- **Delivery Planner (`/planner`)**: production/test dataset toggle (test note: "production routes minus the Lahore–Islamabad direct edge — Lahore→Islamabad now goes via Rawalpindi"); React Flow map from `GET /api/delivery/graph` — nodes/cities/edges/weights all from the API, only screen positions are a static layout map (with a default-position fallback); shortest-path and multi-stop path highlighted; node click sets destination; source fixed to Lahore; plan results in three distinct groups ("On the shortest path", "At the destination", "Needs a detour"); detour side-by-side original vs via with "extra cost" wording (never called an improvement); multi-stop with best stop order, path, totals, vs-direct comparison, and the O(k! × Dijkstra) explainer; trip creation gated on ≥1 checked order + all Processing (inline error names the offending order), disabled in test mode with an explanation, success message with trip ID + link to Trips.
- **Trips (`/trips`)**: cards from `GET /api/delivery/trips`, status filter with counts, order IDs resolved to customer/city/status via one `GET /api/orders`, Complete/Cancel confirm dialogs on Planned trips only, re-fetch of trips + orders + stats after mutations, verbatim backend errors, rules note (Processing-only assignment; explicit completion; cancel releases orders to Processing).

## 5. Live verification (real server, `/tmp` data copy — never the project folder)

- Preview: `/` 200, `/planner` 200, `/trips` 200; new bundle 200.
- Graph: production **12 edges**, test **11 edges**; bogus dataset → 400.
- Plan test Lahore→Islamabad: **275 min, Lahore→Rawalpindi→Islamabad, intermediate Rawalpindi serving 1005 + 1021**.
- Detour production Lahore→Karachi via Faisalabad: **extra +30 min / +20 km** (via 930 min / 1230 km). Detour test via Faisalabad: +165 min.
- Multi-stop production Lahore→Karachi via Faisalabad+Multan: **960 min, stop order Faisalabad first**.
- Trip round-trip 1: created trip #1 (1011+1016 → Karachi, 201) → orders Shipped → completed (200) → orders Delivered.
- Trip round-trip 2: created trip #2 (1005+1021 → Islamabad via Rawalpindi, 201) → orders Shipped → cancelled (200) → orders back to Processing.
- Negative checks: non-Processing order → 400 verbatim ("Order 1001 is Delivered, not Processing. Process it first…"); test-dataset trip → 400 "Trips can only be created on the production routes."
- **Bug found and fixed during verification:** the first planner version built trip `stops` from plan intermediates only, so a detour-city order would have been rejected by the backend ("not on the route"). Fixed: the detour city is added to `stops` when any selected order comes from the detour group. Verified at API level: processed Pending order 1006 (Faisalabad) → trip #3 Lahore→Karachi with stops `["Faisalabad"]` → 201, route via Faisalabad 930 min → cancelled → back to Processing. Rebuilt cleanly after the fix.
- I re-verified the finished state myself: project folder clean (no server/tests binaries, no test_work), original CSV md5s unchanged (`orders.csv` c2e3b6f5…, `delivery_routes.csv` 13749ce0…, `test_routes_intermediate.csv` 36c6e1fa…), routes wired in App/Layout, `dist/` built.

## 6. Remaining issues / honest gaps

- **No screenshot and no rendered-page check in a real browser.** No headless browser was available in this environment; the React Flow map, highlighting, and page layouts were verified via build + HTTP 200s + API round-trips only. The servers were deliberately not exposed publicly (no tunnel — API has no auth, localhost-only by design). **The user must open `/planner` and `/trips` in a real browser before demoing** (same caveat as 4B-2).
- JS chunk 945 kB warning (see §3); code-split the planner route later if wanted.
- Cosmetic only: the planner's client-side pre-check message ("Order X is Y — process it first") is close to but not identical to the backend's 400 wording; the backend message is shown verbatim when it fires.
- Trips page calls `getStats()` after mutations (per spec) but discards the result — the Dashboard is what displays stats.
- Windows server build still untested (`-lws2_32` needed).

## 7. How to run

```bash
# Terminal 1 — backend (from the project folder)
g++ -std=c++17 -Wall -Wextra server.cpp Store.cpp -o server -pthread
./server 8080 .
# Terminal 2 — frontend
cd frontend && npm install && npm run dev   # http://localhost:5173
```

## 8. Next step — Phase 4B-4 (needs approval)

Activity Logs (+ BST visualisation if the backend exposes enough — do not invent tree connections), Processing Queue, and DSA Performance pages.
