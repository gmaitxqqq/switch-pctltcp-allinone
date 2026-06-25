/**
 * pctl_handler.c - Nintendo Switch PCTL service IPC wrapper
 *
 * Thread-safe: all operations are protected by a global mutex.
 * Main thread initializes once at startup; HTTP thread reuses the same session.
 * No reinit between calls (was causing session corruption).
 */

#include "pctl_handler.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Global state                                                        */
/* ------------------------------------------------------------------ */
static bool s_initialized = false;
static Mutex s_pctl_mutex;

static TimeZoneRule s_tz_rule;
static bool s_tz_rule_loaded = false;

/* ------------------------------------------------------------------ */
/* pctl_init / pctl_exit                                              */
/* ------------------------------------------------------------------ */
Result pctl_init(void)
{
    mutexLock(&s_pctl_mutex);
    Result rc = 0;
    if (!s_initialized) {
        rc = pctlInitialize();
        if (R_SUCCEEDED(rc)) {
            Service *srv = pctlGetServiceSession_Service();
            if (srv) {
                s_initialized = true;
            } else {
                pctlExit();
                rc = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
            }
        }
    }
    mutexUnlock(&s_pctl_mutex);
    return rc;
}

void pctl_exit(void)
{
    mutexLock(&s_pctl_mutex);
    if (s_initialized) {
        pctlExit();
        s_initialized = false;
    }
    mutexUnlock(&s_pctl_mutex);
}

bool pctl_is_initialized(void)
{
    return s_initialized;
}

/* ------------------------------------------------------------------ */
/* Helper: get current service session (always fresh)                 */
/* ------------------------------------------------------------------ */
static Service *pctl_srv(void)
{
    return s_initialized ? pctlGetServiceSession_Service() : NULL;
}

/* ------------------------------------------------------------------ */
/* Timezone                                                            */
/* ------------------------------------------------------------------ */
Result pctl_load_timezone(void)
{
    Result rc;
    TimeLocationName tz_name = {0};

    rc = setsysInitialize();
    if (R_FAILED(rc)) return rc;
    rc = setsysGetDeviceTimeZoneLocationName(&tz_name);
    setsysExit();

    if (R_FAILED(rc) || tz_name.name[0] == '\0') return rc;

    rc = timeLoadTimeZoneRule(&tz_name, &s_tz_rule);
    if (R_SUCCEEDED(rc)) s_tz_rule_loaded = true;
    return rc;
}

/* ------------------------------------------------------------------ */
/* Public API (all thread-safe via mutex)                             */
/* ------------------------------------------------------------------ */

Result pctl_start_play_timer(void)
{
    mutexLock(&s_pctl_mutex);
    Service *srv = pctl_srv();
    Result rc = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    if (srv) {
        /* Best-effort: enable system-level restriction so the countdown
         * UI will actually render.  If this fails (e.g. PIN-locked),
         * we still try to start the timer — the timer may work even
         * if the on-screen countdown doesn't appear. */
        Result rc_enable = serviceDispatch(srv, 2);   /* EnableRestriction */
        (void)rc_enable;  /* intentionally ignored */

        rc = serviceDispatch(srv, 1451);               /* StartPlayTimer    */
    }

    mutexUnlock(&s_pctl_mutex);
    return rc;
}

Result pctl_stop_play_timer(void)
{
    mutexLock(&s_pctl_mutex);
    Service *srv = pctl_srv();
    Result rc = srv ? serviceDispatch(srv, 1452)
                    : MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    mutexUnlock(&s_pctl_mutex);
    return rc;
}

Result pctl_is_enabled(bool *enabled)
{
    if (!enabled) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    mutexLock(&s_pctl_mutex);
    Service *srv = pctl_srv();
    u8 tmp = 0;
    Result rc = srv ? serviceDispatchOut(srv, 1453, tmp)
                    : MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (R_SUCCEEDED(rc)) *enabled = (tmp != 0);
    mutexUnlock(&s_pctl_mutex);
    return rc;
}

Result pctl_get_remaining_time(u64 *remaining_ns)
{
    if (!remaining_ns) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    mutexLock(&s_pctl_mutex);
    Service *srv = pctl_srv();
    u64 tmp = 0;
    Result rc = srv ? serviceDispatchOut(srv, 1454, tmp)
                    : MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (R_SUCCEEDED(rc)) *remaining_ns = tmp;
    mutexUnlock(&s_pctl_mutex);
    return rc;
}

