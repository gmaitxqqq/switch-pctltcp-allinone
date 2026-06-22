// Switch Parental Control Manager v11.5
// =============================================================
// Pure .nro homebrew app — no sysmodule required
//
// Based on NX-Pctl-Manager (tailiang2008) and
// Reset-Parental-Controls-NX_Mod (nangongjing1) pctl IPC calls
//
// Key points:
//   - pctlInitialize() works in normal .nro process
//   - sysmodule cannot access pctl service (v1-v10 failure root cause)
//   - SetPlayTimerSettingsForDebug (cmd 195101) compatible with fw 22.1.0
//   - UnlockRestrictionTemporarily (cmd 1201) reads PIN and passes it back
//   - PlayTimerSettings layout: u16[34], per-day at [7+4n], minutes at [7+4n+2]
//   - v11.5: PIN verification on startup — must enter correct system PIN to use app
//
// CRITICAL: printf requires consoleUpdate(NULL) to flush to screen!
//
// Compatible: Atmosphere CFW + fw 22.1.0 (AMS 1.11.1)
// =============================================================

#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pctl_handler.h"
#include "http_server.h"

// ---- Constants ----
#define PT_DAY_NOLIMIT 0xFFFFu
#define MAX_PIN_LEN    8
#define DEFAULT_PIN    "8473"

// Parental control safety levels
enum {
    PctlSafetyLevel_None       = 0,
    PctlSafetyLevel_Custom     = 1,
    PctlSafetyLevel_YoungChild = 2,
    PctlSafetyLevel_Child      = 3,
    PctlSafetyLevel_Teen       = 4,
};

// ---- Pctl Status ----
typedef struct {
    bool safety_level_ok;
    u32  safety_level;
    bool pin_length_ok;
    u32  pin_length;
    bool restriction_enabled_ok;
    bool restriction_enabled;
} PctlStatus;

// ---- Play Timer State ----
typedef struct {
    bool valid;
    bool enabled;
    bool restricted;
    u64  remaining_ns;
    u16  day_min[7];  // Sun..Sat
} PtState;

// ---- Pctl Service Operations (from NX-Pctl-Manager pctl_ops.c) ----

static Result pctl_ops_reinit(void)
{
    pctlExit();
    return pctlInitialize();
}

static void pctl_status_fetch(PctlStatus *out)
{
    memset(out, 0, sizeof(*out));
    Service *srv = pctlGetServiceSession_Service();

    u32 level = 0;
    if (R_SUCCEEDED(serviceDispatchOut(srv, 1032, level))) {
        out->safety_level = level;
        out->safety_level_ok = true;
    }

    u32 pin_len = 0;
    if (R_SUCCEEDED(serviceDispatchOut(srv, 1206, pin_len))) {
        out->pin_length = pin_len;
        out->pin_length_ok = true;
    }

    bool enabled = false;
    if (R_SUCCEEDED(serviceDispatchOut(srv, 1031, enabled))) {
        out->restriction_enabled = enabled;
        out->restriction_enabled_ok = true;
    }
}

// Read system PIN string via GetPinCode (cmd 1208)
// Returns true if PIN was read successfully
static bool pctl_read_pin(char *pin_out, size_t pin_buf_size, u32 *pin_len_out)
{
    pctl_ops_reinit();
    Service *srv = pctlGetServiceSession_Service();

    memset(pin_out, 0, pin_buf_size);
    u32 pin_len = 0;
    Result rc = serviceDispatchOut(srv, 1208, pin_len,
        .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_Out },
        .buffers      = { { pin_out, pin_buf_size } });
    if (R_FAILED(rc)) return false;

    if (pin_len_out) *pin_len_out = pin_len;
    return true;
}

static Result pctl_set_pin(void)
{
    // pctlauth applet opens its own pctl session, so close/reopen around it
    pctlExit();
    Result rc = pctlauthRegisterPasscode();
    pctlInitialize();
    return rc;
}

static Result pctl_delete_parental_controls(void)
{
    return serviceDispatch(pctlGetServiceSession_Service(), 1043);
}

