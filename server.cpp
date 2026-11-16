// server.cpp — Phase 4A (NEW file), Phase 4B JSON update
//
// C++ HTTP API for the Online Store Orders system.
//
//   ./server [port] [data_dir]     (defaults: 8080, ".")
//
// The server is a thin translator: HTTP + JSON in, Store method out,
// JSON back. ALL business logic lives in Store / RoutePlanner
// (Phases 3a-3c) — nothing is duplicated here, and nothing is
// re-implemented in JavaScript or Python. The console (main.cpp)
// and this server share the same Store code and the same CSV files.
//
// Library: cpp-httplib 0.66.0 (httplib.h, single header, MIT, no
// dependencies for plain HTTP). See API.md for the comparison with
// Crow and Drogon and the reasons for the choice.
//
// JSON: nlohmann/json 3.11.3 (json.hpp, single header, MIT), used
// for ALL output and input parsing (Phase 4B). The hand-rolled
// JSON writer/reader from Phase 4A was removed: parsing,
// escaping (quotes, backslashes, control characters, unicode)
// and type checking are now the library's job, not ours.
// ordered_json is used so response fields keep the order they
// are inserted in (the same order Phase 4A produced).
//
// Safety: the server binds to 127.0.0.1 ONLY, CORS allows only
// http://localhost:5173 (the Vite dev server), and every Store call
// is protected by one mutex (Store is not thread-safe; httplib
// handles requests on a thread pool).

#include <algorithm>
#include <cctype>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

// Windows: cpp-httplib 0.66 requires Windows 10+ (_WIN32_WINNT >= 0x0A00).
// MinGW-w64 defaults _WIN32_WINNT to an older Windows version, so
// httplib.h fails with #error even on Windows 10/11. Force it here,
// before httplib.h is included. Harmless on Linux (guarded by _WIN32).
// The same define can also be passed on the command line:
//   g++ ... -D_WIN32_WINNT=0x0A00 server.cpp Store.cpp -o server.exe -lws2_32
#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#undef WINVER
#define WINVER 0x0A00
#endif

#include "httplib.h"
#include "json.hpp"
#include "Store.h"

using json = nlohmann::ordered_json;

static const char *kCorsOrigin = "http://localhost:5173";
// The Vite dev server can be opened as http://localhost:5173 OR
// http://127.0.0.1:5173 — same server, but the browser treats them as
// different origins. If the API only allows one, opening the other
// makes every fetch fail with a CORS error and the app reports
// "API offline" even though the server is running. So both are allowed
// (see the post-routing handler below, which echoes the request's
// origin when it is one of these two).
static const char *kCorsOrigins[] = {"http://localhost:5173", "http://127.0.0.1:5173"};

// ------------------------------------------------------------------
// JSON helpers (nlohmann/json)
// ------------------------------------------------------------------
static json orderToJson(const Order &o) {
    return json{
        {"id", o.id},
        {"customer_name", o.customer},
        {"product", o.product},
        {"category", o.category},
        {"price_pkr", o.price},
        {"quantity", o.quantity},
        {"order_date", o.date},
        {"city", o.city},
        {"status", o.status},
    };
}

static json tripToJson(const DeliveryTrip &tr) {
    return json{
        {"trip_id", tr.tripId},
        {"source", tr.source},
        {"destination", tr.destination},
        {"route", tr.route},
        {"assigned_order_ids", tr.assignedOrderIds},
        {"time_min", tr.totalTimeMin},
        {"distance_km", tr.totalDistanceKm},
        {"status", tr.status},
    };
}

static json logToJson(const ActivityLogEntry &e) {
    return json{
        {"log_id", e.logId},
        {"timestamp", e.timestamp},
        {"actor", e.actor},
        {"action_type", e.actionType},
        {"order_id", e.orderId},
        {"old_status", e.oldStatus},
        {"new_status", e.newStatus},
        {"description", e.description},
    };
}

