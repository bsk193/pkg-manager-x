#ifndef PLATFORM_H
#define PLATFORM_H

/*
 * PKG Manager X - compile-time console selection.
 *
 * Exactly one of these is non-zero for a console build:
 *   PKGMGR_CONSOLE_PS5  prospero toolchain / -DPS5_BUILD
 *   PKGMGR_CONSOLE_PS4  orbis toolchain    / -DPS4_BUILD
 * Host test builds define neither (PKGMGR_ON_CONSOLE == 0).
 */

#if defined(PS4_BUILD) || defined(__ORBIS__)
#define PKGMGR_CONSOLE_PS4 1
#define PKGMGR_CONSOLE_PS5 0
#elif defined(__Prospero__) || defined(PS5_BUILD)
#define PKGMGR_CONSOLE_PS4 0
#define PKGMGR_CONSOLE_PS5 1
#else
#define PKGMGR_CONSOLE_PS4 0
#define PKGMGR_CONSOLE_PS5 0
#endif

#define PKGMGR_ON_CONSOLE (PKGMGR_CONSOLE_PS4 || PKGMGR_CONSOLE_PS5)

/* Lower-case console name used in JSON ("ps5" / "ps4"). Host builds report
 * the console the tests emulate (PKGMGR_HOST_CONSOLE, default ps5). */
#if PKGMGR_CONSOLE_PS4
#define PKGMGR_CONSOLE_NAME "ps4"
#elif PKGMGR_CONSOLE_PS5
#define PKGMGR_CONSOLE_NAME "ps5"
#else
#define PKGMGR_CONSOLE_NAME "host"
#endif

#endif /* PLATFORM_H */