static Result pctl_delete_pairing(void)
{
    return serviceDispatch(pctlGetServiceSession_Service(), 1941);
}

static Result pctl_unlock_restriction_temporarily(void)
{
    // Read current PIN via GetPinCode (cmd 1208),
    // then pass it to UnlockRestrictionTemporarily (cmd 1201)
    pctl_ops_reinit();
    Service *srv = pctlGetServiceSession_Service();

    char pin[32];
    memset(pin, 0, sizeof(pin));
    u32 pin_len = 0;
    Result rc = serviceDispatchOut(srv, 1208, pin_len,
        .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_Out },
        .buffers      = { { pin, sizeof(pin) } });
    if (R_FAILED(rc)) return rc;

    size_t n = (pin_len > 0 && pin_len < (u32)sizeof(pin)) ? ((size_t)pin_len + 1) : sizeof(pin);
    return serviceDispatch(srv, 1201,
        .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_In },
        .buffers      = { { pin, n } });
}

static const char *safety_level_name(u32 level)
{
    switch (level) {
        case PctlSafetyLevel_None:       return "None";
        case PctlSafetyLevel_Custom:     return "Custom";
        case PctlSafetyLevel_YoungChild: return "Young Child";
        case PctlSafetyLevel_Child:      return "Child";
        case PctlSafetyLevel_Teen:       return "Teen";
        default:                         return "Unknown";
    }
}

// ---- Play Timer Operations (from NX-Pctl-Manager pctl_ops.c) ----

static void pctl_play_timer_query(PtState *out)
{
    memset(out, 0, sizeof(*out));
    for (int n = 0; n < 7; n++) out->day_min[n] = PT_DAY_NOLIMIT;
    pctl_ops_reinit();
    Service *srv = pctlGetServiceSession_Service();

    bool b = false;
    if (R_SUCCEEDED(serviceDispatchOut(srv, 1453, b))) out->enabled = b;
    b = false;
    if (R_SUCCEEDED(serviceDispatchOut(srv, 1455, b))) out->restricted = b;
    u64 rem = 0;
    if (R_SUCCEEDED(serviceDispatchOut(srv, 1454, rem))) out->remaining_ns = rem;

    u16 c[34]; memset(c, 0, sizeof(c));
    if (R_SUCCEEDED(serviceDispatchOut(srv, 145601, c))) {
        out->valid = true;
        for (int n = 0; n < 7; n++)
            out->day_min[n] = c[7 + 4 * n + 1] ? c[7 + 4 * n + 2] : PT_DAY_NOLIMIT;
    }
}

static Result pctl_play_timer_set_days(const u16 days_min[7])
{
    pctl_ops_reinit();

    bool any = false;
    for (int n = 0; n < 7; n++) if (days_min[n] != PT_DAY_NOLIMIT) any = true;

    u16 c[34] = {0};
    if (any) {
        c[0] = 0x0101;   // header magic
        c[1] = 0x0001;
        for (int n = 0; n < 7; n++) {
            if (days_min[n] == PT_DAY_NOLIMIT) continue;
            c[7 + 4 * n + 0] = 0x0600;
            c[7 + 4 * n + 1] = 0x0100;
            c[7 + 4 * n + 2] = days_min[n];
        }
    }

    Result rc = serviceDispatchIn(pctlGetServiceSession_Service(), 195101, c);

    return rc;
}

static Result pctl_play_timer_set_uniform(u16 minutes)
{
    u16 d[7];
    for (int i = 0; i < 7; i++) d[i] = minutes;
    return pctl_play_timer_set_days(d);
}

static Result pctl_play_timer_clear(void)
{
    u16 d[7];
    for (int i = 0; i < 7; i++) d[i] = PT_DAY_NOLIMIT;
    return pctl_play_timer_set_days(d);
}

// ---- Pad Input (new libnx API) ----
static PadState g_pad;

static void initPad(void)
{
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
}

static u64 padGetDown(void)
{
    padUpdate(&g_pad);
    return padGetButtonsDown(&g_pad);
}

// ---- UI Helpers ----

