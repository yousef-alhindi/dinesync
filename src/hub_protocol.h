// hub_protocol.h
// Yousef Alhindi
//
// Hub-specific protocol messages for multi-location communication.
// Extends the P1 protocol with REGISTER, SYNC, HEARTBEAT, REPORT, AGGREGATE.
// Uses the same 4-byte length-prefixed JSON framing as P1.

#ifndef HUB_PROTOCOL_H
#define HUB_PROTOCOL_H

#include <string>
#include <vector>
#include <sstream>
#include "data_model.h"

namespace hub_proto {

// ============================================================
// Location -> Hub: Register on startup
// ============================================================
inline std::string registerLocationReq(const std::string& name, int port) {
    return "{"
        "\"msg_type\": \"REGISTER_LOCATION_REQ\", "
        "\"location_name\": \"" + name + "\", "
        "\"port\": " + std::to_string(port) +
        "}";
}

inline std::string registerLocationResp(int status, int location_id, const Menu& menu) {
    std::string json = "{ \"msg_type\": \"REGISTER_LOCATION_RESP\", "
        "\"status\": " + std::to_string(status) + ", "
        "\"location_id\": " + std::to_string(location_id) + ", "
        "\"menu\": { \"items\": [";
    auto items = menu.getAll();
    for (size_t i = 0; i < items.size(); i++) {
        if (i > 0) json += ", ";
        json += "{ \"item_id\": " + std::to_string(items[i].item_id) +
                ", \"name\": \"" + items[i].name + "\""
                ", \"category\": \"" + categoryToString(items[i].category) + "\""
                ", \"price\": " + std::to_string(items[i].price) + " }";
    }
    json += "] } }";
    return json;
}

// ============================================================
// Hub <-> Location: Heartbeat
// ============================================================
inline std::string heartbeatReq(int location_id) {
    return "{"
        "\"msg_type\": \"HEARTBEAT_REQ\", "
        "\"location_id\": " + std::to_string(location_id) +
        "}";
}

inline std::string heartbeatResp(int location_id, int active_clients, int order_count) {
    return "{"
        "\"msg_type\": \"HEARTBEAT_RESP\", "
        "\"location_id\": " + std::to_string(location_id) + ", "
        "\"active_clients\": " + std::to_string(active_clients) + ", "
        "\"order_count\": " + std::to_string(order_count) +
        "}";
}

// ============================================================
// Hub -> Locations: Menu update push
// ============================================================
inline std::string menuUpdatePush(const std::string& action, int item_id,
                                   const std::string& name = "",
                                   const std::string& category = "",
                                   float price = 0.0f) {
    std::string json = "{"
        "\"msg_type\": \"MENU_UPDATE_PUSH\", "
        "\"action\": \"" + action + "\", "
        "\"item\": { \"item_id\": " + std::to_string(item_id);
    if (!name.empty()) json += ", \"name\": \"" + name + "\"";
    if (!category.empty()) json += ", \"category\": \"" + category + "\"";
    if (price > 0) {
        std::ostringstream oss;
        oss << std::fixed;
        oss.precision(2);
        oss << price;
        json += ", \"price\": " + oss.str();
    }
    json += " } }";
    return json;
}

// ============================================================
// Location -> Hub: Report orders
// ============================================================
inline std::string reportOrdersReq(int location_id,
                                    const std::vector<std::pair<int,float>>& orders) {
    // orders = vector of (order_id, total)
    std::string json = "{"
        "\"msg_type\": \"REPORT_ORDERS_REQ\", "
        "\"location_id\": " + std::to_string(location_id) + ", "
        "\"orders\": [";
    for (size_t i = 0; i < orders.size(); i++) {
        if (i > 0) json += ", ";
        std::ostringstream oss;
        oss << std::fixed;
        oss.precision(2);
        oss << orders[i].second;
        json += "{ \"order_id\": " + std::to_string(orders[i].first) +
                ", \"total\": " + oss.str() + " }";
    }
    json += "] }";
    return json;
}

inline std::string reportOrdersResp(int status) {
    return "{ \"msg_type\": \"REPORT_ORDERS_RESP\", \"status\": " +
           std::to_string(status) + " }";
}

// ============================================================
// Admin -> Hub: Login
// ============================================================
inline std::string adminLoginReq(const std::string& username, const std::string& password) {
    return "{"
        "\"msg_type\": \"ADMIN_LOGIN_REQ\", "
        "\"username\": \"" + username + "\", "
        "\"password\": \"" + password + "\""
        "}";
}

inline std::string adminLoginResp(int status, const std::string& token = "",
                                   const std::string& error = "") {
    if (status != 0)
        return "{ \"msg_type\": \"ADMIN_LOGIN_RESP\", \"status\": 1, \"error\": \"" + error + "\" }";
    return "{ \"msg_type\": \"ADMIN_LOGIN_RESP\", \"status\": 0, \"token\": \"" + token + "\" }";
}

// ============================================================
// Admin -> Hub: Update menu (propagates to locations)
// ============================================================
inline std::string adminUpdateMenuReq(const std::string& token, int item_id, float new_price) {
    std::ostringstream oss;
    oss << std::fixed;
    oss.precision(2);
    oss << new_price;
    return "{"
        "\"msg_type\": \"ADMIN_UPDATE_MENU_REQ\", "
        "\"token\": \"" + token + "\", "
        "\"item_id\": " + std::to_string(item_id) + ", "
        "\"new_price\": " + oss.str() +
        "}";
}

inline std::string adminUpdateMenuResp(int status, const std::string& error = "") {
    if (status != 0)
        return "{ \"msg_type\": \"ADMIN_UPDATE_MENU_RESP\", \"status\": 1, \"error\": \"" + error + "\" }";
    return "{ \"msg_type\": \"ADMIN_UPDATE_MENU_RESP\", \"status\": 0 }";
}

// ============================================================
// Admin -> Hub: Aggregate query
// ============================================================
inline std::string aggregateQueryReq(const std::string& token) {
    return "{"
        "\"msg_type\": \"AGGREGATE_QUERY_REQ\", "
        "\"token\": \"" + token + "\""
        "}";
}

inline std::string aggregateQueryResp(float total_revenue, int total_orders,
        const std::vector<std::tuple<int,std::string,float,int,std::string>>& locs) {
    // locs = vector of (id, name, revenue, orders, status)
    std::ostringstream oss;
    oss << std::fixed;
    oss.precision(2);
    oss << "{ \"msg_type\": \"AGGREGATE_QUERY_RESP\", \"status\": 0, "
        << "\"total_revenue\": " << total_revenue << ", "
        << "\"total_orders\": " << total_orders << ", "
        << "\"locations\": [";
    for (size_t i = 0; i < locs.size(); i++) {
        if (i > 0) oss << ", ";
        oss << "{ \"id\": " << std::get<0>(locs[i])
            << ", \"name\": \"" << std::get<1>(locs[i]) << "\""
            << ", \"revenue\": " << std::get<2>(locs[i])
            << ", \"orders\": " << std::get<3>(locs[i])
            << ", \"status\": \"" << std::get<4>(locs[i]) << "\" }";
    }
    oss << "] }";
    return oss.str();
}

// ============================================================
// Admin -> Hub: List locations
// ============================================================
inline std::string listLocationsReq(const std::string& token) {
    return "{"
        "\"msg_type\": \"LIST_LOCATIONS_REQ\", "
        "\"token\": \"" + token + "\""
        "}";
}

inline std::string listLocationsResp(
        const std::vector<std::tuple<int,std::string,std::string>>& locs) {
    // locs = vector of (id, name, status)
    std::string json = "{ \"msg_type\": \"LIST_LOCATIONS_RESP\", \"status\": 0, \"locations\": [";
    for (size_t i = 0; i < locs.size(); i++) {
        if (i > 0) json += ", ";
        json += "{ \"id\": " + std::to_string(std::get<0>(locs[i])) +
                ", \"name\": \"" + std::get<1>(locs[i]) + "\""
                ", \"status\": \"" + std::get<2>(locs[i]) + "\" }";
    }
    json += "] }";
    return json;
}

// ============================================================
// Admin logout
// ============================================================
inline std::string adminLogoutReq(const std::string& token) {
    return "{ \"msg_type\": \"ADMIN_LOGOUT_REQ\", \"token\": \"" + token + "\" }";
}

} // namespace hub_proto

#endif // HUB_PROTOCOL_H