// Phase 4B-4 — recursive tree serialisation for the Logs / Orders
// pages (GET /api/logs/tree, GET /api/orders/tree). The whole tree
// is returned — the dataset is small (30 orders, tens of log
// entries) — and height is a real recursive walk: 0 for an empty
// tree, 1 for a single node. A null child serialises as JSON null.
static json logNodeToJson(const LogNode *n) {
    if (!n) return nullptr;
    return json{
        {"log_id", n->data.logId},
        {"timestamp", n->data.timestamp},
        {"action_type", n->data.actionType},
        {"order_id", n->data.orderId},
        {"left", logNodeToJson(n->left)},
        {"right", logNodeToJson(n->right)},
    };
}
static int logTreeHeight(const LogNode *n) {
    if (!n) return 0;
    return 1 + std::max(logTreeHeight(n->left), logTreeHeight(n->right));
}
static json orderNodeToJson(const TreeNode *n) {
    if (!n) return nullptr;
    return json{
        {"order_id", n->data.id},
        {"customer_name", n->data.customer},
        {"city", n->data.city},
        {"status", n->data.status},
        {"left", orderNodeToJson(n->left)},
        {"right", orderNodeToJson(n->right)},
    };
}
static int orderTreeHeight(const TreeNode *n) {
    if (!n) return 0;
    return 1 + std::max(orderTreeHeight(n->left), orderTreeHeight(n->right));
}

// Parse a request body. Returns false if the body is not valid JSON
// or is not a JSON object (a bare array, string, number, or garbage
// must never reach the field readers). Callers turn a false here
// into a 400 naming the endpoint's first required field.
static bool parseBody(const std::string &body, json &out) {
    try {
        out = json::parse(body);
    } catch (...) {
        return false;
    }
    return out.is_object();
}

// Typed field readers. Each returns false when the field is missing
// or the wrong JSON type — the caller reports 400 naming the field.
static bool getStringField(const json &body, const std::string &key, std::string &out) {
    if (!body.contains(key) || !body[key].is_string()) return false;
    out = body[key].get<std::string>();
    return true;
}
static bool getIntField(const json &body, const std::string &key, int &out) {
    if (!body.contains(key)) return false;
    const json &v = body[key];
    if (!v.is_number_integer() && !v.is_number_unsigned()) return false;
    try {
        out = v.get<int>();
    } catch (...) {
        return false;
    }
    return true;
}
static bool getIntArray(const json &body, const std::string &key, std::vector<int> &out) {
    if (!body.contains(key) || !body[key].is_array()) return false;
    out.clear();
    for (const json &v : body[key]) {
        if (!v.is_number_integer() && !v.is_number_unsigned()) return false;
        try {
            out.push_back(v.get<int>());
        } catch (...) {
            return false;
        }
    }
    return true;
}
static bool getStringArray(const json &body, const std::string &key, std::vector<std::string> &out) {
    if (!body.contains(key) || !body[key].is_array()) return false;
    out.clear();
    for (const json &v : body[key]) {
        if (!v.is_string()) return false;
        out.push_back(v.get<std::string>());
    }
    return true;
}
static bool isValidDate(const std::string &d) {
    if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
    for (size_t i = 0; i < d.size(); i++) {
        if (i == 4 || i == 7) continue;
        if (!std::isdigit(static_cast<unsigned char>(d[i]))) return false;
    }
    int m = std::stoi(d.substr(5, 2));
    int day = std::stoi(d.substr(8, 2));
    return m >= 1 && m <= 12 && day >= 1 && day <= 31;
}
static bool parseId(const std::string &s, int &id) {
    if (s.empty()) return false;
    try {
        size_t used = 0;
        id = std::stoi(s, &used);
        return used == s.size();
    } catch (...) { return false; }
}

