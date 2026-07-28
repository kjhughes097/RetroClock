# Dev Notes (RetroClock)
To build this project, use Visual Studio Code with PlatformIO extension

---

## 1. Project layout (relevant parts)

```
src/
  main.cpp              setup() + loop(); screen/slot definitions, touch gestures
  globals.h / .cpp      APP_VERSION, global instances (dc, tm, sw, wp, buzzer),
                        shared helpers (parseHHMM, ALARM_CUTOFF_MS)
  config/
    webPrefsConfig.h    DeviceConfig struct + input_fields[] (THE config definition)
    webPrefsMacros.h    FIELD_* macros
    settings.html       WebPrefs form (data-up / data-uppost / activation-rules)
    hardware.h          app-side pins (SPEAKER_PIN); display pins live in RetroGfx
  storage.cpp           EEPROM read/write, layout signature, WebPrefs callbacks
  network.cpp           WiFi, NTP, AP rescue, network info screen
  <feature>.cpp         crypto / weather / eggtimer / alarmclock / widgets / screens
lib/
  WebPrefs/             async config server; html/ = source for page.h (auto-generated)
  RetroGfx/             Display, ColorMatrix, PixelFont, Icons (LovyanGFX wrapper)
  MelodyPlayer/ 
  ApiClient/ 
  SimpleWifi/ 
  TimeManager/ 
  SerLog/
data/                   LittleFS content (/ColorMap.bmp, /fonts/*.bmp)
include/
  secrets_template.h    built-in credential defaults (committed)
  secrets.h             your own copy, git-ignored (see below)
minify.py               PRE build: generates lib/WebPrefs/page.h (gzip) from html + settings.html
uploadlfs.py            prod only: builds + flashes LittleFS automatically after upload
partitions_retroclock.csv  partition table (OTA app0/app1 + littlefs)
```

### Credentials (`include/secrets.h`)

(Optional) For testing and debugging purposes only `globals.h` includes `secrets.h` if it exists
and falls back to `secrets_template.h` otherwise, so the project builds out of the box. 
For your own defaults, copy the template to `include/secrets.h` and edit it there; `.gitignore`
keeps that copy out of the repository, the template stays untouched.

These values are only the **built-in defaults**, used when the stored config is
invalid or restore mode wipes it. **They are compiled into the firmware in plain text**,
so a build made with real credentials must not be published or flashed onto anyone
else's board.

---

## 2. Extending the WebPrefs config (adding a new parameter)

A persistent parameter consists of **three** parts that must stay in sync:

### Step 1 - field in the struct (`webPrefsConfig.h`)

Add it inside `struct DeviceConfig` **before** the marker `char __STORAGE_END__;`
(everything after `__STORAGE_END__` is runtime-only and is NOT written to flash):

```c
uint16_t my_new_value;   // e.g. a new color value
```

### Step 2 - entry in `input_fields[]` (same file)

```c
FIELD_UINT16( my_new_value, "10", 0, 31, nullptr ),
//            ^field name    ^def ^min ^max ^callback (or nullptr)
```

The `VAR_NAME()` trick in the macros uses the field name as a string ID - the name
in the `FIELD_*` macro **must** match the struct member and the `id=""` in
`settings.html` exactly.

### Step 3 - input element in `settings.html`

```html
<label>My New Value (0-31)
<input type="text" data-up id="my_new_value"></label>
```

### Available `FIELD_*` macros (`webPrefsMacros.h`)

| Macro | Type | Signature |
|---|---|---|
| `FIELD_STRING` | char[] | `(field, default, min_len, callback)` |
| `FIELD_STRING_SZ` | char[] | `(field, default, min_len, max_len, callback)` |
| `FIELD_STRING_VALID` | char[] | `(field, default, min_len, callback, validator)` |
| `FIELD_PASSWORD` / `_SZ` | char[] | like STRING; field is masked |
| `FIELD_UINT16 / INT16 / UINT32 / INT32` | int | `(field, default, min, max, callback)` |
| `FIELD_FLOAT` | float | `(field, default, min, max, callback)` - min/max are int32-bounded |
| `FIELD_CHECKBOX` | bool | `(field, default, callback)` - range fixed 0..1, default `"on"`/`"off"` |

Size/offset are derived automatically via `sizeof`/`offsetof`, so no manual byte
bookkeeping is needed.

### Important: config layout signature

`storage.cpp::configLayoutSignature()` hashes **type, size, offset, name, min, max**
of every field. If the layout changes (field added/removed/reordered/resized), the
signature stored in flash no longer matches -> on the next boot the defaults are
loaded automatically (no crash, but user settings are lost). A pure value/default
change leaves stored settings intact.

`page.h` is regenerated from `settings.html` on every build - **never** edit
`page.h` by hand.

---

## 3. Callback functions

Callbacks are declared in `webPrefsConfig.h` and implemented in `storage.cpp` /
`alarmclock.cpp` / `crypto.cpp`. There are two layers:

