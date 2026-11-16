#pragma once
// Order.h — Phase 3a: moved verbatim from main.cpp v1 (archive/main_v1.cpp).
// Order struct + CSV parsing. Unchanged.

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ---------- Order ----------
// One row from orders.csv
struct Order {
    int id = 0;
    std::string customer;
    std::string product;
    std::string category;
    int price = 0;
    int quantity = 1;
    std::string date;
    std::string city;
    std::string status = "Pending"; // Pending / Processing / Shipped / Delivered / Cancelled
};

inline void printOrder(const Order &o) {
    std::cout << o.id << " | " << o.customer << " | " << o.product
         << " | " << o.category << " | Rs." << o.price
         << " x" << o.quantity << " | " << o.date
         << " | " << o.city << " | " << o.status << std::endl;
}

// Split one CSV line by comma. Our data has no commas inside fields,
// so a simple split is enough and easy to explain.
inline std::vector<std::string> splitCSV(const std::string &line) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) parts.push_back(item);
    return parts;
}

// Turn one CSV line into an Order. Returns false if the line is bad.
inline bool parseOrder(const std::string &line, Order &o) {
    std::vector<std::string> p = splitCSV(line);
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
