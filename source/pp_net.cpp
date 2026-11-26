// ACNH Power Pockets - network side of the updates, see pp_net.h.
#include "pp_net.h"

#include <curl/curl.h>

#include <arpa/inet.h>
#include <chrono>
#include <netinet/in.h>
#include <cstdio>
#include <cstring>

namespace pp {

namespace {
// The console's socket service is opened once and shared: libnx refuses a second
// socketInitialize() while the first one is still open (LibnxError_AlreadyInitialized).
// The updater and the Receive screen each take a reference.
int g_socketUsers = 0;
Result socketsUp() {
    if (g_socketUsers == 0) {
        const Result rc = socketInitializeDefault();
        if (R_FAILED(rc)) return rc;
    }
    g_socketUsers++;
    return 0;
}
void socketsDown() {
    if (g_socketUsers > 0 && --g_socketUsers == 0) socketExit();
}

struct Transfer {
    const std::function<bool(const uint8_t*, size_t)>* sink;
    const std::function<bool()>* cancelled;
    size_t got, max;
    bool tooBig, refused;
};
size_t onData(char* d, size_t sz, size_t n, void* u) {
    Transfer* t = (Transfer*)u;
    const size_t len = sz * n;
    if (len > t->max - t->got) { t->tooBig = true; return 0; }
    t->got += len;
    if (!(*t->sink)((const uint8_t*)d, len)) { t->refused = true; return 0; }
    return len;
}
int onProgress(void* u, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return (*((Transfer*)u)->cancelled)() ? 1 : 0; }
} // namespace

NetError CurlClient::get(const std::string& url, size_t maxBytes, const std::function<bool(const uint8_t*, size_t)>& sink,
                         const std::function<bool()>& cancelled) {
    CURL* c = curl_easy_init();
    if (!c) return NET_OTHER;
    Transfer t = { &sink, &cancelled, 0, maxBytes, false, false };
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, "ACNH-Power-Pockets");        // GitHub requires one; nothing else is sent
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);                     // release files are served from another host
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 200L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &t);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    const CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(c);
    if (rc == CURLE_OK) return NET_OK;
    if (t.tooBig) return NET_TOO_BIG;
    if (t.refused) return NET_OTHER;
    switch (rc) {
        case CURLE_ABORTED_BY_CALLBACK: return NET_CANCELLED;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CACERT_BADFILE: return NET_TLS;
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT: return NET_OTHER;
        case CURLE_HTTP_RETURNED_ERROR: return http == 403 || http == 429 ? NET_LIMIT : NET_HTTP;
        default: return NET_OTHER;
    }
}

bool ReceiveService::open(std::string& address, int& code, bool& offline) {
    close();
    offline = false;
    error_[0] = 0;
    u32 ip = 0;
    bool connected = false;
    if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) {
        // A local network is enough: an address on it, internet or not.
        connected = R_SUCCEEDED(nifmGetCurrentIpAddress(&ip)) && ip != 0;
        nifmExit();
    }
    if (!connected) { offline = true; return false; }
    const Result rc = socketsUp();
    if (R_FAILED(rc)) { snprintf(error_, sizeof error_, "sockets 0x%X", (unsigned)rc); return false; }
    sockets_ = true;
    u32 r = 0;
    randomGet(&r, sizeof r);
    code = (int)(r % 10000);
    for (uint16_t port = 8080; port < 8090; port++) {
        if (!server_.start(port, code)) continue;
        struct in_addr a;
        a.s_addr = ip;                                   // already in network order
        address = std::string("http://") + inet_ntoa(a) + ":" + std::to_string(port);
        return true;
    }
    snprintf(error_, sizeof error_, "ports 8080-8089: %s", strerror(server_.lastErrno()));
    close();
    return false;
}

void ReceiveService::close() {
    server_.stop();
    if (sockets_) socketsDown();
    sockets_ = false;
}

void ReceiveService::poll(const std::function<ReceiveReply(const std::string&)>& onText) {
    const uint32_t ms = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    server_.poll(ms, onText);
}

bool UpdateService::online() {
    if (R_FAILED(nifmInitialize(NifmServiceType_User))) return false;
    NifmInternetConnectionType type;
    u32 strength = 0;
    NifmInternetConnectionStatus st = NifmInternetConnectionStatus_ConnectingUnknown1;
    const bool ok = R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &st)) && st == NifmInternetConnectionStatus_Connected;
    nifmExit();
    return ok;
}

void UpdateService::entry(void* self) {
    UpdateService* s = (UpdateService*)self;
    if (s->what_ == UPD_CHECKING) s->job_.runCheck(); else s->job_.runDownload();
}

void UpdateService::start(UpdateState s) {
    stop();
    job_.begin(s);
    what_ = s;
    // Sockets only live as long as a job. If they or the thread can't be had, the job fails cleanly.
    if (R_FAILED(socketsUp())) { job_.abort(); return; }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (R_SUCCEEDED(threadCreate(&thread_, entry, this, nullptr, 0x40000, 0x2C, -2))) {
        if (R_SUCCEEDED(threadStart(&thread_))) { running_ = true; return; }
        threadClose(&thread_);
    }
    curl_global_cleanup();
    socketsDown();
    job_.abort();
}

void UpdateService::stop() {
    if (!running_) return;
    job_.cancel();
    threadWaitForExit(&thread_);
    threadClose(&thread_);
    running_ = false;
    curl_global_cleanup();
    socketsDown();
}

} // namespace pp
