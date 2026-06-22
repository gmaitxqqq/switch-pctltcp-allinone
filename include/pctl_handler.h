#ifndef PCTL_HANDLER_H
#define PCTL_HANDLER_H

#include <switch.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define PCTL_DAYS           7
#define PT_DAY_NOLIMIT      0xFFFFu
#define PT_DAY_MINUTES_MAX  1440

#define PCTL_DAY_FLAG_OFFSET(d)    (7 + 4 * (d) + 0)
#define PCTL_DAY_MINUTES_OFFSET(d)  (7 + 4 * (d) + 2)

/* ------------------------------------------------------------------ */
/* Types                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    u16 raw[34];
} PlayTimerSettings;

/* ------------------------------------------------------------------ */
/* Init / Exit                                                        */
/* ------------------------------------------------------------------ */

Result pctl_init(void);
void   pctl_exit(void);
bool   pctl_is_initialized(void);

/* ------------------------------------------------------------------ */
/* Play Timer Control                                                 */
/* ------------------------------------------------------------------ */

Result pctl_start_play_timer(void);
Result pctl_stop_play_timer(void);
Result pctl_is_enabled(bool *enabled);
Result pctl_get_remaining_time(u64 *remaining_ns);
Result pctl_is_restricted(bool *restricted);

/* ------------------------------------------------------------------ */
/* Settings read / write                                              */
/* ------------------------------------------------------------------ */

Result pctl_get_settings(PlayTimerSettings *settings);
Result pctl_set_settings(const PlayTimerSettings *settings);

/* ------------------------------------------------------------------ */
/* Day-level helpers                                                  */
/* ------------------------------------------------------------------ */

Result pctl_get_day_limit_minutes(int day, u32 *minutes);
Result pctl_set_day_limit_minutes(int day, u32 minutes);
Result pctl_set_daily_limit_minutes(u32 minutes);
Result pctl_get_daily_limit_minutes(u32 *minutes);

/* ------------------------------------------------------------------ */
/* Utility                                                           */
/* ------------------------------------------------------------------ */

int  pctl_get_today_day(void);
Result pctl_reset_play_time(void);
Result pctl_enable_restriction(void);

/* ------------------------------------------------------------------ */
/* Timezone (used by sysmodule context; NRO can ignore)             */
/* ------------------------------------------------------------------ */

Result pctl_load_timezone(void);

#ifdef _cplusplus
}
#endif

#endif /* PCTL_HANDLER_H */
