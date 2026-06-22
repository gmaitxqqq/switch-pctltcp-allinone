#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#ifndef HTTP_PORT
#define HTTP_PORT 8081
#endif

/* ------------------------------------------------------------------ */
/* Public API                                                        */
/* ------------------------------------------------------------------ */

void http_server_start(void);
void http_server_stop(void);
bool http_server_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_SERVER_H */
