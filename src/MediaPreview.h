#pragma once
#include <Bitmap.h>
#include <Messenger.h>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

// Only small, disposable thumbnails are cached. Core owns attachment files.
class MediaPreview {
public:
    explicit MediaPreview(BMessenger target);
    ~MediaPreview();
    std::shared_ptr<BBitmap> Get(const std::string& path);
    void Clear();
private:
    void Run();
    BMessenger fTarget;
    std::mutex fMutex;
    std::condition_variable fWake;
    bool fStop = false;
    uint64_t fGeneration = 0;
    std::deque<std::string> fQueue, fOrder;
    std::set<std::string> fPending;
    std::map<std::string,std::shared_ptr<BBitmap>> fCache;
    std::thread fThread;
};
