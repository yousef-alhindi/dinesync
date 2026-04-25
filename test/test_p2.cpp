// test_p2.cpp
// Yousef Alhindi
//
// Integration tests for the multi-location DineSync system.
// Tests Hub registration, menu sync, admin menu updates with push,
// order reporting, aggregate queries, and heartbeat.

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <vector>
#include <cstring>
using namespace std;

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "data_model.h"
#include "tcp_transport.h"
#include "hub_protocol.h"
#include "protocol_messages.h"
#include "protocol_handler.h"

// ============================================================
// Test framework
// ============================================================
mutex g_printMutex;
atomic<int> g_passed(0);
atomic<int> g_failed(0);
atomic<int> g_testNum(0);

void check(bool cond, const string& name) {
    int num = ++g_testNum;
    lock_guard<mutex> lock(g_printMutex);
    if (cond) { cout << "  [PASS] " << num << ". " << name << endl; g_passed++; }
    else       { cout << "  [FAIL] " << num << ". " << name << endl; g_failed++; }
}

// ============================================================
// JSON helpers
// ============================================================
static string jstr(const string& json, const string& key) {
    string s = "\"" + key + "\": \"";
    size_t p = json.find(s);
    if (p == string::npos) { s = "\"" + key + "\":\""; p = json.find(s); }
    if (p == string::npos) return "";
    size_t start = p + s.size();
    size_t end   = json.find("\"", start);
    return (end == string::npos) ? "" : json.substr(start, end - start);
}

static int jint(const string& json, const string& key) {
    string s = "\"" + key + "\": ";
    size_t p = json.find(s);
    if (p == string::npos) { s = "\"" + key + "\":"; p = json.find(s); }
    if (p == string::npos) return -1;
    size_t start = p + s.size();
    while (start < json.size() && json[start] == ' ') start++;
    string num;
    while (start < json.size() && (isdigit(json[start]) || json[start] == '-' || json[start] == '.'))
        num += json[start++];
    return num.empty() ? -1 : stoi(num);
}

static float jfloat(const string& json, const string& key) {
    string s = "\"" + key + "\": ";
    size_t p = json.find(s);
    if (p == string::npos) { s = "\"" + key + "\":"; p = json.find(s); }
    if (p == string::npos) return -1.0f;
    size_t start = p + s.size();
    while (start < json.size() && json[start] == ' ') start++;
    string num;
    while (start < json.size() && (isdigit(json[start]) || json[start] == '-' || json[start] == '.'))
        num += json[start++];
    return num.empty() ? -1.0f : stof(num);
}

// ============================================================
// Embedded Hub Server (for testing)
// ============================================================
struct TestHubState {
    Menu master_menu;
    struct LocInfo {
        int id; string name; int port; int sock; bool online;
        float revenue; int orders;
    };
    vector<LocInfo> locations;
    int next_loc_id = 1;
    map<string, string> admin_sessions;
    int next_tok = 1;
    string admin_user = "chain_admin";
    string admin_pass = "admin2026";
};

TestHubState g_hub;
mutex g_hubMutex;
volatile bool g_hubRunning = false;
int g_hubSock = -1;

