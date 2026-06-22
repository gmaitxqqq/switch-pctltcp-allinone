/**
 * http_server.c - Minimal HTTP server for NRO
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

static volatile bool s_running = false;
static int s_server_fd = -1;
static pthread_t s_thread;

static void *http_thread(void *arg)
{
    (void)arg;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        consoleLog("HTTP: socket() failed");
        return NULL;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        consoleLog("HTTP: bind() failed");
        close(server_fd);
        return NULL;
    }
    
    if (listen(server_fd, 4) < 0) {
        consoleLog("HTTP: listen() failed");
        close(server_fd);
        return NULL;
    }
    
    consoleLog("HTTP server started on port 8000");
    
    while (s_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (s_running) consoleLog("HTTP: accept() failed");
            break;
        }
        
        /* Send a simple response */
        const char *response = 
            "HTTP/1.1 200 OK
"
            "Content-Type: text/html; charset=UTF-8
"
            "Connection: close
"
            "
"
            "<!DOCTYPE html>"
            "<html><head><meta charset='UTF-8'><title>Switch 家长控制</title></head>"
            "<body style='background:#1a1a2e;color:#e0e0e0;font-family:sans-serif;padding:20px;'>"
            "<h1 style='color:#6ee06e;'>🎮 Switch 家长控制管理</h1>"
            "<p>Web UI loaded successfully!</p>"
            "</body></html>";
        
        write(client_fd, response, strlen(response));
        close(client_fd);
    }
    
    close(server_fd);
    return NULL;
}

void http_server_start(void)
{
    s_running = true;
    socketInitializeDefault();
    pthread_create(&s_thread, NULL, http_thread, NULL);
}

void http_server_stop(void)
{
    s_running = false;
    pthread_join(s_thread, NULL);
    socketExit();
}

bool http_server_is_running(void)
{
    return s_running;
}

const char *http_server_get_ip(void)
{
    static char ip[64] = {0};
    
    /* Try getsockname on server_fd */
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (s_server_fd >= 0 && getsockname(s_server_fd, (struct sockaddr*)&addr, &len) == 0) {
        snprintf(ip, sizeof(ip), "%s", inet_ntoa(addr.sin_addr));
        return ip;
    }
    
    /* Fallback: try nifm */
    u32 ipaddr = 0;
    if (R_SUCCEEDED(nifmGetCurrentIpAddress(&ipaddr)) {
        struct in_addr a;
        a.s_addr = ipaddr;
        snprintf(ip, sizeof(ip), "%s", inet_ntoa(a));
        return ip;
    }
    
    return "N/A";
}