static const char *day_names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static void printSeparator(void)
{
    printf("  ========================================\n");
}

static void consoleFlush(void)
{
    consoleUpdate(NULL);
}

static void waitForKey(void)
{
    printf("\n   Press any key to continue...\n");
    consoleFlush();
    while (appletMainLoop()) {
        u64 k = padGetDown();
        if (k) break;
        consoleFlush();
        svcSleepThread(10000000ULL);
    }
}

// ---- PIN Entry Screen ----
// Displays a digit-by-digit PIN entry UI
// cursor: 0..(pin_max-1) = digit position
// Each digit: 0-9, use Up/Down to change
// A = confirm, B = exit app

static bool pinEntryScreen(const char *expected_pin, u32 pin_length)
{
    char input[MAX_PIN_LEN + 1];
    memset(input, '0', pin_length);
    input[pin_length] = '\0';

    int cursor = 0;
    int attempts = 0;
    const int max_attempts = 5;

    while (appletMainLoop()) {
        u64 k = padGetDown();

        consoleClear();
        printf("\n");
        printSeparator();
        printf("   PIN Verification Required\n");
        printSeparator();
        printf("\n");
        printf("   Enter the system parental control\n");
        printf("   PIN to access this app.\n\n");
        printf("   Attempt %d / %d\n\n", attempts + 1, max_attempts);

        // Display PIN digits with cursor
        printf("   PIN:  ");
        for (u32 i = 0; i < pin_length; i++) {
            if (i == (u32)cursor)
                printf("[%c]", input[i]);
            else
                printf(" %c ", input[i]);
        }
        printf("\n\n");

        printf("   Up/Down: Change digit (+/-1)\n");
        printf("   Left/Right: Move cursor\n");
        printf("   A: Confirm   B: Exit app\n");
        consoleFlush();

        if (k & HidNpadButton_Up) {
            if (input[cursor] < '9') input[cursor]++;
            else input[cursor] = '0';
        }
        if (k & HidNpadButton_Down) {
            if (input[cursor] > '0') input[cursor]--;
            else input[cursor] = '9';
        }
        if (k & HidNpadButton_Left) {
            if (cursor > 0) cursor--;
        }
        if (k & HidNpadButton_Right) {
            if ((u32)cursor < pin_length - 1) cursor++;
        }

        if (k & HidNpadButton_B) {
            // User chose to exit
            return false;
        }

        if (k & HidNpadButton_A) {
            // Verify PIN
            if (strncmp(input, expected_pin, pin_length) == 0) {
                // Correct!
                consoleClear();
                printf("\n");
                printSeparator();
                printf("   PIN Accepted!\n");
                printSeparator();
                printf("\n");
                consoleFlush();
                svcSleepThread(500000000ULL);  // 0.5 sec
                return true;
            } else {
                // Wrong PIN
                attempts++;
                consoleClear();
                printf("\n");
                printSeparator();
                printf("   WRONG PIN!\n");
                printSeparator();
                printf("\n");
                printf("   Attempt %d / %d\n", attempts, max_attempts);
                consoleFlush();
                svcSleepThread(1000000000ULL);  // 1 sec

                if (attempts >= max_attempts) {
                    consoleClear();
                    printf("\n");
                    printSeparator();
                    printf("   Too many wrong attempts!\n");
                    printSeparator();
                    printf("\n");
                    printf("   Exiting for security.\n");
                    consoleFlush();
                    svcSleepThread(2000000000ULL);  // 2 sec
                    return false;
                }

                // Reset input
                memset(input, '0', pin_length);
                cursor = 0;
            }
        }

        svcSleepThread(50000000ULL);
    }

    return false;
}

// ---- Menu Screens ----

