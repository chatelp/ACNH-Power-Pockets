// ============================================================================
// ACNH Power Pockets - network side of the updates (libcurl + libnx). Used only
// when the user asks for it in System > About; it knows nothing of the save.
//
// The console's own TLS service checks the certificates (devkitPro's libcurl is
// built on it): the check is always on, and only HTTPS addresses are accepted.
// ============================================================================
#pragma once

#include <switch.h>
#include <string>

#include "pp_ui.h"
#include "pp_update.h"

namespace pp {

class CurlClient : public NetClient {
public:
    NetError get(const std::string& url, size_t maxBytes,
                 const std::function<bool(const uint8_t*, size_t)>& sink,
                 const std::function<bool()>& cancelled) override;
};

// Runs UpdateJob on a worker thread; the UI polls it once per frame.
class UpdateService : public Updater {
public:
    UpdateService(const std::string& sdDir, const std::string& version, bool rightPlace)
        : paths_(sdDir), job_(net_, fs_, sdDir, version), rightPlace_(rightPlace) {}
    ~UpdateService() override { stop(); }
    bool online() override;
    bool canInstall() override { return rightPlace_; }
    void check() override { start(UPD_CHECKING); }
    void download() override { start(UPD_DOWNLOADING); }
    void cancel() override { job_.cancel(); }
    void discard() override { stop(); discardDownload(fs_, paths_); }
    UpdateState state() override { return job_.state(); }
    int percent() override { return job_.percent(); }
    UpdateError error() override { return job_.error(); }
    const ReleaseInfo& release() override { return job_.release(); }
    const std::vector<std::string>& files() const { return job_.files(); }
    void stop();                                         // cancels and waits for the worker
private:
    void start(UpdateState s);
    static void entry(void* self);
    CurlClient net_;
    UpdateFs fs_;
    UpdatePaths paths_;
    UpdateJob job_;
    Thread thread_;
    bool running_ = false;
    UpdateState what_ = UPD_IDLE;
    bool rightPlace_;
};

} // namespace pp