void hubAcceptLoop(int port) {
    // Init master menu
    g_hub.master_menu.addItem(MenuItem(1,  "Bruschetta",          Category::STARTER,  8.99f));
    g_hub.master_menu.addItem(MenuItem(2,  "Soup of the Day",     Category::STARTER,  6.99f));
    g_hub.master_menu.addItem(MenuItem(3,  "Caesar Salad",        Category::STARTER,  7.99f));
    g_hub.master_menu.addItem(MenuItem(4,  "Grilled Salmon",      Category::MAIN,    18.99f));
    g_hub.master_menu.addItem(MenuItem(5,  "Pasta Carbonara",     Category::MAIN,    14.99f));
    g_hub.master_menu.addItem(MenuItem(6,  "Ribeye Steak",        Category::MAIN,    24.99f));
    g_hub.master_menu.addItem(MenuItem(7,  "Margherita Pizza",    Category::MAIN,    12.99f));
    g_hub.master_menu.addItem(MenuItem(8,  "Chicken Parmesan",    Category::MAIN,    16.99f));
    g_hub.master_menu.addItem(MenuItem(9,  "Tiramisu",            Category::DESSERT,  7.99f));
    g_hub.master_menu.addItem(MenuItem(10, "Cheesecake",          Category::DESSERT,  6.99f));
    g_hub.master_menu.addItem(MenuItem(11, "Chocolate Lava Cake", Category::DESSERT,  8.99f));
    g_hub.master_menu.addItem(MenuItem(12, "Lemonade",            Category::DRINK,    3.99f));
    g_hub.master_menu.addItem(MenuItem(13, "Iced Tea",            Category::DRINK,    2.99f));
    g_hub.master_menu.addItem(MenuItem(14, "Sparkling Water",     Category::DRINK,    1.99f));
    g_hub.master_menu.addItem(MenuItem(15, "House Red Wine",      Category::DRINK,    9.99f));
    g_hub.master_menu.addItem(MenuItem(16, "Craft Beer",          Category::DRINK,    5.99f));

    sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family      = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port        = htons(port);

    g_hubSock = socket(PF_INET, SOCK_STREAM, 0);
    int optval = 1;
    setsockopt(g_hubSock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    ::bind(g_hubSock, (sockaddr*)&sin, sizeof(sin));
    listen(g_hubSock, 10);
    g_hubRunning = true;

    while (g_hubRunning) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(g_hubSock, &rfds);
        struct timeval tv = {1, 0};
        if (select(g_hubSock + 1, &rfds, NULL, NULL, &tv) <= 0) continue;

        sockaddr_in  fsin; unsigned int alen = sizeof(fsin);
        int csoc = accept(g_hubSock, (sockaddr*)&fsin, &alen);
        if (csoc < 0) continue;

        // Handle in a thread
        thread([csoc]() {
            string firstMsg;
            if (!recvMessage(csoc, firstMsg)) { close(csoc); return; }
            string mt = jstr(firstMsg, "msg_type");

            if (mt == "REGISTER_LOCATION_REQ") {
                string name  = jstr(firstMsg, "location_name");
                int    port2 = jint(firstMsg, "port");
                int    locId;
                {
                    lock_guard<mutex> lock(g_hubMutex);
                    locId = g_hub.next_loc_id++;
                    g_hub.locations.push_back({locId, name, port2, csoc, true, 0, 0});
                    sendMessage(csoc, hub_proto::registerLocationResp(0, locId, g_hub.master_menu));
                }

                // Keep listening for heartbeat/orders
                while (g_hubRunning) {
                    string msg;
                    if (!recvMessage(csoc, msg) || msg.empty()) break;
                    string mt2 = jstr(msg, "msg_type");
                    lock_guard<mutex> lock(g_hubMutex);

                    if (mt2 == "HEARTBEAT_RESP") {
                        for (auto& l : g_hub.locations)
                            if (l.id == locId) { l.online = true; break; }
                    } else if (mt2 == "REPORT_ORDERS_REQ") {
                        for (auto& l : g_hub.locations) {
                            if (l.id == locId) {
                                size_t pos = 0;
                                while ((pos = msg.find("\"total\"", pos)) != string::npos) {
                                    float t = jfloat(msg.substr(pos > 50 ? pos-50 : 0, 200), "total");
                                    if (t > 0) { l.revenue += t; l.orders++; }
                                    pos += 7;
                                }
                                break;
                            }
                        }
                        sendMessage(csoc, hub_proto::reportOrdersResp(0));
                    }
                }

                lock_guard<mutex> lock(g_hubMutex);
                for (auto& l : g_hub.locations)
                    if (l.id == locId) { l.online = false; l.sock = -1; break; }
                close(csoc);

            } else if (mt == "ADMIN_LOGIN_REQ") {
                string user = jstr(firstMsg, "username");
                string pass = jstr(firstMsg, "password");
                string token;
                {
                    lock_guard<mutex> lock(g_hubMutex);
                    if (user == g_hub.admin_user && pass == g_hub.admin_pass) {
                        token = "admin_tok_" + to_string(g_hub.next_tok++);
                        g_hub.admin_sessions[token] = user;
                        sendMessage(csoc, hub_proto::adminLoginResp(0, token));
                    } else {
                        sendMessage(csoc, hub_proto::adminLoginResp(1, "", "Invalid credentials"));
                        close(csoc); return;
                    }
                }

                // Admin session loop
                while (g_hubRunning) {
                    string msg;
                    if (!recvMessage(csoc, msg) || msg.empty()) break;
                    string mt2 = jstr(msg, "msg_type");
                    lock_guard<mutex> lock(g_hubMutex);

                    if (mt2 == "ADMIN_UPDATE_MENU_REQ") {
                        int   itemId   = jint(msg,   "item_id");
                        float newPrice = jfloat(msg, "new_price");
                        MenuItem* item = g_hub.master_menu.findById(itemId);
                        if (item) {
                            item->price = newPrice;
                            sendMessage(csoc, hub_proto::adminUpdateMenuResp(0));
                            // Push to locations
                            string push = hub_proto::menuUpdatePush("UPDATE", itemId, "", "", newPrice);
                            for (auto& l : g_hub.locations)
                                if (l.online && l.sock >= 0) sendMessage(l.sock, push);
                        } else {
                            sendMessage(csoc, hub_proto::adminUpdateMenuResp(1, "Item not found"));
                        }
                    } else if (mt2 == "AGGREGATE_QUERY_REQ") {
                        float totalRev = 0; int totalOrd = 0;
                        vector<tuple<int,string,float,int,string>> locData;
                        for (auto& l : g_hub.locations) {
                            totalRev += l.revenue; totalOrd += l.orders;
                            locData.push_back({l.id, l.name, l.revenue, l.orders,
                                               l.online ? "ONLINE" : "OFFLINE"});
                        }
                        sendMessage(csoc, hub_proto::aggregateQueryResp(totalRev, totalOrd, locData));
                    } else if (mt2 == "LIST_LOCATIONS_REQ") {
                        vector<tuple<int,string,string>> locList;
                        for (auto& l : g_hub.locations)
                            locList.push_back({l.id, l.name, l.online ? "ONLINE" : "OFFLINE"});
                        sendMessage(csoc, hub_proto::listLocationsResp(locList));
                    } else if (mt2 == "ADMIN_LOGOUT_REQ") {
                        string tk = jstr(msg, "token");
                        g_hub.admin_sessions.erase(tk);
                        sendMessage(csoc, "{ \"msg_type\": \"ADMIN_LOGOUT_RESP\", \"status\": 0 }");
                        break;
                    }
                }
                close(csoc);
            } else {
                close(csoc);
            }
        }).detach();
    }
    close(g_hubSock);
}