static void showStatus(void)
{
    consoleClear();
    printf("\n");
    printSeparator();
    printf("   Current Status\n");
    printSeparator();
    printf("\n");
    consoleFlush();

    PctlStatus st;
    pctl_status_fetch(&st);

    if (st.safety_level_ok)
        printf("   Safety Level:  %s (%u)\n", safety_level_name(st.safety_level), st.safety_level);
    else
        printf("   Safety Level:  (unavailable)\n");

    if (st.pin_length_ok)
        printf("   PIN Length:    %u %s\n", st.pin_length, st.pin_length > 0 ? "(set)" : "(not set)");
    else
        printf("   PIN Length:    (unavailable)\n");

    if (st.restriction_enabled_ok)
        printf("   Restriction:   %s\n", st.restriction_enabled ? "Enabled" : "Disabled");
    else
        printf("   Restriction:   (unavailable)\n");

    printf("\n");
    consoleFlush();

    PtState pt;
    pctl_play_timer_query(&pt);

    printf("   Play Timer:\n");
    printf("   Enabled:       %s\n", pt.enabled ? "Yes" : "No");
    printf("   Today Limited: %s\n", pt.restricted ? "Yes (time up today)" : "No");

    if (pt.remaining_ns > 0) {
        u64 rem_min = pt.remaining_ns / 60000000000ULL;
        printf("   Remaining:     %llu min\n", (unsigned long long)rem_min);
    }

    printf("\n   Daily Time Limits (minutes):\n");
    for (int i = 0; i < 7; i++) {
        if (pt.day_min[i] == PT_DAY_NOLIMIT)
            printf("     %s: No limit\n", day_names[i]);
        else
            printf("     %s: %u min (%uh %um)\n", day_names[i],
                   pt.day_min[i], pt.day_min[i] / 60, pt.day_min[i] % 60);
    }

    if (!pt.valid)
        printf("\n   (Could not read timer settings)\n");

    waitForKey();
}

static void menuSetPin(void)
{
    consoleClear();
    printf("\n");
    printSeparator();
    printf("   Set / Change PIN\n");
    printSeparator();
    printf("\n");
    printf("   Will open the system PIN setup screen.\n");
    printf("   You can set a new PIN or change the\n");
    printf("   existing one.\n\n");
    consoleFlush();

    Result rc = pctl_set_pin();
    consoleClear();
    printf("\n");
    if (R_SUCCEEDED(rc))
        printf("   PIN set successfully!\n");
    else
        printf("   Failed: 0x%08X\n", (unsigned)rc);

    waitForKey();
}

static void menuUnlockTemporarily(void)
{
    consoleClear();
    printf("\n");
    printSeparator();
    printf("   Unlock Temporarily\n");
    printSeparator();
    printf("\n");
    printf("   Auto-reads current PIN and temporarily\n");
    printf("   lifts parental control restrictions.\n\n");
    consoleFlush();

    Result rc = pctl_unlock_restriction_temporarily();
    if (R_SUCCEEDED(rc))
        printf("   Unlocked successfully!\n");
    else
        printf("   Unlock failed: 0x%08X\n", (unsigned)rc);

    waitForKey();
}

