#include "platform.h"
#include "ps4_power.h"

#if PKGMGR_CONSOLE_PS4
#include "installer.h"
/* Public OpenOrbis SystemService.h; exported by the payload SDK's
 * libSceSystemService. This refreshes idle power management, not manual
 * suspend. No persistent console setting is changed. */
extern void sceSystemServicePowerTick(void);
#endif

void ps4_power_update(int installing) {
#if PKGMGR_CONSOLE_PS4
    static int was_installing;
    if (installing) sceSystemServicePowerTick();
    if (!!installing != was_installing) {
        install_log("[POWER] %s", installing
            ? "Refreshing PS4 idle timer during installation; avoid manual rest mode"
            : "Installation idle; normal PS4 power saving restored");
        was_installing = !!installing;
    }
#else
    (void)installing;
#endif
}
