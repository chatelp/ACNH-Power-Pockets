// Fake network for the engine bench: libcurl, nifm, sockets, threads and the "next program
// to start" of hbloader, as source/pp_net.cpp and main.cpp use them. Host test code only.
#include "fake_platform.h"

#include <switch.h>
#include <curl/curl.h>
#include <cstdarg>
#include <cstring>

namespace fake { Net net; }
using fake::net;

struct Easy {
    std::string url, agent;
    long protocols = 0, redirProtocols = 0, peer = -1, host = -1;
    size_t (*write)(char*, size_t, size_t, void*) = nullptr; void* wd = nullptr;
    int (*prog)(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) = nullptr; void* pd = nullptr;
};
static bool g_sockets = false;

CURLcode curl_global_init(long) { return CURLE_OK; }
void curl_global_cleanup(void) {}
CURL* curl_easy_init(void) { return new Easy(); }
void curl_easy_cleanup(CURL* c) { delete (Easy*)c; }
CURLcode curl_easy_setopt(CURL* c, int o, ...) {
    Easy* e = (Easy*)c;
    va_list ap; va_start(ap, o);
    switch (o) {
        case CURLOPT_URL: e->url = va_arg(ap, const char*); break;
        case CURLOPT_USERAGENT: e->agent = va_arg(ap, const char*); break;
        case CURLOPT_PROTOCOLS: e->protocols = va_arg(ap, long); break;
        case CURLOPT_REDIR_PROTOCOLS: e->redirProtocols = va_arg(ap, long); break;
        case CURLOPT_SSL_VERIFYPEER: e->peer = va_arg(ap, long); break;
        case CURLOPT_SSL_VERIFYHOST: e->host = va_arg(ap, long); break;
        case CURLOPT_WRITEFUNCTION: e->write = va_arg(ap, size_t (*)(char*, size_t, size_t, void*)); break;
        case CURLOPT_WRITEDATA: e->wd = va_arg(ap, void*); break;
        case CURLOPT_XFERINFOFUNCTION: e->prog = va_arg(ap, int (*)(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t)); break;
        case CURLOPT_XFERINFODATA: e->pd = va_arg(ap, void*); break;
        default: break;
    }
    va_end(ap);
    return CURLE_OK;
}
CURLcode curl_easy_perform(CURL* c) {
    Easy* e = (Easy*)c;
    net.urls.push_back(e->url);
    // The safety of the connection is not negotiable: any other setting fails the bench.
    if (!g_sockets || e->protocols != CURLPROTO_HTTPS || e->redirProtocols != CURLPROTO_HTTPS || e->peer != 1 || e->host != 2 || e->url.compare(0, 8, "https://") != 0)
        net.unsafe = true;
    if (e->agent != "ACNH-Power-Pockets") net.unsafe = true;
    if (net.curlResult) return (CURLcode)net.curlResult;
    auto it = net.pages.find(e->url);
    if (it == net.pages.end()) { net.http = 404; return CURLE_HTTP_RETURNED_ERROR; }
    std::string& d = it->second;
    for (size_t i = 0; i < d.size(); i += 16384) {
        if (e->prog(e->pd, 0, 0, 0, 0)) return CURLE_ABORTED_BY_CALLBACK;
        size_t n = d.size() - i < 16384 ? d.size() - i : 16384;
        if (e->write(&d[i], 1, n, e->wd) != n) return CURLE_WRITE_ERROR;
    }
    net.http = 200;
    return CURLE_OK;
}
CURLcode curl_easy_getinfo(CURL*, int, ...) {
    return CURLE_OK;                                     // the status code only picks an error message
}

Result nifmInitialize(NifmServiceType) { net.onlineChecks++; return 0; }
void nifmExit(void) {}
Result nifmGetInternetConnectionStatus(NifmInternetConnectionType* t, u32* s, NifmInternetConnectionStatus* st) {
    *t = NifmInternetConnectionType_WiFi; *s = 3;
    *st = net.connected ? NifmInternetConnectionStatus_Connected : NifmInternetConnectionStatus_ConnectingUnknown1;
    return net.connected ? 0 : 1;
}
Result socketInitializeDefault(void) { if (net.socketFail) return 1; g_sockets = true; net.socketInits++; return 0; }
void socketExit(void) { g_sockets = false; net.socketExits++; }

struct FakeThread { ThreadFunc f; void* a; };
Result threadCreate(Thread* t, ThreadFunc f, void* a, void*, size_t, int, int) { if (net.threadFail) return 1; t->handle = new FakeThread { f, a }; return 0; }
Result threadStart(Thread* t) { FakeThread* f = (FakeThread*)t->handle; f->f(f->a); return 0; }   // runs to the end at once
Result threadWaitForExit(Thread*) { return 0; }
Result threadClose(Thread* t) { delete (FakeThread*)t->handle; t->handle = nullptr; return 0; }

bool envHasNextLoad(void) { return true; }
Result envSetNextLoad(const char* path, const char* argv) { net.nextLoad = path; net.nextArgs = argv; return 0; }
