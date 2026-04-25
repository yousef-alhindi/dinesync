// protocol_handler.h
// ECE 470 - Project 1 Part 3
// Yousef Alhindi
//
// Server-side protocol handler. Takes incoming JSON request strings,
// parses the msg_type, and dispatches to the appropriate business
// logic. Returns a JSON response string.
// This is the middleware server operations layer.

#ifndef PROTOCOL_HANDLER_H
#define PROTOCOL_HANDLER_H

#include "data_model.h"
#include "protocol_messages.h"

using marshal::loginResponse;
using marshal::logoutResponse;
using marshal::viewMenuResponse;
using marshal::orderResponse;
using marshal::errorResponse;
using marshal::kitchenNotify;
#include <string>
#include <map>
#include <mutex>
#include <sstream>
#include <algorithm>

// ============================================================
// Minimal JSON parser helpers (extract string/int values by key)
// ============================================================

inline std::string jsonGetString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\": \"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\":\""; // no space
        pos = json.find(search);
    }
    if (pos == std::string::npos) return "";
    size_t start = pos + search.size();
    size_t end = json.find("\"", start);
    if (end == std::string::npos) return "";
    return json.substr(start, end - start);
}

inline int jsonGetInt(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\": ";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\":";
        pos = json.find(search);
    }
    if (pos == std::string::npos) return -1;
    size_t start = pos + search.size();
    // skip whitespace
    while (start < json.size() && json[start] == ' ') start++;
    std::string num;
    while (start < json.size() && (isdigit(json[start]) || json[start] == '-' || json[start] == '.'))
        num += json[start++];
    if (num.empty()) return -1;
    return std::stoi(num);
}

inline float jsonGetFloat(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\": ";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\":";
        pos = json.find(search);
    }
    if (pos == std::string::npos) return -1.0f;
    size_t start = pos + search.size();
    while (start < json.size() && json[start] == ' ') start++;
    std::string num;
    while (start < json.size() && (isdigit(json[start]) || json[start] == '-' || json[start] == '.'))
        num += json[start++];
    if (num.empty()) return -1.0f;
    return std::stof(num);
}

// Extract array of objects [{...}, {...}] as vector of strings
inline std::vector<std::string> jsonGetArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::string search = "\"" + key + "\": [";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\":[";
        pos = json.find(search);
    }
    if (pos == std::string::npos) return result;
    size_t start = json.find('[', pos);
    if (start == std::string::npos) return result;

    int depth = 0;
    size_t objStart = 0;
    for (size_t i = start; i < json.size(); i++) {
        if (json[i] == '{') {
            if (depth == 1) objStart = i;
            depth++;
        } else if (json[i] == '}') {
            depth--;
            if (depth == 1) {
                result.push_back(json.substr(objStart, i - objStart + 1));
            }
        } else if (json[i] == ']' && depth == 1) {
            break;
        } else if (json[i] == '[') {
            depth++;
        }
    }
    return result;
}

// ============================================================
// Server State: users, menu, orders, sessions
// ============================================================

struct ServerState {
    // Users (hardcoded)
    std::vector<User> users;

    // Menu
    Menu menu;

    // Orders
    std::vector<DineInOrder> dineInOrders;
    std::vector<TakeOutOrder> takeOutOrders;
    int nextOrderId = 1001;

    // Active sessions: token -> username
    std::map<std::string, std::string> sessions;
    int nextToken = 1;

    // Kitchen notifications (buffered for retrieval)
    std::vector<std::string> kitchenNotifications;

    ServerState() {
        // Initialize users
        users.push_back(User("manager1", "admin", Role::MANAGER));
        users.push_back(User("server1", "pass123", Role::SERVER));
        users.push_back(User("chef1", "kitchen", Role::CHEF));

        // Initialize menu with sample items matching P2 test data
        menu.addItem(MenuItem(1, "Bruschetta", Category::STARTER, 8.99f));
        menu.addItem(MenuItem(2, "Soup of the Day", Category::STARTER, 6.99f));
        menu.addItem(MenuItem(3, "Caesar Salad", Category::STARTER, 7.99f));
        menu.addItem(MenuItem(4, "Grilled Salmon", Category::MAIN, 18.99f));
        menu.addItem(MenuItem(5, "Pasta Carbonara", Category::MAIN, 14.99f));
        menu.addItem(MenuItem(6, "Ribeye Steak", Category::MAIN, 24.99f));
        menu.addItem(MenuItem(7, "Margherita Pizza", Category::MAIN, 12.99f));
        menu.addItem(MenuItem(8, "Chicken Parmesan", Category::MAIN, 16.99f));
        menu.addItem(MenuItem(9, "Tiramisu", Category::DESSERT, 7.99f));
        menu.addItem(MenuItem(10, "Cheesecake", Category::DESSERT, 6.99f));
        menu.addItem(MenuItem(11, "Chocolate Lava Cake", Category::DESSERT, 8.99f));
        menu.addItem(MenuItem(12, "Lemonade", Category::DRINK, 3.99f));
        menu.addItem(MenuItem(13, "Iced Tea", Category::DRINK, 2.99f));
        menu.addItem(MenuItem(14, "Sparkling Water", Category::DRINK, 1.99f));
        menu.addItem(MenuItem(15, "House Red Wine", Category::DRINK, 9.99f));
        menu.addItem(MenuItem(16, "Craft Beer", Category::DRINK, 5.99f));
    }