### a) Lifecycle callbacks (passed to `wp.begin()`, `storage.cpp::initWebPrefs`)

```c
wp.begin(80, &dc, input_fields, cbDone, cbSave, cbBeforeResponse);
wp.setBusyCheck([]() { return (bool)g_fetchActive; });
```

| Callback | When | Purpose |
|---|---|---|
| `cbBeforeResponse()` | before every JSON response (`/getJson`) | write live values into `dc` before the page reads them (e.g. `date_time`, `info_text`, `alarm_state = buzzer.isPlaying()`) |
| `cbSave(int params)` | after every POST | write to flash - **only when `params > 1`** (saves write cycles: single instant-fields are not persisted) |
| `cbDone()` | click on "Done/Reboot" | `ESP.restart()` |
| `setBusyCheck(fn)` | checked before every request | if `fn()==true` the request gets "busy" (blocks WebPrefs while an SSL fetch runs -> heap protection) |

### b) Field callbacks (per field, 5th/last macro parameter)

**Key point:** a field callback fires **only on an instant POST of a single field**
(`data-uppost`, `params == 1` in `onDataReceive`). On a full form save (multiple
fields at once) the field callbacks do **not** run - there only `cbSave()` runs,
and the reboot via "Done" applies the values.

```
data-uppost field changed
   -> POST /postForm (1 param)
   -> setValue() writes dc.<field> (after optional validate)
   -> fields[i].callback()   ← LIVE, immediately, no reboot
   -> cbSave(1)              ← does NOT write (params==1)
```

Therefore: fields that should take effect immediately (brightness, volume,
alarm-off) need `data-uppost` **and** a field callback. Examples from the project:

| Field | Callback | Effect |
|---|---|---|
| `brightness`, `night_brightness`, `night_from/to` | `cbSetBrightness` | set backlight immediately |
| `crypto_alerts` | `cryptoReloadAlerts` | re-parse + re-baseline alerts live (no reboot) |
| `alarm_state` | `cbAlarmSwitch` | stop the buzzer when the switch is turned off |
| `ntp_server`, `gmt_offset`, `tz_string`, … | `cbNTPConfigUpdate` | re-apply NTP config |
| `date_time` | `cbSetClock` | set the clock manually |

### c) String validators (`FIELD_STRING_VALID`)

Signature `bool(*)(const char*)`. Runs in `setValue` **before** writing: returning
`false` discards the input and keeps the previous value. Examples: `isValidTime`
(`"HH:MM"`), `isValidDays` (7-char 0/1 mask). Perfect so a typo can never move the
alarm or store garbage.

---

## 4. `settings.html` - the data attributes

`settings.html` is only the form body; `head.html`, `script.js` and `style.css`
are assembled around it by `minify.py`. Each input's `id` **must** match the field
name in `input_fields[]` (the JSON roundtrip is keyed by `id`).

### `data-up` - deferred (needs Save + usually reboot)

The change is **not** sent immediately. `script.js::updatePost(el, post=false)`
only shows the banner "Save & Reboot to apply". The value is written to `dc` on the
full form save (the "Save" button) and takes effect via "Done/Reboot".
-> for structural/boot parameters (SSID, screen layout, icon colors, …).

```html
<input type="text" data-up id="wifi_ssid">
```

### `data-uppost` - instant (live, no reboot)

The change is sent **immediately** as a single-field POST
(`updatePost(el, post=true)` -> `POST /postForm` with 1 param). The server runs the
field callback live. But it is **only persisted on the next full save** (cbSave
skips single-param posts).
-> for parameters you want to feel instantly (brightness, volume, alarm-off, alerts).

```html
<input type="text" data-uppost id="brightness">
```

> Without `data-up`/`data-uppost` a field is neither sent live nor captured on save
> (no `name` attribute -> not part of the FormData). One attribute is mandatory.

### `activation-rules` - enable/disable fields conditionally (checkboxes only)

A JSON array of **1-based, absolute field indices** in DOM order.
`script.js::applyRuleset()` counts over the selector set
`input[checkbox|text|password], textarea` (order = as in the HTML).

Rule semantics per index `i`:
- **Positive** (`i > 0`): the target is **enabled when the checkbox is on**, disabled otherwise.
- **Negative** (`-i`): inverse - the target is **disabled/cleared when the checkbox is on**;
  if the target is itself a checkbox it is additionally **force-unchecked** (mutual exclusion).

Examples from `settings.html`:

```html
<!-- static IP on  -> fields 39..43 (IP/mask/GW/DNS1/DNS2) become editable -->
<input type="checkbox" data-up id="staticip_enabled" activation-rules="[39,40,41,42,43]">

<!-- web auth on    -> user(45)+password(46) become editable -->
<input type="checkbox" data-up id="web_auth" activation-rules="[45,46]">

<!-- AP-only on     -> mutually turns off field 38 (negative) -->
<input type="checkbox" data-up id="ap_only" activation-rules="[-38]">

<!-- NTP on         -> field 48 enabled, field 52 (manual time) force-off -->
<input type="checkbox" data-uppost id="ntp_enabled" activation-rules="[48,-52]">
```

