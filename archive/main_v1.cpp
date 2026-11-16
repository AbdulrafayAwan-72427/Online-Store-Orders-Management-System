// ============================================================
//  Online Store Orders Management System
//  DSA Semester Project - Simple C++ Console Program
//
//  Data Structures used (all written by hand, no STL list/map):
//    1. Linked List - store ALL orders
//    2. Queue       - process orders first-come, first-served
//    3. Stack       - Undo last cancel / status change
//    4. BST (Tree)  - fast search by order_id
//    5. Graph       - fastest delivery route (Dijkstra)
//
//  Data files (keep them in the SAME folder as this file):
//    orders.csv          - 30 sample orders
//    delivery_routes.csv - 12 routes between cities
//
//  How to compile and run (Windows / Linux / Mac):
//    g++ main.cpp -o store
//    ./store        (on Windows: store.exe)
// ============================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
using namespace std;

// ---------- Order ----------
// One row from orders.csv
struct Order {
    int id = 0;
    string customer;
    string product;
    string category;
    int price = 0;
    int quantity = 1;
    string date;
    string city;
    string status = "Pending"; // Pending / Processing / Shipped / Delivered / Cancelled
};

void printOrder(const Order &o) {
    cout << o.id << " | " << o.customer << " | " << o.product
         << " | " << o.category << " | Rs." << o.price
         << " x" << o.quantity << " | " << o.date
         << " | " << o.city << " | " << o.status << endl;
}

// Split one CSV line by comma. Our data has no commas inside fields,
// so a simple split is enough and easy to explain.
vector<string> splitCSV(const string &line) {
    vector<string> parts;
    stringstream ss(line);
    string item;
    while (getline(ss, item, ',')) parts.push_back(item);
    return parts;
}

// Turn one CSV line into an Order. Returns false if the line is bad.
bool parseOrder(const string &line, Order &o) {
    vector<string> p = splitCSV(line);
    if (p.size() < 9) return false;
    try {
        o.id = stoi(p[0]);
        o.customer = p[1];
        o.product = p[2];
        o.category = p[3];
        o.price = stoi(p[4]);
        o.quantity = stoi(p[5]);
        o.date = p[6];
        o.city = p[7];
        o.status = p[8];
    } catch (...) { return false; }
    return true;
}

// ============================================================
// 1. LINKED LIST - store all orders
//    insertAtEnd, insertById, deleteById, searchById
// ============================================================
struct ListNode {
    Order data;
    ListNode *next;
    ListNode(Order o) : data(o), next(nullptr) {}
};

class OrderLinkedList {
public:
    ListNode *head = nullptr;
    ListNode *tail = nullptr;
    int size = 0;

    // Insert at end - O(1) because we keep a tail pointer
    void insertAtEnd(Order o) {
        ListNode *node = new ListNode(o);
        if (!head) { head = tail = node; }
        else { tail->next = node; tail = node; }
        size++;
    }

    // Insert by ID (keeps list sorted by ID) - simple version
    void insertById(Order o) {
        ListNode *node = new ListNode(o);
        if (!head || o.id < head->data.id) {
            node->next = head; head = node;
            if (!tail) tail = node;
            size++; return;
        }
        ListNode *cur = head;
        while (cur->next && cur->next->data.id < o.id) cur = cur->next;
        node->next = cur->next; cur->next = node;
        if (!node->next) tail = node;
        size++;
    }

    // Search by ID - O(n). Returns pointer to Order or nullptr.
    // steps counts how many nodes we checked (for the timing test).
    Order* searchById(int id, int &steps) {
        steps = 0;
        ListNode *cur = head;
        while (cur) {
            steps++;
            if (cur->data.id == id) return &cur->data;
            cur = cur->next;
        }
        return nullptr;
    }
    Order* searchById(int id) { int s; return searchById(id, s); }

    // Delete by ID - O(n) to find, O(1) to remove once found
    bool deleteById(int id) {
        if (!head) return false;
        if (head->data.id == id) {
            ListNode *tmp = head; head = head->next;
            if (!head) tail = nullptr;
            delete tmp; size--; return true;
        }
        ListNode *cur = head;
        while (cur->next && cur->next->data.id != id) cur = cur->next;
        if (!cur->next) return false;
        ListNode *tmp = cur->next; cur->next = tmp->next;
        if (!cur->next) tail = cur;
        delete tmp; size--; return true;
    }

