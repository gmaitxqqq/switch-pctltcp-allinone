/**
 * http_server.c - HTTP server for NRO (embedded web UI)
 *
 * Based on switch-pctltcp-remote/http_server.c (working sysmodule version)
 * Adapted for NRO context:
 *   - Call socketInitializeDefault() before using sockets
 *   - Call socketExit() when done
 *   - Use printf() for logging (console available in NRO)
 */

#include "http_server.h"
#include "pctl_handler.h"

#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

/* ---- Logging ---- */
static void log_msg(const char *msg)
{
    printf("[HTTP] %s\n", msg);
    consoleUpdate(NULL);
}

/* ---- State ---- */
static volatile int  s_server_fd = -1;
static volatile bool s_running    = false;
static pthread_t       s_thread;

/* ---- Static IP buffer ---- */
static char s_ip[64] = {0};

const char *http_server_get_ip(void)
{
    return s_ip[0] ? s_ip : "N/A";
}

/* ---- http_send ---- */
static void http_send(int fd, const char *status, const char *ctype, const char *body)
{
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s; charset=UTF-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, ctype, strlen(body));
    write(fd, header, hlen);
    write(fd, body, strlen(body));
}

/* ---- handle_request (stub - will add full UI later) ---- */
static void handle_request(int fd)
{
    char buf[4096] = {0};
    int n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) { close(fd); return; }

    char method[16] = {0}, path[256] = {0};
    sscanf(buf, "%15s %255s", method, path);

    if (strcmp(path, "/api/status") == 0) {
        /* Return JSON status */
        u64 remaining = 0;
        pctl_get_remaining_time(&remaining);
        u32 daily = 0;
        pctl_get_daily_limit_minutes(&daily);
        bool enabled = false, restricted = false;
        pctl_is_enabled(&enabled);
        pctl_is_restricted(&restricted);

        char json[512];
        snprintf(json, sizeof(json),
            "{\"enabled\":%s,\"restricted\":%s,\"remaining_min\":%llu,\"daily_min\":%u}",
            enabled ? "true" : "false",
            restricted ? "true" : "false",
            (unsigned long long)(remaining / 60000000000ULL),
            daily);
        http_send(fd, "200 OK", "application/json", json);
    } else {
        /* Serve simple HTML */
        const char *html =
            "<!DOCTYPE html>"
            "<html><head><meta charset='UTF-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Switch 家长控制</title>"
            "<style>"
            "*{box-sizing:border-box;margin:0;padding:0}"
            "body{font-family:sans-serif;background:#1a1a2e;color:#e0e0e0;min-height:100vh;padding:12px}"
            ".container{max-width:480px;margin:0 auto}"
            "h1{text-align:center;font-size:1.4em;padding:12px 0;color:#6ee06e}"
            ".card{background:#12122a;border-radius:12px;padding:14px;margin-bottom:12px}"
            ".status{display:grid;grid-template-columns:1fr 1fr;gap:8px}"
            ".status-item{background:#0f0f1a;border-radius:8px;padding:10px;text-align:center}"
            "</style></head><body>"
            "<div class='container'>"
            "<h1>🎮 Switch 家长控制</h1>"
            "<div class='card'><div class='status'>"
            "<div class='status-item'>Status: OK</div>"
            "<div class='status-item'>Port: 8000</div>"
            "</div></div>"
            "<p style='text-align:center;color:#888'>Web UI loading...</p>"
            "</div></body></html>";
        http_send(fd, "200 OK", "text/html", html);
    }

    close(fd);
}

/* ---- HTTP thread ---- */
static void *http_thread(void *arg)
{
    (void)arg;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { log_msg("socket() failed"); return NULL; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_msg("bind() failed");
        close(fd);
        return NULL;
    }

    if (listen(fd, 4) < 0) {
        log_msg("listen() failed");
        close(fd);
        return NULL;
    }

    s_server_fd = fd;
    log_msg("HTTP server started");

    while (s_running) {
        struct sockaddr_in client;
        socklen_t len = sizeof(client);
        int cfd = accept(fd, (struct sockaddr*)&client, &len);
        if (cfd < 0) continue;
        handle_request(cfd);
    }

    close(fd);
    s_server_fd = -1;
    return NULL;
}

/* ---- Public API ---- */
void http_server_start(void)
{
    s_running = true;
    socketInitializeDefault();
    pthread_create(&s_thread, NULL, http_thread, NULL);
    log_msg("HTTP server starting...");
}

void http_server_stop(void)
{
    s_running = false;
    pthread_join(s_thread, NULL);
    socketExit();
    log_msg("HTTP server stopped");
}

bool http_server_is_running(void)
{
    return s_running;
}
