#include "Widgets.h"
#include <Button.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <ScrollBar.h>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <Clipboard.h>
#include <ScrollView.h>
#include <CheckBox.h>
#include <TranslationUtils.h>

std::string String(const Json& value, const char* key, const std::string& fallback) {
    auto i = value.find(key);
    return i != value.end() && i->is_string() ? i->get<std::string>() : fallback;
}
Composer::Composer() : BTextView("message", B_WILL_DRAW | B_NAVIGABLE) {
    SetWordWrap(true); SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
    SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
    font_height metrics; GetFontHeight(&metrics);
    float height = ceilf(metrics.ascent + metrics.descent + metrics.leading) + 8;
    SetExplicitMinSize(BSize(180,height));
    SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,height));
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
    time_t when = data.value("timestamp", int64_t(0)), now = time(nullptr);
    char date[64] = ""; tm local{}, today{}; localtime_r(&now, &today);
    if (localtime_r(&when, &local)) strftime(date, sizeof date,
        local.tm_year == today.tm_year && local.tm_yday == today.tm_yday ? "%H:%M" : "%d %b · %H:%M", &local);
    fTime = date;
    auto sender = data.value("sender", Json::object());
    fOutgoing = data.value("fromId", 0) == 1;
    fHeading = fOutgoing ? "You" : String(sender, "displayName", String(sender, "address", "Contact"));
    if (fOutgoing) {
        switch (data.value("state", 0)) {
            case 19: fTime += " · draft"; break;
            case 24: fTime += " · failed"; break;
            case 26: fTime += " · sent"; break;
            case 28: fTime += " · read"; break;
            default: fTime += " · queued";
        }
    }
    if (data.value("isEdited", false)) fTime += " · edited";
    if (!data.value("isInfo",false) && !data.value("showPadlock",true)) fTime += " · email";
}
void MessageItem::Update(BView* owner, const BFont* font) {
    BListItem::Update(owner, font);
    font_height metrics; font->GetHeight(&metrics);
    fLineHeight = ceilf(metrics.ascent + metrics.descent + metrics.leading) + 2;
    float width = std::max(40.0f, owner->Bounds().Width() - 70);
    std::string text;
    if (data.contains("quote") && data["quote"].is_object()) text = "> " + String(data["quote"], "text") + "\n";
    std::string body = String(data, "text");
    if (body.size() > 16384) {
        size_t end = 16384;
        while (end && (static_cast<unsigned char>(body[end]) & 0xc0) == 0x80) --end;
        body.resize(end); body += "\n[Long message: use Copy text for the complete contents]";
    }
    text += body;
    if (!String(data, "fileName").empty()) text += "\n[Attachment: " + String(data, "fileName") + "]";
    std::string download = String(data, "downloadState");
    if (download == "Available" || download == "Failure")
        text += "\n[Use Message → Download attachment]";
    else if (download == "InProgress") text += "\n[Downloading attachment…]";
    if (!String(data, "error").empty()) text += "\n" + String(data, "error");
    if (data.contains("reactions") && data["reactions"].is_object()) {
        auto reactions = data["reactions"].value("reactions", Json::array());
        if (reactions.is_array()) for (const auto& reaction : reactions)
            text += "\n" + String(reaction, "emoji") + "  " + std::to_string(reaction.value("count", 0u));
    }
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
    SetHeight((fLines.size() + 2) * fLineHeight + 24);
}
void MessageItem::DrawItem(BView* owner, BRect rect, bool) {
    owner->SetHighUIColor(B_LIST_BACKGROUND_COLOR); owner->FillRect(rect);
    BRect bubble = rect; bubble.InsetBy(6, 4);
    if (fOutgoing) bubble.left += 24; else bubble.right -= 24;
    rgb_color base = ui_color(IsSelected() ? B_LIST_SELECTED_BACKGROUND_COLOR : B_PANEL_BACKGROUND_COLOR);
    // Blend with theme colors to preserve light/dark theme text contrast.
    if (!IsSelected() && fOutgoing) {
        rgb_color accent = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
        base = {uint8((3 * base.red + accent.red) / 4),
            uint8((3 * base.green + accent.green) / 4),
            uint8((3 * base.blue + accent.blue) / 4),255};
    }
    owner->SetHighColor(base);
    owner->FillRoundRect(bubble, 6, 6);
    owner->SetHighUIColor(IsSelected() ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR);
    BFont font; owner->GetFont(&font); BFont small(font); small.SetSize(font.Size() * 0.88f);
    BPoint point(bubble.left + 10, bubble.top + fLineHeight);
    owner->SetFont(&small); owner->DrawString(fHeading.c_str(), point); owner->SetFont(&font);
    for (const auto& line : fLines) { point.y += fLineHeight; owner->DrawString(line.c_str(), point); }
    owner->SetFont(&small);
    owner->DrawString(fTime.c_str(), BPoint(std::max(bubble.left + 10, bubble.right - 10 - small.StringWidth(fTime.c_str())), bubble.bottom - 7));
    owner->SetFont(&font);
}
MessageList::MessageList() : BListView("messages", B_SINGLE_SELECTION_LIST, B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS) {}
void MessageList::FrameResized(float width, float height) {
    BListView::FrameResized(width, height);
    BFont font; GetFont(&font);
    for (int32 i = 0; i < CountItems(); ++i) ItemAt(i)->Update(this, &font);
    if (auto* bar = ScrollBar(B_VERTICAL)) {
        float total = CountItems() ? ItemFrame(CountItems() - 1).bottom + 1 : 0;
        bar->SetRange(0, std::max(0.0f, total - height));
        bar->SetProportion(total > 0 ? std::min(1.0f, height / total) : 1.0f);
    }
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
                {"Download attachment", 'down'}, {"Open attachment", 'open'}, {"Message details…", 'info'}, {"Retry sending", 'retr'},
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
        auto* control = new BTextControl("field", field.label, "", nullptr);
        // HideTyping(true) clears existing text in Haiku's BTextView.
        control->TextView()->HideTyping(field.secret);
        control->SetText(field.value.c_str());
        layout.Add(control); fFields.push_back(control);
    }
    auto* ok = new BButton("ok", action, new BMessage(kFormOK));
    layout.AddGroup(B_HORIZONTAL).AddGlue()
        .Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED))).Add(ok).End();
    // The window needs a layout too; AddChild alone leaves body at zero size.
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0).Add(body);
    SetDefaultButton(ok); CenterOnScreen();
    if (!fFields.empty()) fFields[0]->MakeFocus();
    Show();
}
void FormWindow::MessageReceived(BMessage* message) {
    if (message->what != kFormOK) { BWindow::MessageReceived(message); return; }
    for (size_t i = 0; i < fFields.size(); ++i)
        fResult.AddString(("field" + std::to_string(i)).c_str(), fFields[i]->Text());
    if (fTarget.SendMessage(&fResult) == B_OK) Quit();
}

