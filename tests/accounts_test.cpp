#include "MainWindow.h"
#include <Application.h>
#include <Button.h>
#include <Directory.h>
#include <MessageRunner.h>
#include <StringView.h>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)

static void Fields(BView* view, std::vector<BTextControl*>& fields) {
    if (auto* field = dynamic_cast<BTextControl*>(view)) fields.push_back(field);
    for (int32 i = 0; i < view->CountChildren(); ++i) Fields(view->ChildAt(i), fields);
}
class AccountsTest : public BApplication {
public:
    AccountsTest() : BApplication("application/x-vnd.trufae-parla-accounts-test") {}
    int status = 1;
    void ReadyToRun() override {
        try {
            fWindow = new MainWindow(); fWindow->Show();
            BMessage tick('test');
            fTimer = std::make_unique<BMessageRunner>(BMessenger(this), &tick, 100000);
        } catch (const std::exception& e) { Fail(e.what()); }
    }
    void MessageReceived(BMessage* message) override {
        if (message->what != 'test') { BApplication::MessageReceived(message); return; }
        try {
            CHECK(++fTicks < 180);
            if (fStage == 0) {
                if (!Has("get_chatlist_items_by_entries", Json::array({7,Json::array()}))) return;
                fWindow->PostMessage('prof'); ++fStage;
            } else if (fStage == 1) {
                if (Submit("New profile", {"Català 日本語", ""})) ++fStage;
            } else if (fStage == 2) {
                if (!Ready(8)) return;
                CHECK(Has("init_transports", Json::array({8,nullptr})));
                CHECK(Has("set_config", Json::array({8,"displayname","Català 日本語"})));
                fWindow->PostMessage('link'); ++fStage;
            } else if (fStage == 3) {
                if (Submit("Link another device", {"Linked", "DCBACKUP:fixture"})) ++fStage;
            } else if (fStage == 4) {
                if (!Ready(9)) return;
                CHECK(Has("get_backup", Json::array({9,"DCBACKUP:fixture"})));
                fWindow->PostMessage('mail'); ++fStage;
            } else if (fStage == 5) {
                if (Submit("Existing email", {"Email", "fixture@example.invalid", "secret"})) ++fStage;
            } else if (fStage == 6) {
                if (!Ready(10)) return;
                CHECK(Has("configure", Json::array({10})));
                CHECK(Has("set_config", Json::array({10,"addr","fixture@example.invalid"})));
                CHECK(Has("set_config", Json::array({10,"mail_pw","secret"})));
                // Menu messages have no account/chat fields. FindUInt32 used to
                // overwrite the selected account with zero, suppressing this form.
                fWindow->PostMessage('sett'); ++fStage;
            } else if (fStage == 7) {
                if (Submit("Profile settings", {"Renamed"})) ++fStage;
            } else if (fStage == 8) {
                if (!Has("set_config", Json::array({10,"displayname","Renamed"}))) return;
                CHECK(Has("get_config", Json::array({10,"displayname"})));
                BMessage exportBackup('exbk'); // Also exercises the selection fallback.
                exportBackup.AddString("field0", "/tmp"); exportBackup.AddString("field1", "");
                fWindow->PostMessage(&exportBackup); ++fStage;
            } else if (fStage == 9) {
                if (!Has("export_backup", Json::array({10,"/tmp",""}))) return;
                // Malformed response metadata used to escape the narrow callback
                // catch and abort the GUI thread, just like missing form strings.
                BMessage malformed(kRPC); malformed.AddString("json", "{\"id\":\"invalid\"}");
                fWindow->PostMessage(&malformed); ++fStage;
            } else if (fStage == 10) {
                if (Status().find("Cannot complete this action:") == std::string::npos) return;
                fWindow->PostMessage('crpr'); // Every field absent: validation, no crash.
                ++fStage;
            } else if (fStage == 11) {
                if (Status() != "Enter a display name") return;
                // Prove the window still accepts actions after both errors.
                fWindow->PostMessage('prof'); ++fStage;
            } else if (fStage == 12) {
                if (!Submit("New profile", {}, true)) return;
                status = 0; PostMessage(B_QUIT_REQUESTED);
            }
        } catch (const std::exception& e) { Fail(e.what()); }
    }
private:
    void Fail(const char* error) {
        fprintf(stderr, "accounts stage %d: %s\n", fStage, error);
        PostMessage(B_QUIT_REQUESTED);
    }
    bool Has(const char* method, const Json& params) {
        std::ifstream log(getenv("PARLA_TEST_RPC_LOG")); std::string line;
        while (std::getline(log, line)) {
            auto request = Json::parse(line, nullptr, false);
            if (request.is_object() && request.value("method", "") == method && request.at("params") == params) return true;
        }
        return false;
    }
    std::string Status() {
        CHECK(fWindow->Lock());
        auto* status = dynamic_cast<BStringView*>(fWindow->FindView("status"));
        std::string text = status ? status->Text() : "";
        fWindow->Unlock(); return text;
    }
    bool Ready(uint32 id) { return Has("start_io", Json::array({id})) && Status() == "Profile ready"; }
    bool Submit(const char* title, std::vector<std::string> values, bool cancel = false) {
        for (int32 i = 0; i < CountWindows(); ++i) {
            BWindow* window = WindowAt(i);
            if (!window || !window->Lock()) continue;
            if (std::string(window->Title()) != title) { window->Unlock(); continue; }
            try {
                std::vector<BTextControl*> fields;
                for (int32 j = 0; j < window->CountChildren(); ++j) Fields(window->ChildAt(j), fields);
                if (!cancel) {
                    CHECK(fields.size() == values.size());
                    for (size_t j = 0; j < fields.size(); ++j) fields[j]->SetText(values[j].c_str());
                }
                auto* button = dynamic_cast<BButton*>(window->FindView(cancel ? "cancel" : "ok"));
                CHECK(button && button->Invoke() == B_OK);
            } catch (...) { window->Unlock(); throw; }
            window->Unlock(); return true;
        }
        return false;
    }
    MainWindow* fWindow = nullptr;
    int fStage = 0, fTicks = 0;
    std::unique_ptr<BMessageRunner> fTimer;
};
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    char directory[] = "/tmp/parla-accounts-XXXXXX";
    if (!mkdtemp(directory)) return 1;
    std::string root(directory), settings = root + "/settings", log = root + "/requests.jsonl";
    create_directory(settings.c_str(), 0700);
    setenv("PARLA_SETTINGS_PATH", settings.c_str(), 1);
    setenv("DC_ACCOUNTS_PATH", (root + "/accounts").c_str(), 1);
    setenv("PARLA_RPC_SERVER", argv[1], 1);
    setenv("PARLA_TEST_RPC_LOG", log.c_str(), 1);
    // A valid JSON file with invalid field types must not crash startup.
    std::ofstream(settings + "/drafts.json") << R"([{"account":null,"chat":1},{"account":7,"chat":8,"quote":null,"text":"saved"}])";
    AccountsTest app; app.Run();
    if (!app.status) puts("accounts: new/link/email forms, selected-account defaults, null settings, invalid drafts and exception recovery OK");
    unlink((settings + "/drafts.json").c_str()); unlink((settings + "/window").c_str());
    unlink(log.c_str()); rmdir(settings.c_str()); rmdir((root + "/accounts").c_str()); rmdir(root.c_str());
    return app.status;
}