    // Change status in the list (used by Cancel / Undo)
    bool updateStatus(int id, const string &newStatus) {
        Order *o = searchById(id);
        if (!o) return false;
        o->status = newStatus;
        return true;
    }

    void displayAll() {
        if (!head) { cout << "No orders yet. Load orders.csv first (option 1).\n"; return; }
        cout << "\n--- All Orders (" << size << ") ---\n";
        ListNode *cur = head;
        while (cur) { printOrder(cur->data); cur = cur->next; }
    }

    int totalSales() {
        int total = 0;
        ListNode *cur = head;
        while (cur) {
            if (cur->data.status != "Cancelled")
                total += cur->data.price * cur->data.quantity;
            cur = cur->next;
        }
        return total;
    }
    int countStatus(const string &st) {
        int c = 0; ListNode *cur = head;
        while (cur) { if (cur->data.status == st) c++; cur = cur->next; }
        return c;
    }
};

// ============================================================
// 2. QUEUE - process orders FIFO (first in, first out)
//    enqueue, dequeue, peek, isEmpty
//    Built on a simple linked list of order IDs.
// ============================================================
struct QueueNode {
    int orderId;
    QueueNode *next;
    QueueNode(int id) : orderId(id), next(nullptr) {}
};

class OrderQueue {
public:
    QueueNode *front = nullptr;
    QueueNode *rear = nullptr;
    int size = 0;

    bool isEmpty() { return front == nullptr; }

    void enqueue(int id) {           // push at rear - O(1)
        QueueNode *node = new QueueNode(id);
        if (!rear) { front = rear = node; }
        else { rear->next = node; rear = node; }
        size++;
    }

    int dequeue() {                  // pop from front - O(1)
        if (isEmpty()) return -1;
        int id = front->orderId;
        QueueNode *tmp = front; front = front->next;
        if (!front) rear = nullptr;
        delete tmp; size--;
        return id;
    }

    int peek() { return isEmpty() ? -1 : front->orderId; }

    void display() {
        if (isEmpty()) { cout << "Queue is empty.\n"; return; }
        cout << "Queue (front -> rear): ";
        QueueNode *cur = front;
        while (cur) { cout << cur->orderId; if (cur->next) cout << " -> "; cur = cur->next; }
        cout << "\n";
    }
};

// ============================================================
// 3. STACK - Undo last action (LIFO)
//    push, pop, peek, isEmpty
// ============================================================
struct Action {
    int orderId;
    string oldStatus;
    string newStatus;
};

struct StackNode {
    Action data;
    StackNode *next;
    StackNode(Action a) : data(a), next(nullptr) {}
};

class ActionStack {
public:
    StackNode *top = nullptr;

    bool isEmpty() { return top == nullptr; }

    void push(Action a) {            // O(1)
        StackNode *node = new StackNode(a);
        node->next = top; top = node;
    }

    bool pop(Action &a) {            // O(1)
        if (isEmpty()) return false;
        a = top->data;
        StackNode *tmp = top; top = top->next;
        delete tmp;
        return true;
    }

    bool peek(Action &a) {
        if (isEmpty()) return false;
        a = top->data; return true;
    }
};

// ============================================================
// 4. BST (Binary Search Tree) - fast search by order_id
//    insert, searchById, deleteById
//    Left child  = smaller ID, Right child = larger ID
//    Search is O(log n) on average, O(n) only if unbalanced.
// ============================================================
struct TreeNode {
    Order data;
    TreeNode *left = nullptr;
    TreeNode *right = nullptr;
    TreeNode(Order o) : data(o) {}
};

class OrderBST {
public:
    TreeNode *root = nullptr;

    void insert(Order o) { root = insertRec(root, o); }

    TreeNode* insertRec(TreeNode *node, Order o) {
        if (!node) return new TreeNode(o);
        if (o.id < node->data.id) node->left = insertRec(node->left, o);
        else if (o.id > node->data.id) node->right = insertRec(node->right, o);
        // if ID already exists, do nothing (IDs are unique)
        return node;
    }

    // Search by ID. steps counts nodes visited.
    Order* searchById(int id, int &steps) {
        steps = 0;
        TreeNode *cur = root;
        while (cur) {
            steps++;
            if (id == cur->data.id) return &cur->data;
            if (id < cur->data.id) cur = cur->left;
            else cur = cur->right;
        }
        return nullptr;
    }
    Order* searchById(int id) { int s; return searchById(id, s); }

