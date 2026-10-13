#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>

using Json = nlohmann::json;

// Callbacks run on the I/O thread. The GUI forwards them through BMessenger.
class RpcTransport {
public:
    using Receiver = std::function<void(const Json&)>;
    ~RpcTransport();
    void Start(const std::string& executable, const std::string& accounts, Receiver receiver);
    uint64_t Call(const std::string& method, const Json& params, int timeoutSeconds = 60);
    void Stop();
    std::string TakeDiagnostics();
private:
    void Run();
    void Wake();
    void Fail(const std::string& reason);
    Receiver fReceiver;
    std::thread fThread;
    std::atomic<bool> fStop{true};
    std::mutex fMutex;
    std::string fOutput, fDiagnostics, fNewDiagnostics;
    struct Pending { std::chrono::steady_clock::time_point deadline; };
    std::map<uint64_t, Pending> fPending;
    uint64_t fNextId = 0;
    int fInput = -1, fRead = -1, fError = -1, fWake[2] = {-1, -1};
    int fPid = -1;
};