ChatItem::ChatItem(const Json& data) : id(data.value("id", 0u)),
    fName(String(data,"name")), fPreview(String(data,"summaryText2")),
    fUnread(data.value("freshMessageCounter",0u)) {
    if (data.value("isSelfTalk",false)) fPreview = "Private notes to yourself";
    if (data.value("isDeviceTalk",false)) fPreview = "Notices from Chatmail";
    auto path = String(data,"avatarPath");
    if (!path.empty()) fAvatar.reset(BTranslationUtils::GetBitmap(path.c_str()));
    unsigned color = 0x607d8b; auto hex = String(data,"color");
    if (!hex.empty()) sscanf(hex.c_str(), "#%x", &color);
    fColor = {uint8(color >> 16),uint8(color >> 8),uint8(color),255};
    size_t end = 1;
    while (end < fName.size() && (static_cast<unsigned char>(fName[end]) & 0xc0) == 0x80) ++end;
    fInitial = fName.substr(0,end);
    if (data.value("isContactRequest",false)) fPreview = "Contact request · " + fPreview;
}
void ChatItem::Update(BView* owner, const BFont* font) {
    BListItem::Update(owner,font); SetHeight(std::max(54.0f, font->Size() * 3.5f));
}
void ChatItem::DrawItem(BView* owner, BRect rect, bool) {
    owner->SetHighUIColor(IsSelected() ? B_LIST_SELECTED_BACKGROUND_COLOR : B_LIST_BACKGROUND_COLOR);
    owner->FillRect(rect);
    BRect avatar(rect.left + 7, rect.top + 8, rect.left + 43, rect.top + 44);
    if (fAvatar) {
        owner->SetDrawingMode(B_OP_ALPHA); owner->DrawBitmap(fAvatar.get(),fAvatar->Bounds(),avatar);
        owner->SetDrawingMode(B_OP_COPY);
    } else {
        owner->SetHighColor(fColor); owner->FillEllipse(avatar);
        owner->SetHighColor(255,255,255); owner->DrawString(fInitial.c_str(),BPoint(avatar.left + 11,avatar.top + 24));
    }
    owner->SetHighUIColor(IsSelected() ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR);
    BFont font; owner->GetFont(&font); BString name(fName.c_str()), preview(fPreview.c_str());
    if (fUnread) name << " (" << fUnread << ")";
    font.TruncateString(&name,B_TRUNCATE_END,std::max(20.0f,rect.Width() - 62));
    owner->DrawString(name.String(),BPoint(rect.left + 53,rect.top + 22));
    BFont small(font); small.SetSize(font.Size() * 0.88f); owner->SetFont(&small);
    small.TruncateString(&preview,B_TRUNCATE_END,std::max(20.0f,rect.Width() - 62));
    owner->DrawString(preview.String(),BPoint(rect.left + 53,rect.top + 42)); owner->SetFont(&font);
}
DiagnosticsWindow::DiagnosticsWindow(const std::string& summary, const std::string& text)
    : BWindow(BRect(30,40,820,580),"Parla diagnostics",B_TITLED_WINDOW,B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE) {
    fSummary = new BTextView("diagnosticSummary", B_WILL_DRAW | B_NAVIGABLE);
    fSummary->MakeEditable(false); fSummary->SetWordWrap(true); fSummary->SetText(summary.c_str());
    fSummary->SetExplicitMinSize(BSize(570,165)); fSummary->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,200));
    fText = new BTextView("diagnostics", B_WILL_DRAW | B_NAVIGABLE);
    fText->MakeEditable(false); fText->SetWordWrap(true); fText->SetText(text.c_str());
    fText->SetExplicitMinSize(BSize(570,220));
    auto* scroll = new BScrollView("logs",fText,0,false,true);
    BLayoutBuilder::Group<>(this,B_VERTICAL).SetInsets(B_USE_WINDOW_SPACING)
        .Add(new BScrollView("summary",fSummary,0,false,true))
        .Add(new BStringView("help","RPC results, core events and server stderr · newest entries below"))
        .Add(scroll,1).AddGroup(B_HORIZONTAL)
            .Add(new BCheckBox("pause","Pause updates",new BMessage('paus')))
            .AddGlue().Add(new BButton("copy","Copy all",new BMessage('copy')))
            .Add(new BButton("close","Close",new BMessage(B_QUIT_REQUESTED))).End();
    Show(); PostMessage('tail');
}
void DiagnosticsWindow::MessageReceived(BMessage* message) {
    if (message->what == 'tail') fText->ScrollToOffset(fText->TextLength());
    else if (message->what == 'logs') {
        const char* text;
        if (!fPaused && message->FindString("text",&text) == B_OK) {
            fText->SetText(text); fText->ScrollToOffset(fText->TextLength());
            if (message->FindString("summary",&text) == B_OK) fSummary->SetText(text);
        }
    } else if (message->what == 'paus') fPaused = !fPaused;
    else if (message->what == 'copy') {
        if (be_clipboard->Lock()) {
            std::string report = std::string(fSummary->Text()) + "\n\n" + fText->Text();
            be_clipboard->Clear(); be_clipboard->Data()->AddData("text/plain",B_MIME_TYPE,report.data(),report.size());
            be_clipboard->Commit(); be_clipboard->Unlock();
        }
    } else BWindow::MessageReceived(message);
}
ProfileWindow::ProfileWindow(BMessenger target, uint32 account, const std::string& name,
    const std::string& details, bool configured)
    : BWindow(BRect(0,0,620,360),"Profile settings",B_TITLED_WINDOW,B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
      fTarget(target), fAccount(account) {
    fName = new BTextControl("name","Display name:",name.c_str(),nullptr);
    auto* info = new BTextView("profileDetails",B_WILL_DRAW | B_NAVIGABLE);
    info->MakeEditable(false); info->SetWordWrap(true); info->SetText(details.c_str());
    info->SetExplicitMinSize(BSize(350,130));
    auto layout = BLayoutBuilder::Group<>(this,B_VERTICAL).SetInsets(B_USE_WINDOW_SPACING);
    layout.Add(fName).Add(new BScrollView("info",info,0,false,true),1);
    if (!configured) layout.AddGroup(B_HORIZONTAL)
        .Add(new BButton("chatmail","Set up Chatmail…",new BMessage('rprf')))
        .Add(new BButton("email","Use email…",new BMessage('rmai')))
        .Add(new BButton("link","Link device…",new BMessage('rlnk'))).End();
    layout.AddGroup(B_HORIZONTAL)
        .Add(new BButton("delete","Delete profile…",new BMessage('rmcf')))
        .Add(new BButton("diagnostics","Diagnostics…",new BMessage('diag'))).AddGlue()
        .Add(new BButton("cancel","Close",new BMessage(B_QUIT_REQUESTED)))
        .Add(new BButton("ok","Save name",new BMessage('svst'))).End();
    CenterOnScreen(); Show();
}
void ProfileWindow::MessageReceived(BMessage* message) {
    switch (message->what) {
    case 'svst': case 'rprf': case 'rmai': case 'rlnk': case 'rmcf': case 'diag': {
        BMessage result(message->what); result.AddUInt32("account",fAccount); result.AddString("field0",fName->Text());
        if (fTarget.SendMessage(&result) == B_OK && message->what != 'diag') Quit();
        break;
    }
    default: BWindow::MessageReceived(message);
    }
}
