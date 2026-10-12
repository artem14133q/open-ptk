// Lookup in Petka's plain-text .INI files (NAMES.INI, CAST.INI, BGS.INI,
// PARTS.INI -- all CP1251). The exe reads them with the Win32 profile API,
// so lookups follow its rules: section and key names match regardless of
// case, whitespace around keys and values is ignored, lines starting with
// ';' are comments, the first matching key wins, and a value wrapped in a
// pair of quotes loses them.
#ifndef PETKA_INI_H
#define PETKA_INI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Copies the value of `key` in `[section]` to `out` (NUL-terminated,
// truncated to fit). Returns 1 if the key exists, 0 otherwise (and `out`
// becomes an empty string).
int petka_ini_get(const uint8_t *data, size_t len, const char *section, const char *key,
                  char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif

#endif
