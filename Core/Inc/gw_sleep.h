#ifndef _GW_SLEEP_H_
#define _GW_SLEEP_H_

#include <stdint.h>
#include <stdbool.h>

typedef void (*sleep_pre_wakeup_callback_t)();
typedef void (*sleep_post_wakeup_callback_t)();
void GW_EnterDeepSleep(bool standby, sleep_pre_wakeup_callback_t pre_wakeup_callback, sleep_post_wakeup_callback_t post_wakeup_callback);

#endif