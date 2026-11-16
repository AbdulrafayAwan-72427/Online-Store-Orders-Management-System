#!/bin/bash
# test_api.sh — Phase 4A automated API integration test.
#
# Starts the real C++ server on a test port with a COPY of the data
# in /tmp/api_test (the project folder is never touched), runs curl
# against every endpoint (success + error cases), checks the HTTP
# status code and a piece of the JSON body, then stops the server.
#
# Usage:  ./test_api.sh
# Requires: ./server built (g++ -std=c++17 -Wall -Wextra server.cpp Store.cpp -o server -pthread)
# Exit code: 0 if all checks pass, 1 otherwise.

PORT=18080
BASE="http://127.0.0.1:$PORT/api"
DIR=/tmp/api_test
PASS=0
FAIL=0

# check "name" "expected-code" "actual-code" "body" "grep-pattern"
check() {
    local name="$1" want="$2" got="$3" body="$4" pat="$5"
    if [ "$got" = "$want" ] && echo "$body" | grep -q "$pat"; then
        PASS=$((PASS+1)); echo "  PASS: $name"
    else
        FAIL=$((FAIL+1)); echo "  FAIL: $name (want HTTP $want + '$pat', got HTTP $got, body: $body)"
    fi
}

# pycheck "name" "python-code" — BODY and CODE are passed via env;
# the code must exit 0 for a pass. Used for JSON checks grep cannot do.
pycheck() {
    local name="$1" code="$2"
    if BODY="$BODY" CODE="$CODE" python3 -c "$code" 2>/dev/null; then
        PASS=$((PASS+1)); echo "  PASS: $name"
    else
        FAIL=$((FAIL+1)); echo "  FAIL: $name (HTTP $CODE, body: $BODY)"
    fi
}

start_server() {
    ./server "$PORT" "$DIR" > /tmp/api_server.log 2>&1 &
    SRV=$!
    for _ in $(seq 1 50); do
        if curl -s -o /dev/null "http://127.0.0.1:$PORT/api/health" 2>/dev/null; then return 0; fi
        sleep 0.2
    done
    echo "Server did not start. Log:"; cat /tmp/api_server.log; exit 1
}
stop_server() { kill $SRV 2>/dev/null; wait $SRV 2>/dev/null; }

echo "=== Phase 4A API integration test ==="
rm -rf "$DIR"; mkdir -p "$DIR"
cp orders.csv delivery_routes.csv test_routes_intermediate.csv "$DIR/"
echo "--- Phase 1: production routes ---"
start_server

