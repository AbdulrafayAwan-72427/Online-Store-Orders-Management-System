// ============================================================
//  Online Store Orders Management System — Console (fallback)
//  DSA Semester Project, Phase 3a
//
//  This file is now THIN: it only shows a menu and reads input.
//  All data structures live in their own headers (Order.h,
//  LinkedList.h, OrderQueue.h, ActionStack.h, OrderBST.h,
//  DeliveryGraph.h) and all business logic lives in Store
//  (Store.h / Store.cpp). The console, the tests (tests.cpp) and,
//  in Phase 4, the HTTP API all use the same Store object, so
//  they can never disagree about an order's status.
//
//  Original v1 console is preserved in archive/main_v1.cpp.
//  Menu options 1-12 behave as in v1, except for two deliberate
//  Phase 3a fixes (see Store.h):
//   * The processing queue now holds Pending orders only (5 on
//     load, not 11). The first order processed is 1006, not 1005.
//   * Cancel / status changes are validated (e.g. you cannot
//     cancel a Delivered order), and Undo restores the queue too.
//  Option 13 (Update order status) is NEW in Phase 3a.
//
//  Build (Windows / Linux / Mac):
//   cmake --build build
//   Run build/bin/store from data/runtime (see docs/WINDOWS_SETUP.md).
//  Tests:
//   cmake --build build
//   ctest --test-dir build --output-on-failure
// ============================================================

#include <iostream>
#include <string>
#include <vector>

#include "services/Store.h"

using namespace std;

static Store store; // the one Store for the whole program

static void showMenu() {
    cout << "\n========== Online Store Orders ==========\n";
    cout << "1. Load orders from orders.csv\n";
    cout << "2. Display all orders (Linked List)\n";
    cout << "3. Add new order\n";
    cout << "4. Search order by ID (BST - fast)\n";
    cout << "5. Cancel order by ID (can Undo)\n";
    cout << "6. Delete order by ID (Linked List + BST)\n";
    cout << "7. Process next order (Queue)\n";
    cout << "8. Undo last action (Stack)\n";
    cout << "9. Fastest delivery route (Graph - Dijkstra)\n";
    cout << "10. Compare search: Linked List vs BST\n";
    cout << "11. Show report\n";
    cout << "12. Save orders to orders.csv\n";
    cout << "13. Update order status (NEW in Phase 3a)\n";
    cout << "14. Show activity logs (NEW in Phase 3b)\n";
    cout << "15. Plan delivery route (NEW in Phase 3c)\n";
    cout << "16. Create delivery trip (NEW in Phase 3c)\n";
    cout << "17. Complete delivery trip (NEW in Phase 3c)\n";
    cout << "18. Show delivery trips (NEW in Phase 3c)\n";
    cout << "0. Exit\n";
    cout << "Enter choice: ";
}

static void showReport() {
    Report r = store.report();
    cout << "\n--- Report ---\n";
    cout << "Total orders: " << r.total << "\n";
    cout << "Pending: " << r.pending << "\n";
    cout << "Processing: " << r.processing << "\n";
    cout << "Shipped: " << r.shipped << "\n";
    cout << "Delivered: " << r.delivered << "\n";
    cout << "Cancelled: " << r.cancelled << "\n";
    cout << "Total sales (not cancelled): Rs." << r.totalSales << "\n";
    cout << "Orders in queue: " << r.queueSize << "\n";
    cout << "Actions you can undo: " << r.undoSize << "\n";
    cout << "Activity log entries: " << r.logSize << "\n";
    cout << "Delivery trips: " << r.tripCount << " (" << r.activeTripCount << " active)\n";
    store.processQueue.display();
}

static vector<int> parseIdList(const string &s) {
    vector<int> out;
    string cur;
    for (char c : s) {
        if (c == ',') { if (!cur.empty()) { try { out.push_back(stoi(cur)); } catch (...) {} cur.clear(); } }
        else if (c != ' ') cur += c;
    }
    if (!cur.empty()) { try { out.push_back(stoi(cur)); } catch (...) {} }
    return out;
}

