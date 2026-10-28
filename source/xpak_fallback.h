#ifndef BO3_XPAK_FALLBACK_H
#define BO3_XPAK_FALLBACK_H
#include <stddef.h>
/* Read-only: returns 1 only for a verified, complete sibling of an incomplete
 * localized Workshop/usermap pack. Neither file is modified. */
int bo3_xpak_fallback(const char *path, char *replacement, size_t capacity);
/* Also permits an English/language-preserving derived pack in cache_directory.
 * Copies only absent, identically named mesh chunks; originals stay untouched.
 * A NULL cache directory retains the read-only sibling-selection behavior. */
int bo3_xpak_resolve(const char *path, const char *cache_directory,
                     char *replacement, size_t capacity);
#endif