// ============================================================
// Helper: connect to Hub
// ============================================================
int connectTo(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) return -1;
    return sock;
}

string rpc(int sock, const string& req) {
    if (!sendMessage(sock, req)) return "ERROR";
    string resp;
    if (!recvMessage(sock, resp)) return "ERROR";
    return resp;
}

// ============================================================
// TEST 1: Hub Protocol Messages (marshaling)
// ============================================================
void testHubProtocol() {
    cout << "\n=== TEST 1: Hub Protocol Messages ===" << endl;

    // Register
    string msg = hub_proto::registerLocationReq("Location A", 5470);
    check(msg.find("REGISTER_LOCATION_REQ") != string::npos, "registerLocationReq has correct msg_type");
    check(msg.find("Location A") != string::npos,            "registerLocationReq has location name");
    check(msg.find("5470") != string::npos,                  "registerLocationReq has port");

    // Heartbeat
    msg = hub_proto::heartbeatReq(1);
    check(msg.find("HEARTBEAT_REQ") != string::npos, "heartbeatReq has correct msg_type");
    check(msg.find("\"location_id\": 1") != string::npos || msg.find("\"location_id\":1") != string::npos,
          "heartbeatReq has location_id");

    msg = hub_proto::heartbeatResp(1, 3, 5);
    check(msg.find("HEARTBEAT_RESP") != string::npos,     "heartbeatResp has correct msg_type");
    check(msg.find("\"active_clients\"") != string::npos, "heartbeatResp has active_clients");

    // Menu update push
    msg = hub_proto::menuUpdatePush("UPDATE", 6, "", "", 28.99f);
    check(msg.find("MENU_UPDATE_PUSH") != string::npos, "menuUpdatePush has correct msg_type");
    check(msg.find("UPDATE") != string::npos,           "menuUpdatePush has action");
    check(msg.find("\"item_id\": 6") != string::npos || msg.find("\"item_id\":6") != string::npos,
          "menuUpdatePush has item_id");

    // Admin login
    msg = hub_proto::adminLoginReq("chain_admin", "admin2026");
    check(msg.find("ADMIN_LOGIN_REQ") != string::npos, "adminLoginReq has correct msg_type");

    // Report orders
    vector<pair<int,float>> orders = {{1001, 73.74f}, {1002, 83.42f}};
    msg = hub_proto::reportOrdersReq(1, orders);
    check(msg.find("REPORT_ORDERS_REQ") != string::npos, "reportOrdersReq has correct msg_type");
    check(msg.find("1001") != string::npos,               "reportOrdersReq has order_id");

    // Aggregate
    msg = hub_proto::aggregateQueryReq("admin_tok_1");
    check(msg.find("AGGREGATE_QUERY_REQ") != string::npos, "aggregateQueryReq has correct msg_type");
}

