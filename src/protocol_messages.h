// protocol_messages.h
// ECE 470 - Project 1 Part 2
// Yousef Alhindi
//
// Application protocol message layer.
// Handles creation, marshaling (serialize to JSON string), and
// unmarshaling (deserialize from JSON string) of protocol messages.
// This corresponds to the gRPC message definitions in restaurant.proto
// and demonstrates the wire format for our length-prefixed JSON protocol.
//
// In the full implementation (Part 3+), gRPC handles serialization via
// protobuf. For Part 2, we demonstrate the concept with JSON strings
// to show creation/marshaling/unmarshaling of each message type.

#ifndef PROTOCOL_MESSAGES_H
#define PROTOCOL_MESSAGES_H

#include "data_model.h"
#include <string>
#include <sstream>
#include <vector>
#include <cstring>
#include <arpa/inet.h>  // for htonl/ntohl

// ============================================================
// Simple JSON builder helpers (no external deps)
// ============================================================

inline std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

inline std::string jsonString(const std::string& key, const std::string& val) {
    return "\"" + key + "\": \"" + jsonEscape(val) + "\"";
}

inline std::string jsonInt(const std::string& key, int val) {
    return "\"" + key + "\": " + std::to_string(val);
}

inline std::string jsonFloat(const std::string& key, float val) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << val;
    return "\"" + key + "\": " + oss.str();
}

inline std::string jsonBool(const std::string& key, bool val) {
    return "\"" + key + "\": " + (val ? "true" : "false");
}

// ============================================================
// Frame encoding: [4-byte big-endian length][JSON payload]
// This is how messages go on the wire over TCP.
// ============================================================

inline std::string frameMessage(const std::string& json) {
    uint32_t len = htonl(static_cast<uint32_t>(json.size()));
    std::string frame(4, '\0');
    std::memcpy(&frame[0], &len, 4);
    frame += json;
    return frame;
}

inline bool unframeMessage(const std::string& frame, std::string& json) {
    if (frame.size() < 4) return false;
    uint32_t len;
    std::memcpy(&len, frame.data(), 4);
    len = ntohl(len);
    if (frame.size() < 4 + len) return false;
    json = frame.substr(4, len);
    return true;
}

// ============================================================
// Message Marshaling (object -> JSON string)
// Each function creates the JSON payload for a specific msg_type.
// ============================================================