static vector<string> parseCityList(const string &s) {
    vector<string> out;
    string cur;
    for (char c : s) {
        if (c == ',') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else if (c != ' ') cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static void printPlan(const Store::PlanResult &pr) {
    if (!pr.route.ok) { cout << "No route: " << pr.route.error << "\n"; return; }
    cout << "\nRoute: ";
    for (size_t i = 0; i < pr.route.path.size(); i++) {
        cout << pr.route.path[i];
        if (i + 1 < pr.route.path.size()) cout << " -> ";
    }
    cout << "\nTime: " << pr.route.totalTimeMin << " min, Distance: "
         << pr.route.totalDistanceKm << " km\n";
    if (pr.route.path.size() <= 2) {
        cout << "No intermediate cities on this route.\n";
    }
    for (const auto &m : pr.intermediate) {
        cout << "Intermediate " << m.city << " (reached in " << m.timeToCityMin
             << " min, " << m.distanceToCityKm << " km): ";
        for (size_t i = 0; i < m.orders.size(); i++) {
            cout << m.orders[i].id << " (" << m.orders[i].customer << ", "
                 << m.orders[i].status << ")";
            if (i + 1 < m.orders.size()) cout << ", ";
        }
        cout << "\n";
    }
    if (!pr.destinationOrders.empty()) {
        cout << "Destination " << pr.route.destination << " eligible orders: ";
        for (size_t i = 0; i < pr.destinationOrders.size(); i++) {
            cout << pr.destinationOrders[i].id << " (" << pr.destinationOrders[i].customer
                 << ", " << pr.destinationOrders[i].status << ")";
            if (i + 1 < pr.destinationOrders.size()) cout << ", ";
        }
        cout << "\n";
    } else {
        cout << "No eligible orders in the destination city.\n";
    }
}

int main() {
    // Load delivery routes at start so option 9 works even before orders.
    if (!store.loadRoutes("delivery_routes.csv"))
        cout << "Warning: could not open delivery_routes.csv.\n";
    // Load the audit log (missing file = first run, empty log).
    store.loadLogs("activity_logs.csv");

    int choice;
    while (true) {
        showMenu();
        if (!(cin >> choice)) break;
        cin.ignore(); // clear newline after number

        if (choice == 0) { cout << "Goodbye!\n"; break; }

        else if (choice == 1) {
            if (store.loadOrders("orders.csv")) {
                Report r = store.report();
                cout << "Loaded " << r.total << " orders from orders.csv.\n";
                cout << "Processing queue has " << r.queueSize
                     << " pending orders.\n";
            } else {
                cout << "Cannot open orders.csv. Keep it in the same folder as the program.\n";
            }
        }

        else if (choice == 2) {
            store.orderList.displayAll();
        }

        else if (choice == 3) {
            Order o;
            string error;
            cout << "Order ID: "; cin >> o.id; cin.ignore();
            cout << "Customer name: "; getline(cin, o.customer);
            cout << "Product: "; getline(cin, o.product);
            cout << "Category: "; getline(cin, o.category);
            cout << "Price (PKR): "; cin >> o.price;
            cout << "Quantity: "; cin >> o.quantity; cin.ignore();
            cout << "Date (YYYY-MM-DD): "; getline(cin, o.date);
            cout << "City: "; getline(cin, o.city);
            if (store.addOrder(o, error))
                cout << "Order " << o.id << " added. Status: Pending.\n";
            else
                cout << "Could not add order: " << error << "\n";
        }

        else if (choice == 4) {
            int id, steps;
            cout << "Enter order ID to search: "; cin >> id;
            Order *o = store.findOrder(id, steps);
            if (o) { cout << "Found in BST in " << steps << " steps:\n"; printOrder(*o); }
            else cout << "Order " << id << " not found.\n";
        }

        else if (choice == 5) {
            int id; string error;
            cout << "Enter order ID to cancel: "; cin >> id;
            if (store.cancelOrder(id, error))
                cout << "Order " << id << " cancelled. Use option 8 to Undo.\n";
            else
                cout << "Could not cancel: " << error << "\n";
        }

        else if (choice == 6) {
            int id; string error;
            cout << "Enter order ID to delete: "; cin >> id;
            if (store.deleteOrder(id, error))
                cout << "Order " << id << " deleted from Linked List and BST.\n";
            else
                cout << "Could not delete: " << error << "\n";
        }

        else if (choice == 7) {
            Order out; string error;
            if (store.processNext(out, error)) {
                cout << "Processing order " << out.id << " (first in queue):\n";
                printOrder(out);
                cout << "Orders left in queue: " << store.processQueue.size << "\n";
                store.processQueue.display();
            } else {
                cout << error << "\n";
            }
        }

        else if (choice == 8) {
            string error;
            if (store.undo(error))
                cout << "Undo done. Check the report (option 11) to see the restored status.\n";
            else
                cout << error << "\n";
        }

        else if (choice == 9) {
            string dest;
            store.graph.display();
            cout << "Warehouse is in Lahore.\nEnter delivery city: ";
            getline(cin, dest);
            store.graph.shortestPath("Lahore", dest);
        }

        else if (choice == 10) {
            store.compareSearch();
        }

        else if (choice == 11) {
            showReport();
        }

        else if (choice == 12) {
            if (store.saveOrders())
                cout << "Saved " << store.orderList.size << " orders to orders.csv (atomic save).\n";
            else
                cout << "Could not save orders.csv. The old file is intact.\n";
        }

        else if (choice == 14) {
            store.logTree.displayAllLogs();
        }

        else if (choice == 13) {
            int id; string newStatus, error;
            cout << "Enter order ID: "; cin >> id; cin.ignore();
            cout << "New status (Pending/Processing/Shipped/Delivered/Cancelled): ";
            getline(cin, newStatus);
            if (store.changeStatus(id, newStatus, error))
                cout << "Order " << id << " is now " << newStatus << ".\n";
            else
                cout << "Could not update: " << error << "\n";
        }

        else if (choice == 15) {
            string dest, detour, err;
            cout << "Warehouse is in Lahore.\nEnter destination city: ";
            getline(cin, dest);
            Store::PlanResult pr = store.planRoute("Lahore", dest, err);
            if (!err.empty()) cout << "Note: " << err << "\n";
            printPlan(pr);
            if (pr.route.ok) {
                cout << "Optional detour city (leave blank for none): ";
                getline(cin, detour);
                if (!detour.empty()) {
                    DetourResult d = store.planDetour("Lahore", dest, detour, err);
                    if (!d.ok) cout << "Detour: " << err << "\n";
                    else {
                        cout << "Detour via " << detour << ": " << d.via.totalTimeMin
                             << " min, " << d.via.totalDistanceKm << " km (extra "
                             << d.extraTimeMin << " min, " << d.extraDistanceKm << " km)\n";
                        if (d.eligibleOrders.empty()) cout << "No eligible orders in " << detour << ".\n";
                        else {
                            cout << "Eligible in " << detour << ": ";
                            for (size_t i = 0; i < d.eligibleOrders.size(); i++) {
                                cout << d.eligibleOrders[i].id << " (" << d.eligibleOrders[i].status << ")";
                                if (i + 1 < d.eligibleOrders.size()) cout << ", ";
                            }
                            cout << "\n";
                        }
                    }
                }
            }
        }

        else if (choice == 16) {
            string dest, stopsStr, idsStr, err;
            cout << "Enter destination city: ";
            getline(cin, dest);
            cout << "Stop cities, comma-separated (leave blank for the shortest route): ";
            getline(cin, stopsStr);
            cout << "Order IDs to assign, comma-separated (Processing orders only): ";
            getline(cin, idsStr);
            vector<string> stops = parseCityList(stopsStr);
            vector<int> ids = parseIdList(idsStr);
            int tripId = 0;
            if (store.createTrip("Lahore", dest, stops, ids, err, tripId))
                cout << "Trip " << tripId << " created (Planned). Assigned orders are now Shipped.\n";
            else
                cout << "Could not create trip: " << err << "\n";
        }

        else if (choice == 17) {
            int tripId; string err;
            cout << "Enter trip ID to complete: "; cin >> tripId; cin.ignore();
            if (store.completeTrip(tripId, err))
                cout << "Trip " << tripId << " completed. Assigned orders are now Delivered.\n";
            else
                cout << "Could not complete trip: " << err << "\n";
        }

        else if (choice == 18) {
            if (store.trips.empty()) { cout << "No delivery trips yet.\n"; }
            else {
                cout << "\n--- Delivery Trips (" << store.trips.size() << ") ---\n";
                for (const DeliveryTrip &tr : store.trips) {
                    cout << "Trip " << tr.tripId << " | " << tr.source << " -> " << tr.destination
                         << " | route ";
                    for (size_t i = 0; i < tr.route.size(); i++) {
                        cout << tr.route[i];
                        if (i + 1 < tr.route.size()) cout << ">";
                    }
                    cout << " | orders ";
                    for (size_t i = 0; i < tr.assignedOrderIds.size(); i++) {
                        cout << tr.assignedOrderIds[i];
                        if (i + 1 < tr.assignedOrderIds.size()) cout << ";";
                    }
                    cout << " | " << tr.totalTimeMin << " min, " << tr.totalDistanceKm
                         << " km | " << tr.status << "\n";
                }
            }
        }

        else {
            cout << "Wrong choice. Try 0-18.\n";
        }
    }
    return 0;
}
