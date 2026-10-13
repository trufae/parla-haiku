// Offline fixture: validates account recovery and sending without contacting a relay.
#include "RpcTransport.h"
#include <fstream>
#include <iostream>
#include <map>
#include <set>
int main() {
    std::ofstream log(getenv("PARLA_TEST_RPC_LOG"));
    uint32_t selected = 7, last = 7;
    uint64_t events = 0;
    bool failedInit = false, sent = false;
    Json staleAccounts; uint64_t staleId = 0;
    std::set<uint32_t> configured{7}, removed;
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto request = Json::parse(line);
        log << request.dump() << std::endl;
        const auto method = request.at("method").get<std::string>();
        const auto params = request.at("params");
        uint32_t account = !params.empty() && params[0].is_number_unsigned() ? params[0].get<uint32_t>() : 0;
        Json result;
        if (method == "get_next_event_batch") { events = request.at("id"); continue; }
        if (method == "get_system_info") result = Json::object();
        else if (method == "get_all_accounts") {
            result = Json::array();
            for (uint32_t id = 7; id <= last; ++id) if (!removed.count(id))
                result.push_back({{"id",id},{"kind",configured.count(id) ? "Configured" : "Unconfigured"},{"displayName","Fixture"}});
        } // Simulate a stale selection snapshot after deletion; UI must validate IDs.
        else if (method == "get_selected_account_id") result = removed.count(9) ? 9 : selected;
        else if (method == "add_account") result = ++last;
        else if (method == "select_account") selected = account;
        else if (method == "remove_account") { removed.insert(account); if (selected == account) selected = 7; }
        else if (method == "get_chatlist_entries") result = account == 9 ? Json::array({42}) : Json::array();
        else if (method == "get_chatlist_items_by_entries") result = account == 9 ? Json{{"42",{
            {"kind","ChatListItem"},{"id",42},{"name","Relay test"},{"summaryText2","Native Haiku chat"},{"color","#507a99"}}}} : Json::object();
        else if (method == "get_full_chat_by_id") result = {{"name","Relay test"},{"canSend",true},{"isEncrypted",true}};
        else if (method == "get_message_ids") result = Json::array({98,99});
        else if (method == "get_messages") result = {{"98",{{"kind","message"},{"id",98},{"chatId",42},{"fromId",2},
            {"timestamp",1791890100},{"text","Hello from Delta Chat"},{"state",16},{"sender",{{"displayName","Relay test"}}}}},
            {"99",{{"kind","message"},{"id",99},{"chatId",42},{"fromId",1},{"timestamp",1791890120},
            {"text","Native Haiku reply"},{"state",sent ? 24 : 28}}}};
        else if (method == "send_msg") { result = 99; sent = true; }
        else if (method == "get_message") result = {{"error","SMTP fixture: connection refused"}};
        else if (method == "is_configured") result = configured.count(account) != 0;
        else if (method == "get_connectivity_html") result = "<html><head><style>HIDDEN_CSS</style></head><body><h3>Incoming messages</h3><li>Connected</li><h3>Outgoing messages</h3><li>SMTP: retry &lt;pending&gt; &amp; waiting</li></body></html>";
        else if (method == "get_config") result = nullptr;
        else if (method == "list_transports") result = configured.count(account) ? Json::array({{
            {"addr","fixture@example.invalid"},{"imapServer","imap.example.invalid"},{"smtpServer","smtp.example.invalid"},
            {"password","MUST_NOT_LOG_TRANSPORT_PASSWORD"}}}) : Json::array();
        else if (method == "init_transports" && !failedInit) {
            failedInit = true;
            std::cerr << "fixture core: relay connection refused" << std::endl;
            std::cout << Json{{"jsonrpc","2.0"},{"id",request.at("id")},{"error",{
                {"code",-32000},{"message","Relay fixture unavailable"}}}}.dump() << std::endl;
            continue;
        } else if (method == "init_transports" || method == "get_backup" || method == "add_or_update_transport") configured.insert(account);
        else if (method != "set_config" && method != "start_io" && method != "start_io_for_all_accounts"
            && method != "export_backup" && method != "markseen_msgs") {
            std::cerr << "Unexpected RPC: " << method << std::endl; return 1;
        }
        std::cout << Json{{"jsonrpc","2.0"},{"id",request.at("id")},{"result",result}}.dump() << std::endl;
        if (method == "get_all_accounts" && removed.count(9) && staleId) {
            std::cout << Json{{"jsonrpc","2.0"},{"id",staleId},{"result",staleAccounts}}.dump() << std::endl;
            staleId = 0;
        }
        if (method == "send_msg" && events) {
            std::cout << Json{{"jsonrpc","2.0"},{"id",events},{"result",Json::array({{
                {"contextId",account},{"event",{{"kind","MsgFailed"},{"chatId",42},{"msgId",99}}}}})}}.dump() << std::endl;
            events = 0;
        }
    }
}
