#include "WebPrefs.h"

void WebPrefs::begin(int port, void *_prefs, 
    const std::vector<input_field> &_fields, 
    std::function<void()> cbDone, 
    std::function<void(int params)> cbSave, 
    std::function<void()> cbBeforeResponse) {
    
    is_auth = false;
    is_running = false;
    prefs = _prefs;
    fields = _fields;
    done = cbDone;
    save = cbSave;
    beforeResponse = cbBeforeResponse;
    server = new AsyncWebServer(port);
}

void WebPrefs::end() {
    stop();
    server->end();
    delete server;
    server = nullptr;   // avoid use-after-free if start() is called again after end()
}

void WebPrefs::start() {
    if (server != nullptr && !is_running) {
        server->on("/postForm", HTTP_POST, std::bind(&WebPrefs::onDataReceive, this, std::placeholders::_1));
        server->on("/update",HTTP_POST, [](AsyncWebServerRequest *request) {             
            request->send(200, "text/plain", (Update.hasError()) ? "Update Failed!" : "Update Successful!");
            LOG_SDEBUG("[WebPrefs] Upload Finished");        
        }, std::bind(&WebPrefs::onDataUpload, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
        server->on("/getJson", HTTP_GET|HTTP_POST, std::bind(&WebPrefs::onDataRequest, this, std::placeholders::_1));
        server->on("/done", HTTP_GET, std::bind(&WebPrefs::onDone, this, std::placeholders::_1));
        server->on("/", HTTP_GET, std::bind(&WebPrefs::onIndex, this, std::placeholders::_1));
        server->onNotFound(std::bind(&WebPrefs::notFound, this, std::placeholders::_1));
        server->begin();
        is_running = true;
    }
}

void WebPrefs::onDataUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    LOG_SDEBUG("[WebPrefs] Filemane %s Index %d size %d done %d",filename.c_str(), index, len, (int)final);
    
    static bool updateError = false;

    if (index == 0) {
        LOG_SDEBUG("[WebPrefs] Upload started: %s", filename.c_str());
        updateError = false;

        int updateCommand = U_FLASH;
        if (request->hasParam("target", true) && request->getParam("target", true)->value() == "fs") {
            updateCommand = U_SPIFFS;
            LOG_SDEBUG("[WebPrefs] Upload target: LittleFS");
        }

        // Begin OTA-Update
#if defined(ESP32)
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, updateCommand)) {
            LOG_SERROR(Update.errorString());
#elif defined(ESP8266)
        if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000, updateCommand)) {  // ESP8266: Calc free space
            LOG_SERROR(Update.getErrorString().c_str());
#endif
            updateError = true;
        }
    }

    if (updateError) {
        return;
    }

    // Write date into flash memory
    if (Update.write(data, len) != len) {
        LOG_SERROR("[WebPrefs] Error writing data!");
        updateError = true;
    }

    // Upload done
    if (final) {
        if (Update.end(true)) {  // Finish OTA-Update
            LOG_SDEBUG("[WebPrefs] Update successful! Rebooting...");
            request->send(200, "text/plain", "Update successful. Rebooting...");
            delay(1000);
        } else {
            #if defined(ESP32)
                LOG_SERROR(Update.errorString());
            #elif defined(ESP8266)
                LOG_SERROR(Update.getErrorString().c_str());            
            #endif
            request->send(500, "text/plain", "Update failed.");
        }
    }    
}

void WebPrefs::setBusyCheck(std::function<bool()> fn) {
    busyCheck = fn;
}

static void sendBusy(AsyncWebServerRequest *request) {
    request->send(503, "text/html",
        "<!DOCTYPE html><html><head><title>RetroClock</title>"
        "<meta charset='utf-8'>"
        "<meta http-equiv='refresh' content='3'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0, "
        "maximum-scale=1.0, minimum-scale=1.0, user-scalable=no'>"
        "<meta name='apple-mobile-web-app-capable' content='yes'>"
        "<meta name='apple-touch-fullscreen' content='yes'>"
        "<link rel='shortcut icon' href='#'>"
        "</head><body style='margin:0;min-height:100vh;display:flex;align-items:center;"
        "justify-content:center;text-align:center;font-family:Arial,Helvetica,sans-serif;"
        "font-size:16px;color:#808080;background-color:#f2f2f2;'>"
        "Device is busy, please wait...</body></html>");
}

