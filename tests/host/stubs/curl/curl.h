// Minimal libcurl declarations for the host checks ONLY (tests/host/run.sh, engine_bench.sh).
#pragma once
#include <cstddef>

typedef void CURL;
typedef long long curl_off_t;
typedef enum { CURLE_OK = 0, CURLE_COULDNT_RESOLVE_HOST = 6, CURLE_COULDNT_CONNECT = 7, CURLE_HTTP_RETURNED_ERROR = 22, CURLE_WRITE_ERROR = 23,
               CURLE_OPERATION_TIMEDOUT = 28, CURLE_SSL_CONNECT_ERROR = 35, CURLE_ABORTED_BY_CALLBACK = 42, CURLE_SSL_CERTPROBLEM = 58,
               CURLE_PEER_FAILED_VERIFICATION = 60, CURLE_SSL_CACERT_BADFILE = 77 } CURLcode;
typedef enum { CURLOPT_URL, CURLOPT_USERAGENT, CURLOPT_PROTOCOLS, CURLOPT_REDIR_PROTOCOLS, CURLOPT_SSL_VERIFYPEER, CURLOPT_SSL_VERIFYHOST,
               CURLOPT_FOLLOWLOCATION, CURLOPT_MAXREDIRS, CURLOPT_FAILONERROR, CURLOPT_NOSIGNAL, CURLOPT_CONNECTTIMEOUT, CURLOPT_LOW_SPEED_LIMIT,
               CURLOPT_LOW_SPEED_TIME, CURLOPT_WRITEFUNCTION, CURLOPT_WRITEDATA, CURLOPT_XFERINFOFUNCTION, CURLOPT_XFERINFODATA, CURLOPT_NOPROGRESS } CURLoption;
typedef enum { CURLINFO_RESPONSE_CODE } CURLINFO;
#define CURLPROTO_HTTPS (1 << 1)
#define CURL_GLOBAL_DEFAULT 3

CURLcode curl_global_init(long flags);
void     curl_global_cleanup(void);
CURL*    curl_easy_init(void);
CURLcode curl_easy_setopt(CURL* c, int o, ...);
CURLcode curl_easy_perform(CURL* c);
CURLcode curl_easy_getinfo(CURL* c, int i, ...);
void     curl_easy_cleanup(CURL* c);