    std::string generateToken() {
        return "tok_" + std::to_string(nextToken++);
    }

    User* findUser(const std::string& username) {
        for (auto& u : users) {
            if (u.username == username) return &u;
        }
        return nullptr;
    }

    std::string getUsernameForToken(const std::string& token) {
        auto it = sessions.find(token);
        if (it != sessions.end()) return it->second;
        return "";
    }

    bool isValidToken(const std::string& token) {
        return sessions.find(token) != sessions.end();
    }

    Role getRoleForToken(const std::string& token) {
        std::string uname = getUsernameForToken(token);
        if (uname.empty()) return Role::SERVER; // default, but isValidToken will catch
        User* u = findUser(uname);
        return u ? u->role : Role::SERVER;
    }
};

// ============================================================
// Protocol Handler - process one JSON request, return response
// ============================================================

inline std::string handleRequest(ServerState& state, const std::string& json) {
    std::string msgType = jsonGetString(json, "msg_type");

    // ---- LOGIN ----
    if (msgType == "LOGIN_REQ") {
        std::string username = jsonGetString(json, "username");
        std::string password = jsonGetString(json, "password");

        User* user = state.findUser(username);
        if (!user || user->password != password) {
            return errorResponse("LOGIN_RESP", "Invalid username or password");
        }

        std::string token = state.generateToken();
        state.sessions[token] = username;
        return loginResponse(token, user->role);
    }

    // ---- LOGOUT ----
    if (msgType == "LOGOUT_REQ") {
        std::string token = jsonGetString(json, "token");
        state.sessions.erase(token);
        return logoutResponse(0);
    }

    // All other requests require valid token
    std::string token = jsonGetString(json, "token");
    Role role = state.getRoleForToken(token);
    if (!state.isValidToken(token)) {
        std::string respType = msgType;
        // Safely derive response type: replace trailing REQ with RESP
        if (respType.size() > 3 && respType.substr(respType.size()-3) == "REQ") {
            respType = respType.substr(0, respType.size()-3) + "RESP";
        } else {
            respType = "ERROR";
        }
        return errorResponse(respType, "Invalid or expired session token");
    }

    // ---- VIEW MENU ----
    if (msgType == "VIEW_MENU_REQ") {
        return viewMenuResponse(state.menu);
    }

    // ---- ADD ITEM (Manager only) ----
    if (msgType == "ADD_ITEM_REQ") {
        if (role != Role::MANAGER) {
            return errorResponse("ADD_ITEM_RESP", "Permission denied: Manager role required");
        }
        std::string name = jsonGetString(json, "name");
        std::string catStr = jsonGetString(json, "category");
        float price = jsonGetFloat(json, "price");

        Category cat = Category::STARTER;
        if (catStr == "STARTER") cat = Category::STARTER;
        else if (catStr == "MAIN") cat = Category::MAIN;
        else if (catStr == "DESSERT") cat = Category::DESSERT;
        else if (catStr == "DRINK") cat = Category::DRINK;

        int newId = state.menu.items.size() + 1;
        MenuItem item = MenuItem(newId, name, cat, price);
        try {
            state.menu.addItem(item);
            std::string resp = "{ \"msg_type\": \"ADD_ITEM_RESP\", \"status\": 0, \"data\": { ";
            resp += jsonInt("item_id", newId) + ", " + jsonString("name", name) + " } }";
            return resp;
        } catch (const std::runtime_error& e) {
            return errorResponse("ADD_ITEM_RESP", e.what());
        }
    }

    // ---- UPDATE ITEM (Manager only) ----
    if (msgType == "UPDATE_ITEM_REQ") {
        if (role != Role::MANAGER) {
            return errorResponse("UPDATE_ITEM_RESP", "Permission denied: Manager role required");
        }
        int itemId = jsonGetInt(json, "item_id");
        float price = jsonGetFloat(json, "price");

        for (auto& item : state.menu.items) {
            if (item.item_id == itemId) {
                if (price > 0) item.price = price;
                std::string resp = "{ \"msg_type\": \"UPDATE_ITEM_RESP\", \"status\": 0, \"data\": { ";
                resp += jsonInt("item_id", itemId) + ", " + jsonFloat("price", item.price) + " } }";
                return resp;
            }
        }
        return errorResponse("UPDATE_ITEM_RESP", "Item not found");
    }

    // ---- REMOVE ITEM (Manager only) ----
    if (msgType == "REMOVE_ITEM_REQ") {
        if (role != Role::MANAGER) {
            return errorResponse("REMOVE_ITEM_RESP", "Permission denied: Manager role required");
        }
        int itemId = jsonGetInt(json, "item_id");
        try {
            state.menu.removeItem(itemId);
            std::string resp = "{ \"msg_type\": \"REMOVE_ITEM_RESP\", \"status\": 0 }";
            return resp;
        } catch (const std::runtime_error& e) {
            return errorResponse("REMOVE_ITEM_RESP", e.what());
        }
    }

    // ---- DINE-IN ORDER (Server only) ----
    if (msgType == "DINEIN_ORDER_REQ") {
        if (role != Role::SERVER) {
            return errorResponse("DINEIN_ORDER_RESP", "Permission denied: Server role required");
        }
        int table = jsonGetInt(json, "table_number");
        auto guestJsons = jsonGetArray(json, "guests");

        std::vector<GuestOrder> guests;
        for (auto& gj : guestJsons) {
            GuestOrder go;
            go.guest_number = jsonGetInt(gj, "guest_number");
            go.drink_id = jsonGetInt(gj, "drink_id");
            go.starter_id = jsonGetInt(gj, "starter_id");
            go.main_id = jsonGetInt(gj, "main_id");
            go.dessert_id = jsonGetInt(gj, "dessert_id");
            guests.push_back(go);
        }

        // Validate item IDs
        for (auto& g : guests) {
            int ids[] = { g.drink_id, g.starter_id, g.main_id, g.dessert_id };
            for (int id : ids) {
                bool found = false;
                for (auto& item : state.menu.items) {
                    if (item.item_id == id && item.available) { found = true; break; }
                }
                if (!found) {
                    return errorResponse("DINEIN_ORDER_RESP",
                        "Invalid menu item ID: " + std::to_string(id));
                }
            }
        }

        int orderId = state.nextOrderId++;
        DineInOrder order = DineInOrder(); order.order_id = orderId; order.table_number = table; order.guests = guests; order.status = OrderStatus::PLACED;
        state.dineInOrders.push_back(order);

        // Compute bill
        Bill bill = Bill(); bill.order_id = orderId; bill.order_type = OrderType::DINE_IN;
        for (auto& g : guests) {
            int ids[] = { g.drink_id, g.starter_id, g.main_id, g.dessert_id };
            for (int id : ids) {
                for (auto& item : state.menu.items) {
                    if (item.item_id == id) {
                        bill.line_items.push_back(LineItem(item.name, 1, item.price));
                        break;
                    }
                }
            }
        }
        bill.calculate();

        // Generate kitchen notification
        std::string summary = "Table " + std::to_string(table) + ", " +
                            std::to_string(guests.size()) + " guests: ";
        for (size_t i = 0; i < bill.line_items.size(); i++) {
            if (i > 0) summary += ", ";
            summary += bill.line_items[i].name;
        }
        KitchenNotification notif; notif.order_id = orderId; notif.order_type = OrderType::DINE_IN; notif.summary = summary;
        state.kitchenNotifications.push_back(kitchenNotify(notif));

        return orderResponse("DINEIN_ORDER_RESP", bill);
    }

    // ---- TAKE-OUT ORDER (Server only) ----
    if (msgType == "TAKEOUT_ORDER_REQ") {
        if (role != Role::SERVER) {
            return errorResponse("TAKEOUT_ORDER_RESP", "Permission denied: Server role required");
        }
        std::string guestName = jsonGetString(json, "guest_name");
        auto itemJsons = jsonGetArray(json, "items");

        std::vector<TakeOutItem> items;
        for (auto& ij : itemJsons) {
            TakeOutItem ti;
            ti.item_id = jsonGetInt(ij, "item_id");
            ti.quantity = jsonGetInt(ij, "quantity");
            items.push_back(ti);
        }

        int orderId = state.nextOrderId++;
        TakeOutOrder order = TakeOutOrder(); order.order_id = orderId; order.guest_name = guestName; order.items = items; order.status = OrderStatus::PLACED;
        state.takeOutOrders.push_back(order);

        // Compute bill
        Bill bill = Bill(); bill.order_id = orderId; bill.order_type = OrderType::TAKE_OUT;
        for (auto& ti : items) {
            for (auto& item : state.menu.items) {
                if (item.item_id == ti.item_id && item.available) {
                    bill.line_items.push_back(LineItem(item.name, ti.quantity, item.price));
                    break;
                }
            }
        }
        bill.calculate();

        // kitchen notification
        std::string summary = "Take-out for " + guestName + ": ";
        for (size_t i = 0; i < bill.line_items.size(); i++) {
            if (i > 0) summary += ", ";
            summary += std::to_string(bill.line_items[i].quantity) + "x " + bill.line_items[i].name;
        }
        KitchenNotification notif; notif.order_id = orderId; notif.order_type = OrderType::TAKE_OUT; notif.summary = summary;
        state.kitchenNotifications.push_back(kitchenNotify(notif));

        return orderResponse("TAKEOUT_ORDER_RESP", bill);
    }

    // ---- GET BILL ----
    if (msgType == "GET_BILL_REQ") {
        int orderId = jsonGetInt(json, "order_id");

        // Search dine-in orders
        for (auto& order : state.dineInOrders) {
            if (order.order_id == orderId) {
                Bill bill; bill.order_id = orderId; bill.order_type = OrderType::DINE_IN;
                for (auto& g : order.guests) {
                    int ids[] = { g.drink_id, g.starter_id, g.main_id, g.dessert_id };
                    for (int id : ids) {
                        for (auto& item : state.menu.items) {
                            if (item.item_id == id) {
                                bill.line_items.push_back(LineItem(item.name, 1, item.price));
                                break;
                            }
                        }
                    }
                }
                bill.calculate();
                return orderResponse("GET_BILL_RESP", bill);
            }
        }

        // Search take-out orders
        for (auto& order : state.takeOutOrders) {
            if (order.order_id == orderId) {
                Bill bill; bill.order_id = orderId; bill.order_type = OrderType::TAKE_OUT;
                for (auto& ti : order.items) {
                    for (auto& item : state.menu.items) {
                        if (item.item_id == ti.item_id && item.available) {
                            bill.line_items.push_back(LineItem(item.name, ti.quantity, item.price));
                            break;
                        }
                    }
                }
                bill.calculate();
                return orderResponse("GET_BILL_RESP", bill);
            }
        }

        return errorResponse("GET_BILL_RESP", "Order not found: " + std::to_string(orderId));
    }

    // ============================================================
    // LIST_ORDERS_REQ - list all orders (Chef and Server)
    // ============================================================
    if (msgType == "LIST_ORDERS_REQ") {
        std::string result = "{ \"msg_type\": \"LIST_ORDERS_RESP\", \"status\": 0, \"orders\": [";
        bool first = true;

        for (auto& o : state.dineInOrders) {
            if (!first) result += ", ";
            first = false;
            result += "{ \"order_id\": " + std::to_string(o.order_id) +
                      ", \"type\": \"DINE_IN\", \"table\": " + std::to_string(o.table_number) +
                      ", \"guests\": " + std::to_string(o.guests.size()) +
                      ", \"status\": \"" + statusToString(o.status) + "\" }";
        }
        for (auto& o : state.takeOutOrders) {
            if (!first) result += ", ";
            first = false;
            result += "{ \"order_id\": " + std::to_string(o.order_id) +
                      ", \"type\": \"TAKEOUT\", \"guest_name\": \"" + o.guest_name +
                      "\", \"items\": " + std::to_string(o.items.size()) +
                      ", \"status\": \"" + statusToString(o.status) + "\" }";
        }

        result += "] }";
        return result;
    }

    // ============================================================
    // ORDER_READY_REQ - Chef marks order as COMPLETE
    // ============================================================
    if (msgType == "ORDER_READY_REQ") {
        Role role = state.getRoleForToken(token);
        if (role != Role::CHEF) {
            return errorResponse("ORDER_READY_RESP", "Permission denied: only Chef can mark orders ready");
        }

        int orderId = jsonGetInt(json, "order_id");

        for (auto& o : state.dineInOrders) {
            if (o.order_id == orderId) {
                o.status = OrderStatus::COMPLETE;
                return "{ \"msg_type\": \"ORDER_READY_RESP\", \"status\": 0, "
                       "\"message\": \"Order " + std::to_string(orderId) + " marked as COMPLETE\" }";
            }
        }
        for (auto& o : state.takeOutOrders) {
            if (o.order_id == orderId) {
                o.status = OrderStatus::COMPLETE;
                return "{ \"msg_type\": \"ORDER_READY_RESP\", \"status\": 0, "
                       "\"message\": \"Order " + std::to_string(orderId) + " marked as COMPLETE\" }";
            }
        }
        return errorResponse("ORDER_READY_RESP", "Order not found: " + std::to_string(orderId));
    }

    return errorResponse("ERROR", "Unknown message type: " + msgType);
}

#endif // PROTOCOL_HANDLER_H