void WebPrefs::stop() {
    if (server != nullptr) {
        server->reset();
    }
    is_running = false;
}

void WebPrefs::setDefaults() {
    for (size_t i = 0; i < fields.size(); i++) {
        setValue(fields[i].value, i);
    }
}

void WebPrefs::debugPrintConfig() {

#if defined(LOG_SERIAL_LEVEL) && (LOG_SERIAL_LEVEL & 8)
    const uint8_t* conf = static_cast<const uint8_t*>(prefs);
    for (size_t i = 0; i < fields.size(); i++) {
        const auto& field = fields[i];
        const void* ptr = conf + field.offset;

        if (field.type == WebPrefs::STRING || field.type == WebPrefs::PASSWORD) {
            LOG_SDEBUG("[WebPrefs] %zu. %s = %s offset: %u", (i + 1), field.name, static_cast<const char*>(ptr), field.offset);
        } else if (field.type == WebPrefs::UINT16) {
            LOG_SDEBUG("[WebPrefs] %zu. %s = %u", (i + 1), field.name, *static_cast<const uint16_t*>(ptr));
        } else if (field.type == WebPrefs::INT16) {
            LOG_SDEBUG("[WebPrefs] %zu. %s = %d", (i + 1), field.name, *static_cast<const int16_t*>(ptr));
        } else if (field.type == WebPrefs::UINT32) {
            LOG_SDEBUG("[WebPrefs] %zu. %s = %u", (i + 1), field.name, *static_cast<const uint32_t*>(ptr));
        } else if (field.type == WebPrefs::INT32) {
            LOG_SDEBUG("[WebPrefs] %zu. %s = %d", (i + 1), field.name, *static_cast<const int32_t*>(ptr));
        } else if (field.type == WebPrefs::FLOAT) {
            LOG_SDEBUG("[WebPrefs] %zu. %s = %f", (i + 1), field.name, *static_cast<const float*>(ptr));
        } else if (field.type == WebPrefs::CHECKBOX) {
            LOG_SDEBUG("[WebPrefs] %zu. %s = %d", (i + 1), field.name, *static_cast<const bool*>(ptr));
        }
    }
#endif
}

void WebPrefs::setAuthentication(const bool auth, const char *_user, const char *_password) {
    if(auth) {
        user = _user;
        password = _password;
    }
    is_auth = auth;
}

void WebPrefs::resetIdleTime() {
    last_activity = millis();
}

unsigned long WebPrefs::getIdleTime() const {
    return millis() - last_activity;
}

// callback function that handles a GET request to the "/getJson" route
// and sends a JSON response containing the values of all the fields in the fields vector.
void WebPrefs::onDataRequest(AsyncWebServerRequest *request) {
    LOG_SDEBUG("[WebPrefs] onDataRequest");
    last_activity = millis();
    String json_data;
    String jvalues;

    if (request->hasParam("fnc")) {
        if (request->getParam("fnc")->value().equalsIgnoreCase("done")) {
            if (done != nullptr) {
                request->send(200);
                return done();
            }
        } else if (request->getParam("fnc")->value().equalsIgnoreCase("form")) {
            if (beforeResponse) beforeResponse();
            json_data = "{$$$}";
            for (size_t t = 0; t < fields.size(); t++) {
                jvalues += getValue(t) + ",";
            }
            jvalues.remove(jvalues.length() - 1);
            json_data.replace("$$$", jvalues);
        }
    }
    request->send(200, "application/json", json_data);
}

// callback function that handles a POST request to the "/postForm" route
// and sets the values of the fields in the fields vector from the request data.
void WebPrefs::onDataReceive(AsyncWebServerRequest *request) {
    LOG_SDEBUG("[WebPrefs] onDataReceive");
    last_activity = millis();
    if (!checkCredentials(request))
        return;
    if (busyCheck && busyCheck()) { sendBusy(request); return; }
    int params = request->params();
    for (int i = 0; i < params; i++) {
        AsyncWebParameter *p = request->getParam(i);        
        setValue(p->value(), p->name());
        //if (p->name() == "web_auth") {
        //    is_auth = (p->value() == "on") ? true : false;
        //}        
        LOG_SDEBUG("[WebPrefs] POST[%s]: %s %d", p->name().c_str(), p->value().c_str(),value_index);
    }
    request->send(200);

    if(params == 1) {
        if(value_index >= 0 && fields[value_index].callback != nullptr)        
            fields[value_index].callback();           
    }

    if (save != nullptr) {            
        save(params);
    }
}