Result pctl_is_restricted(bool *restricted)
{
    if (!restricted) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    mutexLock(&s_pctl_mutex);
    Service *srv = pctl_srv();
    u8 tmp = 0;
    Result rc = srv ? serviceDispatchOut(srv, 1455, tmp)
                    : MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (R_SUCCEEDED(rc)) *restricted = (tmp != 0);
    mutexUnlock(&s_pctl_mutex);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Settings read/write (NO reinit — was corrupting sessions)          */
/* ------------------------------------------------------------------ */

Result pctl_get_settings(PlayTimerSettings *settings)
{
    if (!settings) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    memset(settings, 0, sizeof(*settings));

    mutexLock(&s_pctl_mutex);
    Service *srv = pctl_srv();
    if (!srv) {
        mutexUnlock(&s_pctl_mutex);
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }

    u16 c[34];
    memset(c, 0, sizeof(c));
    Result rc = serviceDispatchOut(srv, 145601, c);
    if (R_SUCCEEDED(rc)) memcpy(settings, c, sizeof(c));
    mutexUnlock(&s_pctl_mutex);
    return rc;
}

Result pctl_set_settings(const PlayTimerSettings *settings)
{
    if (!settings) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    mutexLock(&s_pctl_mutex);
    Service *srv = pctl_srv();
    if (!srv) {
        mutexUnlock(&s_pctl_mutex);
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }

    u16 c[34];
    memcpy(c, settings->raw, sizeof(c));
    Result rc = serviceDispatchIn(srv, 195101, c);
    mutexUnlock(&s_pctl_mutex);
    return rc;
}

/* ------------------------------------------------------------------ */
/* PIN verification (cmd 1)                                            */
/*                                                                    */
/* VerifyPin expects the PIN as a u32 value in BCD format:            */
/*   each nibble (4 bits) stores one decimal digit.                   */
/*   Example: PIN "1234" -> 0x00001234 (u32)                          */
/*                                                                    */
/* IMPORTANT: After each VerifyPin call (success or failure), we      */
/* re-initialize pctl to clear any session state. Otherwise,          */
/* subsequent VerifyPin calls may fail even with the correct PIN.     */
/* The re-init is done OUTSIDE the mutex to avoid deadlock             */
/* (pctl_init() also acquires s_pctl_mutex internally).                */
/* ------------------------------------------------------------------ */

Result pctl_verify_pin(const char *pin)
{
    if (!pin || strlen(pin) == 0 || strlen(pin) > 8)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    u32 pin_len = (u32)strlen(pin);

    /* Convert ASCII PIN to BCD format u32 */
    u32 pin_bcd = 0;
    for (u32 i = 0; i < pin_len; i++) {
        if (pin[i] < '0' || pin[i] > '9')
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        pin_bcd = (pin_bcd << 4) | (u32)(pin[i] - '0');
    }

    mutexLock(&s_pctl_mutex);
    Service *srv = pctl_srv();
    Result rc = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    if (srv) {
        /* VerifyPin (cmd 1): PIN passed as raw u32 in BCD format */
        rc = serviceDispatchIn(srv, 1, pin_bcd);
    }

    /* Release mutex BEFORE re-initializing pctl, because pctl_init()
     * also acquires s_pctl_mutex internally (would deadlock). */
    mutexUnlock(&s_pctl_mutex);

    /* CRITICAL: Re-initialize pctl after VerifyPin to clear session state.
     * The pctl service may cache state after a failed verification,
     * causing subsequent calls to fail even with the correct PIN. */
    pctl_exit();
    pctl_init();

    return rc;
}

/* ------------------------------------------------------------------ */
/* Day-level helpers                                                   */
/* ------------------------------------------------------------------ */

Result pctl_get_day_limit_minutes(int day, u32 *minutes)
{
    if (!minutes || day < 0 || day > 7)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    PlayTimerSettings settings;
    Result rc = pctl_get_settings(&settings);
    if (R_FAILED(rc)) return rc;

    if (day == 7) {
        *minutes = 0;
        for (int d = 0; d < PCTL_DAYS; d++) {
            u16 m = settings.raw[PCTL_DAY_MINUTES_OFFSET(d)];
            if (m == PT_DAY_NOLIMIT) { *minutes = 0; return 0; }
            if (m > *minutes) *minutes = m;
        }
    } else {
        u16 m = settings.raw[PCTL_DAY_MINUTES_OFFSET(day)];
        *minutes = (m == PT_DAY_NOLIMIT) ? 0 : m;
    }
    return 0;
}

Result pctl_set_day_limit_minutes(int day, u32 minutes)
{
    if (day < 0 || day >= PCTL_DAYS)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    PlayTimerSettings settings;
    Result rc = pctl_get_settings(&settings);
    if (R_FAILED(rc)) return rc;

    u16 val;
    if (minutes == 0) {
        val = PT_DAY_NOLIMIT;
    } else {
        if (minutes > 1440) minutes = 1440;
        val = (u16)minutes;
    }

    settings.raw[0] = 0x0101;
    settings.raw[1] = (val != PT_DAY_NOLIMIT) ? 0x0001 : 0x0000;
    settings.raw[PCTL_DAY_FLAG_OFFSET(day)]    = (val != PT_DAY_NOLIMIT) ? 0x0100 : 0x0000;
    settings.raw[PCTL_DAY_MINUTES_OFFSET(day)] = val;

    return pctl_set_settings(&settings);
}

Result pctl_set_daily_limit_minutes(u32 minutes)
{
    PlayTimerSettings settings;
    Result rc = pctl_get_settings(&settings);
    if (R_FAILED(rc)) return rc;

    u16 val;
    if (minutes == 0) {
        val = PT_DAY_NOLIMIT;
    } else {
        if (minutes > 1440) minutes = 1440;
        val = (u16)minutes;
    }

    settings.raw[0] = (val != PT_DAY_NOLIMIT) ? 0x0101 : 0x0000;
    settings.raw[1] = (val != PT_DAY_NOLIMIT) ? 0x0001 : 0x0000;

    for (int d = 0; d < PCTL_DAYS; d++) {
        settings.raw[PCTL_DAY_FLAG_OFFSET(d)]    = (val != PT_DAY_NOLIMIT) ? 0x0100 : 0x0000;
        settings.raw[PCTL_DAY_MINUTES_OFFSET(d)] = val;
    }

    return pctl_set_settings(&settings);
}

int pctl_get_today_day(void)
{
    u64 now_posix = 0;
    Result rc;

    rc = timeGetCurrentTime(TimeType_NetworkSystemClock, &now_posix);
    if (R_FAILED(rc) || now_posix <= 946684800ULL)
        rc = timeGetCurrentTime(TimeType_LocalSystemClock, &now_posix);
    if (R_FAILED(rc) || now_posix <= 946684800ULL)
        rc = timeGetCurrentTime(TimeType_UserSystemClock, &now_posix);

    if (R_SUCCEEDED(rc) && now_posix > 946684800ULL) {
        TimeCalendarTime cal;
        TimeCalendarAdditionalInfo additional;
        rc = timeToCalendarTimeWithMyRule(now_posix, &cal, &additional);
        if (R_SUCCEEDED(rc)) return (int)additional.wday;

        if (s_tz_rule_loaded) {
            rc = timeToCalendarTime(&s_tz_rule, now_posix, &cal, &additional);
            if (R_SUCCEEDED(rc)) return (int)additional.wday;
        }

        time_t t = (time_t)now_posix;
        struct tm *tm_info = gmtime(&t);
        if (tm_info) return tm_info->tm_wday;
    }

    {
        time_t t = time(NULL);
        if (t != (time_t)-1 && t > 946684800ULL) {
            struct tm *tm_info = localtime(&t);
            if (tm_info) return tm_info->tm_wday;
        }
    }
    return 0;
}

Result pctl_get_daily_limit_minutes(u32 *minutes)
{
    int today = pctl_get_today_day();
    return pctl_get_day_limit_minutes(today, minutes);
}

Result pctl_reset_play_time(void)
{
    Result rc = pctl_stop_play_timer();
    if (R_FAILED(rc)) return rc;

    PlayTimerSettings settings;
    rc = pctl_get_settings(&settings);
    if (R_FAILED(rc)) return rc;

    rc = pctl_set_settings(&settings);
    if (R_FAILED(rc)) return rc;

    return pctl_start_play_timer();
}

/* ------------------------------------------------------------------ */
/* Restriction enable/disable                                          */
/*                                                                    */
/* IMPORTANT: raw[1] in PlayTimerSettings is the "time limit enabled"  */
/* flag. This is DIFFERENT from pctlIsRestrictionEnabled() (cmd 1031), */
/* which reads the system-level parental control toggle.               */
/*                                                                    */
/* We must read/write raw[1] directly, NOT pctlIsRestrictionEnabled(), */
/* otherwise the toggle shows wrong state after disabling.             */
/* ------------------------------------------------------------------ */

Result pctl_get_restriction_enabled(bool *enabled)
{
    if (!enabled) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    PlayTimerSettings settings;
    Result rc = pctl_get_settings(&settings);
    if (R_FAILED(rc)) return rc;

    *enabled = (settings.raw[1] == 0x0001);
    return 0;
}

Result pctl_set_restriction_enabled(bool enable)
{
    PlayTimerSettings settings;
    Result rc = pctl_get_settings(&settings);
    if (R_FAILED(rc)) return rc;

    if (enable) {
        if (settings.raw[0] == 0)
            settings.raw[0] = 0x0101;
        settings.raw[1] = 0x0001;
    } else {
        settings.raw[1] = 0x0000;
    }

    return pctl_set_settings(&settings);
}