> The indices are **absolute positions**. Inserting/removing a field
> anywhere in the form shifts every following position - **all** `activation-rules`
> indices after it must be adjusted by the same amount. The `console.log` in
> `initEventListeners()` prints the running indices on load (browser console),
> handy for recounting.

---

## 5. PlatformIO - build / flash / erase / firmware / LittleFS

Three environments (`platformio.ini`), differing mainly in `LOG_SERIAL_LEVEL`
(bitmask: 1=ERROR 2=INFO 4=WARNING 8=DEBUG) and optimization:

| Env | Flags | Serial log | Auto LittleFS upload |
|---|---|---|---|
| `cyd-retroclock-debug` | `-DDEBUG -DLOG_SERIAL_LEVEL=15` | all | no |
| `cyd-retroclock-test` | `-DTEST -DNDEBUG -Os -DLOG_SERIAL_LEVEL=7` | E/I/W | no |
| `cyd-retroclock-prod` | `-DNDEBUG -Os` | off | **yes** (post:uploadlfs.py) |

> Boot-loop trap: `-DDEBUG` **without** `-DNDEBUG` keeps `assert()` live -> early
> abort. The environments above are already configured correctly.

`page.h` is regenerated from `settings.html` on **every** build (`pre:minify.py`) 
after HTML changes a normal build is enough.

### Commands (Windows/CMD or Bash examples)

```bash

# Build/Flash LittleFS
pio run -e cyd-retroclock-debug -t buildfs
pio run -e cyd-retroclock-debug -t uploadfs

# Build/Flash Debug Firmware and show outputs
pio run -e cyd-retroclock-debug -t upload -t monitor

# Build/Flash Production Firmware & LittleFS (All)
pio run -e cyd-retroclock-prod -t upload
#   -> in the prod env LittleFS is flashed automatically afterwards (uploadlfs.py)

# Build Test Firmware
pio run -e cyd-retroclock-test

# Fully erase flash (config/EEPROM + FS gone -> defaults on next boot)
pio run -e cyd-retroclock-prod -t erase

# Serial monitor (115200, with timestamp filter)
pio device monitor

# Clean build artifacts
pio run -t clean
```

### Firmware / LittleFS

### Partitions (`partitions_retroclock.csv`)

```
app0    ota_0   0x10000    0x160000   (1.375 MB app)
app1    ota_1   0x170000   0x160000   (OTA slot)
spiffs  spiffs  0x2D0000   0x10000    (LittleFS, 64 KB - fonts/ColorMap)
```

> The filesystem offset must match between `partitions_retroclock.csv` and any
> shipped installer/`partitions.bin` - a stale offset makes `display.begin()` fail
> to mount LittleFS (black screen).

---

## 6. Restore mode (recovery)

Hold touch while booting -> restore mode: forces an open AP (`AP_PASSWD`), disables
WebPrefs auth and loads defaults so the settings page is always reachable. In
addition `initWifi()` automatically falls back to an open AP if a STA connection
fails (auto-rescue).

---

## 7. Logging (`serlog.h`)

Always log through the macros `LOG_SERROR / LOG_SINFO / LOG_SWARNING /
LOG_SDEBUG` (printf-style), never `Serial.print*` directly - the macros are
compiled in/out at build time depending on the `LOG_SERIAL_LEVEL` bitmask. The only
exceptions are the WiFi connection dots in `SimpleWifi.cpp` and `Serial.begin()` in
`main.cpp`. `serlog.h` has `#pragma once` (covers transitive multi-includes).

---

## 8. Heap / memory - watch this on larger changes

The heap is small and **fragments**: a TLS fetch needs a large *contiguous* block
(mbedTLS grabs two ~16 KB buffers per connection). Too little contiguous heap ->
fetches fail with `-32512` / `-11` and prices/weather silently stop updating.

Key rules when adding features:

- **`inline` for big shared data in headers.** A plain `const std::vector` (like
  `input_fields`) in a widely-included header gets **one heap copy per `.cpp`** -
  this once caused ~80% of fetch failures (~14 KB wasted). Mark it `inline`.
- **Long-lived buffers -> BSS/static or RTC_FAST, not the heap** (heap buffers
  fragment; BSS is near full -> mind linker overflow). See `ApiClient`'s
  `RTC_FAST_ATTR` response buffer.
- **Reuse one TLS session per batch** (`setupTlsClient()` keep-alive), guard new
  handshakes with `tlsHeapOk()` / `TLS_MIN_BLOCK`, and `http.end()` before parsing.

Diagnose via **`max_block`** (largest contiguous block), not just total `free` -
both are logged each tick. If `max_block` nears ~17-20 KB, handshakes start
failing -> move a buffer off the heap or add an `inline`.
