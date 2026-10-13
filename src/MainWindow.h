#pragma once
#include "RpcTransport.h"
#include "Widgets.h"
#include <Button.h>
#include <FilePanel.h>
#include <MenuField.h>
#include <MessageRunner.h>
#include <StringView.h>
#include <map>
#include <memory>
#include <set>

class MainWindow : public BWindow {
public:
    explicit MainWindow(bool smokeTest = false);
    ~MainWindow() override;
    void MessageReceived(BMessage* message) override;
    bool QuitRequested() override;
private:
    using Callback = std::function<void(const Json&)>;
    void Call(const char* method, Json params, Callback callback = {}, int timeout = 60);
    void Start();
    void LoadAccounts();
    void SelectAccount(uint32 id);
    void RefreshChats();
    void SelectChat(uint32 id);
    void RefreshMessages(bool older = false);
    void Events();
    void Send();
    void SaveDraft();
    void ShowError(const std::string& error);
    void Log(const std::string& text);
    void Diagnostics();
    void ProfileSettings(uint32 account);
    void Connection(uint32 account);
    void RemoveProfile(uint32 account);
    void WriteDrafts();
    void Form(const char* title, const char* help, std::vector<FormField> fields, uint32 what, const char* action = "OK", uint32 retryAccount = 0);
    void CreateProfile(const BMessage& message);
    void OpenAttachment();
    void MarkVisible();
    Json SelectedMessage() const;
    void SetComposeEnabled(bool enabled);
    RpcTransport fRpc;
    std::map<uint64_t, Callback> fCallbacks;
    BListView* fChats;
    MessageList* fMessages;
    Composer* fComposer;
    BTextControl* fSearch;
    BMenu* fAccounts;
    BStringView *fTitle, *fStatus, *fDraftLabel;
    BButton *fSend, *fAttach, *fAccept, *fOlder;
    std::unique_ptr<BFilePanel> fPanel;
    std::unique_ptr<BMessageRunner> fPulse;
    uint32 fAccount = 0, fChat = 0, fQuote = 0;
    uint64_t fGeneration = 0, fChatRequest = 0, fMessageRequest = 0;
    bool fReady = false, fCanSend = false, fSending = false, fRefreshing = false, fRefreshAgain = false;
    bool fSmokeTest = false, fArchived = false;
    bool fEventsPending = false;
    int fEventRetryTicks = 0;
    size_t fLimit = 100;
    std::string fAttachment, fSettings;
    std::string fLastError, fLog, fAccountsPath, fStartup;
    std::set<uint32> fSetupAccounts;
    BMessenger fDiagnosticsWindow;
    bool fLogChanged = false;
    std::vector<std::string> fSecrets;
    BMessenger fErrorAlert;
    std::map<std::pair<uint32,uint32>, Json> fDrafts;
    std::map<std::pair<uint32,uint32>, std::string> fSeen;
};