// ============================================================
// TEST 2: Location Registration + Menu Sync
// ============================================================
void testRegistration(int hubPort) {
    cout << "\n=== TEST 2: Location Registration + Menu Sync ===" << endl;

    int sock = connectTo(hubPort);
    check(sock >= 0, "Location connects to Hub");

    string resp = rpc(sock, hub_proto::registerLocationReq("Test Location A", 5470));
    check(resp.find("REGISTER_LOCATION_RESP") != string::npos, "Hub responds with REGISTER_LOCATION_RESP");
    check(jint(resp, "status") == 0,      "Registration succeeds");
    check(jint(resp, "location_id") > 0,  "Hub assigns location_id");
    check(resp.find("Bruschetta")    != string::npos, "Menu sync includes Bruschetta");
    check(resp.find("Ribeye Steak")  != string::npos, "Menu sync includes Ribeye Steak");
    check(resp.find("Lemonade")      != string::npos, "Menu sync includes Lemonade");

    close(sock);
}

// ============================================================
// TEST 3: Admin Login + Menu Update + Push to Location
// ============================================================
void testAdminMenuUpdate(int hubPort) {
    cout << "\n=== TEST 3: Admin Menu Update + Push to Location ===" << endl;

    // Register a location first
    int locSock = connectTo(hubPort);
    check(locSock >= 0, "Location connects to Hub");
    string regResp = rpc(locSock, hub_proto::registerLocationReq("Push Test Loc", 5480));
    check(jint(regResp, "status") == 0, "Location registered");

    // Admin connects and logs in
    int adminSock = connectTo(hubPort);
    check(adminSock >= 0, "Admin connects to Hub");
    string loginResp = rpc(adminSock, hub_proto::adminLoginReq("chain_admin", "admin2026"));
    check(jint(loginResp, "status") == 0, "Admin login succeeds");
    string token = jstr(loginResp, "token");
    check(!token.empty(), "Admin gets token");

    // Bad login
    int badSock = connectTo(hubPort);
    string badResp = rpc(badSock, hub_proto::adminLoginReq("wrong", "wrong"));
    check(jint(badResp, "status") == 1, "Bad admin login rejected");
    close(badSock);

    // Admin updates menu price
    string updateResp = rpc(adminSock, hub_proto::adminUpdateMenuReq(token, 6, 28.99f));
    check(jint(updateResp, "status") == 0, "Admin menu update succeeds");

    // Location should receive the push
    this_thread::sleep_for(chrono::milliseconds(100));
    string pushMsg;
    recvMessage(locSock, pushMsg);
    check(pushMsg.find("MENU_UPDATE_PUSH") != string::npos, "Location receives MENU_UPDATE_PUSH");
    check(pushMsg.find("UPDATE") != string::npos,           "Push action is UPDATE");

    // Admin logs out
    rpc(adminSock, hub_proto::adminLogoutReq(token));
    close(adminSock);
    close(locSock);
}

