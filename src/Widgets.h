#pragma once
#include "RpcTransport.h"
#include <ListView.h>
#include <StringItem.h>
#include <TextView.h>
#include <Window.h>
#include <Messenger.h>
#include <TextControl.h>
#include <vector>

constexpr uint32 kSend = 'send', kFormOK = 'frok', kRPC = 'rpc!';
class Composer : public BTextView {
public:
    Composer();
    void KeyDown(const char* bytes, int32 count) override;
    void MessageReceived(BMessage* message) override;
};
class MessageItem : public BListItem {
public:
    explicit MessageItem(Json data);
    void Update(BView* owner, const BFont* font) override;
    void DrawItem(BView* owner, BRect rect, bool complete = false) override;
    Json data;
private:
    std::vector<std::string> fLines;
    std::string fHeading;
    float fLineHeight = 18;
};
class MessageList : public BListView {
public:
    MessageList();
    void FrameResized(float width, float height) override;
    void MouseDown(BPoint point) override;
};
struct FormField { const char* label; std::string value; bool secret = false; };
class FormWindow : public BWindow {
public:
    FormWindow(const char* title, const char* explanation, std::vector<FormField> fields,
        BMessenger target, BMessage result, const char* action = "OK");
    void MessageReceived(BMessage* message) override;
private:
    BMessenger fTarget;
    BMessage fResult;
    std::vector<BTextControl*> fFields;
};
std::string String(const Json& value, const char* key, const std::string& fallback = "");
