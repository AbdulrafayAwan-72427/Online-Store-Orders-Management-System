# Windows setup

Requirements: Windows 10 or later, MinGW-w64 g++ with `mingw32-make`, CMake 3.20 or later, and Node.js 22 or later with npm. For API tests, Git Bash, curl and Python 3 must be available in that shell.

## Build

From the project root:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
```

Executables are `build/bin/server.exe`, `store.exe` and `store_tests.exe`. CTest supplies isolated sample data to the 347 C++ checks.

## Run

Terminal 1, from the project root:

```powershell
.\build\bin\server.exe 8080 .\data\runtime
```

Terminal 2:

```powershell
cd frontend
npm ci --include=dev --include=optional
npm run dev
```

Open http://localhost:5173. The API also permits http://127.0.0.1:5173 and binds only to 127.0.0.1:8080.

CMake creates missing runtime seed files. It never overwrites existing runtime files. Fresh sample data has 30 orders, five Pending orders and order #1006 at the queue front. An existing migrated working dataset can have different counts.

The API stores order changes, activity logs and trips in `data/runtime/`, which Git ignores. Sample fixtures in `data/sample/` and `data/test/` remain separate.

## Console

The console reads CSVs from its working directory:

```powershell
Push-Location .\data\runtime
& ..\..\build\bin\store.exe
Pop-Location
```

## API and frontend checks

From the root in Git Bash:

```bash
bash backend/tests/test_api.sh
```

The script starts its own server on port 18080 and copies the original fixtures into a temporary directory. It runs 93 checks and leaves runtime data untouched.

From `frontend/`:

```powershell
npm run build
npm test
```

## VS Code

Ctrl+Shift+B runs the configured CMake build. The debug configurations launch either the server with the runtime directory argument or the console with the runtime working directory. Use the **CMake: test** task for regression checks. `g++`, `gdb`, `cmake` and `ctest` must be on PATH.

## Troubleshooting

| Symptom | Action |
| --- | --- |
| CMake cannot find MinGW Makefiles tools | Add the MinGW `bin` directory containing `g++` and `mingw32-make` to PATH, then reopen the terminal. |
| Frontend cannot reach the backend | Start `build/bin/server.exe` and check `curl.exe http://127.0.0.1:8080/api/health`. |
| Port 8080 is busy | Stop the previous project server before starting another one. |
| Lightning CSS Windows binary is missing | Run `npm ci --include=dev --include=optional` inside `frontend`, keeping the lockfile. |
| npm blocks an esbuild install script | Review the named package and npm's approval instructions. |

Dependency audit findings require a separate dependency review. Do not use `npm audit fix --force` as a startup fix.
