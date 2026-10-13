#include "Widgets.h"
#include <Application.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <Button.h>
#include <File.h>
#include <MessageRunner.h>
#include <Screen.h>
#include <TranslatorRoster.h>
#include <TranslatorFormats.h>
#include <cstdio>
#include <memory>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)

static void Fields(BView* view, std::vector<BTextControl*>& fields) {
    if (auto* field = dynamic_cast<BTextControl*>(view)) fields.push_back(field);
    for (int32 i = 0; i < view->CountChildren(); ++i) Fields(view->ChildAt(i), fields);
}
class FormsTest : public BApplication {
public:
    explicit FormsTest(const char* screenshot) : BApplication("application/x-vnd.trufae-parla-forms-test"), fScreenshot(screenshot) {}
    int status = 0;
    void ReadyToRun() override { PostMessage('next'); }
    void MessageReceived(BMessage* message) override {
        try {
            if (message->what == 'next') {
                if (fStage == 4) { PostMessage(B_QUIT_REQUESTED); return; }
                std::vector<FormField> fields = {{"Display name:", "Català 日本語"}, {"Setup code:", "test code"}};
                if (fStage == 1) fields.push_back({"Password:", "secret", true});
                if (fStage == 2) for (int i = 0; i < 3; ++i) fields.push_back({"Details:", "value"});
                fExpected = fields;
                fWindow = new FormWindow("Form regression test", "All fields and action buttons must be visible.",
                    fields, BMessenger(this), BMessage('done'), "Continue");
                fMessenger = BMessenger(fWindow);
                CheckLater();
            } else if (message->what == 'test') {
                if (fCancelled) {
                    CHECK(!fMessenger.IsValid());
                    ++fStage; PostMessage('next'); return;
                }
                CHECK(fWindow->Lock());
                try {
                    CHECK(fWindow->GetLayout() != nullptr);
                    std::vector<BTextControl*> fields;
                    for (int32 i = 0; i < fWindow->CountChildren(); ++i) Fields(fWindow->ChildAt(i), fields);
                    CHECK(fields.size() == fExpected.size());
                    for (auto* field : fields) {
                        CHECK(!field->IsHidden());
                        CHECK(field->Bounds().Width() > 100 && field->Bounds().Height() > 10);
                        CHECK(fWindow->Frame().Contains(field->ConvertToScreen(field->Bounds())));
                    }
                    auto* button = dynamic_cast<BButton*>(fWindow->FindView(fStage == 3 ? "cancel" : "ok"));
                    CHECK(button && button->Bounds().Width() > 20 && !button->IsHidden());
                    CHECK(fWindow->Frame().Contains(button->ConvertToScreen(button->Bounds())));
                    if (fStage == 1 && fScreenshot) {
                        BBitmap* bitmap = nullptr;
                        CHECK(BScreen(fWindow).GetBitmap(&bitmap) == B_OK);
                        BBitmapStream stream(bitmap);
                        BFile file(fScreenshot, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
                        CHECK(BTranslatorRoster::Default()->Translate(&stream, nullptr, nullptr, &file, B_PNG_FORMAT) == B_OK);
                    }
                    CHECK(button->Invoke() == B_OK);
                    fCancelled = fStage == 3;
                } catch (...) { fWindow->Unlock(); throw; }
                fWindow->Unlock();
                if (fCancelled) CheckLater();
            } else if (message->what == 'done') {
                CHECK(!fCancelled);
                for (size_t i = 0; i < fExpected.size(); ++i) {
                    const char* value = nullptr;
                    CHECK(message->FindString(("field" + std::to_string(i)).c_str(), &value) == B_OK);
                    CHECK(fExpected[i].value == value);
                }
                ++fStage; PostMessage('next');
            } else BApplication::MessageReceived(message);
        } catch (const std::exception& error) {
            fprintf(stderr, "forms: %s\n", error.what()); status = 1; PostMessage(B_QUIT_REQUESTED);
        }
    }
private:
    void CheckLater() {
        BMessage check('test');
        fTimer = std::make_unique<BMessageRunner>(BMessenger(this), &check, 250000, 1);
    }
    int fStage = 0;
    bool fCancelled = false;
    const char* fScreenshot;
    FormWindow* fWindow = nullptr;
    BMessenger fMessenger;
    std::vector<FormField> fExpected;
    std::unique_ptr<BMessageRunner> fTimer;
};
int main(int argc, char** argv) {
    FormsTest app(argc > 1 ? argv[1] : nullptr); app.Run();
    if (!app.status) puts("forms: layout, visible fields/buttons, Unicode submission and cancellation OK");
    return app.status;
}
