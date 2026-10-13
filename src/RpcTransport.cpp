#include "RpcTransport.h"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;

namespace {
constexpr size_t kMaxFrame = 16 * 1024 * 1024;
void Close(int& fd) { if (fd >= 0) close(fd); fd = -1; }
void Pipe(int* p) {
    if (pipe(p)) throw std::runtime_error(strerror(errno));
    for (int n = 0; n < 2; ++n) fcntl(p[n], F_SETFD, FD_CLOEXEC);
}
Json Error(uint64_t id, const std::string& text) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32000}, {"message", text}}}};
}
}
RpcTransport::~RpcTransport() { Stop(); }
void RpcTransport::Start(const std::string& executable, const std::string& accounts, Receiver receiver) {
    Stop();
    signal(SIGPIPE, SIG_IGN);
    int in[2] = {-1,-1}, out[2] = {-1,-1}, err[2] = {-1,-1};
    try {
        Pipe(in); Pipe(out); Pipe(err); Pipe(fWake);
        std::vector<std::string> env;
        for (char** e = environ; *e; ++e)
            if (strncmp(*e, "DC_ACCOUNTS_PATH=", 17)) env.emplace_back(*e);
        env.emplace_back("DC_ACCOUNTS_PATH=" + accounts);
        std::vector<char*> envp;
        for (auto& e : env) envp.push_back(e.data());
        envp.push_back(nullptr);
        char* argv[] = {const_cast<char*>(executable.c_str()), nullptr};
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, in[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, err[1], STDERR_FILENO);
        pid_t pid;
        int status = posix_spawn(&pid, executable.c_str(), &actions, nullptr, argv, envp.data());
        posix_spawn_file_actions_destroy(&actions);
        if (status) throw std::runtime_error("Cannot start Chatmail: " + std::string(strerror(status)));
        fPid = pid;
        Close(in[0]); Close(out[1]); Close(err[1]);
        fInput = in[1]; in[1] = -1; fRead = out[0]; out[0] = -1; fError = err[0]; err[0] = -1;
        for (int fd : {fInput, fRead, fError, fWake[0], fWake[1]})
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        fReceiver = std::move(receiver);
        fStop = false;
        fThread = std::thread(&RpcTransport::Run, this);
    } catch (...) {
        for (int* p : {in, out, err}) { Close(p[0]); Close(p[1]); }
        Stop();
        throw;
    }
}
uint64_t RpcTransport::Call(const std::string& method, const Json& params, int timeoutSeconds) {
    std::lock_guard<std::mutex> lock(fMutex);
    if (fStop) throw std::runtime_error("Chatmail is disconnected");
    uint64_t id = ++fNextId;
    std::string frame = Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}}.dump() + "\n";
    if (fOutput.size() + frame.size() > kMaxFrame) throw std::runtime_error("Chatmail request queue is full");
    fOutput += frame;
    fPending[id] = {timeoutSeconds > 0 ? std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds)
                                      : std::chrono::steady_clock::time_point::max()};
    Wake();
    return id;
}
void RpcTransport::Wake() { if (fWake[1] >= 0) { char c = 0; (void)write(fWake[1], &c, 1); } }
void RpcTransport::Fail(const std::string& reason) {
    std::map<uint64_t, Pending> pending;
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fStop = true;
        pending.swap(fPending); fOutput.clear();
    }
    for (const auto& p : pending) fReceiver(Error(p.first, reason));
    fReceiver(Json{{"disconnected", reason}});
}
void RpcTransport::Run() {
    std::string buffer;
    while (!fStop) {
        bool writing;
        { std::lock_guard<std::mutex> lock(fMutex); writing = !fOutput.empty(); }
        pollfd p[] = {{fRead, POLLIN, 0}, {fError, POLLIN, 0}, {fWake[0], POLLIN, 0},
                      {fInput, static_cast<short>(writing ? POLLOUT : 0), 0}};
        if (poll(p, 4, 100) < 0) {
            if (errno == EINTR) continue;
            Fail("Chatmail pipe polling failed"); break;
        }
        char bytes[16384];
        if (p[2].revents) while (read(fWake[0], bytes, sizeof bytes) > 0) {}
        // Drain stderr independently: a full logging pipe must never block RPC.
        if (p[1].revents) {
            ssize_t n = read(fError, bytes, sizeof bytes);
            if (n == 0) Close(fError);
        }
        if (p[3].revents & POLLOUT) {
            bool broken = false;
            {
                std::lock_guard<std::mutex> lock(fMutex);
                ssize_t n = write(fInput, fOutput.data(), fOutput.size());
                if (n > 0) fOutput.erase(0, static_cast<size_t>(n));
                else if (n < 0 && errno != EAGAIN && errno != EINTR) broken = true;
            }
            if (broken) { Fail("Chatmail input pipe closed"); break; }
        }
        if (p[0].revents) {
            ssize_t n = read(fRead, bytes, sizeof bytes);
            if (!n || (n < 0 && errno != EAGAIN && errno != EINTR)) {
                Fail("Chatmail stopped. Restart the application to reconnect."); break;
            }
            if (n > 0) buffer.append(bytes, static_cast<size_t>(n));
            if (buffer.size() > kMaxFrame) { Fail("Chatmail response exceeds 16 MiB"); break; }
            size_t pos;
            while ((pos = buffer.find('\n')) != std::string::npos) {
                Json value = Json::parse(buffer.substr(0, pos), nullptr, false);
                buffer.erase(0, pos + 1);
                if (value.is_discarded() || !value.is_object()) { Fail("Invalid Chatmail JSON response"); break; }
                bool expected = false;
                if (value.contains("id") && value["id"].is_number_unsigned()) {
                    std::lock_guard<std::mutex> lock(fMutex);
                    expected = fPending.erase(value["id"].get<uint64_t>()) != 0;
                }
                if (expected) fReceiver(value);
            }
        }
        std::vector<uint64_t> expired;
        {
            std::lock_guard<std::mutex> lock(fMutex);
            for (auto i = fPending.begin(); i != fPending.end();) {
                if (i->second.deadline <= std::chrono::steady_clock::now()) {
                    expired.push_back(i->first); i = fPending.erase(i);
                } else ++i;
            }
        }
        for (auto id : expired) fReceiver(Error(id, "Chatmail request timed out; its result is unknown"));
    }
}
void RpcTransport::Stop() {
    fStop = true; Wake();
    if (fThread.joinable()) fThread.join();
    Close(fInput); Close(fRead); Close(fError); Close(fWake[0]); Close(fWake[1]);
    if (fPid > 0) {
        kill(fPid, SIGTERM);
        int status = 0; bool reaped = false;
        for (int i = 0; i < 40; ++i) {
            pid_t r = waitpid(fPid, &status, WNOHANG);
            if (r == fPid || (r < 0 && errno == ECHILD)) { reaped = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        if (!reaped) {
            kill(fPid, SIGKILL);
            while (waitpid(fPid, &status, 0) < 0 && errno == EINTR) {}
        }
        fPid = -1;
    }
    std::lock_guard<std::mutex> lock(fMutex);
    fPending.clear(); fOutput.clear();
}
