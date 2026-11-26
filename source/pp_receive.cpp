// ACNH Power Pockets - local "Receive a list" server, see pp_receive.h.
#include "pp_receive.h"
#include "pp_lists.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace pp {

static const size_t HEADER_MAX = 8192;
static const uint32_t CLIENT_TIMEOUT_MS = 20000;
static const int WRONG_CODES_MAX = 5;

// The page is all inline: no outside resource, nothing loaded from the internet.
static const char* const PAGE = R"PAGE(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Power Pockets - send a list</title>
<style>
body{margin:0;background:#7AC143;font:16px/1.45 system-ui,sans-serif;color:#5A4029}
main{max-width:560px;margin:24px auto;background:#F8F5EC;border-radius:10px;overflow:hidden;box-shadow:6px 6px 0 #5C4430}
h1{margin:0;padding:14px 20px;background:#7A5C3E;color:#F8F5EC;font-size:20px}
section{padding:16px 20px}
label{display:block;font-weight:600;margin:12px 0 6px}
input,textarea{box-sizing:border-box;width:100%;font:inherit;padding:10px;border:2px solid #7A5C3E;border-radius:6px;background:#fff;color:#3B2A1A}
#code{font-size:28px;letter-spacing:8px;width:9ch;text-align:center}
textarea{height:90px}
button{margin-top:16px;width:100%;padding:12px;font:inherit;font-weight:700;border:0;border-radius:6px;background:#3F7A2E;color:#F8F5EC}
button:disabled{opacity:.5}
#out{margin-top:14px;padding:10px 12px;border-radius:6px;display:none}
.ok{background:#E3F1E0;color:#256B29}.err{background:#F9E1DD;color:#A32F23}
small{color:#7A5C3E}
</style></head><body><main>
<h1>Send a list to Power Pockets</h1>
<section>
<p>On <b>catalogue.ac</b>: menu &rsaquo; <i>Export</i> gives a file; a list's share button gives a link. Send either one here.</p>
<label for="code">Code shown on the console</label>
<input id="code" inputmode="numeric" maxlength="4" autocomplete="off">
<label for="file">Export file (.json)</label>
<input id="file" type="file" accept=".json,application/json,text/plain">
<label for="link">or a share link</label>
<textarea id="link" placeholder="https://catalogue.ac/?l=..."></textarea>
<button id="send">Send to the console</button>
<div id="out"></div>
<p><small>This page is served by your Switch, on your own network. Nothing goes to the internet, and the console's save is not changed: the list only appears in the Catalog.</small></p>
</section></main>
<script>
const $=id=>document.getElementById(id);
function show(ok,msg){const o=$('out');o.className=ok?'ok':'err';o.textContent=msg;o.style.display='block';}
$('send').onclick=async()=>{
  const code=$('code').value.trim();
  if(!/^\d{4}$/.test(code)){show(false,'Type the 4-digit code shown on the console.');return;}
  let text=$('link').value.trim();
  const f=$('file').files[0];
  if(f){if(f.size>2000000){show(false,'This file is too large.');return;}text=await f.text();}
  if(!text){show(false,'Choose a file or paste a link.');return;}
  $('send').disabled=true;
  try{
    const r=await fetch('/send',{method:'POST',headers:{'X-Code':code,'Content-Type':'text/plain'},body:text});
    const j=await r.json();show(j.ok,j.message);
    if(j.ok){$('file').value='';$('link').value='';}
  }catch(e){show(false,'The console did not answer. Is the Receive screen still open?');}
  $('send').disabled=false;
};
</script></body></html>
)PAGE";

HttpRequest parseHttp(const std::string& raw, size_t maxBody) {
    HttpRequest r;
    const size_t end = raw.find("\r\n\r\n");
    if (end == std::string::npos) { r.state = raw.size() > HEADER_MAX ? HttpRequest::TOO_BIG : HttpRequest::NEED_MORE; return r; }
    if (end > HEADER_MAX) { r.state = HttpRequest::TOO_BIG; return r; }
    const std::string head = raw.substr(0, end);
    size_t eol = head.find("\r\n");
    const std::string first = head.substr(0, eol);
    const size_t s1 = first.find(' '), s2 = first.find(' ', s1 + 1);
    if (s1 == std::string::npos || s2 == std::string::npos || first.compare(s2 + 1, 5, "HTTP/") != 0) { r.state = HttpRequest::BAD; return r; }
    r.method = first.substr(0, s1);
    r.path = first.substr(s1 + 1, s2 - s1 - 1);
    long length = 0;
    bool chunked = false;
    while (eol != std::string::npos) {
        const size_t next = head.find("\r\n", eol + 2);
        const std::string line = head.substr(eol + 2, next == std::string::npos ? std::string::npos : next - eol - 2);
        eol = next;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon), value = line.substr(colon + 1);
        for (char& c : name) c = (char)tolower((unsigned char)c);
        while (!value.empty() && value[0] == ' ') value.erase(0, 1);
        while (!value.empty() && value.back() == ' ') value.pop_back();
        if (name == "content-length") {
            if (value.empty() || value.size() > 9 || value.find_first_not_of("0123456789") != std::string::npos) { r.state = HttpRequest::BAD; return r; }
            length = atol(value.c_str());
        } else if (name == "transfer-encoding") chunked = true;
        else if (name == "x-code") r.code = value;
    }
    if (chunked) { r.state = HttpRequest::BAD; return r; }
    if ((size_t)length > maxBody) { r.state = HttpRequest::TOO_BIG; return r; }
    if (raw.size() - end - 4 < (size_t)length) return r;  // NEED_MORE
    r.body = raw.substr(end + 4, (size_t)length);
    r.state = HttpRequest::DONE;
    return r;
}

bool ReceiveServer::start(uint16_t port, int code) {
    stop();
    lockedOut_ = false; wrong_ = 0; received_ = 0; code_ = code;
    errno_ = 0;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { errno_ = errno; return false; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (sockaddr*)&a, sizeof a) != 0 || listen(s, 4) != 0) { errno_ = errno; close(s); return false; }
    socklen_t len = sizeof a;
    getsockname(s, (sockaddr*)&a, &len);
    port_ = ntohs(a.sin_port);
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    listen_ = s;
    return true;
}

void ReceiveServer::stop() {
    closeClient();
    if (listen_ >= 0) close(listen_);
    listen_ = -1;
}

void ReceiveServer::closeClient() {
    if (client_ >= 0) close(client_);
    client_ = -1;
    in_.clear();
}

void ReceiveServer::respond(int status, const char* type, const std::string& body) {
    const char* reason = status == 200 ? "OK" : status == 403 ? "Forbidden" : status == 404 ? "Not Found" : status == 413 ? "Payload Too Large" : "Bad Request";
    char head[256];
    snprintf(head, sizeof head, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
             status, reason, type, body.size());
    std::string out = head + body;
    // The client socket is blocking for the answer only (a few KB): short, bounded writes.
    fcntl(client_, F_SETFL, fcntl(client_, F_GETFL, 0) & ~O_NONBLOCK);
    struct timeval tv = { 2, 0 };
    setsockopt(client_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    size_t sent = 0;
    while (sent < out.size()) {
        ssize_t n = send(client_, out.data() + sent, out.size() - sent, 0);
        if (n <= 0) break;
        sent += (size_t)n;
    }
    closeClient();
}

static std::string jsonReply(bool ok, const std::string& msg) {
    std::string s = "{\"ok\":" + std::string(ok ? "true" : "false") + ",\"message\":\"";
    for (char c : msg) {
        if (c == '"' || c == '\\') { s += '\\'; s += c; }
        else if ((unsigned char)c < 0x20) s += ' ';
        else s += c;
    }
    return s + "\"}";
}

void ReceiveServer::handle(const std::function<ReceiveReply(const std::string&)>& onText) {
    HttpRequest r = parseHttp(in_, LIST_TEXT_MAX);
    if (r.state == HttpRequest::NEED_MORE) return;
    if (r.state == HttpRequest::TOO_BIG) { respond(413, "application/json", jsonReply(false, "This is too large for a list.")); return; }
    if (r.state == HttpRequest::BAD) { respond(400, "text/plain", "Bad request"); return; }
    if (r.method == "GET" && (r.path == "/" || r.path == "/index.html")) { respond(200, "text/html; charset=utf-8", PAGE); return; }
    if (r.method != "POST" || r.path != "/send") { respond(404, "text/plain", "Not found"); return; }
    char want[8];
    snprintf(want, sizeof want, "%04d", code_);
    if (r.code != want) {
        const bool last = ++wrong_ >= WRONG_CODES_MAX;
        respond(403, "application/json", jsonReply(false, last ? "Wrong code, too many times: the console stopped receiving. Open the Receive screen again."
                                                               : "Wrong code. Type the code shown on the console."));
        if (last) { lockedOut_ = true; stop(); }
        return;
    }
    ReceiveReply rep = onText ? onText(r.body) : ReceiveReply { false, "Not ready." };
    if (rep.ok) received_++;
    respond(200, "application/json", jsonReply(rep.ok, rep.message));
}

void ReceiveServer::poll(uint32_t nowMs, const std::function<ReceiveReply(const std::string&)>& onText) {
    if (listen_ < 0) return;
    if (client_ < 0) {
        int c = accept(listen_, nullptr, nullptr);
        if (c < 0) return;
        fcntl(c, F_SETFL, fcntl(c, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
        int one = 1;                                     // a phone that hangs up must not stop the app (desktop builds)
        setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        client_ = c;
        clientSince_ = nowMs;
        in_.clear();
    }
    char buf[16384];
    for (int k = 0; k < 64; k++) {                       // up to 1 MB per frame, then the next frame
        ssize_t n = recv(client_, buf, sizeof buf, 0);
        if (n > 0) {
            if (in_.size() + (size_t)n > LIST_TEXT_MAX + HEADER_MAX + 16) { respond(413, "application/json", jsonReply(false, "This is too large for a list.")); return; }
            in_.append(buf, (size_t)n);
            continue;
        }
        if (n == 0) { closeClient(); return; }           // the other side gave up
        if (errno != EAGAIN && errno != EWOULDBLOCK) { closeClient(); return; }
        break;
    }
    if (!in_.empty()) handle(onText);
    if (client_ >= 0 && nowMs - clientSince_ > CLIENT_TIMEOUT_MS) closeClient();
}

} // namespace pp
