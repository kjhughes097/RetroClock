#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stddef.h>

void initWebPrefs();       // start the web UI and load the config (defaults if invalid)
void loadDefaultConfig();  // discard the loaded values, use the built-in defaults
void cbDone();
void cbBeforeResponse();
void cbSave(int params_count);
bool readConfig();
bool writeConfig();
uint16_t computeChecksum(const uint8_t *data, size_t size);

#endif // STORAGE_H
