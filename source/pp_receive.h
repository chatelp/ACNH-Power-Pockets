// ============================================================================
// ACNH Power Pockets - "Receive a list": a tiny web server on the local network,
// open only while the Receive screen is shown.
//
// GET  /       a page to drop a catalogue.ac export or paste a share link
// POST /send   the text itself, with the code shown on the console (X-Code header)
//
// One request at a time, Connection: close, bodies up to LIST_TEXT_MAX. The code
// header also stops other web pages from posting here (it forces a CORS preflight,
// which this server never grants). Five wrong codes close the server. It knows
// nothing of the save; what it receives goes to a callback that checks it as a
// list and stores it in the lists folder. POSIX sockets: tested on the host.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace pp {

struct ReceiveReply { bool ok; std::string message; };

class ReceiveServer {
public:
    ~ReceiveServer() { stop(); }
    // Listens on `port` (0: any free port, for tests). False if the port can't be had.
    bool start(uint16_t port, int code);
    void stop();
    bool running() const { return listen_ >= 0; }
    uint16_t port() const { return port_; }
    int  received() const { return received_; }
    bool lockedOut() const { return lockedOut_; }        // too many wrong codes: stopped
    // Call once per frame. Never blocks. `nowMs` is any monotonic clock in milliseconds.
    void poll(uint32_t nowMs, const std::function<ReceiveReply(const std::string&)>& onText);
private:
    void closeClient();
    void respond(int status, const char* type, const std::string& body);
    void handle(const std::function<ReceiveReply(const std::string&)>& onText);
    int listen_ = -1, client_ = -1;
    uint16_t port_ = 0;
    int code_ = 0, wrong_ = 0, received_ = 0;
    bool lockedOut_ = false;
    uint32_t clientSince_ = 0;
    std::string in_;
};

// Exposed for tests: the request parser. NEED_MORE until the headers and the whole body
// (Content-Length) are there; BAD for anything malformed or too large.
struct HttpRequest {
    enum State { NEED_MORE, DONE, BAD, TOO_BIG } state = NEED_MORE;
    std::string method, path, body, code;
};
HttpRequest parseHttp(const std::string& raw, size_t maxBody);

} // namespace pp
