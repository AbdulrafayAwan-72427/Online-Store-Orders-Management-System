#pragma once
// DeliveryGraph.h — Phase 3a: moved verbatim from main.cpp v1 (archive/main_v1.cpp).
// Phase 3c: stores BOTH time and distance per road, and returns a
// RouteResult (path + total time + total distance) instead of only
// printing. shortestTime()/shortestPath() are kept for compatibility.

#include <iostream>
#include <string>
#include <vector>

// 5. GRAPH - fastest delivery route (Dijkstra, simple O(V^2))
//    Cities = nodes, Roads = edges, travel time = weight.
//    Warehouse is in Lahore.
// ============================================================
const int MAX_CITIES = 20;
const int INF = 999999;

// Phase 3c — a reusable route result. Before Phase 3c, shortestPath()
// only printed; no other code could use the path, time or distance.
// ok=false means "no route / unknown city" — check it before using
// the other fields. timeMin/distanceKm are summed along the path.
struct RouteResult {
    bool ok = false;
    std::string source;
    std::string destination;
    std::vector<std::string> path; // source -> ... -> destination
    int totalTimeMin = 0;
    int totalDistanceKm = 0;
    std::string error; // filled when ok=false
};

class DeliveryGraph {
public:
    std::string cities[MAX_CITIES];
    int n = 0; // number of cities
    int timeMin[MAX_CITIES][MAX_CITIES];
    // Phase 3c: distanceKm[i][j] is the road distance in km, or INF
    // if there is no direct road. v1 threw distance_km away in
    // loadRoutes(); now it is stored alongside the time.
    int distanceKm[MAX_CITIES][MAX_CITIES];

    DeliveryGraph() {
        for (int i = 0; i < MAX_CITIES; i++)
            for (int j = 0; j < MAX_CITIES; j++) {
                timeMin[i][j] = (i == j) ? 0 : INF;
                distanceKm[i][j] = (i == j) ? 0 : INF;
            }
    }

    int getIndex(const std::string &city) const {
        for (int i = 0; i < n; i++) if (cities[i] == city) return i;
        return -1;
    }

    int addCity(const std::string &city) {
        int idx = getIndex(city);
        if (idx != -1) return idx;
        if (n >= MAX_CITIES) return -1;
        cities[n] = city;
        return n++;
    }

    // Roads are two-way with the same time and distance.
    // km defaults to 0 for old callers; Store::loadRoutes() always
    // passes the real distance_km from the CSV (Phase 3c).
    void addRoute(const std::string &from, const std::string &to, int minutes, int km = 0) {
        int a = addCity(from);
        int b = addCity(to);
        if (a == -1 || b == -1) return;
        timeMin[a][b] = minutes;
        timeMin[b][a] = minutes;
        distanceKm[a][b] = km;
        distanceKm[b][a] = km;
    }

    // Direct-road lookup, or INF if there is no direct road.
    // Used by RoutePlanner to add up time/distance along a path.
    int edgeTime(const std::string &from, const std::string &to) const {
        int a = -1, b = -1;
        for (int i = 0; i < n; i++) {
            if (cities[i] == from) a = i;
            if (cities[i] == to) b = i;
        }
        if (a == -1 || b == -1) return INF;
        return timeMin[a][b];
    }
    int edgeDistance(const std::string &from, const std::string &to) const {
        int a = -1, b = -1;
        for (int i = 0; i < n; i++) {
            if (cities[i] == from) a = i;
            if (cities[i] == to) b = i;
        }
        if (a == -1 || b == -1) return INF;
        return distanceKm[a][b];
    }

