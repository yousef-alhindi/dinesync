// data_model.h
// ECE 470 - Project 1 Part 2
// Yousef Alhindi
//
// Data model classes for the Restaurant Management System.
// Covers: User, MenuItem, Menu, GuestOrder, DineInOrder,
//         TakeOutItem, TakeOutOrder, Bill, KitchenNotification

#ifndef DATA_MODEL_H
#define DATA_MODEL_H

#include <string>
#include <vector>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>

// ---- Enums ----

enum class Role { MANAGER, SERVER, CHEF };

enum class Category { STARTER, MAIN, DESSERT, DRINK };

enum class OrderStatus { PLACED, IN_PROGRESS, COMPLETE };

enum class OrderType { DINE_IN, TAKE_OUT };

// helper to convert enums to string for printing/debugging
inline std::string roleToString(Role r) {
    switch (r) {
        case Role::MANAGER: return "MANAGER";
        case Role::SERVER:  return "SERVER";
        case Role::CHEF:    return "CHEF";
    }
    return "UNKNOWN";
}

inline std::string categoryToString(Category c) {
    switch (c) {
        case Category::STARTER: return "STARTER";
        case Category::MAIN:    return "MAIN";
        case Category::DESSERT: return "DESSERT";
        case Category::DRINK:   return "DRINK";
    }
    return "UNKNOWN";
}

inline std::string statusToString(OrderStatus s) {
    switch (s) {
        case OrderStatus::PLACED:      return "PLACED";
        case OrderStatus::IN_PROGRESS: return "IN_PROGRESS";
        case OrderStatus::COMPLETE:    return "COMPLETE";
    }
    return "UNKNOWN";
}

inline std::string orderTypeToString(OrderType t) {
    switch (t) {
        case OrderType::DINE_IN:  return "DINE_IN";
        case OrderType::TAKE_OUT: return "TAKE_OUT";
    }
    return "UNKNOWN";
}

// ---- Utility ----

