#define PS4_BUILD 1
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
static int calls, logs;
void sceSystemServicePowerTick(void) { calls++; }
void install_log(const char *fmt, ...) { (void)fmt; logs++; }
#include "../src/ps4_power.c"
int main(void) {
    ps4_power_update(0);
    assert(calls == 0 && logs == 0);
    ps4_power_update(1);
    ps4_power_update(1);
    assert(calls == 2 && logs == 1);
    /* Completion, failure and cancellation all clear is_installing. */
    ps4_power_update(0);
    ps4_power_update(0);
    assert(calls == 2 && logs == 2);
    ps4_power_update(1);
    assert(calls == 3 && logs == 3);
    puts("PS4 power policy passed");
    return 0;
}
