#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../layer3/ipv4.h"

#define HTTP_SERVER_PORT 80
#define HTTP_BUFFER_SIZE 4096

typedef struct Host Host;

typedef struct HTTPServer {
    bool running;
    char root_dir[256];
    Host* bound_host;
    int listen_sockfd;
} HTTPServer;

// Initialize the HTTP server
void http_server_init(void);

// Start HTTP server on the given host (by IP)
// This sets up a listening TCP socket on port 80
int http_server_start(const IpAddress* ip, const char* root_dir);

// Stop HTTP server
int http_server_stop(void);

// Check if server is running
bool http_server_is_running(void);

// Handle an incoming HTTP request (plaintext)
// Parses the request and fills response buffer
int http_server_handle_request(const char* request, size_t req_len,
                                char* response, size_t* resp_len, size_t max_resp_len);

// Parse an HTTP request to extract method, path, version
int http_parse_request(const char* request, size_t req_len,
                        char* method, size_t method_size,
                        char* path, size_t path_size);

// Build an HTTP response
int http_build_response(int status_code, const char* status_text,
                         const char* content_type,
                         const char* body, size_t body_len,
                         char* response, size_t* resp_len, size_t max_resp_len);

// Serve a simple static page
int http_serve_default(const char* path, char* response, size_t* resp_len, size_t max_resp_len);

int http_server_attach_host(Host* host);
Host* http_server_get_bound_host(void);
int http_server_dispatch(Host* host);

#endif
