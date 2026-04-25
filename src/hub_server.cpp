// hub_server.cpp
// Yousef Alhindi
//
// Central Hub Server for the multi-location restaurant chain.
// Accepts location registrations, serves master menu, pushes updates,
// handles chain admin operations, monitors location health.
// Multi-threaded: one thread per connected location/admin.

#include <iostream>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <mutex>
#include <thread>
#include <atomic>
#include <csignal>
#include <algorithm>
#include <sstream>
using namespace std;

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "data_model.h"
#include "tcp_transport.h"
#include "hub_protocol.h"

// ============================================================
// JSON helpers (same minimal parser as P1)
// ============================================================
static string jstr(const string& json, const string& key) {
    string s = "\"" + key + "\": \"";
    size_t p = json.find(s);
    if (p == string::npos) { s = "\"" + key + "\":\""; p = json.find(s); }
    if (p == string::npos) return "";
    size_t start = p + s.size();
    size_t end = json.find("\"", start);
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

static string jmsgtype(const string& json) { return jstr(json, "msg_type"); }

// ============================================================
// Hub State
// ============================================================
struct LocationInfo {
    int id;
    string name;
    int port;
    int socket_fd;      // socket to this location
    bool online;
    int active_clients;
    int order_count;
    float total_revenue;
    int total_orders;
    vector<pair<int,float>> reported_orders; // (order_id, total)
};

struct HubState {
    Menu master_menu;
    vector<LocationInfo> locations;
    int next_location_id = 1;

    // Admin sessions
    map<string, string> admin_sessions; // token -> username
    int next_admin_token = 1;

    // Admin credentials (hardcoded)
    string admin_user = "chain_admin";
    string admin_pass = "admin2026";

    HubState() {
        // Initialize master menu (same as P1)
        master_menu.addItem(MenuItem(1,  "Bruschetta",          Category::STARTER,  8.99f));
        master_menu.addItem(MenuItem(2,  "Soup of the Day",     Category::STARTER,  6.99f));
        master_menu.addItem(MenuItem(3,  "Caesar Salad",        Category::STARTER,  7.99f));
        master_menu.addItem(MenuItem(4,  "Grilled Salmon",      Category::MAIN,    18.99f));
        master_menu.addItem(MenuItem(5,  "Pasta Carbonara",     Category::MAIN,    14.99f));
        master_menu.addItem(MenuItem(6,  "Ribeye Steak",        Category::MAIN,    24.99f));
        master_menu.addItem(MenuItem(7,  "Margherita Pizza",    Category::MAIN,    12.99f));
        master_menu.addItem(MenuItem(8,  "Chicken Parmesan",    Category::MAIN,    16.99f));
        master_menu.addItem(MenuItem(9,  "Tiramisu",            Category::DESSERT,  7.99f));
        master_menu.addItem(MenuItem(10, "Cheesecake",          Category::DESSERT,  6.99f));
        master_menu.addItem(MenuItem(11, "Chocolate Lava Cake", Category::DESSERT,  8.99f));
        master_menu.addItem(MenuItem(12, "Lemonade",            Category::DRINK,    3.99f));
        master_menu.addItem(MenuItem(13, "Iced Tea",            Category::DRINK,    2.99f));
        master_menu.addItem(MenuItem(14, "Sparkling Water",     Category::DRINK,    1.99f));
        master_menu.addItem(MenuItem(15, "House Red Wine",      Category::DRINK,    9.99f));
        master_menu.addItem(MenuItem(16, "Craft Beer",          Category::DRINK,    5.99f));
    }

    bool isAdminTokenValid(const string& token) {
        return admin_sessions.find(token) != admin_sessions.end();
    }

    // Push menu update to all connected locations
    void pushMenuUpdate(const string& updateMsg) {
        for (auto& loc : locations) {
            if (loc.online && loc.socket_fd >= 0) {
                if (!sendMessage(loc.socket_fd, updateMsg)) {
                    cout << "[Hub] Failed to push update to " << loc.name << endl;
                }
            }
        }
    }
};

HubState g_hub;
mutex g_hubMutex;
atomic<bool> g_running(true);

// ============================================================
// Location session handler (one thread per location)
// ============================================================
void locationThread(int csoc) {
    // First message should be REGISTER_LOCATION_REQ
    string req;
    if (!recvMessage(csoc, req)) { close(csoc); return; }

    string msgType = jmsgtype(req);
    if (msgType != "REGISTER_LOCATION_REQ") {
        cout << "[Hub] Expected REGISTER, got " << msgType << endl;
        close(csoc);
        return;
    }

    string locName = jstr(req, "location_name");
    int locPort = jint(req, "port");
    int locId;

    {
        lock_guard<mutex> lock(g_hubMutex);
        locId = g_hub.next_location_id++;
        LocationInfo info;
        info.id = locId;
        info.name = locName;
        info.port = locPort;
        info.socket_fd = csoc;
        info.online = true;
        info.active_clients = 0;
        info.order_count = 0;
        info.total_revenue = 0;
        info.total_orders = 0;
        g_hub.locations.push_back(info);

        // Send registration response with master menu
        string resp = hub_proto::registerLocationResp(0, locId, g_hub.master_menu);
        sendMessage(csoc, resp);
    }

    cout << "[Hub] Location \"" << locName << "\" registered (ID=" << locId
         << ", port=" << locPort << ")" << endl;

    // Session loop: handle heartbeat responses, order reports
    while (g_running) {
        string msg;
        if (!recvMessage(csoc, msg) || msg.empty()) break;

        string mt = jmsgtype(msg);
        lock_guard<mutex> lock(g_hubMutex);

        if (mt == "HEARTBEAT_RESP") {
            // Update location stats
            for (auto& loc : g_hub.locations) {
                if (loc.id == locId) {
                    loc.active_clients = jint(msg, "active_clients");
                    loc.order_count    = jint(msg, "order_count");
                    break;
                }
            }
        } else if (mt == "REPORT_ORDERS_REQ") {
            // Parse orders array and accumulate
            for (auto& loc : g_hub.locations) {
                if (loc.id == locId) {
                    // Parse individual orders from JSON
                    size_t pos = 0;
                    while ((pos = msg.find("\"order_id\"", pos)) != string::npos) {
                        int oid    = jint(msg.substr(pos, 200), "order_id");
                        float total = jfloat(msg.substr(pos, 200), "total");
                        if (oid > 0 && total > 0) {
                            loc.reported_orders.push_back({oid, total});
                            loc.total_revenue += total;
                            loc.total_orders++;
                        }
                        pos += 10;
                    }
                    break;
                }
            }
            sendMessage(csoc, hub_proto::reportOrdersResp(0));
        }
    }

    // Location disconnected
    {
        lock_guard<mutex> lock(g_hubMutex);
        for (auto& loc : g_hub.locations) {
            if (loc.id == locId) {
                loc.online    = false;
                loc.socket_fd = -1;
                break;
            }
        }
    }
    close(csoc);
    cout << "[Hub] Location \"" << locName << "\" disconnected" << endl;
}

// ============================================================
// Admin session handler
// ============================================================
void adminThread(int csoc) {
    string token;
    while (g_running) {
        string req;
        if (!recvMessage(csoc, req) || req.empty()) break;

        string mt = jmsgtype(req);
        lock_guard<mutex> lock(g_hubMutex);

        if (mt == "ADMIN_LOGIN_REQ") {
            string user = jstr(req, "username");
            string pass = jstr(req, "password");
            if (user == g_hub.admin_user && pass == g_hub.admin_pass) {
                token = "admin_tok_" + to_string(g_hub.next_admin_token++);
                g_hub.admin_sessions[token] = user;
                sendMessage(csoc, hub_proto::adminLoginResp(0, token));
                cout << "[Hub] Admin logged in" << endl;
            } else {
                sendMessage(csoc, hub_proto::adminLoginResp(1, "", "Invalid credentials"));
            }
        } else if (mt == "ADMIN_UPDATE_MENU_REQ") {
            string tk = jstr(req, "token");
            if (!g_hub.isAdminTokenValid(tk)) {
                sendMessage(csoc, hub_proto::adminUpdateMenuResp(1, "Invalid token"));
                continue;
            }
            int   itemId   = jint(req,   "item_id");
            float newPrice = jfloat(req, "new_price");

            MenuItem* item = g_hub.master_menu.findById(itemId);
            if (!item) {
                sendMessage(csoc, hub_proto::adminUpdateMenuResp(1, "Item not found"));
                continue;
            }
            item->price = newPrice;
            sendMessage(csoc, hub_proto::adminUpdateMenuResp(0));

            // Push to all locations
            string push = hub_proto::menuUpdatePush("UPDATE", itemId, "", "", newPrice);
            g_hub.pushMenuUpdate(push);
            cout << "[Hub] Menu updated: item " << itemId << " -> $" << newPrice
                 << " (pushed to locations)" << endl;

        } else if (mt == "AGGREGATE_QUERY_REQ") {
            string tk = jstr(req, "token");
            if (!g_hub.isAdminTokenValid(tk)) {
                sendMessage(csoc, "{ \"msg_type\": \"AGGREGATE_QUERY_RESP\", \"status\": 1, \"error\": \"Invalid token\" }");
                continue;
            }
            float totalRev = 0;
            int   totalOrd = 0;
            vector<tuple<int,string,float,int,string>> locData;
            for (auto& loc : g_hub.locations) {
                totalRev += loc.total_revenue;
                totalOrd += loc.total_orders;
                locData.push_back({loc.id, loc.name, loc.total_revenue,
                                   loc.total_orders, loc.online ? "ONLINE" : "OFFLINE"});
            }
            sendMessage(csoc, hub_proto::aggregateQueryResp(totalRev, totalOrd, locData));

        } else if (mt == "LIST_LOCATIONS_REQ") {
            string tk = jstr(req, "token");
            if (!g_hub.isAdminTokenValid(tk)) {
                sendMessage(csoc, "{ \"msg_type\": \"LIST_LOCATIONS_RESP\", \"status\": 1, \"error\": \"Invalid token\" }");
                continue;
            }
            vector<tuple<int,string,string>> locList;
            for (auto& loc : g_hub.locations) {
                locList.push_back({loc.id, loc.name, loc.online ? "ONLINE" : "OFFLINE"});
            }
            sendMessage(csoc, hub_proto::listLocationsResp(locList));

        } else if (mt == "ADMIN_LOGOUT_REQ") {
            string tk = jstr(req, "token");
            g_hub.admin_sessions.erase(tk);
            sendMessage(csoc, "{ \"msg_type\": \"ADMIN_LOGOUT_RESP\", \"status\": 0 }");
            cout << "[Hub] Admin logged out" << endl;

        } else {
            sendMessage(csoc, "{ \"msg_type\": \"ERROR\", \"status\": 1, \"error\": \"Unknown: " + mt + "\" }");
        }
    }
    close(csoc);
}

// ============================================================
// Heartbeat thread — pings all locations periodically
// ============================================================
void heartbeatThread() {
    while (g_running) {
        this_thread::sleep_for(chrono::seconds(5));
        lock_guard<mutex> lock(g_hubMutex);
        for (auto& loc : g_hub.locations) {
            if (loc.online && loc.socket_fd >= 0) {
                string hb = hub_proto::heartbeatReq(loc.id);
                if (!sendMessage(loc.socket_fd, hb)) {
                    loc.online    = false;
                    loc.socket_fd = -1;
                    cout << "[Hub] Location \"" << loc.name << "\" failed heartbeat" << endl;
                }
            }
        }
    }
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <port>" << endl;
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    in_port_t port = (in_port_t)atoi(argv[1]);

    sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family      = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port        = htons(port);

    int ssoc = socket(PF_INET, SOCK_STREAM, 0);
    if (ssoc < 0) { cerr << "[Hub] Socket error" << endl; return 1; }

    int optval = 1;
    setsockopt(ssoc, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (::bind(ssoc, (sockaddr*)&sin, sizeof(sin)) < 0) {
        cerr << "[Hub] Bind error: " << strerror(errno) << endl;
        close(ssoc); return 1;
    }
    if (listen(ssoc, 10) < 0) {
        cerr << "[Hub] Listen error" << endl;
        close(ssoc); return 1;
    }

    cout << "========================================" << endl;
    cout << " DineSync Central Hub Server"             << endl;
    cout << "========================================" << endl;
    cout << "[Hub] Listening on port " << port         << endl;
    cout << "[Hub] Master menu: " << g_hub.master_menu.getAll().size() << " items" << endl;
    cout << "[Hub] Admin: " << g_hub.admin_user        << endl;
    cout << "[Hub] Waiting for connections..."          << endl;

    // Start heartbeat thread
    thread hb(heartbeatThread);
    hb.detach();

    while (g_running) {
        sockaddr_in  fsin;
        unsigned int alen = sizeof(fsin);
        int csoc = accept(ssoc, (sockaddr*)&fsin, &alen);
        if (csoc < 0) continue;

        // Read first message to determine if location or admin
        string firstMsg;
        if (!recvMessage(csoc, firstMsg)) { close(csoc); continue; }

        string mt = jmsgtype(firstMsg);

        if (mt == "REGISTER_LOCATION_REQ") {
            string locName = jstr(firstMsg, "location_name");
            int    locPort = jint(firstMsg, "port");
            int    locId;

            {
                lock_guard<mutex> lock(g_hubMutex);
                locId = g_hub.next_location_id++;
                LocationInfo info;
                info.id            = locId;
                info.name          = locName;
                info.port          = locPort;
                info.socket_fd     = csoc;
                info.online        = true;
                info.active_clients = 0;
                info.order_count   = 0;
                info.total_revenue = 0;
                info.total_orders  = 0;
                g_hub.locations.push_back(info);
                sendMessage(csoc, hub_proto::registerLocationResp(0, locId, g_hub.master_menu));
            }

            cout << "[Hub] Location \"" << locName << "\" registered (ID=" << locId << ")" << endl;

            // Spawn location thread for ongoing communication
            thread t([csoc, locId, locName]() {
                while (g_running) {
                    string msg;
                    if (!recvMessage(csoc, msg) || msg.empty()) break;
                    string mt2 = jmsgtype(msg);
                    lock_guard<mutex> lock(g_hubMutex);

                    if (mt2 == "HEARTBEAT_RESP") {
                        for (auto& loc : g_hub.locations) {
                            if (loc.id == locId) {
                                loc.active_clients = jint(msg, "active_clients");
                                loc.order_count    = jint(msg, "order_count");
                                break;
                            }
                        }
                    } else if (mt2 == "REPORT_ORDERS_REQ") {
                        for (auto& loc : g_hub.locations) {
                            if (loc.id == locId) {
                                size_t pos = 0;
                                while ((pos = msg.find("\"order_id\"", pos)) != string::npos) {
                                    int   oid   = jint(msg.substr(pos, 200),   "order_id");
                                    float total = jfloat(msg.substr(pos, 200), "total");
                                    if (oid > 0 && total > 0) {
                                        loc.reported_orders.push_back({oid, total});
                                        loc.total_revenue += total;
                                        loc.total_orders++;
                                    }
                                    pos += 10;
                                }
                                break;
                            }
                        }
                        sendMessage(csoc, hub_proto::reportOrdersResp(0));
                    }
                }

                {
                    lock_guard<mutex> lock(g_hubMutex);
                    for (auto& loc : g_hub.locations) {
                        if (loc.id == locId) {
                            loc.online    = false;
                            loc.socket_fd = -1;
                            break;
                        }
                    }
                }
                close(csoc);
                cout << "[Hub] Location \"" << locName << "\" disconnected" << endl;
            });
            t.detach();

        } else if (mt == "ADMIN_LOGIN_REQ") {
            // Handle first admin message, then spawn admin thread
            string user = jstr(firstMsg, "username");
            string pass = jstr(firstMsg, "password");
            string token;
            {
                lock_guard<mutex> lock(g_hubMutex);
                if (user == g_hub.admin_user && pass == g_hub.admin_pass) {
                    token = "admin_tok_" + to_string(g_hub.next_admin_token++);
                    g_hub.admin_sessions[token] = user;
                    sendMessage(csoc, hub_proto::adminLoginResp(0, token));
                    cout << "[Hub] Admin logged in" << endl;
                } else {
                    sendMessage(csoc, hub_proto::adminLoginResp(1, "", "Invalid credentials"));
                    close(csoc);
                    continue;
                }
            }
            // Spawn admin thread
            thread t(adminThread, csoc);
            t.detach();

        } else {
            sendMessage(csoc, "{ \"msg_type\": \"ERROR\", \"status\": 1, \"error\": \"Send REGISTER or ADMIN_LOGIN first\" }");
            close(csoc);
        }
    }

    close(ssoc);
    return 0;
}