    // Phase 3c — Dijkstra that RETURNS the route (path + time +
    // distance) instead of printing it. Weight is travel time, as
    // in v1; distance is summed along the chosen path afterwards.
    // Returns ok=false if a city is unknown or there is no route.
    RouteResult shortestRoute(const std::string &start, const std::string &dest) const {
        RouteResult r;
        r.source = start; r.destination = dest;
        std::vector<std::string> path;
        // shortestTime() is non-const in v1; call the shared logic
        // via a mutable copy is wasteful, so re-run Dijkstra here
        // using the same O(V^2) algorithm (see shortestTime below).
        int s = -1, d = -1;
        for (int i = 0; i < n; i++) {
            if (cities[i] == start) s = i;
            if (cities[i] == dest) d = i;
        }
        if (s == -1 || d == -1) { r.error = "City not found in routes file."; return r; }
        if (s == d) { r.ok = true; r.path = {start}; return r; }
        int dist[MAX_CITIES];
        int parent[MAX_CITIES];
        bool done[MAX_CITIES] = {false};
        for (int i = 0; i < n; i++) { dist[i] = INF; parent[i] = -1; }
        dist[s] = 0;
        for (int count = 0; count < n; count++) {
            int u = -1, best = INF;
            for (int i = 0; i < n; i++)
                if (!done[i] && dist[i] < best) { best = dist[i]; u = i; }
            if (u == -1) break;
            done[u] = true;
            for (int v = 0; v < n; v++) {
                if (!done[v] && timeMin[u][v] != INF && dist[u] + timeMin[u][v] < dist[v]) {
                    dist[v] = dist[u] + timeMin[u][v];
                    parent[v] = u;
                }
            }
        }
        if (dist[d] == INF) { r.error = "No route from " + start + " to " + dest + "."; return r; }
        std::vector<std::string> rev;
        for (int cur = d; cur != -1; cur = parent[cur]) rev.push_back(cities[cur]);
        for (int i = (int)rev.size() - 1; i >= 0; i--) path.push_back(rev[i]);
        r.path = path;
        r.totalTimeMin = dist[d];
        // Sum the distance along the path, edge by edge.
        int totalKm = 0;
        for (size_t i = 0; i + 1 < path.size(); i++) {
            int km = edgeDistance(path[i], path[i + 1]);
            if (km == INF) { r.error = "Route uses a road with no stored distance."; return r; }
            totalKm += km;
        }
        r.totalDistanceKm = totalKm;
        r.ok = true;
        return r;
    }

    // NEW in Phase 3a — same Dijkstra as shortestPath() below, but it
    // RETURNS the result instead of printing it, so tests and (in Phase 3c)
    // the route planner can use the path as data.
    // Returns total minutes, or -1 if a city is unknown / no route.
    // pathOut is filled start -> destination on success.
    int shortestTime(const std::string &start, const std::string &dest, std::vector<std::string> &pathOut) {
        pathOut.clear();
        int s = getIndex(start);
        int d = getIndex(dest);
        if (s == -1 || d == -1) return -1;

        int dist[MAX_CITIES];
        int parent[MAX_CITIES];
        bool done[MAX_CITIES] = {false};
        for (int i = 0; i < n; i++) { dist[i] = INF; parent[i] = -1; }
        dist[s] = 0;

        for (int count = 0; count < n; count++) {
            // pick the nearest city not done yet
            int u = -1, best = INF;
            for (int i = 0; i < n; i++)
                if (!done[i] && dist[i] < best) { best = dist[i]; u = i; }
            if (u == -1) break;
            done[u] = true;
            // relax all neighbours of u
            for (int v = 0; v < n; v++) {
                if (!done[v] && timeMin[u][v] != INF && dist[u] + timeMin[u][v] < dist[v]) {
                    dist[v] = dist[u] + timeMin[u][v];
                    parent[v] = u;
                }
            }
        }

        if (dist[d] == INF) return -1;
        // Rebuild path using parent array (in reverse, then flip it)
        std::vector<std::string> rev;
        for (int cur = d; cur != -1; cur = parent[cur]) rev.push_back(cities[cur]);
        for (int i = (int)rev.size() - 1; i >= 0; i--) pathOut.push_back(rev[i]);
        return dist[d];
    }

    // Dijkstra from start city. Prints shortest time + path.
    // (Unchanged behaviour — it now calls shortestTime() above.)
    void shortestPath(const std::string &start, const std::string &dest) {
        std::vector<std::string> path;
        int minutes = shortestTime(start, dest, path);
        if (minutes < 0) {
            if (getIndex(start) == -1 || getIndex(dest) == -1)
                std::cout << "City not found in routes file.\n";
            else
                std::cout << "No route from " << start << " to " << dest << ".\n";
            return;
        }
        std::cout << "\nFastest route: " << start << " -> " << dest << "\n";
        std::cout << "Time: " << minutes << " minutes\nPath: ";
        for (size_t i = 0; i < path.size(); i++) {
            std::cout << path[i];
            if (i + 1 < path.size()) std::cout << " -> ";
        }
        std::cout << "\n";
    }

    void display() {
        std::cout << "\n--- Cities in delivery network (" << n << ") ---\n";
        for (int i = 0; i < n; i++) std::cout << "- " << cities[i] << "\n";
    }
};
