# Windows Setup Guide — Online Store Orders Management System

DSA semester project: C++17 backend (custom data structures) + React + TypeScript frontend.
Everything runs locally on your laptop. No account, no cloud service, no database needed.

## 0. What you need installed first

| Tool | Check it is installed | Install from |
|---|---|---|
| MinGW-w64 g++ (C++17) | `g++ --version` in PowerShell | https://www.msys2.org/ (then `pacman -S mingw-w64-x86_64-gcc`) or https://winlibs.com/ — make sure `g++` is on your PATH |
| Node.js 20+ (includes npm) | `node --version` and `npm --version` | https://nodejs.org/ |
| Python 3 (only for the API test script's JSON checks) | `python --version` | https://www.python.org/ |

> If `g++` is not recognised in PowerShell, close and reopen PowerShell after
> installing MinGW, or add its `bin` folder to PATH. All commands below assume
> you are in the unzipped project folder.

## 1. Unzip and enter the folder

```powershell
# Unzip Online-Store-DSA-Complete-Project.zip (right-click > Extract All works too),
# then in PowerShell:
cd "D:\path\to\Online-Store-DSA-Complete-Project"
dir
```

You should see `main.cpp`, `Store.cpp`, `server.cpp`, `orders.csv`,
`delivery_routes.csv`, `frontend\`, `README.md`, `API.md`, and the
`Phase*-Report.md` files.

## 2. Run the C++ regression tests (347 checks)

```powershell
g++ -std=c++17 -Wall -Wextra tests.cpp Store.cpp -o tests.exe
.\tests.exe
```

Expected last lines:

```
=== Results: 347 passed, 0 failed ===
ALL TESTS PASSED.
```

Notes:
- The tests work on copies in a temporary `test_work\` folder. They never
  touch your real `orders.csv` / `delivery_routes.csv`.
- If you see warnings, read them — a clean build has none.

## 3. Run the console application (optional, the original menu)

```powershell
g++ -std=c++17 -Wall -Wextra main.cpp Store.cpp -o store.exe
.\store.exe
```

Menu: 1 load orders, 2 display, 3 add, 4 search by ID (BST), 5 cancel,
6 delete, 7 process next (Queue), 8 undo (Stack), 9 fastest route
(Dijkstra), 10 compare search, 11 report, 12 save, 14 activity logs,
15 plan route, 16 create trip, 17 complete trip, 18 show trips, 0 exit.

## 4. Build and run the C++ HTTP API server

```powershell
g++ -std=c++17 -Wall -Wextra -D_WIN32_WINNT=0x0A00 server.cpp Store.cpp -o server.exe -lws2_32
.\server.exe 8080 .
```

- `-lws2_32` is required on Windows (httplib needs Winsock). The Linux
  command uses `-pthread` instead — do not use that here.
- `-D_WIN32_WINNT=0x0A00` tells the compiler you are on Windows 10 or
  later. Without it, MinGW defaults to an older Windows version and
  `httplib.h` stops the build with: `#error "cpp-httplib doesn't support
  Windows 8 or lower. Please use Windows 10 or later."` That error is
  about the compiler's default setting, not your actual Windows version.
  (`server.cpp` also defines it now, so the plain command works too —
  the flag is belt and braces.)
- If the build fails, `server.exe` is **not** created, so `.\server.exe`
  then fails with "not recognized" — that second error just means the
  first command did not succeed. Fix the build, then run the exe.
- First build takes a while (1–3 minutes): `httplib.h` + `json.hpp` are
  large headers. That is compile time only.
- Expected output:

```
Loaded 30 orders, 0 trips, 0 log entries from ..
API listening on http://127.0.0.1:8080 (localhost only)
```

- The server binds to `127.0.0.1` only — it is not reachable from other
  machines. Keep this terminal window open while you use the frontend.
- It serves the API at `http://127.0.0.1:8080/api` (health, stats, orders,
  queue, undo, delivery plan/detour/multi-stop, trips, logs, benchmark,
  graph, trees). Full list in `API.md`.
- The server creates `activity_logs.csv` and `delivery_trips.csv` in this
  folder as you use it. That is normal — they are your live data.
  `orders.csv` is only rewritten when you change an order (atomically,
  via a `.tmp` file + replace).

Test it in a second PowerShell window while the server runs:

```powershell
curl.exe http://127.0.0.1:8080/api/health
curl.exe http://127.0.0.1:8080/api/stats
```

Expected stats: 30 orders — Pending 5, Processing 6, Shipped 6,
Delivered 11, Cancelled 2, total sales Rs. 182,300, queue size 5.

## 5. Run the API integration tests (93 checks)

Keep the server **stopped** for this — the script starts its own server
on a test port with a copy of the data in a temp folder.

```powershell
# Git Bash is the easiest way (ships with Git for Windows).
# In PowerShell, if you have Git Bash:
bash ./test_api.sh
```

Expected last line:

```
=== API integration test: 93 passed, 0 failed ===
```

If you do not have Git Bash, skip this step — the C++ suite in §2 and
the live checks in §4 cover the same backend.

## 6. Install the frontend and start React

```powershell
cd frontend
npm install
npm run dev
```

- `npm install` downloads the dependencies (React, Vite, Tailwind,
  Recharts, React Flow, lucide-react) — needs internet once. This
  creates `node_modules\` (about 300 MB, not included in the ZIP).
- `npm run dev` starts Vite on **http://localhost:5173** — open that
  address in your browser. The API's CORS setting allows exactly that
  origin, so the pages can talk to the C++ server.
- Keep the server from §4 running in its own terminal at the same time.
  If a page shows "Cannot reach the C++ backend", the server is not
  running (or is on the wrong port).

Other useful frontend commands (from the `frontend\` folder):

```powershell
npm run build   # production build (must pass with no TypeScript errors)
npm test        # frontend unit tests (Vitest, 8 tests)
npm run preview # serve the production build on http://localhost:5173
```

## 7. The seven pages

| Page | Address | What it does |
|---|---|---|
| Dashboard | http://localhost:5173/ | Live stats, status + sales charts, recent orders/activity |
| Orders | http://localhost:5173/orders | Search (BST), filters, add, status, cancel, delete |
| Delivery Planner | http://localhost:5173/planner | Interactive city graph, shortest route, detours, multi-stop, trip creation |
| Trips | http://localhost:5173/trips | Planned / Completed / Cancelled trips, complete, cancel |
| Activity Logs | http://localhost:5173/logs | Chronological timeline, filters, log-ID search, BST visualisation |
| Processing Queue | http://localhost:5173/queue | FIFO queue, process next, undo |
| DSA Performance | http://localhost:5173/dsa | Linked List vs BST benchmark, all six structures, complexity notes |

## 8. Starting order (every time)

1. Terminal 1: `.\server.exe 8080 .` (from the project folder)
2. Terminal 2: `cd frontend` then `npm run dev`
3. Browser: http://localhost:5173

## 9. If something goes wrong

| Symptom | Fix |
|---|---|
| `g++` not recognised | Reinstall MinGW-w64 / MSYS2 and add its `bin` folder to PATH, then reopen PowerShell |
| Server build error about Winsock / `ws2_32` | You forgot `-lws2_32` — see §4 |
| `Failed to bind to 127.0.0.1:8080` | Another server is already running — close it, or use another port: `.\server.exe 8081 .` (the frontend expects 8080, so prefer closing the old one) |
| Frontend: "Cannot reach the C++ backend" | Start the server (§4) and check `curl.exe http://127.0.0.1:8080/api/health` works |
| Frontend page is blank | Check the browser console (F12) and that `npm run dev` is still running |
| `npm install` fails | Delete `frontend\node_modules` and `frontend\package-lock.json`, then `npm install` again |
| Tests fail | Paste the exact failure lines — do not edit the CSVs by hand; `orders.csv` / `delivery_routes.csv` must stay as shipped (a backup copy is in `archive\`) |

## 10. What is in this project

- `main.cpp`, `Store.h`, `Store.cpp` — console app + the Store that owns
  all data structures and CSVs.
- `Order.h`, `LinkedList.h`, `OrderQueue.h`, `ActionStack.h`, `OrderBST.h`,
  `DeliveryGraph.h`, `ActivityLog.h`, `Delivery.h` — the hand-written
  custom DSA classes (Linked List, Queue, Stack, Order BST, Graph +
  Dijkstra, Activity Log BST, RoutePlanner, DeliveryTrip).
- `server.cpp`, `httplib.h` (cpp-httplib 0.66.0), `json.hpp`
  (nlohmann/json 3.11.3) — the HTTP API. No business logic is duplicated
  in the frontend.
- `orders.csv` (30 orders), `delivery_routes.csv` (12 routes, 6 cities),
  `test_routes_intermediate.csv` (demo dataset: production routes minus
  the Lahore–Islamabad direct edge).
- `tests.cpp` (347 C++ checks), `test_api.sh` (93 API checks),
  `frontend\src\lib\api.test.ts` (8 frontend tests).
- `Project-Proposal-Online-Store-Orders.md` / `.docx` / `.pdf` — the
  original proposal (dated 5 Nov 2026).
- `Phase1-Inspection-and-Plan.md`, `Phase3a/b/c-Report.md`,
  `Phase4a-Report.md`, `Phase4b-Report.md`, `Phase4b3-Report.md`,
  `Phase4b4-Report.md`, `Phase4b5-Report.md`, `API.md`, `README.md` —
  the full build history and test evidence.
- `app.py` — the separate Python (CustomTkinter) demonstration GUI,
  kept as-is per the project decisions.

## 11. Known limitations (honest)

- Windows execution of the server and tests has **not** been verified by
  the author yet — this guide is the first Windows run. Please report
  the exact output if anything differs.
- No auth / HTTPS — the API is localhost-only by design (single admin
  actor). Do not expose port 8080.
- Crash safety is partial: single-file saves are atomic, multi-file trip
  commits use ordered rollback, but there is no true multi-file atomic
  transaction and no fsync — see `Phase4b5-Report.md` §8.
- The Activity Log BST is right-leaning in practice (height == size);
  the Logs and DSA pages explain why.
