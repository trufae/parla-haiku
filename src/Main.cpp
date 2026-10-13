#include "MainWindow.h"
#include "Branding.h"
#include <Application.h>
#include <Alert.h>
#include <cstring>
class ParlaApplication : public BApplication {
public:
    ParlaApplication(bool smoke) : BApplication(kAppSignature), fSmoke(smoke) {}
    void ReadyToRun() override { (new MainWindow(fSmoke))->Show(); }
    void AboutRequested() override {
        (new BAlert("About", "Parla for Haiku 0.1.1\n\nNative messaging with Chatmail core.\nCompatible with Delta Chat.\n\nGNU GPL version 3 or later.\nChatmail core: Mozilla Public License 2.0.\nName and artwork are provisional.", "OK"))->Go(nullptr);
    }
private: bool fSmoke;
};
int main(int argc, char** argv) {
    ParlaApplication app(argc > 1 && !strcmp(argv[1], "--smoke-test"));
    app.Run();
}
