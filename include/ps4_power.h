#ifndef PKGMGR_PS4_POWER_H
#define PKGMGR_PS4_POWER_H
/* Call once every five seconds from the daemon, including while idle. */
void ps4_power_update(int installing);
#endif
