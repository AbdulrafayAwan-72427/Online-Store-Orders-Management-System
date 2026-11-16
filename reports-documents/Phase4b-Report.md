# Phase 4B-1 + 4B-2 — React Admin Dashboard (Dashboard + Orders) — Report

Date: 13 Nov 2026. Status: **DONE and tested against the live C++ backend. Phase 4B-3 (Delivery Planner + Trips) NOT started — awaiting approval.**

## 0. JSON change that came first (user requirement)

Hand-rolled JSON in `server.cpp` replaced with **nlohmann/json 3.11.3** (vendored `json.hpp`, 920 KB, MIT, `ordered_json` so field order is unchanged). Malformed/non-object/wrong-type bodies return 400 naming the field; a customer name with quote + backslash + `é` round-trips correctly. **C++ suite 299/299, API suite 47/47 after the swap.** Server build now takes ~1–3 min on this VM (two large headers) — compile time only. See `API.md` §1/§4/§9.

## 1. Files created (all under `frontend/`)

`package.json`, `vite.config.ts`, `tsconfig.json`, `tsconfig.node.json`, `index.html`, `.gitignore`, `README.md`, `src/main.tsx`, `src/App.tsx` (routes `/`, `/orders`), `src/index.css`, `src/vite-env.d.ts`, `src/lib/api.ts` (typed client + `ApiError`), `src/components/Layout.tsx` (collapsible sidebar, mobile overlay, health pill polling `GET /api/health` every 15 s, dark-mode toggle), `src/components/ui.tsx` (`StatusBadge`, `Spinner`, `EmptyState`, `ErrorBanner`, `ConfirmDialog`, `Modal`), `src/pages/Dashboard.tsx`, `src/pages/Orders.tsx`.

Stack: React 19.3.1, react-router-dom 7.18.5, Recharts 2.15.4, lucide-react 0.469.0, Vite 6.4.6, TypeScript 5.9.3, **Tailwind v4** (`@tailwindcss/vite` + `@import "tailwindcss"`). Dark mode works (`.dark` class, persisted in `localStorage`). No C++ file, CSV, `API.md`, or test file was modified.

## 2. Build result — PASS

`npm run build` (tsc + vite): **clean, no TypeScript errors** (first run caught 2 unused imports, fixed, re-ran green). Output `dist/`: JS 740 kB (gzip 211 kB), CSS 23 kB, 7.37 s. One cosmetic Vite warning: chunk > 500 kB (Recharts + React in one bundle) — code-split later if wanted.

## 3. Pages

- **Dashboard (`/`)**: 8 stat cards from `GET /api/stats`; status donut + daily-sales bar + orders-per-day line charts from `GET /api/analytics` (Recharts); recent orders (latest 8) from `GET /api/orders`; recent activity (latest 8) from `GET /api/logs`; Refresh button; loading/empty/error states per section; PKR via `en-PK` locale.
- **Orders (`/orders`)**: table from `GET /api/orders` with server-side status/city/date filters; ID search calls `GET /api/orders/:id` (backend BST) with the API "not found" shown as an error + clear-search; detail modal with history; Add Order modal (client validation mirrors the API, server 400 messages shown verbatim); per-row status dropdown offers only valid next statuses; Cancel/Delete use confirm dialogs; after every successful mutation the list is re-fetched; on failure the backend error is shown with **no** optimistic UI update.
- Sidebar lists **only** Dashboard and Orders — no dead links to later pages. `getQueue`/`processNext`/`undo` exist in the API client for later phases but **no button wires them**.

## 4. Live verification (real server, `/tmp` data copy — never the project folder)

- Server rebuilt cleanly and run on 8080; `npm run preview` on 5173: `GET /` → 200, `/orders` → 200, JS/CSS → 200. CORS preflight from `http://localhost:5173` → 204.
- API shapes match the TypeScript types: `GET /api/stats` → 30 orders (5/6/6/11/2), sales Rs. 182,300, queue 5; `GET /api/analytics` → status_counts + 15 daily rows; `GET /api/orders/1024` → Waqas Ahmed, Sneakers, Shipped; `GET /api/logs` → `[]` on fresh data (Dashboard correctly shows the empty state, not fake data).
- Mutation round-trip via curl: create 1031 → 201 Pending; PATCH → Processing → 200; invalid PATCH Processing→Delivered → 400; price 0 → 400; GET 9999 → 404; DELETE 1031 → `{"deleted": 1031}`; GET after delete → 404. Stats back to baseline afterwards.
- I re-verified the build myself: rebuilt the server clean, `GET /api/stats` and `/api/health` returned the expected values, preview served `/` with 200.

## 5. Remaining issues / honest gaps

- **No screenshot and no rendered-page check in a real browser.** The cloud browser runs in a remote VM and cannot reach localhost:5173/8080 (it rejected 127.0.0.1/localhost, and a loopback alias refused the connection). I did **not** expose the servers publicly to work around that — the API has no auth and is localhost-only by design. Rendering is verified via build + served HTML + JS/CSS 200s + live API checks only. **Please open the app yourself and look at it before demoing** (run commands in `frontend/README.md`).
- `node_modules/` and `dist/` are kept for the next phase; `tsconfig.tsbuildinfo` sits in `frontend/`.
- Original CSV md5s unchanged after all work.

## 6. How to run it (full steps in `frontend/README.md`)

```bash
# Terminal 1 — backend (from the project folder; on Windows add -lws2_32, untested there)
g++ -std=c++17 -Wall -Wextra server.cpp Store.cpp -o server -pthread
./server 8080 .
# Terminal 2 — frontend
cd frontend && npm install && npm run dev   # http://localhost:5173
```

## 7. Next step — Phase 4B-3 (needs approval)

Interactive Delivery Planner (graph visualisation, detours, multi-stop, trip creation) + Delivery Trips pages. Then 4B-4 (Activity Logs, Queue, DSA Performance).
