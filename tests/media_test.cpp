#include "MediaPreview.h"
#include <Application.h>
#include <BitmapStream.h>
#include <File.h>
#include <MessageRunner.h>
#include <TranslatorRoster.h>
#include <TranslatorFormats.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

class MediaTest : public BApplication {
public:
    explicit MediaTest(std::string folder) : BApplication("application/x-vnd.trufae-parla-media-test"), fFolder(std::move(folder)) {}
    int status = 1;
    void ReadyToRun() override {
        auto* bitmap = new BBitmap(BRect(0,0,63,31),B_RGBA32);
        if (bitmap->InitCheck() != B_OK) { delete bitmap; Fail(); return; }
        memset(bitmap->Bits(),255,bitmap->BitsLength());
        BBitmapStream stream(bitmap);
        BFile good(Path(0).c_str(),B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
        if (BTranslatorRoster::Default()->Translate(&stream,nullptr,nullptr,&good,B_PNG_FORMAT) != B_OK) { Fail(); return; }
        // A tiny file declaring a huge bitmap must not cause a huge allocation.
        TranslatorBitmap header{};
        header.magic = B_HOST_TO_BENDIAN_INT32(B_TRANSLATOR_BITMAP);
        header.bounds = BRect(0,0,B_HOST_TO_BENDIAN_FLOAT(1e8f),B_HOST_TO_BENDIAN_FLOAT(1e8f));
        header.rowBytes = B_HOST_TO_BENDIAN_INT32(4);
        header.colors = color_space(B_HOST_TO_BENDIAN_INT32(B_RGBA32));
        header.dataSize = B_HOST_TO_BENDIAN_INT32(4);
        BFile bad(Path(1).c_str(),B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
        bad.Write(&header,sizeof header); uint32 pixel = 0; bad.Write(&pixel,sizeof pixel);
        BFile truncated(Path(2).c_str(),B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
        truncated.Write("\x89PNG\r\n\x1a\n",8);
        BFile large(Path(3).c_str(),B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
        large.SetSize(32 * 1024 * 1024 + 1);
        fPreviews = std::make_unique<MediaPreview>(BMessenger(this));
        BMessage timeout('tout'); fTimer = std::make_unique<BMessageRunner>(BMessenger(this),&timeout,10000000,1);
        fPreviews->Get(Path(fStage));
    }
    void MessageReceived(BMessage* message) override {
        if (message->what == 'tout') { Fail(); return; }
        if (message->what != 'pvrd') { BApplication::MessageReceived(message); return; }
        auto bitmap = fPreviews->Get(Path(fStage));
        if ((fStage == 0 && (!bitmap || bitmap->Bounds() != BRect(0,0,63,31)))
            || (fStage != 0 && bitmap)) { Fail(); return; }
        fPreviews->Clear();
        if (++fStage == 4) { status = 0; PostMessage(B_QUIT_REQUESTED); }
        else fPreviews->Get(Path(fStage));
    }
    ~MediaTest() override {
        fPreviews.reset();
        for (int i = 0; i < 4; ++i) unlink(Path(i).c_str());
        rmdir(fFolder.c_str());
    }
private:
    std::string Path(int index) const { return fFolder + "/" + std::to_string(index); }
    void Fail() { fprintf(stderr,"media: fixture %d failed\n",fStage); PostMessage(B_QUIT_REQUESTED); }
    std::string fFolder;
    int fStage = 0;
    std::unique_ptr<MediaPreview> fPreviews;
    std::unique_ptr<BMessageRunner> fTimer;
};
int main() {
    char folder[] = "/tmp/parla-media-XXXXXX";
    if (!mkdtemp(folder)) return 1;
    MediaTest app(folder); app.Run();
    return app.status;
}
