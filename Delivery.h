#pragma once
// Delivery.h — Phase 3c (NEW file)
//
// RoutePlanner + DeliveryTrip. RoutePlanner does the delivery maths
// (shortest route, intermediate matching, detours, multiple stops)
// using ONLY the DeliveryGraph and a list of orders passed in — it
// never changes an order, never writes a file, and never touches
// the log. Store (Store.h/.cpp) owns the orders, the trips file,
// the status changes, and the logging; it calls RoutePlanner to
// compute, then decides whether to confirm anything.
//
// Rules that RoutePlanner enforces (see the Phase 3c report):
//  * A city counts as an intermediate stop ONLY if it actually
//    appears in the calculated shortest path. "Nearby" is not "on route".
//  * Eligible orders are Pending or Processing only, in that city,
//    and not already assigned to an active trip.
//  * A detour is never claimed to be faster without calculating
//    its complete route cost (source -> detour -> destination).
//  * Multiple stops use the EXACT method for this dataset: try
//    every ordering of the stops and pick the cheapest. See
//    bestRouteWithStops() for the complexity and the limit.
//  * Planning never changes an order status. Assignment and
//    completion are Store operations, not RoutePlanner operations.

#include <algorithm>
#include <string>
#include <vector>

#include "DeliveryGraph.h"
#include "Order.h"

// One confirmed delivery trip. Stored in delivery_trips.csv:
//   trip_id,source,destination,route,assigned_order_ids,time_min,distance_km,status
// route = cities joined with '>' (e.g. Lahore>Faisalabad>Karachi)
// assigned_order_ids = IDs joined with ';' (e.g. 1006;1022)
// status: Planned (created, not yet completed) or Completed.
// InProgress exists as a value for a future phase; Phase 3c does
// not use it (a trip goes Planned -> Completed on explicit confirm).
struct DeliveryTrip {
    int tripId = 0;
    std::string source;
    std::string destination;
    std::vector<std::string> route; // source -> ... -> destination
    std::vector<int> assignedOrderIds;
    int totalTimeMin = 0;
    int totalDistanceKm = 0;
    std::string status = "Planned"; // Planned | InProgress | Completed
};

// Eligible orders in ONE intermediate city, with the time/distance
// from the source to that city along the planned route.
struct IntermediateMatch {
    std::string city;
    std::vector<Order> orders; // Pending/Processing only, unassigned
    int timeToCityMin = 0;     // cumulative time source -> this city
    int distanceToCityKm = 0;  // cumulative distance source -> this city
};

// A detour proposal: the complete route through an extra city,
// compared with the original shortest route.
struct DetourResult {
    bool ok = false;
    std::string detourCity;
    RouteResult original; // shortest route without the detour
    RouteResult via;      // shortest route that visits detourCity
    int extraTimeMin = 0; // via.totalTimeMin - original.totalTimeMin
    int extraDistanceKm = 0;
    std::vector<Order> eligibleOrders; // Pending/Processing in detourCity
    std::string error;
};

// Best route visiting a set of stops, in the best order.
struct MultiStopResult {
    bool ok = false;
    RouteResult bestRoute; // full concatenated path, deduped
    std::vector<std::string> stopOrder; // stops in the order visited
    int totalTimeMin = 0;
    int totalDistanceKm = 0;
    std::string error;
};

class RoutePlanner {
public:
    explicit RoutePlanner(const DeliveryGraph &g) : graph(g) {}

    RouteResult shortestRoute(const std::string &source,
                              const std::string &destination) const {
        return graph.shortestRoute(source, destination);
    }

