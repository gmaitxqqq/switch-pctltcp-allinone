/**
 * http_server.c - Minimal stub for build testing
 */
#include "http_server.h"
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>

void http_server_start(void) { printf("HTTP stub
"); }
void http_server_stop(void) {}
bool http_server_is_running(void) { return false; }
const char *http_server_get_ip(void) { return "0.0.0.0"; }
