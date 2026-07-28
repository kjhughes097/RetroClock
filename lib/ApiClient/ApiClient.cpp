#include "ApiClient.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "serlog.h"

// === RTC_FAST response buffer ===
// One shared buffer in RTC Fast RAM: BSS is full (font buffers) and a heap buffer
// would fragment the pool so mbedTLS can't get its two ~16.7 KB SSL blocks
// (-> ALLOC_FAILED). RTC_FAST keeps the heap intact. Only the bgUpdate task issues
// requests, so no concurrency on this single buffer.
static constexpr size_t RESPONSE_BUF_SIZE = 8000;  // ~8 KB, fits all API responses
static RTC_FAST_ATTR char s_responseBuf[RESPONSE_BUF_SIZE];

// Stream adapter that fills s_responseBuf; writeToStream() decodes chunked
// Transfer-Encoding, so only clean body bytes arrive here.
namespace {
class BufWriter : public Stream {
public:
    size_t len = 0;

    size_t write(uint8_t c) override {
        if (len < RESPONSE_BUF_SIZE - 1) { s_responseBuf[len++] = c; return 1; }
        return 0;
    }
    size_t write(const uint8_t* data, size_t n) override {
        size_t avail = (RESPONSE_BUF_SIZE - 1) - len;
        if (n > avail) n = avail;
        memcpy(s_responseBuf + len, data, n);
        len += n;
        return n;
    }
    // Stream read interface not used, required by abstract base.
    int available() override { return 0; }
    int read()      override { return -1; }
    int peek()      override { return -1; }
};
}  // namespace
// ===

ApiClient::ApiClient() = default;

void ApiClient::setTimeout(uint16_t ms) {
    _timeout = ms;
}

void ApiClient::setInsecure(bool insecure) {
    _insecure = insecure;
}

void ApiClient::setHeader(const char* name, const char* value) {
    if (!name || !value) return;
    // Override existing header with same name (case-insensitive).
    for (uint8_t i = 0; i < _headerCount; i++) {
        if (_headers[i].name.equalsIgnoreCase(name)) {
            _headers[i].value = value;
            return;
        }
    }
    if (_headerCount < MAX_HEADERS) {
        _headers[_headerCount].name  = name;
        _headers[_headerCount].value = value;
        _headerCount++;
    } else {
        LOG_SWARNING("[ApiClient] header table full, dropping '%s'", name);
    }
}

void ApiClient::setBearerToken(const char* token) {
    if (!token) return;
    String v = "Bearer ";
    v += token;
    setHeader("Authorization", v.c_str());
}

void ApiClient::setApiKey(const char* headerName, const char* key) {
    setHeader(headerName, key);
}

void ApiClient::clearHeaders() {
    for (uint8_t i = 0; i < _headerCount; i++) {
        _headers[i].name  = "";
        _headers[i].value = "";
    }
    _headerCount = 0;
}

int ApiClient::get(const char* url, JsonDocument& responseDoc) {
    return doRequest("GET", url, nullptr, nullptr, responseDoc);
}

int ApiClient::post(const char* url, const JsonDocument& bodyDoc, JsonDocument& responseDoc) {
    String serialized;
    serializeJson(bodyDoc, serialized);
    return doRequest("POST", url, serialized.c_str(), "application/json", responseDoc);
}

int ApiClient::post(const char* url, const char* body, const char* contentType,
                    JsonDocument& responseDoc) {
    return doRequest("POST", url, body, contentType, responseDoc);
}

int ApiClient::doRequest(const char* method, const char* url,
                         const char* body, const char* contentType,
                         JsonDocument& responseDoc) {
    _lastHttpCode = 0;
    _lastBytes    = 0;
    _lastError    = "";
    responseDoc.clear();

    if (!url) {
        _lastHttpCode = ERR_NETWORK;
        _lastError    = "null url";
        return _lastHttpCode;
    }

    LOG_SDEBUG("[ApiClient] %s %s", method, url);
    LOG_SDEBUG("[ApiClient] pre-SSL heap: free=%u max_block=%u",
               (unsigned)heap_caps_get_free_size         (MALLOC_CAP_8BIT),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    WiFiClientSecure client;
    if (_insecure) client.setInsecure();
    // No setBufferSizes() in Arduino-ESP32 3.x: SSL buffers are fixed at 16 KB in
    // the prebuilt SDK (see the RTC_FAST buffer above for the heap-fragmentation fix).
    // setHandshakeTimeout caps the TLS handshake; http.setTimeout() only covers the
    // data transfer, so without it an unreachable server hangs 30–60 s.
    client.setHandshakeTimeout(30);


    HTTPClient http;
    http.setTimeout(_timeout);

    if (!http.begin(client, url)) {
        _lastHttpCode = ERR_NETWORK;
        _lastError    = "http.begin() failed";
        LOG_SERROR("[ApiClient] %s", _lastError.c_str());
        return _lastHttpCode;
    }

    for (uint8_t i = 0; i < _headerCount; i++) {
        http.addHeader(_headers[i].name, _headers[i].value);
    }
    if (body && contentType) {
        http.addHeader("Content-Type", contentType);
    }

    int httpCode;
    if (strcmp(method, "GET") == 0) {
        httpCode = http.GET();
    } else if (strcmp(method, "POST") == 0) {
        if (body) {
            httpCode = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body)),
                                 strlen(body));
        } else {
            httpCode = http.POST("");
        }
    } else {
        http.end();
        _lastHttpCode = ERR_NETWORK;
        _lastError    = String("unsupported method: ") + method;
        LOG_SERROR("[ApiClient] %s", _lastError.c_str());
        return _lastHttpCode;
    }

    if (httpCode <= 0) {
        _lastError    = HTTPClient::errorToString(httpCode);
        _lastHttpCode = ERR_NETWORK;
        LOG_SERROR("[ApiClient] network error %d: %s", httpCode, _lastError.c_str());
        http.end();
        return _lastHttpCode;
    }

    // Stream the response body into the RTC_FAST buffer (no heap allocation).
    // writeToStream() handles chunked Transfer-Encoding decoding internally.
    BufWriter writer;
    http.writeToStream(&writer);
    s_responseBuf[writer.len] = '\0';
    _lastBytes = writer.len;

    // Free the SSL connection (~33 KB) BEFORE building the JsonDocument so the two
    // never overlap on the heap.
    http.end();

    if (httpCode >= 200 && httpCode < 400) {
        // char* input -> zero-copy: JSON strings reference s_responseBuf directly.
        DeserializationError err = deserializeJson(responseDoc, s_responseBuf, writer.len);
        if (err) {
            _lastHttpCode = ERR_PARSE;
            _lastError    = err.c_str();
            LOG_SERROR("[ApiClient] JSON parse error: %s (buf=%u B)", _lastError.c_str(), (unsigned)_lastBytes);
            return _lastHttpCode;
        }
        _lastHttpCode = httpCode;
        LOG_SDEBUG("[ApiClient] %d ok (%u bytes)", httpCode, (unsigned)_lastBytes);
        return httpCode;
    }

    // 4xx / 5xx
    _lastHttpCode = httpCode;
    _lastError    = String("HTTP ") + httpCode;
    if (_lastBytes > 0 && _lastBytes < 256) {
        _lastError += ' ';
        _lastError += s_responseBuf;
    }
    LOG_SDEBUG("[ApiClient] %d (%u bytes)", httpCode, (unsigned)_lastBytes);
    return httpCode;
}
