#include "RpcTransport.h"
#include <condition_variable>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
int main(int argc, char** argv) {
    try {
        CHECK(argc == 2);
        RpcTransport rpc;
        std::mutex mutex; std::condition_variable changed;
        std::map<uint64_t, Json> responses;
        auto receiver = [&](const Json& r) {
            std::lock_guard<std::mutex> guard(mutex);
            if (r.contains("id")) responses[r["id"].get<uint64_t>()] = r;
            changed.notify_all();
        };
        auto wait = [&](uint64_t id) {
            std::unique_lock<std::mutex> lock(mutex);
            CHECK(changed.wait_for(lock, std::chrono::seconds(5), [&]{ return responses.count(id); }));
            return responses.at(id);
        };
        rpc.Start(argv[1], "/tmp/account path with spaces", receiver);
        CHECK(wait(rpc.Call("path", Json::array()))["result"] == "/tmp/account path with spaces");
        Json text = Json::array({"Català 日本語\n\"quoted\""});
        auto longPoll = rpc.Call("hang", Json::array(), 0);
        CHECK(wait(rpc.Call("noisy", text))["result"] == text);
        auto stderrOutput = rpc.TakeDiagnostics();
        CHECK(!stderrOutput.empty() && stderrOutput.size() <= 32768);
        CHECK(rpc.TakeDiagnostics().empty());
        CHECK(wait(rpc.Call("hang", Json::array(), 1)).contains("error"));
        std::vector<uint64_t> ids;
        for (int i=0; i<30; ++i) ids.push_back(rpc.Call("echo", Json::array({i})));
        for (int i=0; i<30; ++i) CHECK(wait(ids[i])["result"][0] == i);
        auto dying = rpc.Call("die", Json::array());
        auto failure = wait(dying);
        CHECK(failure.contains("error")); CHECK(wait(longPoll).contains("error"));
        auto diagnostic = failure["error"]["message"].get<std::string>();
        CHECK(diagnostic.find("core failure diagnostic") != std::string::npos);
        CHECK(diagnostic.size() < 4300);
        rpc.Stop(); rpc.Start(argv[1], "/tmp/restart", receiver);
        CHECK(wait(rpc.Call("bad", Json::array())).contains("error"));
        rpc.Stop();
        bool failed = false;
        try { rpc.Start("/nonexistent/parla-core", "/tmp/no", receiver); }
        catch (...) { failed = true; }
        CHECK(failed);
        std::cout << "transport: partial reads, Unicode, stderr, concurrent requests, timeout, EOF, restart OK\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