// ------------------------------------------------------------------
// Response helpers
// ------------------------------------------------------------------
static void addCors(httplib::Response &res) {
    // NOTE: Access-Control-Allow-Origin is NOT set here. It is set by
    // the post-routing handler in main(), which echoes the request's
    // Origin when it is an allowed Vite origin. Setting it in both
    // places would send TWO Allow-Origin headers, and the browser
    // rejects a response with more than one.
    res.set_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}
static void sendJson(httplib::Response &res, int status, const json &body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
    addCors(res);
}
static void err(httplib::Response &res, int status, const std::string &msg) {
    sendJson(res, status, json{{"error", msg}});
}

// Phase 4B-3 — dataset selector for the read-only planning
// endpoints. Empty means production (the default). Returns false
// (after sending the 400) for an unknown dataset or for "test"
// when the test graph did not load; on success `g` points at the
// chosen graph and `datasetOut` holds its canonical name.
static bool resolveDataset(Store &store, const std::string &datasetParam,
                           httplib::Response &res, const DeliveryGraph *&g,
                           std::string &datasetOut) {
    std::string ds = datasetParam.empty() ? "production" : datasetParam;
    if (ds != "production" && ds != "test") {
        err(res, 400, "Unknown dataset: " + ds + ". Use 'production' or 'test'.");
        return false;
    }
    g = store.graphForDataset(ds);
    if (!g) {
        err(res, 400, "Test dataset not available (test_routes_intermediate.csv was not found next to the routes file).");
        return false;
    }
    datasetOut = ds;
    return true;
}

// Phase 4B-5 — audit flag for the read-only planning endpoints.
// Absent means audited (true, the historical behaviour). Accepts
// only "true" / "false"; anything else is a 400 naming the parameter.
static bool parseAuditParam(const std::string &auditParam,
                            httplib::Response &res, bool &auditOut) {
    if (auditParam.empty() || auditParam == "true") { auditOut = true; return true; }
    if (auditParam == "false") { auditOut = false; return true; }
    err(res, 400, "Query parameter 'audit' must be 'true' or 'false'.");
    return false;
}