    bool updateStatus(int id, const string &newStatus) {
        Order *o = searchById(id);
        if (!o) return false;
        o->status = newStatus;
        return true;
    }

    // Delete by ID - standard BST delete (3 cases)
    void deleteById(int id) { root = deleteRec(root, id); }

    TreeNode* findMin(TreeNode *node) {
        while (node && node->left) node = node->left;
        return node;
    }

    TreeNode* deleteRec(TreeNode *node, int id) {
        if (!node) return nullptr;
        if (id < node->data.id) node->left = deleteRec(node->left, id);
        else if (id > node->data.id) node->right = deleteRec(node->right, id);
        else {
            if (!node->left) { TreeNode *t = node->right; delete node; return t; }
            if (!node->right) { TreeNode *t = node->left; delete node; return t; }
            TreeNode *minNode = findMin(node->right);
            node->data = minNode->data;
            node->right = deleteRec(node->right, minNode->data.id);
        }
        return node;
    }

    void inorder(TreeNode *node) {
        if (!node) return;
        inorder(node->left);
        printOrder(node->data);
        inorder(node->right);
    }
    void displaySorted() {
        if (!root) { cout << "BST is empty.\n"; return; }
        cout << "\n--- Orders sorted by ID (BST inorder) ---\n";
        inorder(root);
    }
};

// ============================================================
// 5. GRAPH - fastest delivery route (Dijkstra, simple O(V^2))
//    Cities = nodes, Roads = edges, travel time = weight.
//    Warehouse is in Lahore.
// ============================================================
const int MAX_CITIES = 20;
const int INF = 999999;

class DeliveryGraph {
public:
    string cities[MAX_CITIES];
    int n = 0; // number of cities
    int timeMin[MAX_CITIES][MAX_CITIES];

    DeliveryGraph() {
        for (int i = 0; i < MAX_CITIES; i++)
            for (int j = 0; j < MAX_CITIES; j++)
                timeMin[i][j] = (i == j) ? 0 : INF;
    }

    int getIndex(const string &city) {
        for (int i = 0; i < n; i++) if (cities[i] == city) return i;
        return -1;
    }

    int addCity(const string &city) {
        int idx = getIndex(city);
        if (idx != -1) return idx;
        if (n >= MAX_CITIES) return -1;
        cities[n] = city;
        return n++;
    }

    // Roads are two-way with the same time
    void addRoute(const string &from, const string &to, int minutes) {
        int a = addCity(from);
        int b = addCity(to);
        if (a == -1 || b == -1) return;
        timeMin[a][b] = minutes;
        timeMin[b][a] = minutes;
    }

    // Dijkstra from start city. Prints shortest time + path.
    void shortestPath(const string &start, const string &dest) {
        int s = getIndex(start);
        int d = getIndex(dest);
        if (s == -1 || d == -1) { cout << "City not found in routes file.\n"; return; }

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

        if (dist[d] == INF) { cout << "No route from " << start << " to " << dest << ".\n"; return; }

        // Rebuild path using parent array (in reverse)
        vector<string> path;
        for (int cur = d; cur != -1; cur = parent[cur]) path.push_back(cities[cur]);
        cout << "\nFastest route: " << start << " -> " << dest << "\n";
        cout << "Time: " << dist[d] << " minutes\nPath: ";
        for (int i = (int)path.size() - 1; i >= 0; i--) {
            cout << path[i];
            if (i > 0) cout << " -> ";
        }
        cout << "\n";
    }

    void display() {
        cout << "\n--- Cities in delivery network (" << n << ") ---\n";
        for (int i = 0; i < n; i++) cout << "- " << cities[i] << "\n";
    }
};

// ============================================================
// Global objects (simple for a student project)
// ============================================================
OrderLinkedList orderList;
OrderBST orderTree;
OrderQueue processQueue;
ActionStack undoStack;
DeliveryGraph graph;
bool ordersLoaded = false;
bool routesLoaded = false;

// ---------- Load orders.csv ----------
// NOTE: IDs in orders.csv are sorted (1001-1030). Inserting them
// into a BST in that order would make a straight line (degenerate
// tree, search O(n) - same as a list). So we insert the middle
// order first, then the middles of the two halves, and so on.
// That keeps the BST balanced, so search stays O(log n).
void insertBalancedBST(vector<Order> &v, int lo, int hi) {
    if (lo > hi) return;
    int mid = (lo + hi) / 2;
    orderTree.insert(v[mid]);
    insertBalancedBST(v, lo, mid - 1);
    insertBalancedBST(v, mid + 1, hi);
}

