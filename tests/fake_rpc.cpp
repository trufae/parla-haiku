#include "RpcTransport.h"
#include <iostream>
#include <unistd.h>
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        auto request = Json::parse(line);
        std::string method = request.at("method");
        if (method == "hang") continue;
        if (method == "die") return 0;
        if (method == "bad") { std::cout << "{broken\n" << std::flush; continue; }
        if (method == "noisy") {
            std::string noise(256 * 1024, 'x');
            std::cerr << noise << std::flush;
        }
        Json result = method == "path" ? Json(getenv("DC_ACCOUNTS_PATH")) : request["params"];
        std::string reply = Json{{"jsonrpc","2.0"},{"id",request["id"]},{"result",result}}.dump() + "\n";
        // Force the transport to deal with partial lines.
        std::cout << reply.substr(0, 3) << std::flush;
        usleep(2000);
        std::cout << reply.substr(3) << std::flush;
    }
}