static void menuSetPlayTimer(void)
{
    u16 days[7];
    // Read current values
    PtState pt;
    pctl_play_timer_query(&pt);
    for (int i = 0; i < 7; i++) days[i] = pt.day_min[i];

    int cursor = 0;  // 0..6 = per-day, 7 = apply, 8 = cancel
    bool editing_value = false;
    u16 edit_val = 0;
    bool done = false;

    while (appletMainLoop() && !done) {
        u64 k = padGetDown();

        consoleClear();
        printf("\n");
        printSeparator();
        printf("   Set Weekly Play Time\n");
        printSeparator();
        printf("\n");
        printf("   Daily play time limits (minutes)\n");
        printf("   0=blocked all day  65535=no limit\n\n");

        for (int i = 0; i < 7; i++) {
            bool sel = (!editing_value && cursor == i);
            if (editing_value && cursor == i) {
                printf("   %s %s [%u min]  <- editing\n",
                       sel ? ">" : " ",
                       day_names[i], edit_val);
                printf("     Up/Down: +/-1  L/R: +/-10\n");
                printf("     A=Confirm  B=Cancel\n");
            } else {
                if (days[i] == PT_DAY_NOLIMIT)
                    printf("   %s %s  No limit\n",
                           sel ? ">" : " ", day_names[i]);
                else
                    printf("   %s %s  %u min (%uh %um)\n",
                           sel ? ">" : " ", day_names[i],
                           days[i], days[i] / 60, days[i] % 60);
            }
        }

        printf("\n");
        printf("   %s [ Apply ]\n", (!editing_value && cursor == 7) ? ">" : " ");
        printf("   %s [ Cancel ]\n", (!editing_value && cursor == 8) ? ">" : " ");
        printf("\n");
        printf("   A=Select/Edit  B=Back  X=Set all 15min\n");
        consoleFlush();

        if (editing_value) {
            if (k & HidNpadButton_Up)    { if (edit_val < 65535) edit_val += 1; }
            if (k & HidNpadButton_Down)  { if (edit_val > 0) edit_val -= 1; }
            if (k & HidNpadButton_Right) { if (edit_val <= 65525) edit_val += 10; }
            if (k & HidNpadButton_Left)  { if (edit_val >= 10) edit_val -= 10; else edit_val = 0; }
            if (k & HidNpadButton_R)     { edit_val = PT_DAY_NOLIMIT; }  // R = no limit
            if (k & HidNpadButton_L)     { edit_val = 0; }               // L = blocked
            if (k & HidNpadButton_A)     { days[cursor] = edit_val; editing_value = false; }
            if (k & HidNpadButton_B)     { editing_value = false; }
        } else {
            if (k & HidNpadButton_Up)   { if (cursor > 0) cursor--; }
            if (k & HidNpadButton_Down) { if (cursor < 8) cursor++; }
            if (k & HidNpadButton_A) {
                if (cursor <= 6) {
                    editing_value = true;
                    edit_val = days[cursor];
                } else if (cursor == 7) {
                    // Apply
                    Result rc = pctl_play_timer_set_days(days);
                    consoleClear();
                    printf("\n");
                    if (R_SUCCEEDED(rc))
                        printf("   Play time set successfully!\n");
                    else
                        printf("   Failed: 0x%08X\n", (unsigned)rc);
                    waitForKey();
                    done = true;
                } else {
                    done = true;
                }
            }
            if (k & HidNpadButton_B) done = true;
            if (k & HidNpadButton_X) {
                // Quick set: all 15 minutes
                for (int i = 0; i < 7; i++) days[i] = 15;
            }
        }

        svcSleepThread(50000000ULL);
    }
}

static void menuSetUniformTimer(void)
{
    u16 minutes = 15;
    bool done = false;

    while (appletMainLoop() && !done) {
        u64 k = padGetDown();

        consoleClear();
        printf("\n");
        printSeparator();
        printf("   Set Uniform Daily Time\n");
        printSeparator();
        printf("\n");
        printf("   Set the same play time limit for\n");
        printf("   every day of the week.\n\n");
        if (minutes == PT_DAY_NOLIMIT)
            printf("   Play time: [ No limit ]\n\n");
        else if (minutes == 0)
            printf("   Play time: [ 0 min (blocked) ]\n\n");
        else
            printf("   Play time: [ %u min ] (%uh %um)\n\n",
                   minutes, minutes / 60, minutes % 60);
        printf("   Up/Down:  +/- 1 min\n");
        printf("   Left/Right: +/- 10 min\n");
        printf("   L: Blocked (0 min)\n");
        printf("   R: No limit\n\n");
        printf("   A : Apply\n");
        printf("   B : Cancel\n");
        consoleFlush();

        if (k & HidNpadButton_Up)    { if (minutes < 65535) minutes += 1; if (minutes == PT_DAY_NOLIMIT) minutes = 65534; }
        if (k & HidNpadButton_Down)  { if (minutes > 0) minutes -= 1; }
        if (k & HidNpadButton_Right) { if (minutes <= 65525) minutes += 10; }
        if (k & HidNpadButton_Left)  { if (minutes >= 10) minutes -= 10; else minutes = 0; }
        if (k & HidNpadButton_L)     { minutes = 0; }
        if (k & HidNpadButton_R)     { minutes = PT_DAY_NOLIMIT; }

        if (k & HidNpadButton_A) {
            Result rc = pctl_play_timer_set_uniform(minutes);
            consoleClear();
            printf("\n");
            if (R_SUCCEEDED(rc)) {
                if (minutes == PT_DAY_NOLIMIT)
                    printf("   Time limit cleared (no limit)!\n");
                else if (minutes == 0)
                    printf("   Blocked all day!\n");
                else
                    printf("   Set %u min/day successfully!\n", minutes);
            } else {
                printf("   Failed: 0x%08X\n", (unsigned)rc);
            }
            waitForKey();
            done = true;
        }
        if (k & HidNpadButton_B) done = true;

        svcSleepThread(50000000ULL);
    }
}