inline std::string currentTimestamp() {
    auto t = std::time(nullptr);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

// ---- Data Model Classes ----

struct User {
    std::string username;
    std::string password;
    Role role;

    User() : role(Role::SERVER) {}
    User(const std::string& u, const std::string& p, Role r)
        : username(u), password(p), role(r) {}

    std::string toString() const {
        return "User{username=" + username + ", role=" + roleToString(role) + "}";
    }
};

struct MenuItem {
    int item_id;
    std::string name;
    Category category;
    float price;
    bool available;

    MenuItem() : item_id(0), category(Category::STARTER), price(0.0f), available(true) {}
    MenuItem(int id, const std::string& n, Category cat, float p, bool avail = true)
        : item_id(id), name(n), category(cat), price(p), available(avail) {}

    std::string toString() const {
        std::ostringstream oss;
        oss << "MenuItem{id=" << item_id << ", name=" << name
            << ", category=" << categoryToString(category)
            << ", price=" << std::fixed << std::setprecision(2) << price
            << ", available=" << (available ? "true" : "false") << "}";
        return oss.str();
    }
};

// Category capacity limits per project constraints
const int MAX_STARTERS = 3;
const int MAX_MAINS    = 5;
const int MAX_DESSERTS = 3;
const int MAX_DRINKS   = 5;

class Menu {
public:
    std::vector<MenuItem> items;

    // addItem: fails if category at capacity
    void addItem(const MenuItem& item) {
        int count = countByCategory(item.category);
        int cap = categoryCapacity(item.category);
        if (count >= cap) {
            throw std::runtime_error("Cannot add item: " +
                categoryToString(item.category) + " category at capacity (" +
                std::to_string(cap) + ")");
        }
        items.push_back(item);
    }

    // removeItem: sets available=false instead of deleting
    void removeItem(int item_id) {
        for (auto& it : items) {
            if (it.item_id == item_id) {
                it.available = false;
                return;
            }
        }
        throw std::runtime_error("Item not found: " + std::to_string(item_id));
    }

    void updateItem(int item_id, const std::string& newName, float newPrice) {
        for (auto& it : items) {
            if (it.item_id == item_id) {
                if (!newName.empty()) it.name = newName;
                if (newPrice > 0) it.price = newPrice;
                return;
            }
        }
        throw std::runtime_error("Item not found: " + std::to_string(item_id));
    }

    std::vector<MenuItem> getItemsByCategory(Category cat) const {
        std::vector<MenuItem> result;
        for (const auto& it : items) {
            if (it.category == cat && it.available) result.push_back(it);
        }
        return result;
    }

    std::vector<MenuItem> getAll() const { return items; }

    MenuItem* findById(int item_id) {
        for (auto& it : items) {
            if (it.item_id == item_id) return &it;
        }
        return nullptr;
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "Menu{" << items.size() << " items: [";
        for (size_t i = 0; i < items.size(); i++) {
            if (i > 0) oss << ", ";
            oss << items[i].toString();
        }
        oss << "]}";
        return oss.str();
    }

private:
    int countByCategory(Category cat) const {
        return std::count_if(items.begin(), items.end(),
            [cat](const MenuItem& m) { return m.category == cat && m.available; });
    }

    int categoryCapacity(Category cat) const {
        switch (cat) {
            case Category::STARTER: return MAX_STARTERS;
            case Category::MAIN:    return MAX_MAINS;
            case Category::DESSERT: return MAX_DESSERTS;
            case Category::DRINK:   return MAX_DRINKS;
        }
        return 0;
    }
};

struct GuestOrder {
    int guest_number;
    int drink_id;
    int starter_id;
    int main_id;
    int dessert_id;

    GuestOrder() : guest_number(0), drink_id(0), starter_id(0), main_id(0), dessert_id(0) {}
    GuestOrder(int gn, int d, int s, int m, int ds)
        : guest_number(gn), drink_id(d), starter_id(s), main_id(m), dessert_id(ds) {}

    std::string toString() const {
        std::ostringstream oss;
        oss << "GuestOrder{guest=" << guest_number
            << ", drink=" << drink_id << ", starter=" << starter_id
            << ", main=" << main_id << ", dessert=" << dessert_id << "}";
        return oss.str();
    }
};

struct DineInOrder {
    int order_id;
    int table_number;
    std::vector<GuestOrder> guests;
    OrderStatus status;
    std::string timestamp;

    DineInOrder() : order_id(0), table_number(1), status(OrderStatus::PLACED) {
        timestamp = currentTimestamp();
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "DineInOrder{id=" << order_id << ", table=" << table_number
            << ", guests=" << guests.size() << ", status=" << statusToString(status)
            << ", time=" << timestamp << "}";
        return oss.str();
    }
};

struct TakeOutItem {
    int item_id;
    int quantity;

    TakeOutItem() : item_id(0), quantity(0) {}
    TakeOutItem(int id, int qty) : item_id(id), quantity(qty) {}

    std::string toString() const {
        std::ostringstream oss;
        oss << "TakeOutItem{item=" << item_id << ", qty=" << quantity << "}";
        return oss.str();
    }
};

struct TakeOutOrder {
    int order_id;
    std::string guest_name;
    std::vector<TakeOutItem> items;
    OrderStatus status;
    std::string timestamp;

    TakeOutOrder() : order_id(0), status(OrderStatus::PLACED) {
        timestamp = currentTimestamp();
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "TakeOutOrder{id=" << order_id << ", guest=" << guest_name
            << ", items=" << items.size() << ", status=" << statusToString(status)
            << ", time=" << timestamp << "}";
        return oss.str();
    }
};

struct LineItem {
    std::string name;
    int quantity;
    float unit_price;
    float subtotal;

    LineItem() : quantity(0), unit_price(0), subtotal(0) {}
    LineItem(const std::string& n, int q, float up)
        : name(n), quantity(q), unit_price(up), subtotal(q * up) {}

    std::string toString() const {
        std::ostringstream oss;
        oss << name << " x" << quantity << " @ $" << std::fixed
            << std::setprecision(2) << unit_price << " = $" << subtotal;
        return oss.str();
    }
};

struct Bill {
    int order_id;
    OrderType order_type;
    std::vector<LineItem> line_items;
    float tax_rate;
    float tax;
    float total;

    Bill() : order_id(0), order_type(OrderType::DINE_IN),
             tax_rate(0.07f), tax(0), total(0) {}

    void calculate() {
        float subtotal = 0;
        for (const auto& li : line_items) subtotal += li.subtotal;
        tax = subtotal * tax_rate;
        total = subtotal + tax;
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "Bill{order=" << order_id << ", type=" << orderTypeToString(order_type)
            << ", items=[";
        for (size_t i = 0; i < line_items.size(); i++) {
            if (i > 0) oss << "; ";
            oss << line_items[i].toString();
        }
        oss << "], tax=$" << std::fixed << std::setprecision(2) << tax
            << ", total=$" << total << "}";
        return oss.str();
    }
};

struct KitchenNotification {
    int order_id;
    OrderType order_type;
    std::string summary;
    std::string timestamp;

    KitchenNotification() : order_id(0), order_type(OrderType::DINE_IN) {
        timestamp = currentTimestamp();
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "KitchenNotification{order=" << order_id
            << ", type=" << orderTypeToString(order_type)
            << ", summary=" << summary << "}";
        return oss.str();
    }
};

#endif // DATA_MODEL_H