bool loadOrders(const string &filename) {
    ifstream file(filename);
    if (!file) { cout << "Cannot open " << filename << ". Keep it in the same folder as the program.\n"; return false; }
    string line;
    getline(file, line); // skip header
    vector<Order> all;
    int count = 0;
    while (getline(file, line)) {
        if (line.empty()) continue;
        Order o;
        if (!parseOrder(line, o)) continue;
        all.push_back(o);
        orderList.insertAtEnd(o);
        // Only active orders go into the processing queue
        if (o.status == "Pending" || o.status == "Processing")
            processQueue.enqueue(o.id);
        count++;
    }
    insertBalancedBST(all, 0, (int)all.size() - 1);
    cout << "Loaded " << count << " orders from " << filename << ".\n";
    cout << "Processing queue has " << processQueue.size << " pending/processing orders.\n";
    ordersLoaded = true;
    return true;
}

// ---------- Load delivery_routes.csv ----------
bool loadRoutes(const string &filename) {
    ifstream file(filename);
    if (!file) { cout << "Cannot open " << filename << ".\n"; return false; }
    string line;
    getline(file, line); // skip header
    int count = 0;
    while (getline(file, line)) {
        if (line.empty()) continue;
        vector<string> p = splitCSV(line);
        if (p.size() < 4) continue;
        graph.addRoute(p[0], p[1], stoi(p[3])); // use travel_time_min (column 4)
        count++;
    }
    cout << "Loaded " << count << " routes from " << filename << ".\n";
    routesLoaded = true;
    return true;
}

// ---------- Save orders back to CSV ----------
void saveOrders(const string &filename) {
    ofstream file(filename);
    file << "order_id,customer_name,product,category,price_pkr,quantity,order_date,city,status\n";
    ListNode *cur = orderList.head;
    while (cur) {
        Order &o = cur->data;
        file << o.id << "," << o.customer << "," << o.product << ","
             << o.category << "," << o.price << "," << o.quantity << ","
             << o.date << "," << o.city << "," << o.status << "\n";
        cur = cur->next;
    }
    cout << "Saved " << orderList.size << " orders to " << filename << ".\n";
}

// ---------- Timing test: Linked List vs BST ----------
// Same data, same searches. Count steps + time. This is the proof
// for the proposal: BST search is O(log n), List search is O(n).
void compareSearch() {
    if (!ordersLoaded) { cout << "Load orders first (option 1).\n"; return; }
    int testIds[] = {1001, 1010, 1024, 1030, 1017};
    const int REPEAT = 200; // 5 IDs x 200 = 1000 searches

    long listSteps = 0, treeSteps = 0;
    auto start = chrono::high_resolution_clock::now();
    for (int r = 0; r < REPEAT; r++)
        for (int id : testIds) { int s; orderList.searchById(id, s); listSteps += s; }
    auto mid = chrono::high_resolution_clock::now();
    for (int r = 0; r < REPEAT; r++)
        for (int id : testIds) { int s; orderTree.searchById(id, s); treeSteps += s; }
    auto end = chrono::high_resolution_clock::now();

    long listUs = chrono::duration_cast<chrono::microseconds>(mid - start).count();
    long treeUs = chrono::duration_cast<chrono::microseconds>(end - mid).count();

    cout << "\n--- Search Test: 1000 searches (IDs 1001, 1010, 1024, 1030, 1017) ---\n";
    cout << "Linked List: total steps = " << listSteps << ", avg steps/search = " << (listSteps / 1000.0)
         << ", time = " << listUs << " microseconds\n";
    cout << "BST:         total steps = " << treeSteps << ", avg steps/search = " << (treeSteps / 1000.0)
         << ", time = " << treeUs << " microseconds\n";
    cout << "Result: BST checked far fewer nodes. List = O(n), BST = O(log n).\n";
    cout << "Example - search 1030: List checks 30 nodes, BST checks about 5.\n";
}

// ---------- Menu ----------
void showMenu() {
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
    cout << "0. Exit\n";
    cout << "Enter choice: ";
}