static void menuDeleteParentalControls(void)
{
    consoleClear();
    printf("\n");
    printSeparator();
    printf("   !!! DELETE Parental Controls !!!\n");
    printSeparator();
    printf("\n");
    printf("   WARNING: This cannot be undone!\n");
    printf("   Will delete PIN and all restriction\n");
    printf("   settings.\n\n");
    printf("   Press A to confirm, B to cancel.\n");
    consoleFlush();

    while (appletMainLoop()) {
        u64 k = padGetDown();
        if (k & HidNpadButton_A) {
            Result rc = pctl_delete_parental_controls();
            consoleClear();
            printf("\n");
            if (R_SUCCEEDED(rc))
                printf("   Parental controls deleted!\n");
            else
                printf("   Delete failed: 0x%08X\n", (unsigned)rc);
            waitForKey();
            break;
        }
        if (k & HidNpadButton_B) break;
        consoleFlush();
        svcSleepThread(10000000ULL);
    }
}

static void menuDeletePairing(void)
{
    consoleClear();
    printf("\n");
    printSeparator();
    printf("   Delete Phone Pairing\n");
    printSeparator();
    printf("\n");
    printf("   Unlink the Nintendo Switch Parental\n");
    printf("   Controls smartphone app from this\n");
    printf("   console.\n\n");
    printf("   Press A to confirm, B to cancel.\n");
    consoleFlush();

    while (appletMainLoop()) {
        u64 k = padGetDown();
        if (k & HidNpadButton_A) {
            Result rc = pctl_delete_pairing();
            consoleClear();
            printf("\n");
            if (R_SUCCEEDED(rc))
                printf("   Phone pairing deleted!\n");
            else
                printf("   Delete failed: 0x%08X\n", (unsigned)rc);
            waitForKey();
            break;
        }
        if (k & HidNpadButton_B) break;
        consoleFlush();
        svcSleepThread(10000000ULL);
    }
}

static void menuClearPlayTimer(void)
{
    consoleClear();
    printf("\n");
    printSeparator();
    printf("   Clear Play Time Limits\n");
    printSeparator();
    printf("\n");
    printf("   Remove daily play time limits.\n");
    printf("   The timer will be disabled.\n\n");
    printf("   Press A to confirm, B to cancel.\n");
    consoleFlush();

    while (appletMainLoop()) {
        u64 k = padGetDown();
        if (k & HidNpadButton_A) {
            Result rc = pctl_play_timer_clear();
            consoleClear();
            printf("\n");
            if (R_SUCCEEDED(rc))
                printf("   Play time limits cleared!\n");
            else
                printf("   Clear failed: 0x%08X\n", (unsigned)rc);
            waitForKey();
            break;
        }
        if (k & HidNpadButton_B) break;
        consoleFlush();
        svcSleepThread(10000000ULL);
    }
}

// ---- Main Menu ----