// ------------------------------------------------------------------
// main
// ------------------------------------------------------------------
int main(int argc, char *argv[]) {
    int port = 8080;
    std::string dataDir = ".";
    if (argc > 1) port = std::stoi(argv[1]);
    if (argc > 2) dataDir = argv[2];

    Store store;
    std::string sep = (dataDir.empty() || dataDir.back() == '/') ? "" : "/";
    store.ordersPath = dataDir + sep + "orders.csv";
    store.routesPath = dataDir + sep + "delivery_routes.csv";
    store.logsPath = dataDir + sep + "activity_logs.csv";
    store.tripsPath = dataDir + sep + "delivery_trips.csv";
    if (!store.loadAll()) {
        std::cerr << "Failed to load data from " << dataDir << "\n";
        return 1;
    }
    Store::RecoveryReport rec = store.checkRecovery();
    std::cout << "Loaded " << store.report().total << " orders, "
              << store.report().tripCount << " trips, "
              << store.report().logSize << " log entries from " << dataDir << ".\n";
    if (!rec.warnings.empty()) {
        std::cout << "Recovery warnings:\n";
        for (const std::string &w : rec.warnings) std::cout << "  - " << w << "\n";
    }
    std::cout << "API listening on http://127.0.0.1:" << port << " (localhost only)\n";

    std::mutex mu;
    httplib::Server svr;
    // httplib 0.66 manages its own thread pool; Store is guarded by mu below.

    // CORS: after every response, set Access-Control-Allow-Origin to
    // the request's Origin when it is one of the allowed Vite origins
    // (localhost or 127.0.0.1 on port 5173), and to the default
    // (localhost:5173) otherwise. This is the ONLY place that header
    // is set — setting it in the handlers too would send two of them
    // and the browser would reject the response.
    svr.set_post_routing_handler([](const httplib::Request &req, httplib::Response &res) {
        std::string origin = req.get_header_value("Origin");
        for (const char *allowed : kCorsOrigins) {
            if (origin == allowed) {
                res.set_header("Access-Control-Allow-Origin", origin);
                return;
            }
        }
        res.set_header("Access-Control-Allow-Origin", kCorsOrigin);
    });

    // Preflight for any /api path.
    svr.Options("/api/.*", [](const httplib::Request &, httplib::Response &res) {
        res.status = 204;
        addCors(res);
    });

    // -- Health -------------------------------------------------------
    svr.Get("/api/health", [&](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        Report r = store.report();
        Store::RecoveryReport rr = store.checkRecovery();
        json recovery = json{
            {"leftover_tmp_files", rr.leftoverTmpFiles},
            {"corrupt_log_lines", rr.corruptLogLines},
            {"corrupt_trip_lines", rr.corruptTripLines},
            {"warnings", rr.warnings},
        };
        sendJson(res, 200, json{
            {"status", "ok"},
            {"orders", r.total},
            {"trips", r.tripCount},
            {"logs", r.logSize},
            {"recovery", recovery},
        });
    });

    // -- Dashboard stats + analytics ----------------------------------
    svr.Get("/api/stats", [&](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        Report r = store.report();
        sendJson(res, 200, json{
            {"total_orders", r.total},
            {"pending", r.pending},
            {"processing", r.processing},
            {"shipped", r.shipped},
            {"delivered", r.delivered},
            {"cancelled", r.cancelled},
            {"total_sales_pkr", r.totalSales},
            {"queue_size", r.queueSize},
            {"log_entries", r.logSize},
            {"trips", r.tripCount},
            {"active_trips", r.activeTripCount},
        });
    });

    svr.Get("/api/analytics", [&](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        std::vector<Order> orders = store.allOrders();
        // Daily trend: date -> (count, sales). Small dataset, simple loop.
        std::vector<std::string> dates;
        std::vector<int> counts;
        std::vector<long long> sales;
        for (const Order &o : orders) {
            if (o.status == "Cancelled") continue;
            size_t i = 0;
            while (i < dates.size() && dates[i] != o.date) i++;
            if (i == dates.size()) { dates.push_back(o.date); counts.push_back(0); sales.push_back(0); }
            counts[i]++;
            sales[i] += (long long)o.price * o.quantity;
        }
        json daily = json::array();
        for (size_t i = 0; i < dates.size(); i++) {
            daily.push_back(json{
                {"date", dates[i]},
                {"orders", counts[i]},
                {"sales_pkr", sales[i]},
            });
        }
        Report r = store.report();
        sendJson(res, 200, json{
            {"status_counts", json{
                {"pending", r.pending},
                {"processing", r.processing},
                {"shipped", r.shipped},
                {"delivered", r.delivered},
                {"cancelled", r.cancelled},
            }},
            {"daily", daily},
        });
    });

    svr.Get("/api/benchmark", [&](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        const int ids[5] = {1001, 1010, 1024, 1030, 1017};
        int listSteps = 0, bstSteps = 0;
        for (int rep = 0; rep < 200; rep++) {
            for (int id : ids) {
                int s = 0;
                store.orderList.searchById(id, s);
                listSteps += s;
                s = 0;
                store.orderTree.searchById(id, s);
                bstSteps += s;
            }
        }
        int steps1024 = 0;
        store.orderTree.searchById(1024, steps1024);
        sendJson(res, 200, json{
            {"searches", 1000},
            {"linked_list_steps", listSteps},
            {"bst_steps", bstSteps},
            {"bst_search_1024_steps", steps1024},
            {"note", "BST is O(log n) average with the balanced-load fix, O(n) worst case; the tree is not self-balancing."},
        });
    });

    // -- Orders -------------------------------------------------------
    svr.Get("/api/orders", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        std::string status = req.get_param_value("status");
        std::string city = req.get_param_value("city");
        std::string date = req.get_param_value("date");
        json out = json::array();
        for (const Order &o : store.allOrders()) {
            if (!status.empty() && o.status != status) continue;
            if (!city.empty() && o.city != city) continue;
            if (!date.empty() && o.date != date) continue;
            out.push_back(orderToJson(o));
        }
        sendJson(res, 200, out);
    });

    // Phase 4B-4 — the whole Order BST, for the Orders page tree view.
    // Registered BEFORE /api/orders/:id so httplib does not read
    // "tree" as an order ID.
    svr.Get("/api/orders/tree", [&](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        sendJson(res, 200, json{
            {"root", orderNodeToJson(store.orderTree.root)},
            {"size", store.report().total},
            {"height", orderTreeHeight(store.orderTree.root)},
        });
    });

    svr.Get("/api/orders/:id", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        int id;
        if (!parseId(req.path_params.at("id"), id)) { err(res, 400, "Invalid order ID."); return; }
        Order *o = store.findOrder(id);
        if (!o) { err(res, 404, "Order " + std::to_string(id) + " not found."); return; }
        json hist = json::array();
        std::vector<ActivityLogEntry> logs = store.logTree.searchLogsByOrderId(id);
        for (const ActivityLogEntry &e : logs) hist.push_back(logToJson(e));
        sendJson(res, 200, json{
            {"order", orderToJson(*o)},
            {"history", hist},
        });
    });

    svr.Post("/api/orders", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        json body;
        if (!parseBody(req.body, body))
            { err(res, 400, "Field 'customer_name' is required and must not be empty."); return; }
        std::string customer, product, category, date, city;
        int price = 0, quantity = 0;
        if (!getStringField(body, "customer_name", customer) || customer.empty())
            { err(res, 400, "Field 'customer_name' is required and must not be empty."); return; }
        if (!getStringField(body, "product", product) || product.empty())
            { err(res, 400, "Field 'product' is required and must not be empty."); return; }
        if (!getStringField(body, "category", category) || category.empty())
            { err(res, 400, "Field 'category' is required and must not be empty."); return; }
        if (!getStringField(body, "order_date", date) || !isValidDate(date))
            { err(res, 400, "Field 'order_date' is required in YYYY-MM-DD format."); return; }
        if (!getStringField(body, "city", city) || city.empty())
            { err(res, 400, "Field 'city' is required and must not be empty."); return; }
        if (!getIntField(body, "price_pkr", price) || price <= 0)
            { err(res, 400, "Field 'price_pkr' is required and must be greater than 0."); return; }
        if (!getIntField(body, "quantity", quantity) || quantity < 1)
            { err(res, 400, "Field 'quantity' is required and must be at least 1."); return; }
        int id = 0;
        if (getIntField(body, "order_id", id)) {
            if (store.findOrder(id)) { err(res, 400, "Order ID " + std::to_string(id) + " already exists."); return; }
        } else {
            id = store.nextOrderId();
        }
        Order o{id, customer, product, category, price, quantity, date, city, "Pending"};
        std::string error;
        if (!store.addOrder(o, error)) { err(res, 400, error); return; }
        sendJson(res, 201, orderToJson(o));
    });

    svr.Patch("/api/orders/:id/status", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        int id;
        if (!parseId(req.path_params.at("id"), id)) { err(res, 400, "Invalid order ID."); return; }
        json body;
        if (!parseBody(req.body, body))
            { err(res, 400, "Field 'status' is required."); return; }
        std::string status;
        if (!getStringField(body, "status", status) || status.empty())
            { err(res, 400, "Field 'status' is required."); return; }
        std::string error;
        if (!store.changeStatus(id, status, error)) {
            if (error.find("not found") != std::string::npos) err(res, 404, error);
            else err(res, 400, error);
            return;
        }
        sendJson(res, 200, orderToJson(*store.findOrder(id)));
    });

    svr.Post("/api/orders/:id/cancel", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        int id;
        if (!parseId(req.path_params.at("id"), id)) { err(res, 400, "Invalid order ID."); return; }
        std::string error;
        if (!store.cancelOrder(id, error)) {
            if (error.find("not found") != std::string::npos) err(res, 404, error);
            else err(res, 400, error);
            return;
        }
        sendJson(res, 200, orderToJson(*store.findOrder(id)));
    });

    svr.Delete("/api/orders/:id", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        int id;
        if (!parseId(req.path_params.at("id"), id)) { err(res, 400, "Invalid order ID."); return; }
        std::string error;
        if (!store.deleteOrder(id, error)) { err(res, 404, error); return; }
        sendJson(res, 200, json{{"deleted", id}});
    });

    // -- Queue + Undo -------------------------------------------------
    svr.Get("/api/queue", [&](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        json out = json::array();
        QueueNode *cur = store.processQueue.front;
        while (cur) {
            Order *o = store.findOrder(cur->orderId);
            if (o) out.push_back(orderToJson(*o));
            cur = cur->next;
        }
        sendJson(res, 200, out);
    });

    svr.Post("/api/queue/process", [&](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        Order out;
        std::string error;
        if (!store.processNext(out, error)) { err(res, 409, error); return; }
        sendJson(res, 200, orderToJson(out));
    });

    svr.Post("/api/undo", [&](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        std::string error;
        if (!store.undo(error)) { err(res, 409, error); return; }
        sendJson(res, 200, json{{"undone", true}});
    });

    // -- Delivery -----------------------------------------------------
    // Phase 4B-3 — read-only graph endpoint. The frontend draws the
    // network from this; nothing about the network is hardcoded in
    // React. One edge per undirected road (i < j), from the loaded
    // matrices — an edge exists where time != INF.
    svr.Get("/api/delivery/graph", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        const DeliveryGraph *g = nullptr;
        std::string dataset;
        if (!resolveDataset(store, req.get_param_value("dataset"), res, g, dataset)) return;
        json cities = json::array();
        for (int i = 0; i < g->n; i++) cities.push_back(g->cities[i]);
        json edges = json::array();
        for (int i = 0; i < g->n; i++) {
            for (int j = i + 1; j < g->n; j++) {
                if (g->timeMin[i][j] == INF) continue;
                edges.push_back(json{
                    {"from", g->cities[i]},
                    {"to", g->cities[j]},
                    {"time_min", g->timeMin[i][j]},
                    {"distance_km", g->distanceKm[i][j]},
                });
            }
        }
        sendJson(res, 200, json{{"dataset", dataset}, {"cities", cities}, {"edges", edges}});
    });

    svr.Get("/api/delivery/plan", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        std::string source = req.get_param_value("source");
        std::string dest = req.get_param_value("destination");
        if (source.empty()) source = "Lahore";
        if (dest.empty()) { err(res, 400, "Query parameter 'destination' is required."); return; }
        const DeliveryGraph *g = nullptr;
        std::string dataset;
        if (!resolveDataset(store, req.get_param_value("dataset"), res, g, dataset)) return;
        bool audit = true;
        if (!parseAuditParam(req.get_param_value("audit"), res, audit)) return;
        std::string error;
        Store::PlanResult pr = store.planRouteOn(source, dest, error, *g, audit);
        if (!pr.route.ok) { err(res, 400, pr.route.error); return; }
        json inter = json::array();
        for (const IntermediateMatch &m : pr.intermediate) {
            json os = json::array();
            for (const Order &o : m.orders) os.push_back(orderToJson(o));
            inter.push_back(json{
                {"city", m.city},
                {"time_to_city_min", m.timeToCityMin},
                {"distance_to_city_km", m.distanceToCityKm},
                {"orders", os},
            });
        }
        json destOrders = json::array();
        for (const Order &o : pr.destinationOrders) destOrders.push_back(orderToJson(o));
        json body = json{
            {"source", source},
            {"destination", dest},
            {"path", pr.route.path},
            {"total_time_min", pr.route.totalTimeMin},
            {"total_distance_km", pr.route.totalDistanceKm},
            {"intermediate", inter},
            {"destination_orders", destOrders},
        };
        if (!error.empty()) {
            // Route is returned, but the ROUTE_GENERATED log failed.
            body["log_warning"] = error;
        }
        sendJson(res, 200, body);
    });

    svr.Get("/api/delivery/detour", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        std::string source = req.get_param_value("source");
        std::string dest = req.get_param_value("destination");
        std::string via = req.get_param_value("via");
        if (source.empty()) source = "Lahore";
        if (dest.empty() || via.empty()) { err(res, 400, "Query parameters 'destination' and 'via' are required."); return; }
        const DeliveryGraph *g = nullptr;
        std::string dataset;
        if (!resolveDataset(store, req.get_param_value("dataset"), res, g, dataset)) return;
        bool audit = true;
        if (!parseAuditParam(req.get_param_value("audit"), res, audit)) return;
        std::string error;
        DetourResult d = store.planDetourOn(source, dest, via, error, *g, audit);
        if (!d.ok) { err(res, 400, d.error); return; }
        json os = json::array();
        for (const Order &o : d.eligibleOrders) os.push_back(orderToJson(o));
        json body = json{
            {"detour_city", via},
            {"original", json{
                {"time_min", d.original.totalTimeMin},
                {"distance_km", d.original.totalDistanceKm},
            }},
            {"via", json{
                {"time_min", d.via.totalTimeMin},
                {"distance_km", d.via.totalDistanceKm},
            }},
            {"extra_time_min", d.extraTimeMin},
            {"extra_distance_km", d.extraDistanceKm},
            {"eligible_orders", os},
        };
        if (!error.empty()) body["log_warning"] = error;
        sendJson(res, 200, body);
    });

    svr.Post("/api/delivery/multi-stop", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        json body;
        if (!parseBody(req.body, body))
            { err(res, 400, "Field 'destination' is required."); return; }
        std::string source, dest;
        std::vector<std::string> stops;
        if (!getStringField(body, "source", source)) source = "Lahore";
        if (!getStringField(body, "destination", dest) || dest.empty())
            { err(res, 400, "Field 'destination' is required."); return; }
        if (!getStringArray(body, "stops", stops))
            { err(res, 400, "Field 'stops' is required (an array of city names, possibly empty)."); return; }
        std::string datasetParam;
        // "dataset" is optional in the body; a wrong-typed value is a 400.
        if (body.contains("dataset")) {
            if (!getStringField(body, "dataset", datasetParam))
                { err(res, 400, "Field 'dataset' must be 'production' or 'test'."); return; }
        }
        const DeliveryGraph *g = nullptr;
        std::string dataset;
        if (!resolveDataset(store, datasetParam, res, g, dataset)) return;
        // Phase 4B-5: "audit" is an optional JSON boolean; a
        // wrong-typed value is a 400. Absent means audited (true).
        bool audit = true;
        if (body.contains("audit")) {
            if (!body["audit"].is_boolean())
                { err(res, 400, "Field 'audit' must be true or false."); return; }
            audit = body["audit"].get<bool>();
        }
        std::string error;
        MultiStopResult ms = store.planMultiStopOn(source, dest, stops, error, *g, audit);
        if (!ms.ok) { err(res, 400, ms.error); return; }
        json out = json{
            {"stop_order", ms.stopOrder},
            {"path", ms.bestRoute.path},
            {"total_time_min", ms.totalTimeMin},
            {"total_distance_km", ms.totalDistanceKm},
        };
        if (!error.empty()) out["log_warning"] = error;
        sendJson(res, 200, out);
    });

    svr.Get("/api/delivery/trips", [&](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        json out = json::array();
        for (const DeliveryTrip &tr : store.trips) out.push_back(tripToJson(tr));
        sendJson(res, 200, out);
    });

    svr.Post("/api/delivery/trips", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        json body;
        if (!parseBody(req.body, body))
            { err(res, 400, "Field 'destination' is required."); return; }
        std::string source, dest;
        std::vector<std::string> stops;
        std::vector<int> orderIds;
        if (!getStringField(body, "source", source)) source = "Lahore";
        if (!getStringField(body, "destination", dest) || dest.empty())
            { err(res, 400, "Field 'destination' is required."); return; }
        if (!getStringArray(body, "stops", stops)) stops.clear();
        if (!getIntArray(body, "order_ids", orderIds) || orderIds.empty())
            { err(res, 400, "Field 'order_ids' is required and must not be empty."); return; }
        // Phase 4B-3: trips are production-only. A demo-mode route
        // must never create a real trip (its orders would be marked
        // Shipped on a route that does not exist in production).
        std::string tripDataset;
        if (getStringField(body, "dataset", tripDataset) && tripDataset != "production") {
            err(res, 400, "Trips can only be created on the production routes.");
            return;
        }
        std::string error;
        int tripId = 0;
        if (!store.createTrip(source, dest, stops, orderIds, error, tripId)) { err(res, 400, error); return; }
        sendJson(res, 201, tripToJson(*store.findTrip(tripId)));
    });

    svr.Post("/api/delivery/trips/:id/complete", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        int id;
        if (!parseId(req.path_params.at("id"), id)) { err(res, 400, "Invalid trip ID."); return; }
        std::string error;
        if (!store.completeTrip(id, error)) {
            if (error.find("not found") != std::string::npos) err(res, 404, error);
            else err(res, 400, error);
            return;
        }
        sendJson(res, 200, tripToJson(*store.findTrip(id)));
    });

    svr.Post("/api/delivery/trips/:id/cancel", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        int id;
        if (!parseId(req.path_params.at("id"), id)) { err(res, 400, "Invalid trip ID."); return; }
        std::string error;
        if (!store.cancelTrip(id, error)) {
            if (error.find("not found") != std::string::npos) err(res, 404, error);
            else err(res, 400, error);
            return;
        }
        sendJson(res, 200, tripToJson(*store.findTrip(id)));
    });

    // -- Activity logs ------------------------------------------------
    svr.Get("/api/logs", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        std::string orderIdStr = req.get_param_value("order_id");
        std::string actionType = req.get_param_value("action_type");
        std::string from = req.get_param_value("from");
        std::string to = req.get_param_value("to");
        std::vector<ActivityLogEntry> all;
        store.logTree.inorderTraversal(all);
        json out = json::array();
        for (const ActivityLogEntry &e : all) {
            if (!orderIdStr.empty() && std::to_string(e.orderId) != orderIdStr) continue;
            if (!actionType.empty() && e.actionType != actionType) continue;
            if (!from.empty() && e.timestamp < from) continue;
            if (!to.empty() && e.timestamp > to) continue;
            out.push_back(logToJson(e));
        }
        sendJson(res, 200, out);
    });

    // Phase 4B-4 — the whole Activity Log BST, for the Logs page tree
    // view. Registered BEFORE /api/logs/:id for the same reason.
    svr.Get("/api/logs/tree", [&](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        sendJson(res, 200, json{
            {"root", logNodeToJson(store.logTree.root)},
            {"size", store.logTree.size},
            {"height", logTreeHeight(store.logTree.root)},
        });
    });

    svr.Get("/api/logs/:id", [&](const httplib::Request &req, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(mu);
        int id;
        if (!parseId(req.path_params.at("id"), id)) { err(res, 400, "Invalid log ID."); return; }
        ActivityLogEntry *e = store.logTree.searchLogById(id);
        if (!e) { err(res, 404, "Log entry " + std::to_string(id) + " not found."); return; }
        sendJson(res, 200, logToJson(*e));
    });

    if (!svr.listen("127.0.0.1", port)) {
        std::cerr << "Failed to bind to 127.0.0.1:" << port << " (is another server running?)\n";
        return 1;
    }
    return 0;
}
