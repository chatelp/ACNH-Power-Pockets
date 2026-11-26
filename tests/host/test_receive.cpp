// Tests of the "Receive a list" server (source/pp_receive.cpp) over real local sockets.
#include "../../source/pp_receive.h"
#include "../../source/pp_lists.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

using namespace pp;

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("  FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static uint32_t g_now = 0;
static std::string g_got;
static int g_calls = 0;
static ReceiveReply onText(const std::string& t) { g_calls++; g_got = t; return t.find("good") != std::string::npos ? ReceiveReply { true, "Got \"it\"" } : ReceiveReply { false, "Not a list" }; }

// Sends `req` in pieces while the server polls, returns the whole answer ("" if none).
static std::string talk(ReceiveServer& s, const std::string& req, size_t piece = 32768, bool closeEarly = false) {
    int c = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(s.port()); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(c, (sockaddr*)&a, sizeof a) != 0) { close(c); return "<no connect>"; }
    struct timeval tv = { 0, 20000 };
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    const size_t upTo = closeEarly ? req.size() / 2 : req.size();   // gone half-way through the body
    for (size_t i = 0; i < upTo; i += piece) {
        size_t n = std::min(piece, upTo - i);
        if (send(c, req.data() + i, n, 0) != (ssize_t)n) break;
        for (int k = 0; k < 3; k++) { g_now += 16; s.poll(g_now, onText); }
    }
    if (closeEarly) { close(c); for (int k = 0; k < 5; k++) { g_now += 16; s.poll(g_now, onText); } return ""; }
    std::string out;
    char buf[4096];
    for (int k = 0; k < 400; k++) {
        g_now += 16; s.poll(g_now, onText);
        ssize_t n = recv(c, buf, sizeof buf, 0);
        if (n > 0) { out.append(buf, (size_t)n); continue; }
        if (n == 0) break;
    }
    close(c);
    return out;
}
static std::string post(const std::string& code, const std::string& body, const char* path = "/send") {
    return std::string("POST ") + path + " HTTP/1.1\r\nHost: 192.168.1.23:8080\r\nX-Code: " + code + "\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

static void testParser() {
    HttpRequest r = parseHttp("GET / HTTP/1.1\r\nHost: x\r\n\r\n", 100);
    CHECK(r.state == HttpRequest::DONE && r.method == "GET" && r.path == "/");
    CHECK(parseHttp("GET / HTTP/1.1\r\nHost: x\r\n", 100).state == HttpRequest::NEED_MORE);
    r = parseHttp("POST /send HTTP/1.1\r\nx-code:  0042 \r\ncontent-length: 5\r\n\r\nhel", 100);
    CHECK(r.state == HttpRequest::NEED_MORE);
    r = parseHttp("POST /send HTTP/1.1\r\nX-CODE: 0042\r\nContent-Length: 5\r\n\r\nhello", 100);
    CHECK(r.state == HttpRequest::DONE && r.code == "0042" && r.body == "hello");
    CHECK(parseHttp("POST /send HTTP/1.1\r\nContent-Length: 500\r\n\r\n", 100).state == HttpRequest::TOO_BIG);
    CHECK(parseHttp("POST /send HTTP/1.1\r\nContent-Length: -5\r\n\r\n", 100).state == HttpRequest::BAD);
    CHECK(parseHttp("POST /send HTTP/1.1\r\nContent-Length: 99999999999\r\n\r\n", 100).state == HttpRequest::BAD);
    CHECK(parseHttp("POST /send HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", 100).state == HttpRequest::BAD);
    CHECK(parseHttp("garbage\r\n\r\n", 100).state == HttpRequest::BAD);
    CHECK(parseHttp(std::string(9000, 'x'), 100).state == HttpRequest::TOO_BIG);
    srand(3);
    const std::string ok = "POST /send HTTP/1.1\r\nX-Code: 1234\r\nContent-Length: 5\r\n\r\nhello";
    for (int i = 0; i < 50000; i++) { std::string m = ok; m[(size_t)rand() % m.size()] = (char)rand(); parseHttp(m.substr(0, (size_t)rand() % (m.size() + 1)), 100); }
    CHECK(true);
}

static void testServer() {
    ReceiveServer s;
    CHECK(s.start(0, 4821) && s.running() && s.port() > 0);
    std::string page = talk(s, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    CHECK(page.compare(0, 15, "HTTP/1.1 200 OK") == 0 && page.find("<h1>Send a list</h1>") != std::string::npos && page.find("/send") != std::string::npos && page.find("X-Code") != std::string::npos);
    CHECK(page.find("http://") == std::string::npos && page.find("https://cdn") == std::string::npos);   // nothing from outside
    CHECK(talk(s, "GET /etc/passwd HTTP/1.1\r\n\r\n").compare(0, 12, "HTTP/1.1 404") == 0);
    CHECK(talk(s, "GET /author.png HTTP/1.1\r\n\r\n").compare(0, 12, "HTTP/1.1 404") == 0);   // no file provider yet
    s.setFiles([](const std::string& name, std::string& data) { if (name == "font.ttf") return false; data = "PNG:" + name; return true; });
    std::string img = talk(s, "GET /author.png HTTP/1.1\r\n\r\n");
    CHECK(img.compare(0, 15, "HTTP/1.1 200 OK") == 0 && img.find("Content-Type: image/png") != std::string::npos && img.find("PNG:author.png") != std::string::npos);
    CHECK(talk(s, "GET /icon.png HTTP/1.1\r\n\r\n").find("PNG:icon.png") != std::string::npos);
    CHECK(talk(s, "GET /font.ttf HTTP/1.1\r\n\r\n").compare(0, 12, "HTTP/1.1 404") == 0);   // missing on the SD card
    CHECK(talk(s, "GET /items.txt HTTP/1.1\r\n\r\n").compare(0, 12, "HTTP/1.1 404") == 0);  // nothing else is served
    CHECK(talk(s, "GET /../author.png HTTP/1.1\r\n\r\n").compare(0, 12, "HTTP/1.1 404") == 0);
    CHECK(talk(s, "POST /author.png HTTP/1.1\r\nContent-Length: 0\r\n\r\n").compare(0, 12, "HTTP/1.1 404") == 0);
    CHECK(talk(s, "GET /send HTTP/1.1\r\n\r\n").compare(0, 12, "HTTP/1.1 404") == 0);
    CHECK(talk(s, "OPTIONS /send HTTP/1.1\r\nOrigin: https://evil.example\r\n\r\n").find("Access-Control") == std::string::npos);
    std::string r = talk(s, post("4821", "a good list"));
    CHECK(r.compare(0, 15, "HTTP/1.1 200 OK") == 0 && r.find("{\"ok\":true,\"message\":\"Got \\\"it\\\"\"}") != std::string::npos && g_got == "a good list" && s.received() == 1);
    r = talk(s, post("4821", "nothing"));
    CHECK(r.find("\"ok\":false") != std::string::npos && s.received() == 1);
    // Sent in small pieces, over several frames.
    std::string big(LIST_TEXT_MAX - 100, 'x'); big += "good";
    r = talk(s, post("4821", big), 7000);
    CHECK(r.find("\"ok\":true") != std::string::npos && g_got.size() == big.size() && s.received() == 2);
    CHECK(talk(s, post("4821", std::string(LIST_TEXT_MAX + 1, 'y'))).compare(0, 12, "HTTP/1.1 413") == 0);
    // A client that connects and says nothing doesn't block the next one for ever.
    {
        int c = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(s.port()); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK(connect(c, (sockaddr*)&a, sizeof a) == 0);
        for (int k = 0; k < 5; k++) { g_now += 16; s.poll(g_now, onText); }
        g_now += 25000;
        s.poll(g_now, onText);
        close(c);
        CHECK(talk(s, post("4821", "good again")).find("\"ok\":true") != std::string::npos);
    }
    talk(s, post("4821", "good"), 10, true);           // gone half-way: nothing received
    CHECK(s.received() == 3 && s.running());
    // Wrong codes, the fifth closes the server.
    int calls = g_calls;
    const char* bad[] = { "0000", "", "48210", "4822" };
    for (const char* b : bad) { r = talk(s, post(b, "good")); CHECK(r.compare(0, 12, "HTTP/1.1 403") == 0); }
    CHECK(g_calls == calls && s.running() && !s.lockedOut());
    r = talk(s, post("1111", "good"));
    CHECK(r.find("too many times") != std::string::npos && s.lockedOut() && !s.running());
    s.stop();
    CHECK(s.start(0, 7) && talk(s, post("0007", "good")).find("\"ok\":true") != std::string::npos);
    s.stop();
    CHECK(!s.running());
}

int main() {
    signal(SIGPIPE, SIG_IGN);                            // a peer that hangs up must not kill the test
    testParser(); testServer();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
