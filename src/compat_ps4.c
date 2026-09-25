/*
 * PKG Manager X - PS4 SDK compatibility shims
 *
 * The ps4-payload-sdk links libc + libSceLibcInternal, which lack a few
 * symbols that the FreeBSD 9 headers and our sources (and libsmb2/mbedTLS)
 * expect. Compiled to nothing on other targets.
 */

#include "platform.h"

#if PKGMGR_CONSOLE_PS4

#include <ctype.h>
#include <runetype.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

/* Without __NO_TLS, FreeBSD 9 <runetype.h> makes every ctype macro
 * (isalnum, tolower, ...) read this thread-local locale pointer, but no SDK
 * library defines it. Point it at the SDK's C locale, which is what its own
 * __getCurrentRuneLocale() returns. */
_Thread_local const _RuneLocale *_ThreadRuneLocale = &_DefaultRuneLocale;

/* Used by upstream code and http_source.c; not exported on PS4. */
char *strcasestr(const char *s, const char *find) {
    size_t n = strlen(find);
    if (n == 0) return (char *)s;
    for (; *s; s++) {
        if (strncasecmp(s, find, n) == 0) return (char *)s;
    }
    return NULL;
}

#endif /* PKGMGR_CONSOLE_PS4 */