namespace marshal {

// LOGIN_REQ
inline std::string loginRequest(const std::string& username, const std::string& password) {
    return "{"
        "\"msg_type\": \"LOGIN_REQ\", "
        "\"data\": {" +
            jsonString("username", username) + ", " +
            jsonString("password", password) +
        "}}";
}

// LOGIN_RESP
inline std::string loginResponse(const std::string& token, Role role, int status = 0) {
    return "{"
        "\"msg_type\": \"LOGIN_RESP\", " +
        jsonInt("status", status) + ", "
        "\"data\": {" +
            jsonString("token", token) + ", " +
            jsonString("role", roleToString(role)) +
        "}}";
}

// LOGOUT_REQ
inline std::string logoutRequest(const std::string& token) {
    return "{"
        "\"msg_type\": \"LOGOUT_REQ\", " +
        jsonString("token", token) +
    "}";
}

// LOGOUT_RESP
inline std::string logoutResponse(int status = 0) {
    return "{"
        "\"msg_type\": \"LOGOUT_RESP\", " +
        jsonInt("status", status) +
    "}";
}

// VIEW_MENU_REQ
inline std::string viewMenuRequest(const std::string& token) {
    return "{"
        "\"msg_type\": \"VIEW_MENU_REQ\", " +
        jsonString("token", token) +
    "}";
}

// VIEW_MENU_RESP
inline std::string viewMenuResponse(const Menu& menu) {
    std::ostringstream oss;
    oss << "{\"msg_type\": \"VIEW_MENU_RESP\", \"status\": 0, \"data\": {\"menu\": [";
    for (size_t i = 0; i < menu.items.size(); i++) {
        if (i > 0) oss << ", ";
        const auto& it = menu.items[i];
        oss << "{" << jsonInt("item_id", it.item_id) << ", "
            << jsonString("name", it.name) << ", "
            << jsonString("category", categoryToString(it.category)) << ", "
            << jsonFloat("price", it.price) << ", "
            << jsonBool("available", it.available) << "}";
    }
    oss << "]}}";
    return oss.str();
}

// ADD_ITEM_REQ
inline std::string addItemRequest(const std::string& token, const std::string& name,
                                   Category cat, float price) {
    return "{"
        "\"msg_type\": \"ADD_ITEM_REQ\", " +
        jsonString("token", token) + ", "
        "\"data\": {" +
            jsonString("name", name) + ", " +
            jsonString("category", categoryToString(cat)) + ", " +
            jsonFloat("price", price) +
        "}}";
}

// UPDATE_ITEM_REQ
inline std::string updateItemRequest(const std::string& token, int item_id,
                                      float newPrice) {
    return "{"
        "\"msg_type\": \"UPDATE_ITEM_REQ\", " +
        jsonString("token", token) + ", "
        "\"data\": {" +
            jsonInt("item_id", item_id) + ", " +
            jsonFloat("price", newPrice) +
        "}}";
}

// DINEIN_ORDER_REQ
inline std::string dineInOrderRequest(const std::string& token, int table,
                                       const std::vector<GuestOrder>& guests) {
    std::ostringstream oss;
    oss << "{\"msg_type\": \"DINEIN_ORDER_REQ\", "
        << jsonString("token", token) << ", "
        << "\"data\": {" << jsonInt("table_number", table) << ", \"guests\": [";
    for (size_t i = 0; i < guests.size(); i++) {
        if (i > 0) oss << ", ";
        const auto& g = guests[i];
        oss << "{" << jsonInt("guest_number", g.guest_number) << ", "
            << jsonInt("drink_id", g.drink_id) << ", "
            << jsonInt("starter_id", g.starter_id) << ", "
            << jsonInt("main_id", g.main_id) << ", "
            << jsonInt("dessert_id", g.dessert_id) << "}";
    }
    oss << "]}}";
    return oss.str();
}

// TAKEOUT_ORDER_REQ
inline std::string takeOutOrderRequest(const std::string& token,
                                        const std::string& guestName,
                                        const std::vector<TakeOutItem>& items) {
    std::ostringstream oss;
    oss << "{\"msg_type\": \"TAKEOUT_ORDER_REQ\", "
        << jsonString("token", token) << ", "
        << "\"data\": {" << jsonString("guest_name", guestName) << ", \"items\": [";
    for (size_t i = 0; i < items.size(); i++) {
        if (i > 0) oss << ", ";
        oss << "{" << jsonInt("item_id", items[i].item_id) << ", "
            << jsonInt("quantity", items[i].quantity) << "}";
    }
    oss << "]}}";
    return oss.str();
}

// ORDER_RESP (used for both DINEIN_ORDER_RESP and TAKEOUT_ORDER_RESP)
inline std::string orderResponse(const std::string& msgType, const Bill& bill) {
    std::ostringstream oss;
    oss << "{\"msg_type\": \"" << msgType << "\", \"status\": 0, \"data\": {"
        << jsonInt("order_id", bill.order_id) << ", \"bill\": {"
        << "\"line_items\": [";
    for (size_t i = 0; i < bill.line_items.size(); i++) {
        if (i > 0) oss << ", ";
        const auto& li = bill.line_items[i];
        oss << "{" << jsonString("name", li.name) << ", "
            << jsonInt("qty", li.quantity) << ", "
            << jsonFloat("unit_price", li.unit_price) << ", "
            << jsonFloat("subtotal", li.subtotal) << "}";
    }
    oss << "], " << jsonFloat("tax", bill.tax) << ", "
        << jsonFloat("total", bill.total) << "}}}";
    return oss.str();
}

// KITCHEN_NOTIFY
inline std::string kitchenNotify(const KitchenNotification& notif) {
    return "{"
        "\"msg_type\": \"KITCHEN_NOTIFY\", "
        "\"data\": {" +
            jsonInt("order_id", notif.order_id) + ", " +
            jsonString("order_type", orderTypeToString(notif.order_type)) + ", " +
            jsonString("summary", notif.summary) + ", " +
            jsonString("timestamp", notif.timestamp) +
        "}}";
}

// ERROR response
inline std::string errorResponse(const std::string& msgType, const std::string& errMsg) {
    return "{"
        "\"msg_type\": \"" + msgType + "\", " +
        jsonInt("status", 1) + ", "
        "\"data\": {" +
            jsonString("error_message", errMsg) +
        "}}";
}

inline std::string removeItemRequest(const std::string& token, int item_id) {
    return "{"
        " \"msg_type\": \"REMOVE_ITEM_REQ\","
        " " + jsonString("token", token) + ","
        " " + jsonInt("item_id", item_id) +
        " }";
}

inline std::string getBillRequest(const std::string& token, int order_id) {
    return "{"
        " \"msg_type\": \"GET_BILL_REQ\","
        " " + jsonString("token", token) + ","
        " " + jsonInt("order_id", order_id) +
        " }";
}

// ============================================================
// P4: List Orders and Order Ready
// ============================================================

inline std::string listOrdersRequest(const std::string& token) {
    return "{"
        "\"msg_type\": \"LIST_ORDERS_REQ\", "
        "\"token\": \"" + token + "\""
    "}";
}

inline std::string orderReadyRequest(const std::string& token, int order_id) {
    return "{"
        "\"msg_type\": \"ORDER_READY_REQ\", "
        "\"token\": \"" + token + "\", "
        "\"order_id\": " + std::to_string(order_id) +
    "}";
}

} // namespace marshal

// ============================================================
// Convenience aliases for app_protocol.h (server-side responses)
// These wrap the marshal namespace functions for cleaner server code
// ============================================================

inline std::string marshalLoginResp(const std::string& token, const std::string& role) {
    // map string role to Role enum
    Role r = Role::SERVER;
    if (role == "MANAGER") r = Role::MANAGER;
    else if (role == "CHEF") r = Role::CHEF;
    return marshal::loginResponse(token, r, 0);
}

inline std::string marshalLogoutResp() {
    return marshal::logoutResponse(0);
}

inline std::string marshalViewMenuResp(const Menu& menu) {
    return marshal::viewMenuResponse(menu);
}

inline std::string marshalUpdateItemResp(const MenuItem& item) {
    return "{" + jsonString("msg_type", "UPDATE_ITEM_RESP") + ", " +
           jsonInt("status", 0) + ", \"data\": {" +
           jsonInt("item_id", item.item_id) + ", " +
           jsonString("name", item.name) + ", " +
           jsonFloat("price", item.price) + "}}";
}

inline std::string marshalDineInOrderResp(int orderId, const Bill& bill) {
    return marshal::orderResponse("DINEIN_ORDER_RESP", bill);
}

inline std::string marshalError(const std::string& msgType, const std::string& errMsg) {
    return marshal::errorResponse(msgType, errMsg);
}

#endif // PROTOCOL_MESSAGES_H