    // Intermediate-city matching.
    // route: the calculated shortest route (from shortestRoute).
    // orders: all orders (Store passes the Linked List contents).
    // excludedOrderIds: orders already assigned to an active trip.
    // Returns one entry per intermediate city (path[1..n-2]) that
    // has at least one eligible order. A city with no eligible
    // orders is NOT returned — the caller can tell the difference
    // between "no intermediate city" and "intermediate city, no
    // orders" from route.path vs this list.
    // Eligible: status Pending or Processing, city == intermediate
    // city, orderId not in excludedOrderIds.
    std::vector<IntermediateMatch> findIntermediateOrders(
        const RouteResult &route, const std::vector<Order> &orders,
        const std::vector<int> &excludedOrderIds) const {
        std::vector<IntermediateMatch> result;
        if (!route.ok || route.path.size() < 3) return result; // no intermediates
        // Cumulative time/distance along the path, city by city.
        std::vector<int> cumTime(route.path.size(), 0);
        std::vector<int> cumDist(route.path.size(), 0);
        for (size_t i = 1; i < route.path.size(); i++) {
            cumTime[i] = cumTime[i - 1] + graph.edgeTime(route.path[i - 1], route.path[i]);
            cumDist[i] = cumDist[i - 1] + graph.edgeDistance(route.path[i - 1], route.path[i]);
        }
        for (size_t i = 1; i + 1 < route.path.size(); i++) {
            IntermediateMatch m;
            m.city = route.path[i];
            m.timeToCityMin = cumTime[i];
            m.distanceToCityKm = cumDist[i];
            for (const Order &o : orders) {
                if (o.city != m.city) continue;
                if (o.status != "Pending" && o.status != "Processing") continue;
                if (std::find(excludedOrderIds.begin(), excludedOrderIds.end(), o.id) !=
                    excludedOrderIds.end())
                    continue;
                m.orders.push_back(o);
            }
            if (!m.orders.empty()) result.push_back(m);
        }
        return result;
    }

    // Eligible orders in ONE city (used for detours and for the
    // destination city). Same eligibility rule as above.
    std::vector<Order> eligibleOrdersInCity(
        const std::string &city, const std::vector<Order> &orders,
        const std::vector<int> &excludedOrderIds) const {
        std::vector<Order> result;
        for (const Order &o : orders) {
            if (o.city != city) continue;
            if (o.status != "Pending" && o.status != "Processing") continue;
            if (std::find(excludedOrderIds.begin(), excludedOrderIds.end(), o.id) !=
                excludedOrderIds.end())
                continue;
            result.push_back(o);
        }
        return result;
    }

    // Optional detour through one extra city.
    // via = best route that visits detourCity (exact, via
    // bestRouteWithStops with one stop). extra = via - original.
    // If detourCity is already on the shortest path, it is not a
    // detour: via == original, extra == 0, and the caller should
    // use findIntermediateOrders instead.
    DetourResult calculateDetour(const std::string &source,
                                 const std::string &destination,
                                 const std::string &detourCity,
                                 const std::vector<Order> &orders,
                                 const std::vector<int> &excludedOrderIds) const {
        DetourResult d;
        d.detourCity = detourCity;
        d.original = graph.shortestRoute(source, destination);
        if (!d.original.ok) { d.error = d.original.error; return d; }
        if (detourCity == source || detourCity == destination) {
            d.error = "Detour city must not be the source or the destination.";
            return d;
        }
        if (graph.getIndex(detourCity) == -1) {
            d.error = "Unknown city: " + detourCity;
            return d;
        }
        MultiStopResult ms = bestRouteWithStops(source, destination, {detourCity});
        if (!ms.ok) { d.error = ms.error; return d; }
        d.via = ms.bestRoute;
        d.extraTimeMin = d.via.totalTimeMin - d.original.totalTimeMin;
        d.extraDistanceKm = d.via.totalDistanceKm - d.original.totalDistanceKm;
        d.eligibleOrders = eligibleOrdersInCity(detourCity, orders, excludedOrderIds);
        d.ok = true;
        return d;
    }