int main() {
    // Load delivery routes at start so option 9 works even before orders
    loadRoutes("delivery_routes.csv");

    int choice;
    while (true) {
        showMenu();
        if (!(cin >> choice)) break;
        cin.ignore(); // clear newline after number

        if (choice == 0) { cout << "Goodbye!\n"; break; }

        else if (choice == 1) {
            loadOrders("orders.csv");
        }

        else if (choice == 2) {
            orderList.displayAll();
        }

        else if (choice == 3) {
            Order o;
            cout << "Order ID: "; cin >> o.id; cin.ignore();
            cout << "Customer name: "; getline(cin, o.customer);
            cout << "Product: "; getline(cin, o.product);
            cout << "Category: "; getline(cin, o.category);
            cout << "Price (PKR): "; cin >> o.price;
            cout << "Quantity: "; cin >> o.quantity; cin.ignore();
            cout << "Date (YYYY-MM-DD): "; getline(cin, o.date);
            cout << "City: "; getline(cin, o.city);
            o.status = "Pending";
            if (orderList.searchById(o.id)) {
                cout << "Order ID " << o.id << " already exists. Use a new ID.\n";
            } else {
                orderList.insertAtEnd(o);
                orderTree.insert(o);
                processQueue.enqueue(o.id);
                cout << "Order " << o.id << " added. Status: Pending.\n";
                ordersLoaded = true;
            }
        }

        else if (choice == 4) {
            int id, steps;
            cout << "Enter order ID to search: "; cin >> id;
            Order *o = orderTree.searchById(id, steps);
            if (o) { cout << "Found in BST in " << steps << " steps:\n"; printOrder(*o); }
            else cout << "Order " << id << " not found.\n";
        }

        else if (choice == 5) { // Cancel (undoable)
            int id;
            cout << "Enter order ID to cancel: "; cin >> id;
            Order *o = orderList.searchById(id);
            if (!o) { cout << "Order not found.\n"; }
            else if (o->status == "Cancelled") { cout << "Order is already cancelled.\n"; }
            else {
                undoStack.push({id, o->status, "Cancelled"});
                orderList.updateStatus(id, "Cancelled");
                orderTree.updateStatus(id, "Cancelled");
                cout << "Order " << id << " cancelled. Use option 8 to Undo.\n";
            }
        }

        else if (choice == 6) { // Delete
            int id;
            cout << "Enter order ID to delete: "; cin >> id;
            if (orderList.deleteById(id)) {
                orderTree.deleteById(id);
                cout << "Order " << id << " deleted from Linked List and BST.\n";
            } else cout << "Order not found.\n";
        }

        else if (choice == 7) { // Process next (Queue FIFO)
            int id = processQueue.dequeue();
            if (id == -1) { cout << "Queue is empty. No order to process.\n"; }
            else {
                Order *o = orderList.searchById(id);
                cout << "Processing order " << id << " (first in queue):\n";
                if (o) {
                    undoStack.push({id, o->status, "Processing"});
                    printOrder(*o);
                    orderList.updateStatus(id, "Processing");
                    orderTree.updateStatus(id, "Processing");
                }
                cout << "Orders left in queue: " << processQueue.size << "\n";
                processQueue.display();
            }
        }

        else if (choice == 8) { // Undo (Stack LIFO)
            Action a;
            if (!undoStack.pop(a)) { cout << "Nothing to undo.\n"; }
            else {
                orderList.updateStatus(a.orderId, a.oldStatus);
                orderTree.updateStatus(a.orderId, a.oldStatus);
                cout << "Undo done. Order " << a.orderId
                     << " status back to: " << a.oldStatus << "\n";
            }
        }

        else if (choice == 9) { // Graph - fastest route
            string dest;
            graph.display();
            cout << "Warehouse is in Lahore.\nEnter delivery city: ";
            getline(cin, dest);
            graph.shortestPath("Lahore", dest);
        }

        else if (choice == 10) {
            compareSearch();
        }

        else if (choice == 11) {
            cout << "\n--- Report ---\n";
            cout << "Total orders: " << orderList.size << "\n";
            cout << "Pending: " << orderList.countStatus("Pending") << "\n";
            cout << "Processing: " << orderList.countStatus("Processing") << "\n";
            cout << "Shipped: " << orderList.countStatus("Shipped") << "\n";
            cout << "Delivered: " << orderList.countStatus("Delivered") << "\n";
            cout << "Cancelled: " << orderList.countStatus("Cancelled") << "\n";
            cout << "Total sales (not cancelled): Rs." << orderList.totalSales() << "\n";
            processQueue.display();
        }

        else if (choice == 12) {
            saveOrders("orders.csv");
        }

        else {
            cout << "Wrong choice. Try 0-12.\n";
        }
    }
    return 0;
}
