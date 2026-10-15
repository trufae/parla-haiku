#include "MainWindow.h"
#include <Application.h>
#include <Alert.h>
#include <BitmapStream.h>
#include <File.h>
#include <Screen.h>
#include <TranslatorRoster.h>
#include <TranslatorFormats.h>
#include <Button.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <cmath>
#include <Directory.h>
#include <Entry.h>
#include <cstring>
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
            std::string image = std::string(getenv("PARLA_SETTINGS_PATH")) + "/photo.png";
            create_directory(getenv("PARLA_SETTINGS_PATH"),0700);
            auto* bitmap = new BBitmap(BRect(0,0,63,31),B_RGBA32);
            CHECK(bitmap->InitCheck() == B_OK);
            memset(bitmap->Bits(),255,bitmap->BitsLength());
            BBitmapStream stream(bitmap); BFile file(image.c_str(),B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
            CHECK(BTranslatorRoster::Default()->Translate(&stream,nullptr,nullptr,&file,B_PNG_FORMAT) == B_OK);
            setenv("PARLA_TEST_IMAGE",image.c_str(),1);
            fWindow = new MainWindow(); fWindow->Show();
            BMessage tick('test');
            fTimer = std::make_unique<BMessageRunner>(BMessenger(this), &tick, 100000);
        } catch (const std::exception& e) { Fail(e.what()); }
    }
    void MessageReceived(BMessage* message) override {
        if (message->what != 'test') { BApplication::MessageReceived(message); return; }
        try {
            CHECK(++fTicks < 220);
            if (fStage == 0) {
                if (!Has("get_chatlist_items_by_entries", Json::array({7,Json::array()}))) return;
                fWindow->PostMessage('prof'); ++fStage;
            } else if (fStage == 1) {
                if (Submit("New profile", {"Català 日本語", ""})) ++fStage;
            } else if (fStage == 2) {
                if (!fRetried) {
                    if (Status().find("init_transports") == std::string::npos) return;
                    CHECK(Status().find("Relay fixture unavailable") != std::string::npos);
                    fWindow->PostMessage('sett'); fRetried = true; return;
                }
                if (Click("Profile settings","chatmail")) return;
                if (Submit("Finish Chatmail setup", {"Recovered", ""})) return;
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
                CHECK(Has("add_or_update_transport", Json::array({10,Json{{"addr","fixture@example.invalid"},{"password","secret"}}})));
                CHECK(!Has("configure", Json::array({8})));
                CHECK(!Has("configure", Json::array({10})));
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
                for (int32 i = 0; i < CountWindows(); ++i) {
                    auto* window = WindowAt(i);
                    if (dynamic_cast<BAlert*>(window)) window->PostMessage(B_QUIT_REQUESTED);
                }
                BMessage select('acct'); select.AddUInt32("account",9); fWindow->PostMessage(&select); ++fStage;
            } else if (fStage == 13) {
                CHECK(fWindow->Lock());
                auto* chats = dynamic_cast<BListView*>(fWindow->FindView("chats"));
                bool ready = chats && chats->CountItems();
                if (ready) chats->Select(0);
                fWindow->Unlock(); if (ready) ++fStage;
            } else if (fStage == 14) {
                CHECK(fWindow->Lock());
                auto* send = dynamic_cast<BButton*>(fWindow->FindView("send"));
                auto* composer = dynamic_cast<BTextView*>(fWindow->FindView("message"));
                auto* messages = dynamic_cast<BListView*>(fWindow->FindView("messages"));
                bool ready = send && send->IsEnabled() && messages && messages->CountItems() == 2;
                if (ready && !ConversationActions(messages,composer)) { fWindow->Unlock(); return; }
                if (ready) { CHECK(composer->Bounds().Height() < 40);
                    auto* attach = fWindow->FindView("attach");
                    CHECK(attach && fabsf(attach->Bounds().Width()-attach->Bounds().Height()) < 2);
                    CHECK(attach->Bounds().Width() < 45);
                    CHECK(fWindow->FindView("messageScroll")->Bounds().Height() > fWindow->Bounds().Height() * .65);
                    BBitmap* bitmap = nullptr;
                    CHECK(BScreen(fWindow).GetBitmap(&bitmap) == B_OK);
                    BBitmapStream stream(bitmap); BFile file("conversation-native.png",B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
                    CHECK(BTranslatorRoster::Default()->Translate(&stream,nullptr,nullptr,&file,B_PNG_FORMAT) == B_OK);
                    composer->SetText("PRIVATE_MESSAGE_BODY"); CHECK(send->Invoke() == B_OK); }
                fWindow->Unlock(); if (ready) ++fStage;
            } else if (fStage == 15) {
                if (Status().find("SMTP fixture: connection refused") == std::string::npos) return;
                CHECK(Has("send_msg",Json::array({9,42,Json{{"text","PRIVATE_MESSAGE_BODY"},{"quotedMessageId",98}}})));
                fWindow->PostMessage('diag'); ++fStage;
            } else if (fStage == 16) {
                for (int32 i = 0; i < CountWindows(); ++i) {
                    auto* window = WindowAt(i);
                    if (!window || !window->Lock()) continue;
                    auto* text = dynamic_cast<BTextView*>(window->FindView("diagnostics"));
                    if (!text) { window->Unlock(); continue; }
                    std::string log(text->Text());
                    CHECK(log.find("init_transports") != std::string::npos);
                    CHECK(log.find("fixture core: relay connection refused") != std::string::npos);
                    CHECK(log.find("send_msg") != std::string::npos);
                    CHECK(log.find("SMTP fixture: connection refused") != std::string::npos);
                    CHECK(log.find("SMTP: retry <pending> & waiting") != std::string::npos);
                    CHECK(log.find("HIDDEN_CSS") == std::string::npos);
                    CHECK(log.find("PRIVATE_MESSAGE_BODY") == std::string::npos);
                    CHECK(log.find("MUST_NOT_LOG_TRANSPORT_PASSWORD") == std::string::npos);
                    BBitmap* bitmap = nullptr;
                    CHECK(BScreen(window).GetBitmap(&bitmap) == B_OK);
                    BBitmapStream stream(bitmap); BFile file("diagnostics-native.png",B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
                    CHECK(BTranslatorRoster::Default()->Translate(&stream,nullptr,nullptr,&file,B_PNG_FORMAT) == B_OK);
                    window->Unlock();
                    fWindow->PostMessage('rmcf'); ++fStage; return;
                }
            } else if (fStage == 17) {
                if (ConfirmDelete()) ++fStage;
            } else if (fStage == 18) {
                if (!Has("remove_account",Json::array({9}))) return;
                CHECK(fWindow->Lock());
                auto* menu = dynamic_cast<BMenuBar*>(fWindow->FindView("menu"));
                auto* profiles = menu->SubmenuAt(0)->SubmenuAt(4);
                CHECK(profiles);
                bool selected = false, deletedStillListed = false;
                for (int32 i = 0; i < profiles->CountItems(); ++i) {
                    auto* item = profiles->ItemAt(i);
                    if (item->Message()->GetUInt32("account",0) == 9) deletedStillListed = true;
                    if (item->Message()->GetUInt32("account",0) == 7 && item->IsMarked()) selected = true;
                }
                // The fixture logs requests before their responses reach the UI.
                fWindow->Unlock(); if (!selected) return;
                CHECK(!deletedStillListed);
                fSelectionCalls = SelectionCalls();
                BMessage away('acct'); away.AddUInt32("account",8); fWindow->PostMessage(&away);
                BMessage back('acct'); back.AddUInt32("account",7); fWindow->PostMessage(&back);
                ++fStage;
            } else if (fStage == 19) {
                if (SelectionCalls() < fSelectionCalls + 2) return;
                CHECK(fLastSelected == 7);
                status = 0; PostMessage(B_QUIT_REQUESTED);
            }
        } catch (const std::exception& e) { Fail(e.what()); }
    }
private:
    int fAction = 0;
    bool ConversationActions(BListView* messages, BTextView* composer) {
        auto* first = static_cast<MessageItem*>(messages->ItemAt(0));
        if (fAction == 0) {
            if (!first->preview) return false;
            CHECK(first->preview->Bounds().Width() == 63);
            messages->Select(0); fWindow->PostMessage('repl'); ++fAction;
        } else if (fAction == 1) {
            CHECK(!fWindow->FindView("reply")->IsHidden());
            std::ifstream file(std::string(getenv("PARLA_SETTINGS_PATH"))+"/drafts.json");
            auto drafts = Json::parse(file);
            bool saved = false;
            for (const auto& draft : drafts) if (draft["account"] == 9 && draft["chat"] == 42) saved = draft["quote"] == 98;
            CHECK(saved);
            entry_ref ref; CHECK(get_ref_for_path(getenv("PARLA_TEST_IMAGE"),&ref) == B_OK);
            BMessage attach(B_REFS_RECEIVED); attach.AddRef("refs",&ref); fWindow->PostMessage(&attach); ++fAction;
        } else if (fAction == 2) {
            CHECK(!fWindow->FindView("reply")->IsHidden()); CHECK(!fWindow->FindView("draft")->IsHidden());
            fWindow->PostMessage('catt'); fWindow->PostMessage('reac'); ++fAction;
        } else if (fAction == 3) {
            CHECK(fWindow->FindView("draft")->IsHidden()); CHECK(!fWindow->FindView("reply")->IsHidden());
            if (Click("React with emoji","chooseEmoji")) ++fAction;
        } else if (fAction == 4) {
            if (!Has("send_reaction",Json::array({9,98,Json::array({"❤️","👍"})}))) return false;
            fWindow->PostMessage('reac'); ++fAction;
        } else if (fAction == 5) {
            if (Click("React with emoji","chooseEmoji")) ++fAction;
        } else if (fAction == 6) {
            if (!Has("send_reaction",Json::array({9,98,Json::array({"❤️"})}))) return false;
            fWindow->PostMessage('emoj'); ++fAction;
        } else if (fAction == 7) {
            if (Click("Insert emoji","chooseEmoji")) ++fAction;
        } else if (fAction == 8) {
            if (std::string(composer->Text()) != "👍") return false;
            BMessage stale('emok'); stale.AddString("emoji","WRONG_CHAT"); stale.AddUInt64("generation",UINT64_MAX);
            fWindow->PostMessage(&stale); ++fAction;
        } else {
            CHECK(std::string(composer->Text()) == "👍");
            return true;
        }
        return false;
    }
    size_t fSelectionCalls = 0;
    uint32 fLastSelected = 0;
    size_t SelectionCalls() {
        size_t count = 0; std::ifstream log(getenv("PARLA_TEST_RPC_LOG")); std::string line;
        while (std::getline(log,line)) {
            auto value = Json::parse(line,nullptr,false);
            if (value.is_object() && String(value,"method") == "select_account") {
                ++count; fLastSelected = value["params"][0].get<uint32>();
            }
        }
        return count;
    }
    bool Click(const char* title, const char* name) {
        for (int32 i = 0; i < CountWindows(); ++i) {
            auto* window = WindowAt(i);
            if (!window || !window->Lock()) continue;
            auto* button = std::string(window->Title()) == title ? dynamic_cast<BButton*>(window->FindView(name)) : nullptr;
            if (button) CHECK(button->Invoke() == B_OK);
            window->Unlock(); if (button) return true;
        }
        return false;
    }
    bool ConfirmDelete() {
        for (int32 i = 0; i < CountWindows(); ++i) {
            auto* window = WindowAt(i);
            if (!window || !window->Lock()) continue;
            auto* alert = dynamic_cast<BAlert*>(window);
            bool found = alert && std::string(alert->TextView()->Text()).find("Permanently delete profile 9") != std::string::npos;
            if (found) CHECK(alert->ButtonAt(1)->Invoke() == B_OK);
            window->Unlock(); if (found) return true;
        }
        return false;
    }
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
    bool fRetried = false;
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