    // Multiple stops — EXACT method.
    // Try every ordering (permutation) of the stops. For each
    // ordering, add up the shortest segment times/distances:
    //   source -> stop1 -> stop2 -> ... -> destination
    // Pick the ordering with the smallest total TIME; break ties
    // by smallest total DISTANCE, then by the lexicographically
    // smallest stop order (so the result is deterministic).
    // Complexity: O(k! * (k+1) * Dijkstra), where k = number of
    // stops and Dijkstra here is O(V^2) with V = number of cities.
    // For this dataset V = 6, so k <= 4 (every city except source
    // and destination): at most 24 permutations, trivial.
    // Practical limit: k = 6 -> 720 permutations (still fine);
    // k = 8 -> 40,320 (slow); k >= 10 is impractical — a real
    // system would switch to a heuristic (nearest neighbour +
    // 2-opt) and SAY SO in the docs. Do not pretend the exact
    // method scales to dozens of stops.
    // Invalid input (empty stops, duplicate stops, a stop equal
    // to source/destination, unknown city) returns ok=false.
    MultiStopResult bestRouteWithStops(const std::string &source,
                                       const std::string &destination,
                                       std::vector<std::string> stops) const {
        MultiStopResult r;
        if (stops.empty()) {
            RouteResult direct = graph.shortestRoute(source, destination);
            if (!direct.ok) { r.error = direct.error; return r; }
            r.bestRoute = direct;
            r.totalTimeMin = direct.totalTimeMin;
            r.totalDistanceKm = direct.totalDistanceKm;
            r.ok = true;
            return r;
        }
        // Validate stops.
        std::sort(stops.begin(), stops.end());
        for (size_t i = 1; i < stops.size(); i++)
            if (stops[i] == stops[i - 1]) { r.error = "Duplicate stop: " + stops[i]; return r; }
        for (const std::string &s : stops) {
            if (s == source || s == destination) {
                r.error = "Stop must not be the source or the destination: " + s;
                return r;
            }
            if (graph.getIndex(s) == -1) { r.error = "Unknown city: " + s; return r; }
        }
        if (stops.size() > 6) {
            r.error = "Too many stops for the exact method (max 6). Use a heuristic for larger sets.";
            return r;
        }
        // Try every permutation. stops is sorted, so next_permutation
        // visits them all in lexicographic order.
        bool found = false;
        int bestTime = 0, bestDist = 0;
        std::vector<std::string> bestOrder;
        RouteResult bestRoute;
        std::sort(stops.begin(), stops.end());
        do {
            // Build the route for this ordering, segment by segment.
            std::vector<std::string> legs;
            legs.push_back(source);
            for (const std::string &s : stops) legs.push_back(s);
            legs.push_back(destination);
            int totalTime = 0, totalDist = 0;
            std::vector<std::string> fullPath;
            bool valid = true;
            for (size_t i = 0; i + 1 < legs.size(); i++) {
                RouteResult seg = graph.shortestRoute(legs[i], legs[i + 1]);
                if (!seg.ok) { valid = false; break; }
                totalTime += seg.totalTimeMin;
                totalDist += seg.totalDistanceKm;
                // Concatenate, skipping the duplicated joint city.
                for (size_t j = 0; j < seg.path.size(); j++) {
                    if (!fullPath.empty() && j == 0) continue;
                    fullPath.push_back(seg.path[j]);
                }
            }
            if (!valid) continue;
            if (!found || totalTime < bestTime ||
                (totalTime == bestTime && totalDist < bestDist)) {
                found = true;
                bestTime = totalTime;
                bestDist = totalDist;
                bestOrder = stops;
                bestRoute.ok = true;
                bestRoute.source = source;
                bestRoute.destination = destination;
                bestRoute.path = fullPath;
                bestRoute.totalTimeMin = totalTime;
                bestRoute.totalDistanceKm = totalDist;
            }
        } while (std::next_permutation(stops.begin(), stops.end()));
        if (!found) { r.error = "No route visits all stops."; return r; }
        r.bestRoute = bestRoute;
        r.stopOrder = bestOrder;
        r.totalTimeMin = bestTime;
        r.totalDistanceKm = bestDist;
        r.ok = true;
        return r;
    }

private:
    const DeliveryGraph &graph;
};