// ============================================================
// TEST 4: Order Reporting + Aggregate Query
// ============================================================
void testOrderAggregation(int hubPort) {
    cout << "\n=== TEST 4: Order Reporting + Aggregate Query ===" << endl;

    // Register two locations
    int locA = connectTo(hubPort);
    rpc(locA, hub_proto::registerLocationReq("Agg Loc A", 5490));

    int locB = connectTo(hubPort);
    rpc(locB, hub_proto::registerLocationReq("Agg Loc B", 5491));

    check(locA >= 0 && locB >= 0, "Both locations connected");

    // Location A reports orders
    vector<pair<int,float>> ordersA = {{2001, 73.74f}};
    string repResp = rpc(locA, hub_proto::reportOrdersReq(0, ordersA));
    check(jint(repResp, "status") == 0, "Location A order report accepted");

    // Location B reports orders
    vector<pair<int,float>> ordersB = {{2002, 45.50f}, {2003, 38.00f}};
    repResp = rpc(locB, hub_proto::reportOrdersReq(0, ordersB));
    check(jint(repResp, "status") == 0, "Location B order report accepted");

    // Admin queries aggregate
    int adminSock = connectTo(hubPort);
    string loginResp = rpc(adminSock, hub_proto::adminLoginReq("chain_admin", "admin2026"));
    string token     = jstr(loginResp, "token");

    string aggResp = rpc(adminSock, hub_proto::aggregateQueryReq(token));
    check(aggResp.find("AGGREGATE_QUERY_RESP") != string::npos, "Aggregate response received");
    check(jint(aggResp, "status") == 0, "Aggregate query succeeds");

    float totalRev = jfloat(aggResp, "total_revenue");
    int   totalOrd = jint(aggResp,   "total_orders");
    check(totalRev > 100.0f, "Total revenue > $100 (from 3 orders)");
    check(totalOrd >= 3,     "Total orders >= 3");
    check(aggResp.find("Agg Loc A") != string::npos, "Aggregate includes Location A");
    check(aggResp.find("Agg Loc B") != string::npos, "Aggregate includes Location B");

    // List locations
    string listResp = rpc(adminSock, hub_proto::listLocationsReq(token));
    check(listResp.find("LIST_LOCATIONS_RESP") != string::npos, "List locations response received");
    check(listResp.find("ONLINE") != string::npos,              "Locations show as ONLINE");

    rpc(adminSock, hub_proto::adminLogoutReq(token));
    close(adminSock);
    close(locA);
    close(locB);
}

// ============================================================
// TEST 5: Multiple Locations Register Simultaneously
// ============================================================
void testMultiLocation(int hubPort) {
    cout << "\n=== TEST 5: Multiple Simultaneous Locations ===" << endl;

    const int N = 3;
    vector<int> socks(N);
    vector<int> ids(N);

    for (int i = 0; i < N; i++) {
        socks[i] = connectTo(hubPort);
        check(socks[i] >= 0, "Location " + to_string(i+1) + " connects");
        string resp = rpc(socks[i], hub_proto::registerLocationReq(
            "Multi Loc " + to_string(i+1), 5500 + i));
        ids[i] = jint(resp, "location_id");
        check(ids[i] > 0, "Location " + to_string(i+1) + " gets ID " + to_string(ids[i]));
    }

    // Verify all IDs are unique
    bool unique = true;
    for (int i = 0; i < N; i++)
        for (int j = i+1; j < N; j++)
            if (ids[i] == ids[j]) unique = false;
    check(unique, "All location IDs are unique");

    for (int i = 0; i < N; i++) close(socks[i]);
}

// ============================================================
// Main
// ============================================================
int main() {
    int hubPort = 16470;

    cout << "========================================" << endl;
    cout << "ECE 470 - Project 2 (DineSync)"          << endl;
    cout << "Integration Tests"                        << endl;
    cout << "Yousef Alhindi"                           << endl;
    cout << "========================================" << endl;

    // Start embedded Hub
    cout << "\nStarting Hub Server on port " << hubPort << "..." << endl;
    thread hubThread(hubAcceptLoop, hubPort);
    hubThread.detach();
    while (!g_hubRunning) this_thread::sleep_for(chrono::milliseconds(50));
    this_thread::sleep_for(chrono::milliseconds(200));
    cout << "Hub ready.\n" << endl;

    // Run tests
    testHubProtocol();
    testRegistration(hubPort);
    testAdminMenuUpdate(hubPort);
    testOrderAggregation(hubPort);
    testMultiLocation(hubPort);

    // Shutdown
    g_hubRunning = false;
    this_thread::sleep_for(chrono::milliseconds(300));

    cout << "\n========================================" << endl;
    cout << "RESULTS: " << g_passed.load() << "/" << (g_passed.load() + g_failed.load())
         << " tests passed" << endl;
    if (g_failed.load() == 0) cout << "ALL TESTS PASSED" << endl;
    else cout << g_failed.load() << " TESTS FAILED" << endl;
    cout << "========================================" << endl;

    return g_failed.load() > 0 ? 1 : 0;
}