# Health + stats
BODY=$(curl -s -w "\n%{http_code}" "$BASE/health"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "health returns ok" 200 "$CODE" "$BODY" '"status":"ok"'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/stats"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "stats: 30 orders, Rs.182300 sales" 200 "$CODE" "$BODY" '"total_orders":30'
check "stats: pending 5" 200 "$CODE" "$BODY" '"pending":5'
check "stats: total_sales_pkr 182300" 200 "$CODE" "$BODY" '"total_sales_pkr":182300'

# Orders: list + filters + get + 404
BODY=$(curl -s -w "\n%{http_code}" "$BASE/orders"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "list all orders (30)" 200 "$CODE" "$BODY" '"id":1030'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/orders?status=Pending"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "filter status=Pending" 200 "$CODE" "$BODY" '"id":1006'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/orders?city=Faisalabad"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "filter city=Faisalabad" 200 "$CODE" "$BODY" '"id":1022'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/orders/1024"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "get order 1024 (BST search)" 200 "$CODE" "$BODY" '"customer_name":"Waqas Ahmed"'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/orders/9999"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "get missing order 9999 -> 404" 404 "$CODE" "$BODY" 'not found'

# Create order: success + validation errors
BODY=$(curl -s -w "\n%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"customer_name":"API Test","product":"Test Item","category":"Electronics","price_pkr":5000,"quantity":2,"order_date":"2026-10-16","city":"Lahore"}' "$BASE/orders"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "create order 1031" 201 "$CODE" "$BODY" '"id":1031'
BODY=$(curl -s -w "\n%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"customer_name":"Bad","product":"X","category":"Electronics","price_pkr":0,"quantity":1,"order_date":"2026-10-16","city":"Lahore"}' "$BASE/orders"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "create with price 0 -> 400" 400 "$CODE" "$BODY" 'price_pkr'
BODY=$(curl -s -w "\n%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"product":"X","category":"Electronics","price_pkr":100,"quantity":1,"order_date":"2026-10-16","city":"Lahore"}' "$BASE/orders"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "create missing customer_name -> 400" 400 "$CODE" "$BODY" 'customer_name'

# Status update: valid + invalid transition
BODY=$(curl -s -w "\n%{http_code}" -X PATCH -H 'Content-Type: application/json' -d '{"status":"Processing"}' "$BASE/orders/1012/status"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "1012 Pending -> Processing" 200 "$CODE" "$BODY" '"status":"Processing"'
BODY=$(curl -s -w "\n%{http_code}" -X PATCH -H 'Content-Type: application/json' -d '{"status":"Processing"}' "$BASE/orders/1001/status"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "1001 Delivered -> Processing rejected" 400 "$CODE" "$BODY" 'error'

# Queue: list, process, undo, cancel
BODY=$(curl -s -w "\n%{http_code}" "$BASE/queue"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "queue front is 1006" 200 "$CODE" "$BODY" '"id":1006'
BODY=$(curl -s -w "\n%{http_code}" -X POST "$BASE/queue/process"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "process next (1006)" 200 "$CODE" "$BODY" '"id":1006'
BODY=$(curl -s -w "\n%{http_code}" -X POST "$BASE/undo"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "undo process" 200 "$CODE" "$BODY" '"undone":true'
BODY=$(curl -s -w "\n%{http_code}" -X POST "$BASE/orders/1006/cancel"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "cancel 1006" 200 "$CODE" "$BODY" '"status":"Cancelled"'
BODY=$(curl -s -w "\n%{http_code}" -X POST "$BASE/orders/1006/cancel"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "cancel 1006 again -> 400" 400 "$CODE" "$BODY" 'error'

# Delivery: plan, detour, multi-stop
BODY=$(curl -s -w "\n%{http_code}" "$BASE/delivery/plan?destination=Karachi"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "plan Lahore->Karachi 900 min, 1210 km" 200 "$CODE" "$BODY" '"total_time_min":900'
check "plan Lahore->Karachi distance 1210" 200 "$CODE" "$BODY" '"total_distance_km":1210'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/delivery/detour?destination=Karachi&via=Faisalabad"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "detour via Faisalabad extra +30 min" 200 "$CODE" "$BODY" '"extra_time_min":30'
check "detour via Faisalabad extra +20 km" 200 "$CODE" "$BODY" '"extra_distance_km":20'
BODY=$(curl -s -w "\n%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"source":"Lahore","destination":"Karachi","stops":["Faisalabad","Multan"]}' "$BASE/delivery/multi-stop"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "multi-stop Faisalabad+Multan 960 min" 200 "$CODE" "$BODY" '"total_time_min":960'
check "multi-stop stop order Faisalabad first" 200 "$CODE" "$BODY" '"stop_order":\["Faisalabad","Multan"\]'

# Phase 4B-3: graph endpoint + dataset demo mode
BODY=$(curl -s -w "\n%{http_code}" "$BASE/delivery/graph"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "graph production dataset" 200 "$CODE" "$BODY" '"dataset":"production"'
check "graph production has Lahore-Karachi edge" 200 "$CODE" "$BODY" '"from":"Lahore","to":"Karachi","time_min":900,"distance_km":1210'
EDGES=$(echo "$BODY" | grep -o '"from":' | wc -l)
if [ "$EDGES" = "12" ]; then PASS=$((PASS+1)); echo "  PASS: graph production has 12 edges"; else FAIL=$((FAIL+1)); echo "  FAIL: graph production edges (want 12, got $EDGES)"; fi
CITIES=$(echo "$BODY" | grep -o '"Lahore"' | head -1)
check "graph production cities include Lahore" 200 "$CODE" "$BODY" '"cities":\["Lahore"'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/delivery/graph?dataset=test"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "graph test dataset" 200 "$CODE" "$BODY" '"dataset":"test"'
EDGES=$(echo "$BODY" | grep -o '"from":' | wc -l)
if [ "$EDGES" = "11" ]; then PASS=$((PASS+1)); echo "  PASS: graph test has 11 edges"; else FAIL=$((FAIL+1)); echo "  FAIL: graph test edges (want 11, got $EDGES)"; fi
if echo "$BODY" | grep -q '"from":"Lahore","to":"Islamabad"'; then FAIL=$((FAIL+1)); echo "  FAIL: graph test must NOT have Lahore-Islamabad edge"; else PASS=$((PASS+1)); echo "  PASS: graph test has no Lahore-Islamabad edge"; fi
BODY=$(curl -s -w "\n%{http_code}" "$BASE/delivery/graph?dataset=bogus"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "graph invalid dataset -> 400" 400 "$CODE" "$BODY" 'Unknown dataset'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/delivery/plan?destination=Islamabad&dataset=test"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "plan test: Lahore->Islamabad 275 min" 200 "$CODE" "$BODY" '"total_time_min":275'
check "plan test: intermediate Rawalpindi" 200 "$CODE" "$BODY" '"city":"Rawalpindi"'
check "plan test: intermediate order 1005" 200 "$CODE" "$BODY" '"id":1005'
check "plan test: intermediate order 1021" 200 "$CODE" "$BODY" '"id":1021'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/delivery/plan?destination=Islamabad&dataset=bogus"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "plan invalid dataset -> 400" 400 "$CODE" "$BODY" 'Unknown dataset'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/delivery/detour?destination=Islamabad&via=Faisalabad&dataset=test"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "detour test via Faisalabad extra +165 min" 200 "$CODE" "$BODY" '"extra_time_min":165'
BODY=$(curl -s -w "\n%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"source":"Lahore","destination":"Islamabad","stops":["Faisalabad"],"dataset":"test"}' "$BASE/delivery/multi-stop"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "multi-stop test via Faisalabad 440 min" 200 "$CODE" "$BODY" '"total_time_min":440'
BODY=$(curl -s -w "\n%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"source":"Lahore","destination":"Islamabad","stops":[],"order_ids":[1005],"dataset":"test"}' "$BASE/delivery/trips"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "create trip with dataset=test -> 400" 400 "$CODE" "$BODY" 'production routes'

# Trips: create, duplicate, cancel, re-create, complete
BODY=$(curl -s -w "\n%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"source":"Lahore","destination":"Karachi","stops":[],"order_ids":[1011,1016]}' "$BASE/delivery/trips"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "create trip 1 (1011, 1016)" 201 "$CODE" "$BODY" '"trip_id":1'
BODY=$(curl -s -w "\n%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"source":"Lahore","destination":"Karachi","stops":[],"order_ids":[1011]}' "$BASE/delivery/trips"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "duplicate assignment rejected" 400 "$CODE" "$BODY" 'already assigned'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/delivery/trips"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "list trips (1 trip)" 200 "$CODE" "$BODY" '"trip_id":1'
BODY=$(curl -s -w "\n%{http_code}" -X POST "$BASE/delivery/trips/1/cancel"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "cancel trip 1" 200 "$CODE" "$BODY" '"status":"Cancelled"'
BODY=$(curl -s -w "\n%{http_code}" -X POST "$BASE/delivery/trips/1/cancel"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "cancel trip 1 again -> 400" 400 "$CODE" "$BODY" 'error'
BODY=$(curl -s -w "\n%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"source":"Lahore","destination":"Karachi","stops":[],"order_ids":[1011]}' "$BASE/delivery/trips"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "re-create trip 2 after cancel" 201 "$CODE" "$BODY" '"trip_id":2'
BODY=$(curl -s -w "\n%{http_code}" -X POST "$BASE/delivery/trips/2/complete"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "complete trip 2" 200 "$CODE" "$BODY" '"status":"Completed"'
BODY=$(curl -s -w "\n%{http_code}" -X POST "$BASE/delivery/trips/2/complete"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "complete trip 2 again -> 400" 400 "$CODE" "$BODY" 'error'
BODY=$(curl -s -w "\n%{http_code}" -X POST "$BASE/delivery/trips/2/cancel"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "cancel Completed trip -> 400" 400 "$CODE" "$BODY" 'error'

# Logs, benchmark, analytics, CORS, delete
BODY=$(curl -s -w "\n%{http_code}" "$BASE/logs?action_type=DELIVERY_ASSIGNMENT"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "logs filtered by DELIVERY_ASSIGNMENT" 200 "$CODE" "$BODY" 'DELIVERY_ASSIGNMENT'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/logs/9999"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "get missing log 9999 -> 404" 404 "$CODE" "$BODY" 'not found'

# Phase 4B-4: tree endpoints + log filters + process/undo round-trip.
# Queue state at this point: 1012 was PATCHed to Processing, 1006 was
# processed, undone, then cancelled — so the front is 1017, not 1006.
# The round-trip below therefore uses whatever the front currently is.
BODY=$(curl -s -w "\n%{http_code}" "$BASE/logs/tree"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "logs/tree returns 200 with size" 200 "$CODE" "$BODY" '"size":'
check "logs/tree has a root object" 200 "$CODE" "$BODY" '"root":{'
pycheck "logs/tree root log_id exists, height >= 1" 'import json,os; d=json.loads(os.environ["BODY"]); assert isinstance(d["root"],dict) and "log_id" in d["root"] and d["height"]>=1 and d["size"]>=1'
# Inorder consistency: the first (oldest) entry of GET /api/logs must be
# the leftmost node of the tree.
LOGS_BODY=$(curl -s "$BASE/logs")
TREE_BODY=$(curl -s "$BASE/logs/tree")
if LOGS_BODY="$LOGS_BODY" TREE_BODY="$TREE_BODY" python3 -c 'import json,os; logs=json.loads(os.environ["LOGS_BODY"]); tree=json.loads(os.environ["TREE_BODY"]); node=tree["root"]; assert node is not None
while node["left"] is not None: node=node["left"]
assert logs and logs[0]["log_id"]==node["log_id"]' 2>/dev/null; then
    PASS=$((PASS+1)); echo "  PASS: logs/tree leftmost node matches first log entry"
else
    FAIL=$((FAIL+1)); echo "  FAIL: logs/tree leftmost node does not match first log entry"
fi
BODY=$(curl -s -w "\n%{http_code}" "$BASE/logs?action_type=ORDER_CREATED"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
pycheck "logs?action_type=ORDER_CREATED only returns ORDER_CREATED" 'import json,os; d=json.loads(os.environ["BODY"]); assert len(d)>=1 and all(e["action_type"]=="ORDER_CREATED" for e in d)'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/logs?order_id=1006"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
pycheck "logs?order_id=1006 only returns order 1006" 'import json,os; d=json.loads(os.environ["BODY"]); assert len(d)>=1 and all(e["order_id"]==1006 for e in d)'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/logs?from=2026-11-13T00:00:00&to=2026-11-13T23:59:59"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
pycheck "logs?from/to date range returns 200" 'import os; assert int(os.environ["CODE"])==200'
# Process + undo round-trip on the CURRENT queue front.
FRONT_ID=$(curl -s "$BASE/queue" | python3 -c "import json,sys; print(json.load(sys.stdin)[0]['id'])")
BODY=$(curl -s -w "\n%{http_code}" -X POST "$BASE/queue/process"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "process current queue front ($FRONT_ID)" 200 "$CODE" "$BODY" "\"id\":$FRONT_ID"
check "processed order status is Processing" 200 "$CODE" "$BODY" '"status":"Processing"'
BODY=$(curl -s -w "\n%{http_code}" -X POST "$BASE/undo"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "undo process round-trip" 200 "$CODE" "$BODY" '"undone":true'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/orders/$FRONT_ID"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "round-trip order back to Pending" 200 "$CODE" "$BODY" '"status":"Pending"'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/queue"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
if FRONT_ID="$FRONT_ID" BODY="$BODY" python3 -c 'import json,os; d=json.loads(os.environ["BODY"]); assert d and d[0]["id"]==int(os.environ["FRONT_ID"])' 2>/dev/null; then
    PASS=$((PASS+1)); echo "  PASS: round-trip order back at queue front"
else
    FAIL=$((FAIL+1)); echo "  FAIL: round-trip order not back at queue front (body: $BODY)"
fi
# orders/tree: 1031 still exists here (deleted only at the end of Phase 1),
# so the total is 31, not 30.
BODY=$(curl -s -w "\n%{http_code}" "$BASE/orders/tree"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "orders/tree returns 200 with size 31" 200 "$CODE" "$BODY" '"size":31'
pycheck "orders/tree root exists, height >= 1" 'import json,os; d=json.loads(os.environ["BODY"]); assert isinstance(d["root"],dict) and "order_id" in d["root"] and d["height"]>=1'

# Phase 4B-5: audit=false planning + error wording + tree child check.
LOG0=$(curl -s "$BASE/stats" | python3 -c "import json,sys; print(json.load(sys.stdin)['log_entries'])")
BODY=$(curl -s -w "\n%{http_code}" "$BASE/delivery/plan?destination=Karachi&audit=false"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "plan audit=false returns 200" 200 "$CODE" "$BODY" '"total_time_min":900'
pycheck "plan audit=false has no log_warning" 'import json,os; d=json.loads(os.environ["BODY"]); assert "log_warning" not in d'
LOG1=$(curl -s "$BASE/stats" | python3 -c "import json,sys; print(json.load(sys.stdin)['log_entries'])")
if LOG0="$LOG0" LOG1="$LOG1" python3 -c 'import os; assert int(os.environ["LOG0"])==int(os.environ["LOG1"])' 2>/dev/null; then PASS=$((PASS+1)); echo "  PASS: plan audit=false adds no log entry"; else FAIL=$((FAIL+1)); echo "  FAIL: plan audit=false changed log count ($LOG0 -> $LOG1)"; fi
BODY=$(curl -s -w "\n%{http_code}" "$BASE/delivery/plan?destination=Karachi&audit=maybe"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "plan audit=maybe -> 400 naming audit" 400 "$CODE" "$BODY" "'audit'"
BODY=$(curl -s -w "\n%{http_code}" "$BASE/delivery/plan?destination=Karachi"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "plan audited (default) returns 200" 200 "$CODE" "$BODY" '"total_time_min":900'
LOG2=$(curl -s "$BASE/stats" | python3 -c "import json,sys; print(json.load(sys.stdin)['log_entries'])")
if LOG1="$LOG1" LOG2="$LOG2" python3 -c 'import os; assert int(os.environ["LOG2"])==int(os.environ["LOG1"])+1' 2>/dev/null; then PASS=$((PASS+1)); echo "  PASS: audited plan adds exactly 1 log entry"; else FAIL=$((FAIL+1)); echo "  FAIL: audited plan log count ($LOG1 -> $LOG2)"; fi
BODY=$(curl -s -w "\n%{http_code}" "$BASE/delivery/detour?destination=Karachi&via=Faisalabad&audit=false"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "detour audit=false returns 200" 200 "$CODE" "$BODY" '"extra_time_min":30'
pycheck "detour audit=false has no log_warning" 'import json,os; d=json.loads(os.environ["BODY"]); assert "log_warning" not in d'
LOG3=$(curl -s "$BASE/stats" | python3 -c "import json,sys; print(json.load(sys.stdin)['log_entries'])")
if LOG2="$LOG2" LOG3="$LOG3" python3 -c 'import os; assert int(os.environ["LOG2"])==int(os.environ["LOG3"])' 2>/dev/null; then PASS=$((PASS+1)); echo "  PASS: detour audit=false adds no log entry"; else FAIL=$((FAIL+1)); echo "  FAIL: detour audit=false changed log count ($LOG2 -> $LOG3)"; fi
BODY=$(curl -s -w "\n%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"source":"Lahore","destination":"Karachi","stops":["Faisalabad","Multan"],"audit":false}' "$BASE/delivery/multi-stop"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "multi-stop audit:false returns 200" 200 "$CODE" "$BODY" '"total_time_min":960'
pycheck "multi-stop audit:false has no log_warning" 'import json,os; d=json.loads(os.environ["BODY"]); assert "log_warning" not in d'
LOG4=$(curl -s "$BASE/stats" | python3 -c "import json,sys; print(json.load(sys.stdin)['log_entries'])")
if LOG3="$LOG3" LOG4="$LOG4" python3 -c 'import os; assert int(os.environ["LOG3"])==int(os.environ["LOG4"])' 2>/dev/null; then PASS=$((PASS+1)); echo "  PASS: multi-stop audit:false adds no log entry"; else FAIL=$((FAIL+1)); echo "  FAIL: multi-stop audit:false changed log count ($LOG3 -> $LOG4)"; fi
BODY=$(curl -s -w "\n%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"source":"Lahore","destination":"Karachi","stops":["Faisalabad"],"audit":"no"}' "$BASE/delivery/multi-stop"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "multi-stop audit:string -> 400 naming audit" 400 "$CODE" "$BODY" "'audit'"
# 1017 is Pending at this point (queue round-trip restored it), so
# assigning it must fail with the new message — no console wording.
BODY=$(curl -s -w "\n%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"source":"Lahore","destination":"Karachi","stops":[],"order_ids":[1017]}' "$BASE/delivery/trips"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "assign Pending 1017 -> 400, new wording" 400 "$CODE" "$BODY" 'Process it first, then assign it to a trip.'
pycheck "assign Pending error has no console-menu wording" 'import json,os; d=json.loads(os.environ["BODY"]); assert "option" not in d["error"]'
# Tree check (Phase 4B-5 C): every non-root node appears exactly once
# as some node's left/right child, and the reported height equals an
# independently computed recursive height.
TREE_BODY=$(curl -s "$BASE/logs/tree")
if TREE_BODY="$TREE_BODY" python3 -c 'import json,os
def walk(n, ids, children):
    if n is None: return 0
    ids.append(n["log_id"])
    for c in (n["left"], n["right"]):
        if c is not None: children.append(c["log_id"])
    return 1 + max(walk(n["left"], ids, children), walk(n["right"], ids, children))
t=json.loads(os.environ["TREE_BODY"]); ids=[]; children=[]
h=walk(t["root"], ids, children)
assert t["size"]==len(ids) and t["height"]==h, (t["size"], len(ids), t["height"], h)
assert sorted(children)==sorted(set(ids)-{t["root"]["log_id"]}) and len(children)==len(set(children))' 2>/dev/null; then
    PASS=$((PASS+1)); echo "  PASS: logs/tree children unique + height matches recursive walk"
else
    FAIL=$((FAIL+1)); echo "  FAIL: logs/tree children/height check"
fi

BODY=$(curl -s -w "\n%{http_code}" "$BASE/benchmark"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "benchmark List 16400 steps" 200 "$CODE" "$BODY" '"linked_list_steps":16400'
check "benchmark BST 4600 steps" 200 "$CODE" "$BODY" '"bst_steps":4600'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/analytics"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "analytics has daily trend" 200 "$CODE" "$BODY" '"daily":'
CORS=$(curl -s -i -X OPTIONS "$BASE/orders" | grep -i "access-control-allow-origin" | tr -d '\r')
if echo "$CORS" | grep -q "http://localhost:5173"; then PASS=$((PASS+1)); echo "  PASS: CORS preflight allows localhost:5173"; else FAIL=$((FAIL+1)); echo "  FAIL: CORS preflight (got: $CORS)"; fi
BODY=$(curl -s -w "\n%{http_code}" -X DELETE "$BASE/orders/1031"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "delete order 1031" 200 "$CODE" "$BODY" '"deleted":1031'
BODY=$(curl -s -w "\n%{http_code}" "$BASE/orders/1031"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "1031 gone after delete" 404 "$CODE" "$BODY" 'not found'

stop_server

echo "--- Phase 2: test dataset (genuine intermediate city) ---"
cp test_routes_intermediate.csv "$DIR/delivery_routes.csv"
rm -f "$DIR/activity_logs.csv" "$DIR/delivery_trips.csv"
start_server
BODY=$(curl -s -w "\n%{http_code}" "$BASE/delivery/plan?destination=Islamabad"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "test dataset: Lahore->Islamabad 275 min" 200 "$CODE" "$BODY" '"total_time_min":275'
check "test dataset: intermediate Rawalpindi" 200 "$CODE" "$BODY" '"city":"Rawalpindi"'
check "test dataset: intermediate order 1005" 200 "$CODE" "$BODY" '"id":1005'
BODY=$(curl -s -w "\n%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"source":"Lahore","destination":"Islamabad","stops":[],"order_ids":[1005,1021]}' "$BASE/delivery/trips"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "test dataset: create intermediate trip" 201 "$CODE" "$BODY" '"trip_id":1'
BODY=$(curl -s -w "\n%{http_code}" -X POST "$BASE/delivery/trips/1/complete"); CODE=$(echo "$BODY" | tail -1); BODY=$(echo "$BODY" | head -n -1)
check "test dataset: complete intermediate trip" 200 "$CODE" "$BODY" '"status":"Completed"'
stop_server

echo ""
echo "=== API integration test: $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ]
