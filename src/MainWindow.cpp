#include "MainWindow.h"
#include "Branding.h"
#include <Alert.h>
#include <Application.h>
#include <Clipboard.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Notification.h>
#include <Path.h>
#include <Roster.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <SplitView.h>
#include <StringItem.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <ctime>
#include <Invoker.h>
#include "DiagnosticText.h"
#include <cmath>

namespace {
void Clear(BListView* list) { while (auto* item = list->RemoveItem(int32(0))) delete item; }
const char* Field(const BMessage& message, const char* name) {
    // FindString clears its output on failure; initialization is not a fallback.
    const char* value = nullptr;
    return message.FindString(name, &value) == B_OK && value ? value : "";
}
BMenuItem* MenuItem(BMenu* menu, const char* label, uint32 what, char shortcut = 0) {
    auto* item = new BMenuItem(label, new BMessage(what), shortcut);
    menu->AddItem(item); return item;
}
void Copy(const std::string& text) {
    if (be_clipboard->Lock()) {
        be_clipboard->Clear(); be_clipboard->Data()->AddData("text/plain", B_MIME_TYPE, text.data(), text.size());
        be_clipboard->Commit(); be_clipboard->Unlock();
    }
}
}
MainWindow::MainWindow(bool smokeTest)
    : BWindow(BRect(100,80,1100,780), kAppName, B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
      fSmokeTest(smokeTest) {
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK) throw std::runtime_error("Cannot find user settings");
    path.Append(kSettingsFolder); fSettings = path.Path();
    if (const char* settingsOverride = getenv("PARLA_SETTINGS_PATH")) fSettings = settingsOverride;
    if (fSmokeTest && getenv("DC_ACCOUNTS_PATH"))
        fSettings = std::string(getenv("DC_ACCOUNTS_PATH")) + "-settings";
    create_directory(fSettings.c_str(), 0700); chmod(fSettings.c_str(), 0700);
    BMessage settings; BFile file((fSettings + "/window").c_str(), B_READ_ONLY);
    if (settings.Unflatten(&file) == B_OK) {
        BRect frame;
        if (settings.FindRect("frame", &frame) == B_OK && frame.IsValid()) { MoveTo(frame.LeftTop()); ResizeTo(frame.Width(), frame.Height()); }
    }
    std::ifstream drafts(fSettings + "/drafts.json");
    if (drafts) {
        Json data = Json::parse(drafts, nullptr, false);
        if (data.is_array()) for (auto& draft : data)
            if (draft.is_object() && draft.contains("account") && draft["account"].is_number_unsigned()
                && draft["account"].get<uint64_t>() > 0 && draft["account"].get<uint64_t>() <= UINT32_MAX
                && draft.contains("chat") && draft["chat"].is_number_unsigned()
                && draft["chat"].get<uint64_t>() > 0 && draft["chat"].get<uint64_t>() <= UINT32_MAX) {
                if (!draft.contains("quote") || !draft["quote"].is_number_unsigned()
                    || draft["quote"].get<uint64_t>() > UINT32_MAX) draft["quote"] = 0u;
                fDrafts[{draft["account"].get<uint32>(),draft["chat"].get<uint32>()}] = draft;
            }
    }
    auto* menu = new BMenuBar("menu");
    auto* profile = new BMenu("Profile"); menu->AddItem(profile);
    MenuItem(profile,"New profile…",'prof','N');
    MenuItem(profile,"Use existing email…",'mail');
    MenuItem(profile,"Link another device…",'link');
    profile->AddSeparatorItem();
    fAccounts = new BMenu("Switch profile"); profile->AddItem(fAccounts);
    MenuItem(profile,"Profile settings…",'sett');
    MenuItem(profile,"Delete this profile…",'rmcf');
    MenuItem(profile,"Open accounts folder",'adir');
    MenuItem(profile,"Copy my invitation",'invt');
    profile->AddSeparatorItem();
    MenuItem(profile,"Import backup…",'impt'); MenuItem(profile,"Export backup…",'expt');
    profile->AddSeparatorItem();
    MenuItem(profile,"About Parla…",B_ABOUT_REQUESTED)->SetTarget(be_app);
    MenuItem(profile,"Last error…",'err!');
    MenuItem(profile,"Diagnostics…",'diag','D');
    MenuItem(profile,"Quit",B_QUIT_REQUESTED,'Q');
    auto* editMenu = new BMenu("Edit"); menu->AddItem(editMenu);
    MenuItem(editMenu,"Cut",B_CUT,'X'); MenuItem(editMenu,"Copy",B_COPY,'C');
    MenuItem(editMenu,"Paste",B_PASTE,'V'); MenuItem(editMenu,"Select all",B_SELECT_ALL,'A');
    auto* chat = new BMenu("Chat"); menu->AddItem(chat);
    MenuItem(chat,"New chat…",'newc','J'); MenuItem(chat,"New group…",'newg');
    MenuItem(chat,"Join invitation…",'join'); MenuItem(chat,"Copy group invitation",'ginv');
    MenuItem(chat,"Add group member…",'memb');
    chat->AddSeparatorItem();
    MenuItem(chat,"Archive / unarchive",'arch');
    MenuItem(chat,"Show archived chats",'sarc'); MenuItem(chat,"Refresh",'rfrs','R');
    auto* message = new BMenu("Message"); menu->AddItem(message);
    MenuItem(message,"Reply",'repl'); MenuItem(message,"Copy text",'copy');
    MenuItem(message,"Edit…",'edit'); MenuItem(message,"React…",'reac');
    MenuItem(message,"Download attachment",'down');
    MenuItem(message,"Open attachment",'open','O'); MenuItem(message,"Save attachment…",'save');
    MenuItem(message,"Details…",'info','I'); MenuItem(message,"Retry sending",'retr');
    MenuItem(message,"Delete…",'dele');
    fSearch = new BTextControl("search","Find chats:","",new BMessage('srch'));
    fSearch->SetModificationMessage(new BMessage('srch'));
    fChats = new BListView("chats"); fChats->SetSelectionMessage(new BMessage('chat'));
    auto* chatScroll = new BScrollView("chatScroll",fChats,0,false,true);
    chatScroll->SetExplicitMinSize(BSize(200,250));
    auto* side = new BGroupView(B_VERTICAL);
    BLayoutBuilder::Group<>(side,B_VERTICAL).SetInsets(B_USE_DEFAULT_SPACING)
        .Add(fSearch,0).Add(chatScroll,1).Add(new BButton("new","New chat…",new BMessage('newc')),0);
    fTitle = new BStringView("title","Welcome to Parla");
    fMessages = new MessageList(); fMessages->SetInvocationMessage(new BMessage('open'));
    auto* scroll = new BScrollView("messageScroll",fMessages,0,false,true);
    scroll->SetExplicitMinSize(BSize(380,300));
    fComposer = new Composer();
    auto* composeScroll = new BScrollView("composeScroll",fComposer,0,false,true);
    fSend = new BButton("send","Send",new BMessage(kSend));
    fAttach = new BButton("attach","+",new BMessage('atch'));
    fAttach->SetToolTip("Attach a file");
    font_height metrics; be_plain_font->GetHeight(&metrics);
    float buttonSize = ceilf(metrics.ascent + metrics.descent + metrics.leading) + 12;
    fAttach->SetExplicitMinSize(BSize(buttonSize,buttonSize));
    fAttach->SetExplicitPreferredSize(BSize(buttonSize,buttonSize));
    fAttach->SetExplicitMaxSize(BSize(buttonSize,buttonSize));
    composeScroll->SetExplicitMinSize(BSize(180,buttonSize));
    composeScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,buttonSize));
    fAccept = new BButton("accept","Accept contact request",new BMessage('acpt'));
    fOlder = new BButton("older","Load earlier messages",new BMessage('oldr'));
    fDraftLabel = new BStringView("draft","");
    fComposer->SetToolTip("Enter sends · Shift+Enter adds a line");
    auto* conversation = new BGroupView(B_VERTICAL);
    BLayoutBuilder::Group<>(conversation,B_VERTICAL).SetInsets(B_USE_DEFAULT_SPACING)
        .AddGroup(B_HORIZONTAL,B_USE_DEFAULT_SPACING,0).Add(fTitle).AddGlue().Add(fAccept).End()
        .Add(fOlder,0).Add(scroll,1).Add(fDraftLabel,0)
        .AddGroup(B_HORIZONTAL,B_USE_DEFAULT_SPACING,0).Add(fAttach,0).Add(composeScroll,1).Add(fSend,0).End();
    auto* clear = new BMenuItem("Clear attachment / reply", new BMessage('clra'));
    message->AddItem(clear);
    auto* split = new BSplitView(B_HORIZONTAL); split->AddChild(side); split->AddChild(conversation);
    split->SetItemWeight(split->GetLayout()->ItemAt(0),0.32f);
    split->SetItemWeight(split->GetLayout()->ItemAt(1),0.68f);
    fStatus = new BStringView("status","Starting Chatmail…");
    fStatus->SetTruncation(B_TRUNCATE_END);
    fTitle->SetTruncation(B_TRUNCATE_END);
    fDraftLabel->SetTruncation(B_TRUNCATE_END);
    BLayoutBuilder::Group<>(this,B_VERTICAL,0).Add(menu,0).Add(split,1).Add(fStatus,0);
    fDraftLabel->Hide(); fAccept->Hide(); fOlder->Hide(); fOlder->SetEnabled(false); SetComposeEnabled(false);
    BMessage pulse('tick'); fPulse = std::make_unique<BMessageRunner>(BMessenger(this), &pulse, 500000);
    PostMessage('boot');
}
MainWindow::~MainWindow() { fPulse.reset(); fRpc.Stop(); Clear(fChats); Clear(fMessages); }
void MainWindow::ShowError(const std::string& error) {
    std::string text = DiagnosticText(error);
    fLastError = text; fStatus->SetText(text.c_str()); Log("ERROR " + text);
    fprintf(stderr, "Parla: %s\n", text.c_str());
    // Keep full diagnostics visible without stacking alerts for pending RPCs.
    if (!fErrorAlert.IsValid()) {
        auto* alert = new BAlert("Parla error", text.c_str(), "OK", "Diagnostics…");
        fErrorAlert = BMessenger(alert); alert->Go(new BInvoker(new BMessage('dtls'),this));
    }
}
std::string MainWindow::DiagnosticText(const std::string& text) const {
    std::string safe = text;
    for (const auto& secret : fSecrets) if (!secret.empty()) {
        size_t pos = 0;
        while ((pos = safe.find(secret, pos)) != std::string::npos) { safe.replace(pos, secret.size(), "[redacted]"); pos += 10; }
    }
    // Normalize invalid UTF-8 from stderr before handing it to native text views.
    return Json::parse(Json(safe).dump(-1, ' ', false, Json::error_handler_t::replace)).get<std::string>();
}
void MainWindow::Log(const std::string& text) {
    std::string safe = DiagnosticText(text);
    time_t now = time(nullptr); tm local{}; localtime_r(&now, &local);
    char stamp[32]; strftime(stamp, sizeof stamp, "%H:%M:%S", &local);
    fLog += std::string(stamp) + " " + safe + "\n";
    if (fLog.size() > 65536) {
        size_t cut = fLog.find('\n', fLog.size() - 65536);
        fLog.erase(0, cut == std::string::npos ? fLog.size() : cut + 1);
    }
    fLogChanged = true;
}
void MainWindow::Diagnostics() {
    std::string text = std::string("Parla ") + kAppVersion + " · " + std::to_string(sizeof(void*) * 8) + "-bit\n"
        + fConnectionDetails[fAccount] + "\nActive profile: " + std::to_string(fAccount)
        + (fReady ? " · RPC pipe open\n" : " · RPC disconnected\n")
        + "Last reply: " + (fLastReply ? std::to_string((system_time()-fLastReply)/1000000) + "s ago" : "none") + "\n"
        + "Last error: " + (fLastError.empty() ? "none" : fLastError);
    if (!fDiagnosticsWindow.IsValid()) fDiagnosticsWindow = BMessenger(new DiagnosticsWindow(text, fLog));
    else { BMessage update('logs'); update.AddString("summary",text.c_str()); update.AddString("text",fLog.c_str()); fDiagnosticsWindow.SendMessage(&update); }
}
void MainWindow::Call(const char* method, Json params, Callback callback, int timeout) {
    std::string operation(method);
    if (!params.empty() && params[0].is_number()) operation += " profile=" + params[0].dump();
    if (std::string(method) == "send_msg" && params.size() > 1) operation += " chat=" + params[1].dump();
    bool trace = std::string(method) != "get_next_event_batch";
    try {
        uint64_t id = fRpc.Call(method,params,timeout);
        operation = "RPC #" + std::to_string(id) + " " + operation;
        auto started = system_time();
        if (trace) Log(operation + " request"); // Never log passwords, setup codes or message bodies.
        fCallbacks[id] = [this,callback,operation,started,trace](const Json& response) {
            std::string elapsed = " (" + std::to_string((system_time() - started) / 1000) + " ms)";
            if (response.contains("error")) {
                ShowError(operation + elapsed + ": " + String(response["error"],"message","Chatmail operation failed"));
                if (callback) callback(Json{{"_rpcError", true},{"message",String(response["error"],"message")}});
            } else {
                auto result = response.value("result",Json());
                if (trace) Log(operation + elapsed + " result=" +
                    (result.is_primitive() && !result.is_string() ? result.dump() : std::string(result.type_name())));
                if (callback) callback(result);
            }
        };
    } catch (const std::exception& e) { ShowError(operation + ": " + e.what()); if (callback) callback(Json{{"_rpcError", true}}); }
}
void MainWindow::Start() {
    app_info info; be_app->GetAppInfo(&info);
    BPath app(&info.ref); app.GetParent(&app); app.Append("deltachat-rpc-server");
    const char* override = getenv("PARLA_RPC_SERVER");
    std::string executable = override ? override : app.Path();
    const char* accountsOverride = getenv("DC_ACCOUNTS_PATH");
    std::string accounts = accountsOverride ? accountsOverride : fSettings + "/accounts";
    fAccountsPath = accounts;
    fStartup = std::string("Parla ") + kAppVersion + " · " + std::to_string(sizeof(void*) * 8) + "-bit\nAccounts: " + accounts
        + "\nServer: " + executable + "\nTLS CA: "
        + (getenv("SSL_CERT_FILE") ? getenv("SSL_CERT_FILE") : "/boot/system/data/ssl/CARootCertificates.pem");
    fStartup += "\nLog: " + fSettings + "/diagnostics.log";
    rename((fSettings + "/diagnostics.log").c_str(),(fSettings + "/diagnostics.previous.log").c_str());
    Log(fStartup);
    create_directory(accounts.c_str(),0700); chmod(accounts.c_str(),0700);
    try {
        BMessenger target(this);
        fRpc.Start(executable,accounts,[target](const Json& response) {
            BMessage message(kRPC);
            message.AddString("json",response.dump(-1, ' ', false, Json::error_handler_t::replace).c_str());
            target.SendMessage(&message,static_cast<BHandler*>(nullptr),1000000);
        });
        Call("get_system_info",Json::array(),[this](const Json& result) {
            if (!result.is_object() || result.contains("_rpcError")) return;
            fReady = true; fStatus->SetText("Ready");
            if (fSmokeTest) {
                if (const char* marker = getenv("PARLA_SMOKE_MARKER")) std::ofstream(marker) << "GUI and Chatmail handshake OK\n";
                // Leave the native window visible briefly for CI's screenshot.
                BMessage quit(B_QUIT_REQUESTED);
                fPulse = std::make_unique<BMessageRunner>(BMessenger(this), &quit, 5000000, 1);
                return;
            }
            Events(); LoadAccounts();
        },20);
    } catch (const std::exception& e) { ShowError(e.what()); }
}
void MainWindow::LoadAccounts() {
    const auto request = ++fAccountListRequest;
    Call("get_all_accounts",Json::array(),[this,request](const Json& accounts) {
        if (request != fAccountListRequest || !accounts.is_array()) return;
        std::set<uint32> ids;
        while (auto* item = fAccounts->RemoveItem(int32(0))) delete item;
        for (const auto& account : accounts) {
            uint32 id = account.value("id",0u);
            if (!id || fDeletingAccounts.count(id)) continue;
            ids.insert(id);
            // Avoid holding the core's global accounts write lock while starting
            // every scheduler. Each profile starts independently and idempotently.
            if (String(account,"kind") != "Unconfigured" && !fSetupAccounts.count(id)
                && fStartedAccounts.insert(id).second) {
                Call("start_io",Json::array({id}),[this,id](const Json& result) {
                    if (!result.is_null()) fStartedAccounts.erase(id);
                    else { Log("Network tasks started profile=" + std::to_string(id)); Connection(id); }
                });
            }
            std::string label = String(account,"displayName","Profile " + std::to_string(id));
            if (String(account,"kind") == "Unconfigured") label += " (setup incomplete)";
            auto* m = new BMessage('acct'); m->AddUInt32("account",id);
            auto* item = new BMenuItem(label.c_str(),m); item->SetMarked(id == fAccount); fAccounts->AddItem(item);
            item->SetTarget(this);
        }
        if (fAccount && !ids.count(fAccount) && !fDeletingAccounts.count(fAccount)) {
            fAccount = fChat = fQuote = 0; ++fGeneration; ++fChatRequest;
            fRefreshing = fRefreshAgain = false;
            fComposer->SetText(""); fAttachment.clear(); DraftLabel("");
            Clear(fChats); Clear(fMessages); SetComposeEnabled(false);
        }
        if (!fAccount && !fSelecting && !ids.empty()) {
            Call("get_selected_account_id",Json::array(),[this,ids,request](const Json& selected) {
                if (request != fAccountListRequest || fAccount || fSelecting) return;
                uint32 id = selected.is_number_unsigned() ? selected.get<uint32>() : 0;
                SelectAccount(ids.count(id) ? id : *ids.begin());
            });
        }
        if (ids.empty() && !fSelecting && fDeletingAccounts.empty())
            fTitle->SetText("Create a profile to start chatting");
    });
}
void MainWindow::SaveDraft() {
    if (!fAccount || !fChat) return;
    fDrafts[{fAccount,fChat}] = {{"account",fAccount},{"chat",fChat},{"text",fComposer->Text()},
        {"file",fAttachment},{"quote",fQuote}};
    WriteDrafts();
}
void MainWindow::WriteDrafts() {
    Json all = Json::array();
    for (const auto& d : fDrafts) if (!String(d.second,"text").empty() || !String(d.second,"file").empty()) all.push_back(d.second);
    std::string temp = fSettings + "/drafts.json.tmp";
    { std::ofstream file(temp); file << all.dump(); if (!file) { ShowError("Cannot save draft"); return; } }
    chmod(temp.c_str(),0600); rename(temp.c_str(),(fSettings + "/drafts.json").c_str());
}
void MainWindow::SelectAccount(uint32 id, bool force) {
    if (!id || fDeletingAccounts.count(id)) return;
    fDesiredAccount = id;
    if (fSelecting) return; // Serialize core selection; IDs are never menu indexes.
    if (id == fAccount && !force) return;
    fSelecting = true;
    Call("select_account",Json::array({id}),[this,id](const Json& result) {
        fSelecting = false;
        if (fDesiredAccount != id) { SelectAccount(fDesiredAccount,true); return; }
        if (!result.is_null() || fDeletingAccounts.count(id)) { LoadAccounts(); return; }
        SaveDraft(); fAccount = id; fChat = 0; ++fGeneration; ++fChatRequest;
        fRefreshing = fRefreshAgain = false;
        fQuote = 0; fAttachment.clear(); fComposer->SetText(""); DraftLabel("");
        Clear(fChats); Clear(fMessages); SetComposeEnabled(false);
        fTitle->SetText("Select a conversation");
        for (int32 i = 0; i < fAccounts->CountItems(); ++i) {
            auto* item = fAccounts->ItemAt(i);
            item->SetMarked(item->Message() && item->Message()->GetUInt32("account",0) == id);
        }
        Log("Selected profile=" + std::to_string(id));
        RefreshChats(); Connection(id);
    });
}
void MainWindow::DraftLabel(const std::string& text) {
    fDraftLabel->SetText(text.c_str());
    if (text.empty() && !fDraftLabel->IsHidden()) fDraftLabel->Hide();
    if (!text.empty() && fDraftLabel->IsHidden()) fDraftLabel->Show();
}
void MainWindow::RefreshChats() {
    if (!fReady || !fAccount) return;
    uint32 account = fAccount; uint64_t request = ++fChatRequest;
    std::string query = fSearch->Text();
    Call("get_chatlist_entries",Json::array({account,fArchived ? 1 : 0,query.empty() ? Json() : Json(query),nullptr}),
        [this,account,request](const Json& ids) {
        if (!ids.is_array() || account != fAccount || request != fChatRequest) return;
        Call("get_chatlist_items_by_entries",Json::array({account,ids}),[this,ids,account,request](const Json& items) {
            if (!items.is_object() || account != fAccount || request != fChatRequest) return;
            Clear(fChats); int32 selected = -1;
            for (const auto& id : ids) {
                auto key = std::to_string(id.get<uint32>());
                if (!items.contains(key)) continue;
                const auto& item = items[key];
                if (String(item,"kind") != "ChatListItem") continue;
                if (id.get<uint32>() == fChat) selected = fChats->CountItems();
                fChats->AddItem(new ChatItem(item));
            }
            if (selected >= 0) fChats->Select(selected);
        });
    });
}
void MainWindow::SetComposeEnabled(bool enabled) {
    fCanSend = enabled; fComposer->MakeEditable(enabled && !fSending);
    fSend->SetEnabled(enabled && !fSending); fAttach->SetEnabled(enabled && !fSending);
}
void MainWindow::SelectChat(uint32 id) {
    if (!id || id == fChat) return;
    SaveDraft(); fChat = id; ++fGeneration; fLimit = 100; fRefreshing = false; fRefreshAgain = false;
    fAttachment.clear(); fQuote = 0; fComposer->SetText(""); Clear(fMessages);
    auto draft = fDrafts.find({fAccount,fChat});
    if (draft != fDrafts.end()) {
        fComposer->SetText(String(draft->second,"text").c_str());
        fAttachment = String(draft->second,"file"); fQuote = draft->second.value("quote",0u);
    }
    DraftLabel(fAttachment);
    SetComposeEnabled(false); RefreshMessages();
}
void MainWindow::RefreshMessages(bool older) {
    if (!fAccount || !fChat) return;
    if (fRefreshing) { fRefreshAgain = true; return; }
    if (older) fLimit += 100;
    fRefreshing = true;
    uint64_t generation = fGeneration, request = ++fMessageRequest;
    uint32 account = fAccount, chat = fChat;
    Call("get_full_chat_by_id",Json::array({account,chat}),[this,generation](const Json& data) {
        if (generation != fGeneration || !data.is_object() || data.contains("_rpcError")) return;
        fTitle->SetText((String(data,"name") + (data.value("isEncrypted",false) ? "" : " · unencrypted email")).c_str());
        SetComposeEnabled(data.value("canSend",false));
        bool request = data.value("isContactRequest",false);
        if (request && fAccept->IsHidden()) fAccept->Show();
        else if (!request && !fAccept->IsHidden()) fAccept->Hide();
    });
    Call("get_message_ids",Json::array({account,chat,false,false}),[this,generation,request,account,chat,older](const Json& all) {
        if (generation != fGeneration || request != fMessageRequest) return;
        if (!all.is_array()) { fRefreshing = false; return; }
        Json ids = Json::array(); size_t first = all.size() > fLimit ? all.size() - fLimit : 0;
        fOlder->SetEnabled(first > 0);
        if (first && fOlder->IsHidden()) fOlder->Show();
        else if (!first && !fOlder->IsHidden()) fOlder->Hide();
        for (size_t i = first; i < all.size(); ++i) ids.push_back(all[i]);
        Call("get_messages",Json::array({account,ids}),[this,generation,request,ids,older](const Json& objects) {
            if (generation != fGeneration || request != fMessageRequest) return;
            fRefreshing = false;
            if (!objects.is_object() || objects.contains("_rpcError")) return;
            BRect bounds = fMessages->Bounds();
            float lastBottom = fMessages->CountItems() ? fMessages->ItemFrame(fMessages->CountItems()-1).bottom : 0;
            bool bottom = bounds.bottom >= lastBottom - 10;
            uint32 selection = SelectedMessage().value("id",0u);
            // Preserve both selection and scroll position when updating delivery receipts.
            uint32 anchor = 0; float offset = 0;
            int32 top = fMessages->IndexOf(BPoint(2,bounds.top + 1));
            if (auto* item = dynamic_cast<MessageItem*>(fMessages->ItemAt(top))) {
                anchor = item->data.value("id",0u); offset = bounds.top - fMessages->ItemFrame(top).top;
            }
            Clear(fMessages); int32 anchorIndex = -1, selected = -1;
            for (const auto& id : ids) {
                std::string key = std::to_string(id.get<uint32>());
                if (!objects.contains(key) || String(objects[key],"kind") != "message") continue;
                if (id == anchor) anchorIndex = fMessages->CountItems();
                if (id == selection) selected = fMessages->CountItems();
                fMessages->AddItem(new MessageItem(objects[key]));
            }
            if (selected >= 0) fMessages->Select(selected);
            if (bottom && !older && fMessages->CountItems()) fMessages->ScrollTo(0,std::max(0.0f,
                fMessages->ItemFrame(fMessages->CountItems()-1).bottom - fMessages->Bounds().Height()));
            else if (anchorIndex >= 0) fMessages->ScrollTo(0,fMessages->ItemFrame(anchorIndex).top + offset);
            if (++fConnectionTicks >= 20) { fConnectionTicks = 0; Connection(fAccount); }
        MarkVisible();
            if (fRefreshAgain) { fRefreshAgain = false; RefreshMessages(); }
        });
    });
}
void MainWindow::MarkVisible() {
    if (!fReady || !IsActive() || IsMinimized() || !fAccount || !fChat) return;
    Json ids = Json::array(); BRect bounds = fMessages->Bounds();
    for (int32 i = 0; i < fMessages->CountItems(); ++i) {
        auto* item = static_cast<MessageItem*>(fMessages->ItemAt(i));
        BRect row = fMessages->ItemFrame(i);
        if (row.top >= bounds.top && row.bottom <= bounds.bottom
            && item->data.value("fromId",0) != 1 && item->data.value("state",0) < 16)
            ids.push_back(item->data.value("id",0u));
    }
    if (ids.empty()) return;
    auto key = std::make_pair(fAccount,fChat); std::string signature = ids.dump();
    if (fSeen[key] == signature) return;
    fSeen[key] = signature;
    Call("markseen_msgs",Json::array({fAccount,ids}),[this,key,signature](const Json& result) {
        if (!result.is_null() && fSeen[key] == signature) fSeen.erase(key);
    });
}
void MainWindow::Events() {
    if (!fReady || fEventsPending) return;
    fEventsPending = true;
    Call("get_next_event_batch",Json::array(),[this](const Json& events) {
        fEventsPending = false;
        if (!events.is_array()) { fEventRetryTicks = 10; return; }
        bool chats = false, messages = false;
        for (const auto& event : events) {
            auto data = event.value("event",Json::object());
            std::string kind = String(data,"kind"); uint32 account = event.value("contextId",0u);
            if (kind == "IncomingMsg" && (!IsActive() || account != fAccount || data.value("chatId",0u) != fChat)) {
                // Do not put message bodies on the desktop by default.
                BNotification notification(B_INFORMATION_NOTIFICATION); notification.SetGroup(kAppName);
                notification.SetTitle("New message"); notification.SetContent("Open Parla to read your messages.");
                notification.SetOnClickApp(kAppSignature); notification.Send();
            }
            std::string detail = String(data,"msg",String(data,"comment"));
            if (!detail.empty() || kind == "MsgFailed" || kind == "MsgDelivered" || kind == "ConfigureProgress")
                Log("Core profile=" + std::to_string(account) + " " + kind + " " + detail
                    + (data.contains("msgId") ? " message=" + data["msgId"].dump() : ""));
            if (kind == "AccountsChanged" || kind == "AccountChanged") LoadAccounts();
            if (kind == "MsgFailed") {
                uint32 msg = data.value("msgId",0u);
                Call("get_message",Json::array({account,msg}),[this,account,msg](const Json& value) {
                    if (value.is_object() && !value.contains("_rpcError"))
                        ShowError("Sending failed · profile " + std::to_string(account) + " · message " + std::to_string(msg)
                            + ": " + String(value,"error","See Diagnostics for server details."));
                });
            }
            if (account != fAccount) continue;
            if (kind == "Error") ShowError(String(data,"msg"));
            if (kind == "ConfigureProgress" || kind == "ImexProgress") {
                int progress = data.value("progress", 0);
                std::string text = kind == "ImexProgress" ? "Transferring profile: " : "Configuring profile: ";
                text += std::to_string(progress / 10) + "%";
                std::string comment = String(data, "comment");
                if (!comment.empty()) text += " · " + comment;
                fStatus->SetText(text.c_str());
            }
            if (kind == "MsgsChanged" || kind == "IncomingMsg" || kind == "MsgsNoticed" || kind == "ChatlistChanged"
                || kind == "ChatlistItemChanged" || kind == "ChatModified") chats = true;
            if (kind == "MsgsChanged" || kind == "IncomingMsg" || kind == "MsgDelivered" || kind == "MsgRead"
                || kind == "MsgFailed" || kind == "ChatModified" || kind == "ReactionsChanged") messages = true;
            if (kind == "ConnectivityChanged") Connection(account);
            if (kind == "MsgDelivered") fStatus->SetText("Message sent to server");
        }
        if (chats) RefreshChats();
        if (messages) RefreshMessages();
        Events();
    },0);
}
void MainWindow::Connection(uint32 account) {
    if (!account || fDeletingAccounts.count(account) || !fConnectionPending.insert(account).second) return;
    Call("get_connectivity_html",Json::array({account}),[this,account](const Json& html) {
        fConnectionPending.erase(account);
        if (fDeletingAccounts.count(account)) return;
        std::string detail = html.is_string() ? DiagnosticText(ConnectivityText(html.get<std::string>()))
            : "Connectivity query failed · see last RPC error below";
        if (fConnectionDetails[account] != detail) {
            fConnectionDetails[account] = detail;
            Log("Connectivity profile=" + std::to_string(account) + "\n" + detail);
        }
    },20);
}
void MainWindow::Send() {
    if (!fReady || !fCanSend || fSending || (!fComposer->TextLength() && fAttachment.empty())) return;
    uint32 account = fAccount, chat = fChat; uint64_t generation = fGeneration;
    Json data = {{"text",fComposer->Text()}};
    if (!fAttachment.empty()) data["file"] = fAttachment;
    if (fQuote) data["quotedMessageId"] = fQuote;
    SaveDraft(); fSending = true; SetComposeEnabled(fCanSend); fStatus->SetText("Sending…");
    Call("send_msg",Json::array({account,chat,data}),[this,account,chat,generation](const Json& result) {
        fSending = false;
        if (result.is_number()) {
            fDrafts.erase({account,chat});
            if (generation == fGeneration) {
                fComposer->SetText(""); fQuote = 0; fAttachment.clear(); SaveDraft();
                DraftLabel(""); fStatus->SetText("Message queued · waiting for server");
            }
        }
        SetComposeEnabled(fCanSend); RefreshChats(); if (generation == fGeneration) RefreshMessages();
    });
}
Json MainWindow::SelectedMessage() const {
    auto* item = dynamic_cast<MessageItem*>(fMessages->ItemAt(fMessages->CurrentSelection()));
    return item ? item->data : Json::object();
}
void MainWindow::Form(const char* title, const char* help, std::vector<FormField> fields, uint32 what, const char* action, uint32 retryAccount) {
    BMessage message(what);
    if (retryAccount) message.AddUInt32("retryAccount", retryAccount);
    message.AddUInt32("account",fAccount); message.AddUInt32("chat",fChat);
    message.AddUInt32("message",SelectedMessage().value("id",0u));
    new FormWindow(title,help,std::move(fields),BMessenger(this),message,action);
}
void MainWindow::CreateProfile(const BMessage& message) {
    std::string name = Field(message,"field0"), credential = Field(message,"field1"), secret = Field(message,"field2");
    if (name.empty()) { ShowError("Enter a display name"); return; }
    uint32 action = message.what;
    if ((action == 'crml' || action == 'crlk') && credential.empty()) {
        ShowError(action == 'crml' ? "Enter an email address" : "Enter a second-device setup code"); return;
    }
    if (!fSetupAccounts.empty()) { ShowError("A profile setup is still running. Wait for it to finish before retrying."); return; }
    auto setup = [this,name,credential,secret,action](const Json& result) {
        if (!result.is_number_unsigned()) return;
        uint32 id = result.get<uint32>();
        fSetupAccounts.insert(id);
        if (!credential.empty()) fSecrets.push_back(credential);
        if (!secret.empty()) fSecrets.push_back(secret);
        SelectAccount(id); LoadAccounts(); fStatus->SetText("Setting up profile…");
        auto done = [this,id](const Json& result) {
            if (String(result,"message").find("result is unknown") != std::string::npos) {
                Call("stop_ongoing_process",Json::array({id}),[this,id](const Json& stopped) {
                    if (stopped.is_null()) fSetupAccounts.erase(id);
                });
                return;
            }
            fSetupAccounts.erase(id);
            if (result.is_object() && result.contains("_rpcError")) { LoadAccounts(); return; }
            Call("is_configured",Json::array({id}),[this,id](const Json& configured) {
                LoadAccounts();
                if (configured == true) Call("start_io",Json::array({id}),[this,id](const Json& started) {
                    if (started.is_null() && id == fAccount) { RefreshChats(); fStatus->SetText("Profile ready"); }
                });
                else if (configured == false) ShowError("Profile setup incomplete. Open Profile settings to choose how to finish setup.");
            });
        };
        Call("set_config",Json::array({id,"displayname",name}),[this,id,credential,secret,action,done](const Json& setName) {
            if (!setName.is_null()) { done(setName); return; }
            if (action == 'crml') {
                // Use the current API. Legacy configure() reads unrelated stored keys.
                Call("add_or_update_transport",Json::array({id,Json{{"addr",credential},{"password",secret}}}),done,300);
            } else if (action == 'crlk') Call("get_backup",Json::array({id,credential}),done,300);
            else Call("init_transports",Json::array({id,credential.empty() ? Json() : Json(credential)}),done,300);
        });
    };
    uint32 retry = message.GetUInt32("retryAccount", 0);
    if (retry) Call("is_configured",Json::array({retry}),[this,retry,setup](const Json& configured) {
        if (configured == false) setup(Json(retry));
        else if (configured == true) ShowError("This profile is already configured. Open Diagnostics to investigate connectivity.");
    });
    else { fStatus->SetText("Creating profile…"); Call("add_account",Json::array(),setup); }
}
void MainWindow::ProfileSettings(uint32 account) {
    if (!account) return;
    Call("get_config",Json::array({account,"displayname"}),[this,account](const Json& name) {
        if (!name.is_string() && !name.is_null()) return;
        Call("is_configured",Json::array({account}),[this,account,name](const Json& configured) {
            if (!configured.is_boolean()) return;
            Call("list_transports",Json::array({account}),[this,account,name,configured](const Json& transports) {
                if (!transports.is_array() || account != fAccount) return;
                std::string details = "Profile " + std::to_string(account)
                    + (configured == true ? " · configured\n" : " · setup incomplete\nChoose Chatmail, existing email or device transfer below.\n");
                for (const auto& transport : transports) details += "\nEmail: " + String(transport,"addr")
                    + "\nIMAP: " + String(transport,"imapServer","automatic")
                    + " · SMTP: " + String(transport,"smtpServer","automatic") + "\n";
                details += "\nAccounts folder: " + fAccountsPath;
                new ProfileWindow(BMessenger(this),account,name.is_string() ? name.get<std::string>() : "",details,configured == true);
            });
        });
    });
}
void MainWindow::RemoveProfile(uint32 account) {
    if (!account) return;
    if (fSetupAccounts.count(account)) { ShowError("Wait for profile setup to finish before deleting it."); return; }
    auto* alert = new BAlert("Delete profile", ("Permanently delete profile " + std::to_string(account)
        + " and its messages and attachments from this computer? Other devices are unaffected.\n\nFolder: "
        + fAccountsPath).c_str(), "Cancel", "Delete", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
    auto* result = new BMessage('rmac'); result->AddUInt32("account",account);
    alert->Go(new BInvoker(result,this));
}
void MainWindow::DeleteProfile(uint32 account) {
    if (!account || fSetupAccounts.count(account) || !fDeletingAccounts.insert(account).second) return;
    ++fAccountListRequest;
    LoadAccounts(); fStatus->SetText("Deleting profile…");
    Call("remove_account",Json::array({account}),[this,account](const Json& result) {
        if (!result.is_null()) { fDeletingAccounts.erase(account); LoadAccounts(); return; }
        ++fAccountListRequest;
        if (fDesiredAccount == account) fDesiredAccount = 0;
        if (fAccount == account) {
            fAccount = fChat = fQuote = 0; ++fGeneration; ++fChatRequest;
            fRefreshing = fRefreshAgain = false; fAttachment.clear(); fComposer->SetText("");
            DraftLabel(""); Clear(fChats); Clear(fMessages); SetComposeEnabled(false);
        }
        for (auto i = fDrafts.begin(); i != fDrafts.end();) {
            if (i->first.first == account) i = fDrafts.erase(i); else ++i;
        }
        for (auto i = fSeen.begin(); i != fSeen.end();) {
            if (i->first.first == account) i = fSeen.erase(i); else ++i;
        }
        fConnectionDetails.erase(account); fStartedAccounts.erase(account);
        fDeletingAccounts.erase(account);
        WriteDrafts(); LoadAccounts(); fStatus->SetText("Profile deleted from this computer");
    });
}

void MainWindow::OpenAttachment() {
    auto message = SelectedMessage(); std::string path = String(message,"file");
    if (path.empty()) { ShowError("Select a downloaded attachment first"); return; }
    entry_ref ref;
    if (get_ref_for_path(path.c_str(),&ref) != B_OK) { ShowError("Attachment is unavailable"); return; }
    // Opening files is explicit. Never execute received applications automatically.
    BEntry entry(&ref); BPath actual; entry.GetPath(&actual);
    auto* alert = new BAlert("Open attachment",("Open “" + String(message,"fileName","attachment") + "” with its associated application?").c_str(),"Cancel","Open");
    if (alert->Go() == 1 && be_roster->Launch(&ref) != B_OK) ShowError("No application could open this attachment");
}
bool MainWindow::QuitRequested() {
    SaveDraft(); BMessage settings; settings.AddRect("frame",Frame());
    BFile file((fSettings + "/window").c_str(),B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE); settings.Flatten(&file);
    be_app->PostMessage(B_QUIT_REQUESTED); return true;
}
void MainWindow::MessageReceived(BMessage* message) try {
    if (!fReady && (message->what == 'prof' || message->what == 'mail' || message->what == 'link')) {
        ShowError(fLastError.empty() ? "Chatmail is not connected yet. If startup failed, restart Parla to reconnect." : fLastError);
        return;
    }
    // FindUInt32 also zeroes its output on failure. Menu and drop messages
    // usually omit these fields and must retain the current selection.
    uint32 account = message->GetUInt32("account", fAccount);
    uint32 chat = message->GetUInt32("chat", fChat);
    uint32 msgid = message->GetUInt32("message", 0);
    auto selected = SelectedMessage();
    uint32 selectedId = selected.value("id",0u);
    switch (message->what) {
    case B_CUT: case B_COPY: case B_PASTE: case B_SELECT_ALL:
        if (CurrentFocus()) CurrentFocus()->MessageReceived(message);
        break;
    case 'boot': Start(); break;
    case kRPC: {
        Json response = Json::parse(Field(*message,"json"),nullptr,false);
        if (!response.is_object()) return;
        if (response.contains("disconnected")) {
            fReady = false; SetComposeEnabled(false); ShowError(String(response,"disconnected")); return;
        }
        if (response.contains("result")) fLastReply = system_time();
        uint64_t id = response.value("id",uint64_t(0));
        auto i = fCallbacks.find(id);
        if (i != fCallbacks.end()) {
            auto callback = std::move(i->second); fCallbacks.erase(i);
            try { callback(response); } catch (const std::exception& e) { ShowError(e.what()); }
        }
        break;
    }
    case 'tick': {
        auto output = fRpc.TakeDiagnostics();
        if (!output.empty()) Log("Server stderr: " + output);
        if (fLogChanged) {
            // Keep a bounded report after closing Parla; retain the previous run.
            std::string path = fSettings + "/diagnostics.log";
            BFile log(path.c_str(),B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
            chmod(path.c_str(),0600);
            std::string report = fStartup + "\nLast error: " + fLastError + "\n\n" + fLog;
            log.Write(report.data(),report.size());
            if (fDiagnosticsWindow.IsValid()) Diagnostics();
            fLogChanged = false;
        }
        if (++fConnectionTicks >= 20) { fConnectionTicks = 0; Connection(fAccount); }
        MarkVisible();
        if (fEventRetryTicks > 0 && --fEventRetryTicks == 0) Events();
        break;
    }
    case 'dtls': if (message->GetInt32("which",0) == 1) Diagnostics(); break;
    case 'diag': Diagnostics(); Connection(fAccount); break;
    case 'rmcf': RemoveProfile(account); break;
    case 'rmac': if (message->GetInt32("which",0) == 1) DeleteProfile(account); break;
    case 'adir': {
        entry_ref ref;
        if (get_ref_for_path(fAccountsPath.c_str(), &ref) == B_OK) be_roster->Launch(&ref);
        break;
    }
    case 'err!': (new BAlert("Last error",fLastError.empty() ? "No errors reported" : fLastError.c_str(),"OK"))->Go(nullptr); break;
    case 'acct': SelectAccount(account); break;
    case 'srch': RefreshChats(); break;
    case 'chat': {
        auto* item = dynamic_cast<ChatItem*>(fChats->ItemAt(fChats->CurrentSelection()));
        if (item) SelectChat(item->id);
        break;
    }
    case 'rfrs': RefreshChats(); RefreshMessages(); Call("maybe_network",Json::array()); break;
    case 'oldr': RefreshMessages(true); break;
    case kSend: Send(); break;
    case 'prof':
        if (fReady) Form("New profile","Choose a name. Leave the invitation empty to use Chatmail relays.",
            {{"Display name:",""},{"Invitation (optional):",""}},'crpr',"Create");
        break;
    case 'mail':
        if (fReady) Form("Existing email","Sign in to an existing email account.",
            {{"Display name:",""},{"Email address:",""},{"Password:","",true}},'crml',"Sign in");
        break;
    case 'link':
        if (fReady) Form("Link another device","Paste the setup code from Delta Chat's Add second device screen.",
            {{"Display name:",""},{"Setup code:",""}},'crlk',"Link");
        break;
    case 'crpr': case 'crml': case 'crlk': CreateProfile(*message); break;
    case 'sett': ProfileSettings(account); break;
    case 'svst':
        if (account) Call("set_config",Json::array({account,"displayname",Field(*message,"field0")}),[this](const Json& result) {
            if (result.is_null()) { LoadAccounts(); fStatus->SetText("Profile name saved"); }
        }); break;
    case 'rprf': case 'rmai': case 'rlnk': {
        uint32 what = message->what;
        std::string name = Field(*message,"field0");
        if (what == 'rprf') Form("Finish Chatmail setup","Leave invitation empty to select Chatmail relays.",
            {{"Display name:",name},{"Invitation (optional):",""}},'crpr',"Set up",account);
        else if (what == 'rmai') Form("Finish email setup","Enter your existing email credentials.",
            {{"Display name:",name},{"Email address:",""},{"Password:","",true}},'crml',"Sign in",account);
        else Form("Finish device transfer","Get a fresh Add second device code from your other device.",
            {{"Display name:",name},{"Setup code:",""}},'crlk',"Link",account);
        break;
    }
    case 'newc':
        if (fAccount) Form("New chat","Enter an email address; use Join invitation for an encrypted contact invite.",
            {{"Name:",""},{"Email address:",""}},'mkch',"Create chat");
        break;
    case 'mkch': {
        std::string name = Field(*message,"field0"), addr = Field(*message,"field1");
        if (addr.empty()) { ShowError("Enter an email address"); break; }
        Call("create_contact",Json::array({account,addr,name}),[this,account](const Json& id) {
            if (!id.is_number()) return;
            Call("create_chat_by_contact_id",Json::array({account,id}),[this,account](const Json& chat) {
                if (account == fAccount && chat.is_number()) { SelectChat(chat.get<uint32>()); RefreshChats(); }
            });
        }); break;
    }
    case 'newg': if (fAccount) Form("New group","Create a group, then share its invitation or add members.",{{"Group name:",""}},'mkgr',"Create"); break;
    case 'mkgr':
        Call("create_group_chat",Json::array({account,Field(*message,"field0"),false}),[this,account](const Json& id) {
            if (account == fAccount && id.is_number()) { SelectChat(id.get<uint32>()); RefreshChats(); }
        }); break;
    case 'join': if (fAccount) Form("Join invitation","Paste a Delta Chat contact or group invitation.",{{"Invitation:",""}},'joqr',"Join"); break;
    case 'joqr':
        Call("secure_join",Json::array({account,Field(*message,"field0")}),[this,account](const Json& id) {
            if (account == fAccount && id.is_number()) { SelectChat(id.get<uint32>()); RefreshChats(); }
        },120); break;
    case 'invt': case 'ginv':
        if (fAccount) Call("get_chat_securejoin_qr_code",Json::array({fAccount,message->what == 'ginv' ? Json(fChat) : Json()}),
            [this](const Json& qr) { if (qr.is_string()) { Copy(qr.get<std::string>()); fStatus->SetText("Invitation copied to clipboard"); } });
        break;
    case 'memb': if (fChat) Form("Add group member","Choose an existing contact by email address.",{{"Email address:",""}},'admb',"Add"); break;
    case 'admb': {
        std::string address = Field(*message,"field0");
        Call("lookup_contact_id_by_addr",Json::array({account,address}),[this,account,chat](const Json& contact) {
            if (!contact.is_number() || contact == 0) { ShowError("Contact not found. Exchange invitations first."); return; }
            Call("add_contact_to_chat",Json::array({account,chat,contact}),[this](const Json&){ RefreshMessages(); });
        }); break;
    }
    case 'sarc': fArchived = !fArchived; RefreshChats(); break;
    case 'arch':
        if (fChat) Call("set_chat_visibility",Json::array({fAccount,fChat,fArchived ? "Normal" : "Archived"}),[this](const Json&){ RefreshChats(); });
        break;
    case 'acpt':
        if (fChat) Call("accept_chat",Json::array({fAccount,fChat}),[this](const Json&){ RefreshChats(); RefreshMessages(); });
        break;
    case 'atch': {
        if (!fCanSend || fSending) break;
        BMessage result(B_REFS_RECEIVED); result.AddUInt32("account",fAccount); result.AddUInt32("chat",fChat);
        BMessenger target(this);
        fPanel = std::make_unique<BFilePanel>(B_OPEN_PANEL,&target,nullptr,B_FILE_NODE,false,&result); fPanel->Show(); break;
    }
    case B_SIMPLE_DATA: case B_REFS_RECEIVED: {
        if (!fCanSend || fSending || account != fAccount || chat != fChat) break;
        entry_ref ref; BPath path;
        if (message->FindRef("refs",&ref) == B_OK && BEntry(&ref,true).GetPath(&path) == B_OK) {
            BEntry entry(path.Path());
            if (!entry.IsFile()) { ShowError("Only regular files can be attached"); break; }
            fAttachment = path.Path(); DraftLabel(fAttachment); SaveDraft();
        }
        break;
    }
    case 'clra': fAttachment.clear(); fQuote = 0; DraftLabel(""); SaveDraft(); break;
    case 'repl': if (selectedId && fCanSend) { fQuote = selectedId; DraftLabel("Replying to: " + String(selected,"text")); fComposer->MakeFocus(); } break;
    case 'copy': if (selectedId) Copy(String(selected,"text")); break;
    case 'down':
        if (selectedId) Call("download_full_message",Json::array({fAccount,selectedId}),[this](const Json& result) {
            if (result.is_null()) { fStatus->SetText("Attachment download queued"); RefreshMessages(); }
        });
        break;
    case 'open': OpenAttachment(); break;
    case 'info':
        if (selectedId) Form("Message details","Message and encryption metadata",{{"Sender:",String(selected.value("sender",Json::object()),"displayName")},
            {"Contact ID:",std::to_string(selected.value("fromId",0))},
            {"State:",std::to_string(selected.value("state",0))},
            {"Encrypted:",selected.value("showPadlock",false) ? "Yes" : "No"},
            {"Error:",String(selected,"error")}},'noop',"Close");
        break;
    case 'edit':
        if (selectedId && selected.value("fromId",0) == 1)
            Form("Edit message","Update your message for everyone.",{{"Text:",String(selected,"text")}},'edok',"Save");
        break;
    case 'edok': Call("send_edit_request",Json::array({account,msgid,Field(*message,"field0")}),[this](const Json&){ RefreshMessages(); }); break;
    case 'reac': if (selectedId) Form("React","Enter an emoji; leave empty to remove your reaction.",{{"Emoji:",""}},'rcok',"React"); break;
    case 'rcok': {
        std::string emoji = Field(*message,"field0"); Json reactions = Json::array(); if (!emoji.empty()) reactions.push_back(emoji);
        Call("send_reaction",Json::array({account,msgid,reactions}),[this](const Json&){ RefreshMessages(); }); break;
    }
    case 'retr':
        if (selectedId && selected.value("fromId",0) == 1 && selected.value("state",0) == 24)
            Call("resend_messages",Json::array({fAccount,Json::array({selectedId})}),[this](const Json&){ RefreshMessages(); });
        break;
    case 'dele':
        if (selectedId && (new BAlert("Delete message","Delete this message from this device?","Cancel","Delete"))->Go() == 1)
            Call("delete_messages",Json::array({fAccount,Json::array({selectedId})}),[this](const Json&){ RefreshMessages(); });
        break;
    case 'save': {
        if (!selectedId) break;
        BMessage result('svfl'); result.AddUInt32("account",fAccount); result.AddUInt32("message",selectedId);
        BMessenger target(this); fPanel = std::make_unique<BFilePanel>(B_SAVE_PANEL,&target,nullptr,B_FILE_NODE,false,&result);
        fPanel->SetSaveText(String(selected,"fileName","attachment").c_str()); fPanel->Show(); break;
    }
    case 'svfl': {
        entry_ref directory; BPath path;
        if (message->FindRef("directory",&directory) != B_OK || BEntry(&directory).GetPath(&path) != B_OK) break;
        path.Append(Field(*message,"name")); Call("save_msg_file",Json::array({account,msgid,path.Path()})); break;
    }
    case 'impt': {
        BMessage result('ibak'); BMessenger target(this);
        fPanel = std::make_unique<BFilePanel>(B_OPEN_PANEL,&target,nullptr,B_FILE_NODE,false,&result); fPanel->Show(); break;
    }
    case 'ibak': {
        entry_ref ref; BPath path;
        if (message->FindRef("refs",&ref) != B_OK || BEntry(&ref).GetPath(&path) != B_OK) break;
        Form("Import backup","Restore into a new profile; existing profiles are retained.",{{"Backup:",path.Path()},{"Passphrase:","",true}},'imbk',"Import"); break;
    }
    case 'imbk': {
        std::string path = Field(*message,"field0"), pass = Field(*message,"field1");
        Call("add_account",Json::array(),[this,path,pass](const Json& id) {
            if (!id.is_number()) return;
            SelectAccount(id.get<uint32>()); LoadAccounts();
            Call("import_backup",Json::array({id,path,pass}),[this,id](const Json& result) {
                if (!result.is_null()) { LoadAccounts(); return; }
                LoadAccounts(); Call("is_configured",Json::array({id}),[this,id](const Json& c) {
                    if (c == true) { SelectAccount(id.get<uint32>()); Call("start_io",Json::array({id})); }
                });
            },300);
        }); break;
    }
    case 'expt': {
        if (!fAccount) break;
        BMessage result('ebak'); result.AddUInt32("account",fAccount); BMessenger target(this);
        fPanel = std::make_unique<BFilePanel>(B_OPEN_PANEL,&target,nullptr,B_DIRECTORY_NODE,false,&result); fPanel->Show(); break;
    }
    case 'ebak': {
        entry_ref ref; BPath path;
        if (message->FindRef("refs",&ref) != B_OK || BEntry(&ref).GetPath(&path) != B_OK) break;
        BMessage result('exbk'); result.AddUInt32("account",account);
        new FormWindow("Export backup","Choose a passphrase to protect your backup.",{{"Folder:",path.Path()},{"Passphrase:","",true}},BMessenger(this),result,"Export"); break;
    }
    case 'exbk': Call("export_backup",Json::array({account,Field(*message,"field0"),Field(*message,"field1")}),{},300); break;
    case 'noop': break;
    default: BWindow::MessageReceived(message);
    }
} catch (const std::exception& error) {
    // Never let input/JSON exceptions unwind through Haiku's window looper.
    ShowError(std::string("Cannot complete this action: ") + error.what());
}
