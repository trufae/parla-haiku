// Offline fixture: validates the GUI's account setup sequence, never connects.
#include "RpcTransport.h"
#include <fstream>
#include <iostream>
int main() {
    std::ofstream log(getenv("PARLA_TEST_RPC_LOG"));
    uint32_t selected = 7, last = 7;
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto request = Json::parse(line);
        log << request.dump() << std::endl;
        const auto method = request.at("method").get<std::string>();
        const auto params = request.at("params");
        Json result;
        if (method == "get_next_event_batch") continue;
        if (method == "get_system_info") result = Json::object();
        else if (method == "get_all_accounts") {
            result = Json::array();
            for (uint32_t id = 7; id <= last; ++id)
                result.push_back({{"id",id},{"kind","Configured"},{"displayName","Fixture"}});
        } else if (method == "get_selected_account_id") result = selected;
        else if (method == "add_account") result = ++last;
        else if (method == "select_account") selected = params.at(0).get<uint32_t>();
        else if (method == "get_chatlist_entries") result = Json::array();
        else if (method == "get_chatlist_items_by_entries") result = Json::object();
        else if (method == "is_configured") result = true;
        else if (method == "get_config") result = nullptr; // Valid unnamed profile.
        else if (method != "set_config" && method != "init_transports" && method != "get_backup"
            && method != "configure" && method != "start_io" && method != "start_io_for_all_accounts"
            && method != "export_backup") {
            std::cerr << "Unexpected RPC: " << method << std::endl; return 1;
        }
        std::cout << Json{{"jsonrpc","2.0"},{"id",request.at("id")},{"result",result}}.dump() << std::endl;
    }
}
