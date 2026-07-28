#ifndef APICLIENT_H
#define APICLIENT_H

#include <Arduino.h>
#include <ArduinoJson.h>

class ApiClient {
public:
    // Negative error codes (positive return = HTTP code, 0 = uninitialized)
    static constexpr int ERR_NETWORK = -1;   // connect/timeout/DNS fail
    static constexpr int ERR_PARSE   = -2;   // response is not valid JSON

    ApiClient();

    // -- Configuration --
    void setTimeout(uint16_t ms);                              // default 5000
    void setInsecure(bool insecure = true);                    // skip TLS cert check (default true)
    void setHeader(const char* name, const char* value);       // adds or overrides
    void setBearerToken(const char* token);                    // "Authorization: Bearer <token>"
    void setApiKey(const char* headerName, const char* key);   // e.g. "X-API-Key", "abc123"
    void clearHeaders();

    // -- Requests --
    // GET: response body is parsed as JSON into responseDoc.
    int get(const char* url, JsonDocument& responseDoc);

    // POST with JSON body (Content-Type: application/json).
    int post(const char* url, const JsonDocument& bodyDoc, JsonDocument& responseDoc);

    // POST with arbitrary body + caller-provided Content-Type.
    int post(const char* url, const char* body, const char* contentType,
             JsonDocument& responseDoc);

    // -- Last-call info --
    int           lastHttpCode() const { return _lastHttpCode; }
    size_t        lastBytes() const    { return _lastBytes; }
    const String& lastError() const    { return _lastError; }

private:
    static constexpr uint8_t MAX_HEADERS = 8;
    struct Header { String name; String value; };

    int doRequest(const char* method, const char* url,
                  const char* body, const char* contentType,
                  JsonDocument& responseDoc);

    uint16_t _timeout      = 5000;
    bool     _insecure     = true;
    Header   _headers[MAX_HEADERS];
    uint8_t  _headerCount  = 0;

    int      _lastHttpCode = 0;
    size_t   _lastBytes    = 0;
    String   _lastError;
};

#endif  // APICLIENT_H
