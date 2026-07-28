#ifndef WEBPREFSMACROS_H
#define WEBPREFSMACROS_H


// _SZ variants allow manual size specification

// STRING field - auto size (default)
#define FIELD_STRING(field, default_val, min_len, callback) \
    { WebPrefs::STRING, VAR_NAME(field), default_val, \
      sizeof(DeviceConfig::field), offsetof(DeviceConfig, field), \
      min_len, sizeof(DeviceConfig::field)-1, callback }

// STRING field - manual size
#define FIELD_STRING_SZ(field, default_val, min_len, max_len, callback) \
    { WebPrefs::STRING, VAR_NAME(field), default_val, \
      sizeof(DeviceConfig::field), offsetof(DeviceConfig, field), \
      min_len, max_len, callback }

// STRING field with a format validator. The validator (bool(*)(const char*))
// runs in setValue BEFORE writing: returns false -> the value is ignored and
// the previous value is kept.
#define FIELD_STRING_VALID(field, default_val, min_len, callback, validator) \
    { WebPrefs::STRING, VAR_NAME(field), default_val, \
      sizeof(DeviceConfig::field), offsetof(DeviceConfig, field), \
      min_len, sizeof(DeviceConfig::field)-1, callback, validator }

// PASSWORD field - auto size (default)
#define FIELD_PASSWORD(field, default_val, min_len, callback) \
    { WebPrefs::PASSWORD, VAR_NAME(field), default_val, \
      sizeof(DeviceConfig::field), offsetof(DeviceConfig, field), \
      min_len, sizeof(DeviceConfig::field)-1, callback }

// PASSWORD field - manual size
#define FIELD_PASSWORD_SZ(field, default_val, min_len, max_len, callback) \
    { WebPrefs::PASSWORD, VAR_NAME(field), default_val, \
      sizeof(DeviceConfig::field), offsetof(DeviceConfig, field), \
      min_len, max_len, callback }

// Numeric fields (size is fixed by type, no _SZ variant needed)
#define FIELD_UINT16(field, default_val, min_val, max_val, callback) \
    { WebPrefs::UINT16, VAR_NAME(field), default_val, \
      sizeof(DeviceConfig::field), offsetof(DeviceConfig, field), \
      min_val, max_val, callback }

#define FIELD_INT16(field, default_val, min_val, max_val, callback) \
    { WebPrefs::INT16, VAR_NAME(field), default_val, \
      sizeof(DeviceConfig::field), offsetof(DeviceConfig, field), \
      min_val, max_val, callback }

#define FIELD_UINT32(field, default_val, min_val, max_val, callback) \
    { WebPrefs::UINT32, VAR_NAME(field), default_val, \
      sizeof(DeviceConfig::field), offsetof(DeviceConfig, field), \
      min_val, max_val, callback }

#define FIELD_INT32(field, default_val, min_val, max_val, callback) \
    { WebPrefs::INT32, VAR_NAME(field), default_val, \
      sizeof(DeviceConfig::field), offsetof(DeviceConfig, field), \
      min_val, max_val, callback }

#define FIELD_FLOAT(field, default_val, min_val, max_val, callback) \
    { WebPrefs::FLOAT, VAR_NAME(field), default_val, \
      sizeof(DeviceConfig::field), offsetof(DeviceConfig, field), \
      min_val, max_val, callback }

// CHECKBOX field (always 0-1 range)
#define FIELD_CHECKBOX(field, default_val, callback) \
    { WebPrefs::CHECKBOX, VAR_NAME(field), default_val, \
      sizeof(DeviceConfig::field), offsetof(DeviceConfig, field), \
      0, 1, callback }

#endif // WEBPREFSMACROS_H