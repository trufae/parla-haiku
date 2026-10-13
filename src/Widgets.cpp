#include "Widgets.h"
#include <Button.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <cmath>

std::string String(const Json& value, const char* key, const std::string& fallback) {
    auto i = value.find(key);
    return i != value.end() && i->is_string() ? i->get<std::string>() : fallback;
}
Composer::Composer() : BTextView("message", B_WILL_DRAW | B_NAVIGABLE) {
    SetWordWrap(true); SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
    SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
    SetExplicitMinSize(BSize(180, 65));
    SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 140));
}
void Composer::KeyDown(const char* bytes, int32 count) {
    if (count == 1 && bytes[0] == B_ENTER && !(modifiers() & B_SHIFT_KEY))
        Window()->PostMessage(kSend);
    else BTextView::KeyDown(bytes, count);
}
void Composer::MessageReceived(BMessage* message) {
    if (message->what == B_REFS_RECEIVED || message->WasDropped()) {
        entry_ref ref;
        if (message->FindRef("refs", &ref) == B_OK) { Window()->PostMessage(message); return; }
    }
    BTextView::MessageReceived(message);
}
MessageItem::MessageItem(Json value) : data(std::move(value)) {
    time_t when = data.value("timestamp", int64_t(0));
    char date[64] = ""; tm local{};
    if (localtime_r(&when, &local)) strftime(date, sizeof date, "%x %H:%M", &local);
    auto sender = data.value("sender", Json::object());
    fHeading = data.value("fromId", 0) == 1 ? "You" : String(sender, "displayName", String(sender, "address", "Contact"));
    fHeading += "  ·  "; fHeading += date;
    if (data.value("fromId", 0) == 1) {
        switch (data.value("state", 0)) {
            case 19: fHeading += "  ·  draft"; break;
            case 24: fHeading += "  ·  failed"; break;
            case 26: fHeading += "  ·  delivered"; break;
            case 28: fHeading += "  ·  read"; break;
            default: fHeading += "  ·  queued";
        }
    }
    if (data.value("isEdited", false)) fHeading += "  ·  edited";
    if (!data.value("showPadlock", true)) fHeading += "  ·  unencrypted email";
}
void MessageItem::Update(BView* owner, const BFont* font) {
    BListItem::Update(owner, font);
    font_height metrics; font->GetHeight(&metrics);
    fLineHeight = ceilf(metrics.ascent + metrics.descent + metrics.leading) + 2;
    float width = std::max(40.0f, owner->Bounds().Width() - 28);
    std::string text;
    if (data.contains("quote") && data["quote"].is_object()) text = "> " + String(data["quote"], "text") + "\n";
    text += String(data, "text");
    if (!String(data, "fileName").empty()) text += "\n[Attachment: " + String(data, "fileName") + "]";
    if (!String(data, "error").empty()) text += "\n" + String(data, "error");
    fLines.clear();
    std::istringstream input(text); std::string paragraph;
    while (std::getline(input, paragraph)) {
        if (paragraph.empty()) { fLines.emplace_back(); continue; }
        size_t start = 0;
        while (start < paragraph.size()) {
            size_t end = start, next = start;
            while (next < paragraph.size()) {
                ++next;
                while (next < paragraph.size() && (static_cast<unsigned char>(paragraph[next]) & 0xc0) == 0x80) ++next;
                if (end > start && font->StringWidth(paragraph.data() + start, next - start) > width) break;
                end = next;
            }
            fLines.push_back(paragraph.substr(start, end - start)); start = end;
        }
    }
    if (fLines.empty()) fLines.emplace_back();
    SetHeight((fLines.size() + 1) * fLineHeight + 20);
}
void MessageItem::DrawItem(BView* owner, BRect rect, bool) {
    owner->SetHighUIColor(IsSelected() ? B_LIST_SELECTED_BACKGROUND_COLOR : B_LIST_BACKGROUND_COLOR);
    owner->FillRect(rect);
    owner->SetHighUIColor(IsSelected() ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR);
    BPoint point(rect.left + 12, rect.top + fLineHeight + 2);
    owner->DrawString(fHeading.c_str(), point);
    for (const auto& line : fLines) { point.y += fLineHeight; owner->DrawString(line.c_str(), point); }
}
MessageList::MessageList() : BListView("messages", B_SINGLE_SELECTION_LIST, B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS) {}
void MessageList::FrameResized(float width, float height) {
    BListView::FrameResized(width, height);
    BFont font; GetFont(&font);
    for (int32 i = 0; i < CountItems(); ++i) ItemAt(i)->Update(this, &font);
    Invalidate();
}
void MessageList::MouseDown(BPoint point) {
    uint32 buttons = 0; Window()->CurrentMessage()->FindUInt32("buttons", &buttons);
    if (buttons & B_SECONDARY_MOUSE_BUTTON) {
        int32 index = IndexOf(point);
        if (index < 0) return;
        Select(index);
        BPopUpMenu menu("Message");
        for (auto item : {std::pair<const char*, uint32>{"Reply", 'repl'}, {"Copy text", 'copy'},
                {"Open attachment", 'open'}, {"Message details…", 'info'}, {"Retry sending", 'retr'},
                {"Delete…", 'dele'}})
            menu.AddItem(new BMenuItem(item.first, new BMessage(item.second)));
        BMenuItem* chosen = menu.Go(ConvertToScreen(point));
        if (chosen) Window()->PostMessage(chosen->Message());
    } else BListView::MouseDown(point);
}
FormWindow::FormWindow(const char* title, const char* explanation, std::vector<FormField> fields,
    BMessenger target, BMessage result, const char* action)
    : BWindow(BRect(0,0,470,160), title, B_TITLED_WINDOW,
        B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_CLOSE_ON_ESCAPE),
      fTarget(target), fResult(result) {
    auto* body = new BGroupView(B_VERTICAL);
    auto layout = BLayoutBuilder::Group<>(body, B_VERTICAL, B_USE_DEFAULT_SPACING).SetInsets(B_USE_WINDOW_SPACING);
    layout.Add(new BStringView("explanation", explanation));
    for (const auto& field : fields) {
        auto* control = new BTextControl("field", field.label, field.value.c_str(), nullptr);
        control->TextView()->HideTyping(field.secret);
        layout.Add(control); fFields.push_back(control);
    }
    auto* ok = new BButton("ok", action, new BMessage(kFormOK));
    layout.AddGroup(B_HORIZONTAL).AddGlue()
        .Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED))).Add(ok).End();
    AddChild(body); SetDefaultButton(ok); CenterOnScreen();
    if (!fFields.empty()) fFields[0]->MakeFocus();
    Show();
}
void FormWindow::MessageReceived(BMessage* message) {
    if (message->what != kFormOK) { BWindow::MessageReceived(message); return; }
    for (size_t i = 0; i < fFields.size(); ++i)
        fResult.AddString(("field" + std::to_string(i)).c_str(), fFields[i]->Text());
    if (fTarget.SendMessage(&fResult) == B_OK) Quit();
}
