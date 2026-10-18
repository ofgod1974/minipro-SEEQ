/* POSIX string helpers missing from some MinGW-w64 distributions.
 * GPL-3.0-or-later; see LICENSE. */
#ifndef MINIPRO_WIN_COMPAT_H
#define MINIPRO_WIN_COMPAT_H
#ifdef _WIN32
#include <stdlib.h>
#include <string.h>
static inline char *mp_strndup(const char *s, size_t max)
{
	size_t n = 0;
	while (n < max && s[n]) n++;
	char *p = malloc(n+1);
	if (p) {memcpy(p, s, n); p[n] = 0;}
	return p;
}
static inline char *mp_stpcpy(char *dest, const char *src)
{
	size_t n = strlen(src);
	memcpy(dest, src, n+1);
	return dest+n;
}
#define strndup mp_strndup
#define stpcpy mp_stpcpy
#endif
#endif