// Serve a gzip-compressed page from PROGMEM, chunk-wise so the RAM footprint
// stays tiny (important on the ESP8266). The raw gzip bytes go out with a
// Content-Encoding: gzip header the browser inflates them, the ESP never
// decompresses. One array + the callback's own index cursor replace the old
// multi-block ChunkState (which existed only to walk the 4 plaintext blocks).
static void sendGzipChunked(AsyncWebServerRequest* request,
                            const uint8_t* gz, size_t gzLen) {
    auto response = request->beginChunkedResponse("text/html",
        [gz, gzLen](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
            size_t remaining = (index < gzLen) ? (gzLen - index) : 0;
            size_t toCopy = (remaining > maxLen) ? maxLen : remaining;
            if (toCopy) memcpy_P(buffer, gz + index, toCopy);
            return toCopy;
        });
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

// callback function that handles a GET request to the "/"
// route and sends the gzip-compressed settings page.
void WebPrefs::onIndex(AsyncWebServerRequest *request) {
    LOG_SDEBUG("[WebPrefs] onIndex");
    last_activity = millis();
    if (!checkCredentials(request))
        return;
    if (busyCheck && busyCheck()) { sendBusy(request); return; }

    sendGzipChunked(request, index_html_gz, index_html_gz_len);
}

void WebPrefs::onDone(AsyncWebServerRequest *request) {
    LOG_SDEBUG("[WebPrefs] onDone");
    if (!checkCredentials(request))
        return;
    sendGzipChunked(request, done_html_gz, done_html_gz_len);
}

// callback function that handles a request when the route
// is not found and sends an HTML response containing the 404 not found HTML.
void WebPrefs::notFound(AsyncWebServerRequest *request) {
    if (!checkCredentials(request))
        return;
    request->send(404, "text/plain", "Not found");
}

// checks if the request has proper credentials (username and password) if authentication is enabled
bool WebPrefs::checkCredentials(AsyncWebServerRequest *request) {
    if (is_auth) {
        if (!request->authenticate(user, password)) {
            request->requestAuthentication();
            return false;
        }
    }
    return true;
}

bool WebPrefs::setValue(const String &value, const String &name) const {
    for (size_t i = 0; i < fields.size(); i++) {
        if (name.equalsIgnoreCase(fields[i].name)) {
            value_index = i;
            setValue(value, i);
            return true;
        }
    }
    value_index = -1;
    return false;
}

void WebPrefs::setValue(const String &value, int index) const {
    const input_field &field = fields[index];
    void *ptr = ((uint8_t *)prefs) + field.offset;

    switch (field.type) {
        case WebPrefs::PASSWORD:
            if (strncmp(value.c_str(), "***", 3) == 0)
                return;
            [[fallthrough]];
        case WebPrefs::STRING: {
            LOG_SDEBUG("[WebPrefs] String: %s Value: %s", field.name, value.c_str());
            char *str = static_cast<char *>(ptr);
            if (strncmp(str, value.c_str(), field.size) != 0) {
                bool len_ok = value.length() >= (uint32_t)field.min && value.length() <= (uint32_t)field.max;
                bool fmt_ok = (field.validate == nullptr) || field.validate(value.c_str());
                if (len_ok && fmt_ok) {
                    snprintf(str, field.size, "%s", value.c_str());
                } else if (!len_ok) {
                    LOG_SERROR("[WebPrefs] String length not allowed, min: %d max: %d", field.min, field.max);
                } else {
                    LOG_SWARNING("[WebPrefs] Value rejected by validator (kept previous): %s", value.c_str());
                }
            }
            break;
        }

        case WebPrefs::UINT16: {
            LOG_SDEBUG("[WebPrefs] UINT16: %s Value: %s", field.name, value.c_str());
            uint16_t val = static_cast<uint16_t>(value.toInt());
            if (val >= (uint16_t)field.min && val <= (uint16_t)field.max) {
                *static_cast<uint16_t *>(ptr) = val;
            } else {
                LOG_SERROR("[WebPrefs] UINT16 not allowed, min: %d max: %d", field.min, field.max);
            }
            break;
        }

        case WebPrefs::INT16: {
            LOG_SDEBUG("[WebPrefs] INT16: %s Value: %s", field.name, value.c_str());
            int16_t val = static_cast<int16_t>(value.toInt());
            if (val >= field.min && val <= field.max) {
                *static_cast<int16_t *>(ptr) = val;
            } else {
                LOG_SERROR("[WebPrefs] INT16 not allowed, min: %d max: %d", field.min, field.max);
            }
            break;
        }

        case WebPrefs::UINT32: {
            LOG_SDEBUG("[WebPrefs] UINT32: %s Value: %s", field.name, value.c_str());
            uint32_t val = strtoul(value.c_str(), nullptr, 10);
            if (val >= (uint32_t)field.min && val <= (uint32_t)field.max) {
                *static_cast<uint32_t *>(ptr) = val;
            } else {
                LOG_SERROR("[WebPrefs] UINT32 not allowed, min: %d max: %d", field.min, field.max);
            }
            break;
        }

        case WebPrefs::INT32: {
            LOG_SDEBUG("[WebPrefs] INT32: %s Value: %s", field.name, value.c_str());
            int32_t val = static_cast<int32_t>(value.toInt());
            if (val >= field.min && val <= field.max) {
                *static_cast<int32_t *>(ptr) = val;
            } else {
                LOG_SERROR("[WebPrefs] INT32 not allowed, min: %d max: %d", field.min, field.max);
            }
            break;
        }

        case WebPrefs::FLOAT: {
            LOG_SDEBUG("[WebPrefs] FLOAT: %s Value: %s", field.name, value.c_str());
            float val = value.toFloat();
            if (val >= static_cast<float>(field.min) && val <= static_cast<float>(field.max)) {
                *static_cast<float *>(ptr) = val;
            } else {
                LOG_SERROR("[WebPrefs] FLOAT not allowed, min: %d max: %d", field.min, field.max);
            }
            break;
        }

        case WebPrefs::CHECKBOX: {
            LOG_SDEBUG("[WebPrefs] Checkbox: %s Value: %s", field.name, value.c_str());
            *static_cast<bool *>(ptr) = value.equalsIgnoreCase("on");
            break;
        }
    }

    // Optionaler Callback
    // if (field.callback) {
    //     field.callback();
    // }
}

String WebPrefs::getValue(int index) const {
    String jvalue;

    if (index >= 0 && index < (int)fields.size()) {
        const input_field &field = fields[index];
        const void *ptr = ((const uint8_t *)prefs) + field.offset;
        String value;

        switch (field.type) {
            case WebPrefs::STRING:
                value = url_encode(static_cast<const char *>(ptr));
                break;

            case WebPrefs::PASSWORD:
                value = "***";
                break;

            case WebPrefs::UINT16:
                value = url_encode(String(*static_cast<const uint16_t *>(ptr)));
                break;

            case WebPrefs::INT16:
                value = url_encode(String(*static_cast<const int16_t *>(ptr)));
                break;

            case WebPrefs::UINT32:
                value = url_encode(String(*static_cast<const uint32_t *>(ptr)));
                break;

            case WebPrefs::INT32:
                value = url_encode(String(*static_cast<const int32_t *>(ptr)));
                break;

            case WebPrefs::FLOAT:
                value = url_encode(String(*static_cast<const float *>(ptr), 4));
                break;

            case WebPrefs::CHECKBOX:
                value = *static_cast<const bool *>(ptr) ? "on" : "off";
                break;
        }

        jvalue = "\"" + String(field.name) + "\":\"" + value + "\"";
    }

    return jvalue;
}

String WebPrefs::url_encode(const String &str) const {
    String estr;
    char c;
    const char *chars = str.c_str();
    char hexval[4];
    int len = strlen(chars);

    for (int i = 0; i < len; i++) {
        c = chars[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            estr += c;
        else {
            snprintf(hexval, 4, "%%%02X", (uint8_t)c);
            estr += hexval;
        }
    }
    return estr;
}

String WebPrefs::url_decode(const String &str, bool decode_plus) const {
    String dstr;
    int i, ii, len = str.length();

    for (i = 0; i < len; i++) {
        if (str[i] != '%') {
            if (str[i] == '+' && decode_plus)
                dstr += ' ';
            else
                dstr += str[i];
        } else {
            sscanf(str.substring(i + 1, i + 3).c_str(), "%x", &ii);
            dstr += static_cast<char>(ii);
            i = i + 2;
        }
    }
    return dstr;
}