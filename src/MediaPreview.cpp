#include "MediaPreview.h"
#include <DataIO.h>
#include <Message.h>
#include <File.h>
#include <BitmapStream.h>
#include <TranslatorRoster.h>
#include <TranslatorFormats.h>
#include <View.h>
#include <algorithm>
#include <cmath>

namespace {
// Bound the translated raster before BTranslationUtils allocates a bitmap.
// Oversized/unsupported files remain available through Open / Save attachment.
class RasterBuffer : public BMallocIO {
public:
    ssize_t WriteAt(off_t offset, const void* buffer, size_t size) override {
        if (offset < 0 || offset > kLimit || size > size_t(kLimit - offset)) return B_NO_MEMORY;
        return BMallocIO::WriteAt(offset, buffer, size);
    }
    status_t SetSize(off_t size) override {
        return size < 0 || size > kLimit ? B_NO_MEMORY : BMallocIO::SetSize(size);
    }
private:
    static constexpr off_t kLimit = 32 * 1024 * 1024;
};
std::shared_ptr<BBitmap> Thumbnail(const std::string& path) {
    BFile file(path.c_str(),B_READ_ONLY); off_t size = 0;
    if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK || size > 32 * 1024 * 1024) return {};
    RasterBuffer raster;
    if (BTranslatorRoster::Default()->Translate(&file,nullptr,nullptr,&raster,B_TRANSLATOR_BITMAP) != B_OK) return {};
    TranslatorBitmap header;
    if (raster.ReadAt(0,&header,sizeof header) != sizeof header) return {};
    float right = B_BENDIAN_TO_HOST_FLOAT(header.bounds.right);
    float bottom = B_BENDIAN_TO_HOST_FLOAT(header.bounds.bottom);
    uint32 rowBytes = B_BENDIAN_TO_HOST_INT32(header.rowBytes);
    uint32 dataSize = B_BENDIAN_TO_HOST_INT32(header.dataSize);
    if (B_BENDIAN_TO_HOST_INT32(header.magic) != B_TRANSLATOR_BITMAP
        || header.bounds.left != 0 || header.bounds.top != 0
        || !std::isfinite(right) || !std::isfinite(bottom) || right < 0 || bottom < 0
        || right >= 8192 || bottom >= 8192 || !rowBytes
        || double(rowBytes) * (floorf(bottom)+1) != dataSize
        || dataSize > raster.BufferLength() - sizeof header) return {};
    // Decode the already-translated bitmap directly, without running translators twice.
    BBitmapStream decoded;
    BBitmap* bitmapData = nullptr;
    if (decoded.Write(raster.Buffer(),raster.BufferLength()) != ssize_t(raster.BufferLength())
        || decoded.DetachBitmap(&bitmapData) != B_OK) return {};
    std::unique_ptr<BBitmap> source(bitmapData);
    if (!source || source->InitCheck() != B_OK) return {};
    float width = source->Bounds().Width() + 1, height = source->Bounds().Height() + 1;
    float scale = std::min({1.0f,320.0f / width,240.0f / height});
    auto bitmap = std::make_shared<BBitmap>(BRect(0,0,std::max(1.0f,floorf(width*scale))-1,
        std::max(1.0f,floorf(height*scale))-1),B_BITMAP_ACCEPTS_VIEWS,B_RGBA32);
    if (bitmap->InitCheck() != B_OK || !bitmap->Lock()) return {};
    auto* view = new BView(bitmap->Bounds(),"thumbnail",B_FOLLOW_NONE,0);
    bitmap->AddChild(view);
    view->SetHighColor(0,0,0,0); view->FillRect(view->Bounds());
    view->SetDrawingMode(B_OP_COPY);
    view->DrawBitmap(source.get(),source->Bounds(),view->Bounds()); view->Sync();
    bitmap->RemoveChild(view); delete view; bitmap->Unlock();
    return bitmap;
}
}
MediaPreview::MediaPreview(BMessenger target) : fTarget(target), fThread([this] { Run(); }) {}
MediaPreview::~MediaPreview() {
    { std::lock_guard<std::mutex> lock(fMutex); fStop = true; }
    fWake.notify_one(); fThread.join();
}
void MediaPreview::Clear() {
    std::lock_guard<std::mutex> lock(fMutex);
    ++fGeneration; fQueue.clear(); fPending.clear(); fCache.clear(); fOrder.clear();
}
std::shared_ptr<BBitmap> MediaPreview::Get(const std::string& path) {
    std::lock_guard<std::mutex> lock(fMutex);
    auto i = fCache.find(path);
    if (i != fCache.end()) return i->second;
    if (!path.empty() && fQueue.size() < 32 && fPending.insert(path).second) {
        fQueue.push_back(path); fWake.notify_one();
    }
    return {};
}
void MediaPreview::Run() {
    for (;;) {
        std::unique_lock<std::mutex> lock(fMutex);
        fWake.wait(lock,[this] { return fStop || !fQueue.empty(); });
        if (fStop) return;
        auto path = fQueue.front(); fQueue.pop_front(); auto generation = fGeneration;
        lock.unlock();
        std::shared_ptr<BBitmap> bitmap;
        try { bitmap = Thumbnail(path); } catch (...) { /* Keep the attachment fallback. */ }
        lock.lock();
        if (generation != fGeneration) continue;
        fPending.erase(path);
        // At most 64 x 320 x 240 x 4 bytes, including failed-decode entries.
        while (fOrder.size() >= 64) { fCache.erase(fOrder.front()); fOrder.pop_front(); }
        fCache[path] = std::move(bitmap); fOrder.push_back(path);
        lock.unlock();
        BMessage ready('pvrd');
        fTarget.SendMessage(&ready,static_cast<BHandler*>(nullptr),0);
    }
}
