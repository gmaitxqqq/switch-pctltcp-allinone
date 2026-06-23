/**
 * http_server.c - Minimal HTTP server for Switch parental control (NRO version)
 *
 * REST API:
 *   GET  /              -> Embedded HTML UI (中文)
 *   GET  /api/status    -> JSON: {daily_limit_min, remaining_min, played_min,
 *                                today, today_name, restriction_enabled, version}
 *   POST /api/allow     -> Add minutes to today's limit (additive)
 *                          body: minutes=N
 *   POST /api/toggle    -> Toggle restriction on/off
 *
 * NRO version: uses libnx Thread API (NOT pthread) for compatibility.
 */

#include "http_server.h"
#include "pctl_handler.h"

/* Forward declaration */
Result pctl_set_restriction_enabled(bool enable);

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
static volatile int  s_server_fd    = -1;
static volatile bool s_running      = false;
static Thread       s_thread;
static volatile bool s_thread_created = false;

/* ------------------------------------------------------------------ */
/* HTTP helpers                                                        */
/* ------------------------------------------------------------------ */
static void http_send(int fd, const char *status, const char *ctype, const char *body)
{
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        status, ctype, (int)strlen(body));
    write(fd, header, hlen);
    write(fd, body, strlen(body));
}

static int http_read_request(int fd, char *buf, int bufsize)
{
    int total = 0;
    while (total < bufsize - 1) {
        int n = read(fd, buf + total, bufsize - 1 - total);
        if (n <= 0) break;
        total += n;
        buf[total] = 0;
        if (strstr(buf, "\r\n\r\n")) break;
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* API handlers                                                        */
/* ------------------------------------------------------------------ */
static void api_status(int fd)
{
    u64 remaining_ns = 0;
    u32 daily_limit  = 0;
    u32 remaining_min = 0;
    u32 played_min    = 0;
    int today = 0;
    bool restriction_enabled = false;

    /* Main thread already initialized pctl; just call functions directly.
       Each function is protected by mutex internally. */
    if (pctl_is_initialized()) {
        pctl_get_remaining_time(&remaining_ns);
        pctl_get_daily_limit_minutes(&daily_limit);
        remaining_min = (remaining_ns == 0) ? 0u :
                      (remaining_ns > 86400000000000ULL) ? 0u :
                      (u32)(remaining_ns / 60000000000ULL);
        played_min    = (daily_limit > remaining_min) ? (daily_limit - remaining_min) : 0;
        today = pctl_get_today_day();
        pctl_get_restriction_enabled(&restriction_enabled);
    }

    char json[512];
    static const char *day_names[] = {"周日","周一","周二","周三","周四","周五","周六"};
    snprintf(json, sizeof(json),
        "{\"daily_limit_min\":%u,\"remaining_min\":%u,\"played_min\":%u,\"today\":%d,\"today_name\":\"%s\",\"restriction_enabled\":%s,\"version\":\"v11.7\"}",
        daily_limit, remaining_min, played_min, today,
        (today >= 0 && today < 7) ? day_names[today] : "Unknown",
        restriction_enabled ? "true" : "false");

    http_send(fd, "200 OK", "application/json", json);
}

static void api_allow(int fd, const char *body)
{
    int allow_min = 0;
    const char *p = strstr(body, "minutes=");
    if (p) {
        allow_min = atoi(p + 8);
    }

    if (!pctl_is_initialized()) {
        http_send(fd, "200 OK", "application/json", "{\"success\":0,\"error\":\"pctl_not_init\"}");
        return;
    }

    int today = pctl_get_today_day();
    Result rc = 0;

    if (allow_min == 0) {
        rc = pctl_set_day_limit_minutes(today, 0);
    } else {
        u32 daily_limit = 0;
        pctl_get_daily_limit_minutes(&daily_limit);

        int new_limit = (int)daily_limit + allow_min;
        if (new_limit < 0) new_limit = 0;
        if (new_limit > 1440) new_limit = 1440;

        rc = pctl_set_day_limit_minutes(today, (u32)new_limit);
        if (R_SUCCEEDED(rc)) {
            pctl_stop_play_timer();
            pctl_start_play_timer();
        }
    }

    char json[128];
    snprintf(json, sizeof(json), "{\"success\":%d}", R_SUCCEEDED(rc) ? 1 : 0);
    http_send(fd, "200 OK", "application/json", json);
}

static void api_toggle_restriction(int fd)
{
    if (!pctl_is_initialized()) {
        http_send(fd, "200 OK", "application/json",
                  "{\"success\":0,\"error\":\"pctl_not_init\"}");
        return;
    }

    bool enabled = false;
    pctl_get_restriction_enabled(&enabled);

    Result rc = pctl_set_restriction_enabled(!enabled);

    /* Read back actual state after setting */
    bool new_enabled = enabled;
    if (R_SUCCEEDED(rc)) {
        pctl_get_restriction_enabled(&new_enabled);
    }

    char json[128];
    snprintf(json, sizeof(json),
             "{\"success\":%d,\"enabled\":%s}",
             R_SUCCEEDED(rc) ? 1 : 0,
             new_enabled ? "true" : "false");
    http_send(fd, "200 OK", "application/json", json);
}

/* Embedded Web UI (中文) */
static const char *WEB_HTML =
"<!DOCTYPE html>"
"<html>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Switch 家长控制 v11.7</title>"
"<style>"
"body{font-family:sans-serif;background:#1a1a2e;color:#fff;text-align:center;padding:20px;margin:0}"
".box{background:rgba(255,255,255,0.1);border-radius:12px;padding:20px;margin:15px 0}"
".big{font-size:2.5em;font-weight:bold;margin:10px 0}"
".lbl{color:rgba(255,255,255,0.6);font-size:0.9em}"
".row{display:flex;gap:10px;justify-content:center;margin:15px 0}"
".tile{flex:1;background:rgba(255,255,255,0.08);border-radius:10px;padding:14px}"
"input{width:90px;font-size:1.5em;text-align:center;padding:8px;border:none;border-radius:8px;background:rgba(255,255,255,0.15);color:#fff}"
".btns{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;margin:12px 0}"
"button{font-size:1em;padding:10px 18px;border:none;border-radius:8px;background:#3b82f6;color:#fff;cursor:pointer}"
"button:active{transform:scale(0.95)}"
".btn-sm{background:#374151;font-size:0.9em;padding:8px 14px}"
".btn-minus{background:#7f1d1d;font-size:0.9em;padding:8px 14px}"
"#msg{margin-top:8px;color:#fbbf24;font-size:0.9em;min-height:20px}"
".badge{display:inline-block;background:#10b981;color:#fff;font-size:0.7em;padding:2px 8px;border-radius:10px;margin-left:8px}"
".switch{position:relative;display:inline-block;width:50px;height:26px;margin:10px auto;vertical-align:middle}"
".switch input{opacity:0;width:0;height:0}"
".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#374151;border-radius:26px;transition:.3s}"
".slider:before{position:absolute;content:'';height:20px;width:20px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.3s}"
"input:checked+.slider{background:#10b981}"
"input:checked+.slider:before{transform:translateX(24px)}"
"</style>"
"</head>"
"<body>"
"<h2>Switch 家长控制 <small>v11.7</small> <span class='badge'>LAN</span></h2>"
"<div class='box'>"
"<div class='row'>"
"<div class='tile'><div class='lbl'>已玩时间</div><div class='big' id='played'>--</div></div>"
"<div class='tile'><div class='lbl'>剩余时间</div><div class='big' id='remain'>--</div></div>"
"</div>"
"<div class='lbl' style='margin-top:4px'>今日限制: <span id='limit'>--</span> 分钟</div>"
"</div>"
"<div class='box'>"
"<div style='display:flex;align-items:center;justify-content:center;gap:12px'>"
"<span class='lbl'>家长控制</span>"
"<label class='switch'>"
"<input type='checkbox' id='toggleSw' onchange='toggleRestriction()'>"
"<span class='slider'></span>"
"</label>"
"<span id='toggleLabel' class='lbl'>已禁用</span>"
"</div>"
"</div>"
"<div class='box'>"
"<div class='lbl'>追加/减少时间 (分钟)</div>"
"<input type='number' id='min' value='30' min='-1440' max='1440'>"
"<br>"
"<div class='btns'>"
"<button class='btn-minus' onclick='quickSet(-30)'>-30</button>"
"<button class='btn-minus' onclick='quickSet(-10)'>-10</button>"
"<button class='btn-sm' onclick='quickSet(15)'>+15</button>"
"<button class='btn-sm' onclick='quickSet(30)'>+30</button>"
"<button class='btn-sm' onclick='quickSet(60)'>+60</button>"
"<button class='btn-sm' onclick='quickSet(90)'>+90</button>"
"</div>"
"<button onclick='allow()'>确认</button>"
"<div id='msg'></div>"
"</div>"
"<script>"
"function load(){"
"fetch('/api/status').then(r=>r.json()).then(d=>{"
"document.getElementById('limit').textContent=d.daily_limit_min;"
"document.getElementById('remain').textContent=d.remaining_min+'m';"
"document.getElementById('played').textContent=d.played_min+'m';"
"document.getElementById('toggleSw').checked=d.restriction_enabled;"
"document.getElementById('toggleLabel').textContent=d.restriction_enabled?'已启用':'已禁用';"
"}).catch(()=>{document.getElementById('msg').textContent='加载失败'});"
"}"
"function quickSet(m){document.getElementById('min').value=m;}"
"function allow(){"
"var m=parseInt(document.getElementById('min').value)||0;"
"document.getElementById('msg').textContent='保存中...';"
"fetch('/api/allow',{method:'POST',body:'minutes='+m}).then(r=>r.json()).then(d=>{"
"document.getElementById('msg').textContent=d.success?'完成!':'失败';"
"setTimeout(function(){document.getElementById('msg').textContent='';load();},1200);"
"}).catch(()=>{document.getElementById('msg').textContent='错误'});"
"}"
"function toggleRestriction(){"
"document.getElementById('msg').textContent='保存中...';"
"fetch('/api/toggle',{method:'POST'}).then(r=>r.json()).then(d=>{"
"document.getElementById('toggleSw').checked=d.enabled;"
"document.getElementById('toggleLabel').textContent=d.enabled?'已启用':'已禁用';"
"document.getElementById('msg').textContent=d.success?'完成!':'失败';"
"setTimeout(function(){document.getElementById('msg').textContent='';},1200);"
"}).catch(()=>{document.getElementById('msg').textContent='错误'});"
"}"
"load();setInterval(load,30000);"
"</script>"
"</body>"
"</html>";

/* ------------------------------------------------------------------ */
/* Route dispatcher                                                    */
/* ------------------------------------------------------------------ */
static void handle_request(int fd)
{
    char buf[2048];
    int n = http_read_request(fd, buf, sizeof(buf));
    if (n <= 0) { close(fd); return; }

    char method[16] = {0}, path[256] = {0};
    sscanf(buf, "%15s %255s", method, path);

    if (strcmp(method, "OPTIONS") == 0) {
        http_send(fd, "204 No Content", "text/plain", "");
        close(fd);
        return;
    }

    char *body = strstr(buf, "\r\n\r\n");
    if (body) body += 4;

    if (strcmp(path, "/") == 0 && strcmp(method, "GET") == 0) {
        http_send(fd, "200 OK", "text/html", WEB_HTML);
    } else if (strcmp(path, "/api/status") == 0) {
        api_status(fd);
    } else if (strcmp(path, "/api/allow") == 0 && strcmp(method, "POST") == 0) {
        api_allow(fd, body ? body : "");
    } else if (strcmp(path, "/api/toggle") == 0 && strcmp(method, "POST") == 0) {
        api_toggle_restriction(fd);
    } else {
        http_send(fd, "404 Not Found", "application/json", "{\"error\":\"not found\"}");
    }

    close(fd);
}

/* ------------------------------------------------------------------ */
/* Server thread (uses libnx Thread API, NOT pthread)                   */
/* ------------------------------------------------------------------ */

/* Large stack for the HTTP thread to handle request processing */
#define HTTP_THREAD_STACK_SIZE  0x40000  /* 256 KB */

static void http_thread_func(void *arg)
{
    (void)arg;

    while (s_running) {
        int fd = s_server_fd;
        if (fd < 0) {
            svcSleepThread(200000000ULL);
            continue;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 200000;  /* 200ms - check s_running frequently */

        int ret = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (!s_running) break;  /* check right after select returns */
        if (ret <= 0) {
            if (s_server_fd != fd) continue;
            continue;
        }

        if (FD_ISSET(fd, &rfds)) {
            if (s_server_fd != fd) continue;

            int client_fd = accept(fd, NULL, NULL);
            if (client_fd < 0) continue;

            struct timeval tmo;
            tmo.tv_sec  = 3;
            tmo.tv_usec = 0;
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));
            setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tmo, sizeof(tmo));

            handle_request(client_fd);
        }
    }

    /* Thread is exiting — do NOT call threadClose here, main will do it */
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

static int create_server_socket(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 4) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

void http_server_start(void)
{
    if (s_running) {
        if (s_server_fd < 0) {
            int fd = create_server_socket();
            if (fd >= 0) s_server_fd = fd;
        }
        return;
    }

    int fd = create_server_socket();
    if (fd < 0) return;

    s_server_fd      = fd;
    s_running        = true;
    s_thread_created = false;

    Result rc = threadCreate(&s_thread, http_thread_func, NULL, NULL,
                             HTTP_THREAD_STACK_SIZE, 0x2C, -2);
    if (R_FAILED(rc)) {
        close(fd);
        s_server_fd = -1;
        s_running   = false;
        return;
    }
    s_thread_created = true;

    rc = threadStart(&s_thread);
    if (R_FAILED(rc)) {
        threadClose(&s_thread);
        s_thread_created = false;
        close(fd);
        s_server_fd = -1;
        s_running   = false;
    }
}

void http_server_stop(void)
{
    s_running = false;

    /* Close server socket to unblock select() */
    if (s_server_fd >= 0) {
        int fd = s_server_fd;
        s_server_fd = -1;
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }

    /* Wait for thread to actually exit, then close it.
       This is REQUIRED — not closing leaks kernel resources and crashes on NRO exit. */
    if (s_thread_created) {
        threadWaitForExit(&s_thread);
        threadClose(&s_thread);
        s_thread_created = false;
    }
}

bool http_server_is_running(void)
{
    return s_running && s_server_fd >= 0;
}