int main(int argc, char **argv)
{
    consoleInit(NULL);

    // Initialize network (NRO uses Default initializer)
    socketInitializeDefault();

    // Start HTTP server (Web UI on port 8081)
    http_server_start();

    // Show splash immediately
    consoleClear();
    printf("\n");
    printSeparator();
    printf("   Switch Parental Control Manager\n");
    printf("   v11.5 - fw 22.1.0 compatible\n");
    printSeparator();
    printf("\n");
    printf("   Initializing...\n");
    consoleFlush();

    // Initialize pad
    initPad();

    // Initialize pctl service
    Result pctl_rc = pctlInitialize();

    if (R_FAILED(pctl_rc)) {
        printf("\n   !! pctl init failed: 0x%08X\n", (unsigned)pctl_rc);
        printf("   !! Make sure you are running under\n");
        printf("   !! CFW (Atmosphere). Some features\n");
        printf("   !! may not work.\n\n");
        consoleFlush();
    } else {
        printf("   pctl service initialized OK.\n\n");
        consoleFlush();

        // ---- PIN Verification Gate ----
        // Try to read the system PIN
        char system_pin[32] = {0};
        u32 system_pin_len = 0;
        bool has_pin = false;

        if (R_SUCCEEDED(pctl_rc)) {
            has_pin = pctl_read_pin(system_pin, sizeof(system_pin), &system_pin_len);
        }

        if (has_pin && system_pin_len > 0) {
            // System PIN exists — require user to enter it
        if (!pinEntryScreen(system_pin, system_pin_len)) {
            // Wrong PIN or user chose to exit
            http_server_stop();
            if (R_SUCCEEDED(pctl_rc)) pctlExit();
            socketExit();
            consoleExit(NULL);
            return 0;
        }
        } else {
            // No system PIN set — use default fallback password
            consoleClear();
            printf("\n");
            printSeparator();
            printf("   Fallback Password\n");
            printSeparator();
            printf("\n");
            printf("   No system PIN is set.\n");
            printf("   Using default password for access.\n\n");
            consoleFlush();
            svcSleepThread(1500000000ULL);  // 1.5 sec

            if (!pinEntryScreen(DEFAULT_PIN, strlen(DEFAULT_PIN))) {
                http_server_stop();
                if (R_SUCCEEDED(pctl_rc)) pctlExit();
                socketExit();
                consoleExit(NULL);
                return 0;
            }
        }
    }

    int cursor = 0;
    const int menu_count = 8;
    const char *menu_items[] = {
        "View Current Status",
        "Set / Change PIN",
        "Unlock Temporarily",
        "Set Weekly Play Time (per day)",
        "Set Uniform Daily Time",
        "Clear Play Time Limits",
        "Delete Parental Controls",
        "Delete Phone Pairing",
    };

    // Main loop
    while (appletMainLoop()) {
        u64 k = padGetDown();

        consoleClear();
        printf("\n");
        printSeparator();
        printf("   Switch Parental Control Manager\n");
        printf("   v11.5 - fw 22.1.0 compatible\n");
        printSeparator();
        printf("\n");

        if (R_FAILED(pctl_rc)) {
            printf("   !! pctl init failed: 0x%08X\n", (unsigned)pctl_rc);
            printf("   !! Make sure CFW is active\n\n");
        }

        for (int i = 0; i < menu_count; i++) {
            printf("   %s %s\n", (cursor == i) ? ">" : " ", menu_items[i]);
        }

        printf("\n");
        printf("   Up/Down: Navigate   A: Select   B: Exit\n");

        consoleFlush();

        if (k & HidNpadButton_Up)   { if (cursor > 0) cursor--; }
        if (k & HidNpadButton_Down) { if (cursor < menu_count - 1) cursor++; }
        if (k & HidNpadButton_B) break;

        if (k & HidNpadButton_A) {
            if (R_FAILED(pctl_rc) && cursor != 0) {
                continue;
            }

            switch (cursor) {
                case 0: showStatus(); break;
                case 1: menuSetPin(); break;
                case 2: menuUnlockTemporarily(); break;
                case 3: menuSetPlayTimer(); break;
                case 4: menuSetUniformTimer(); break;
                case 5: menuClearPlayTimer(); break;
                case 6: menuDeleteParentalControls(); break;
                case 7: menuDeletePairing(); break;
            }
        }

        svcSleepThread(50000000ULL);
    }

    http_server_stop();
    if (R_SUCCEEDED(pctl_rc)) pctlExit();
    socketExit();
    consoleExit(NULL);
    return 0;
}
