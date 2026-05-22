#include "http_server.h"
#include "magi_socket.h"
#include "../layer2/host.h"
#include "../layer4/tcp.h"
#include "../core/packet.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static HTTPServer g_server;

void http_server_init(void)
{
    g_server.running = false;
    g_server.root_dir[0] = '\0';
    g_server.bound_host = NULL;
    g_server.listen_sockfd = -1;
}

int http_server_start(const IpAddress* ip, const char* root_dir)
{
    if (ip == NULL) return 0;

    g_server.running = true;
    if (root_dir) {
        strncpy(g_server.root_dir, root_dir, sizeof(g_server.root_dir) - 1);
    } else {
        strncpy(g_server.root_dir, "/var/www", sizeof(g_server.root_dir) - 1);
    }
    g_server.root_dir[sizeof(g_server.root_dir) - 1] = '\0';

    printf("[HTTP] Server started on ");
    ip_to_string(ip, (char[32]){0}, 32, false);
    printf(":80 (root: %s)\n", g_server.root_dir);

    return 1;
}

int http_server_stop(void)
{
    if (!g_server.running) return 0;

    if (g_server.listen_sockfd >= 0) {
        magi_close(g_server.listen_sockfd);
        g_server.listen_sockfd = -1;
    }
    g_server.bound_host = NULL;
    g_server.running = false;
    printf("[HTTP] Server stopped\n");

    return 1;
}

bool http_server_is_running(void)
{
    return g_server.running;
}

int http_parse_request(const char* request, size_t req_len,
                        char* method, size_t method_size,
                        char* path, size_t path_size)
{
    size_t i;

    if (request == NULL || method == NULL || path == NULL) return 0;

    // Parse method (e.g., "GET")
    i = 0;
    while (i < req_len && request[i] != ' ' && i < method_size - 1) {
        method[i] = request[i];
        i++;
    }
    method[i] = '\0';
    if (i >= req_len || request[i] != ' ') return 0;
    i++; // Skip space

    // Parse path (e.g., "/index.html")
    size_t p = 0;
    while (i < req_len && request[i] != ' ' && p < path_size - 1) {
        path[p] = request[i];
        p++;
        i++;
    }
    path[p] = '\0';

    return p > 0 ? 1 : 0;
}

int http_build_response(int status_code, const char* status_text,
                         const char* content_type,
                         const char* body, size_t body_len,
                         char* response, size_t* resp_len, size_t max_resp_len)
{
    int written;

    if (response == NULL || resp_len == NULL || status_text == NULL) return 0;
    if (content_type == NULL) content_type = "text/html";
    if (body == NULL) { body = ""; body_len = 0; }

    written = snprintf(response, max_resp_len,
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Type: %s\r\n"
                       "Content-Length: %zu\r\n"
                       "Server: MagiSystem/1.0\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       status_code, status_text,
                       content_type,
                       body_len);

    if (written < 0 || (size_t)written >= max_resp_len) return 0;

    *resp_len = (size_t)written;

    // Append body
    if (body_len > 0 && *resp_len + body_len < max_resp_len) {
        memcpy(response + *resp_len, body, body_len);
        *resp_len += body_len;
    }

    return 1;
}

int http_serve_default(const char* path, char* response, size_t* resp_len, size_t max_resp_len)
{
    if (response == NULL || resp_len == NULL) return 0;

    // Ignore the path for now - serve the same default page
    (void)path;

    const char* actual_body =
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "  <title>Magi System HTTP Server</title>\n"
        "  <style>\n"
        "    body { font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 0; "
        "           background: linear-gradient(135deg, #0a0a2e 0%, #1a1a4e 50%, #0a0a2e 100%);\n"
        "           color: #e0e0ff; min-height: 100vh; display: flex; justify-content: center; "
        "           align-items: center; }\n"
        "    .container { text-align: center; padding: 40px; border: 1px solid #4a4a8a; "
        "                 border-radius: 15px; background: rgba(10, 10, 46, 0.8); "
        "                 box-shadow: 0 0 40px rgba(74, 74, 138, 0.3); max-width: 600px; }\n"
        "    h1 { color: #7f7fff; font-size: 2.5em; margin-bottom: 10px; "
        "         text-shadow: 0 0 20px rgba(127, 127, 255, 0.5); }\n"
        "    p { font-size: 1.1em; line-height: 1.6; color: #b0b0d0; }\n"
        "    .status { color: #00ff88; font-size: 1.2em; margin: 20px 0; }\n"
        "    .footer { margin-top: 30px; font-size: 0.85em; color: #6a6a8a; }\n"
        "    .badge { display: inline-block; padding: 5px 15px; border-radius: 20px; "
        "             background: rgba(0, 255, 136, 0.1); border: 1px solid #00ff88; "
        "             color: #00ff88; font-size: 0.9em; margin: 10px 0; }\n"
        "  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <div class=\"container\">\n"
        "    <h1>Welcome to Magi System!</h1>\n"
        "    <div class=\"badge\">HTTP/1.1 200 OK</div>\n"
        "    <p>This is the default page served by the Magi System HTTP Server.</p>\n"
        "    <div class=\"status\">&#10003; Connection Established</div>\n"
        "    <div class=\"footer\">Magi System v1.0 | NERV Simulation Framework</div>\n"
        "  </div>\n"
        "</body>\n"
        "</html>\n";

    size_t body_len = strlen(actual_body);
    return http_build_response(200, "OK", "text/html; charset=utf-8",
                                actual_body, body_len,
                                response, resp_len, max_resp_len);
}

int http_server_handle_request(const char* request, size_t req_len,
                                char* response, size_t* resp_len, size_t max_resp_len)
{
    char method[16];
    char path[512];

    if (request == NULL || response == NULL || resp_len == NULL) return 0;

    // Parse the request
    if (!http_parse_request(request, req_len, method, sizeof(method), path, sizeof(path))) {
        // Bad request
        return http_build_response(400, "Bad Request", "text/plain",
                                    "Bad Request", 11,
                                    response, resp_len, max_resp_len);
    }

    printf("[HTTP] %s %s\n", method, path);

    if (strcmp(method, "GET") == 0) {
        return http_serve_default(path, response, resp_len, max_resp_len);
    }

    if (strcmp(method, "HEAD") == 0) {
        return http_build_response(200, "OK", "text/html",
                                    NULL, 0,
                                    response, resp_len, max_resp_len);
    }

    // Method not allowed
    return http_build_response(405, "Method Not Allowed", "text/plain",
                                "Method Not Allowed", 18,
                                response, resp_len, max_resp_len);
}

int http_server_attach_host(Host* host)
{
    if (host == NULL || !g_server.running) return 0;

    int sockfd = magi_socket(AF_INET, SOCK_STREAM);
    if (sockfd < 0) return 0;

    if (magi_socket_attach_host(sockfd, host) < 0) {
        magi_close(sockfd);
        return 0;
    }
    if (magi_bind(sockfd, &host->ip_address, HTTP_SERVER_PORT) < 0) {
        magi_close(sockfd);
        return 0;
    }
    if (magi_listen(sockfd, 5) < 0) {
        magi_close(sockfd);
        return 0;
    }

    g_server.bound_host = host;
    g_server.listen_sockfd = sockfd;
    return 1;
}

Host* http_server_get_bound_host(void)
{
    return g_server.bound_host;
}

static int http_server_send_tcp(Host* host, TCPSocket* sock, uint8_t flags,
                                 const uint8_t* payload, size_t payload_len)
{
    if (host == NULL || sock == NULL) return 0;

    TCPSegment seg;
    uint8_t tcp_raw[TCP_HEADER_SIZE + TCP_MAX_PAYLOAD];
    size_t tcp_len;
    IPv4Packet ip_pkt;
    uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
    size_t ip_len;

    tcp_init(&seg);
    seg.src_port = sock->local_port;
    seg.dst_port = sock->remote_port;
    seg.seq_num = sock->send_seq;
    seg.ack_num = sock->recv_seq;
    seg.flags = flags;
    seg.window = TCP_WINDOW_SIZE;
    if (payload != NULL && payload_len > 0) {
        size_t cap = payload_len < TCP_MAX_PAYLOAD ? payload_len : TCP_MAX_PAYLOAD;
        memcpy(seg.payload, payload, cap);
        seg.payload_len = cap;
    }

    tcp_len = packet_to_bytes((Packet*)&seg, tcp_raw, sizeof(tcp_raw));
    if (tcp_len == 0) return 0;
    seg.checksum = tcp_compute_checksum(tcp_raw, tcp_len, &sock->local_ip, &sock->remote_ip);
    tcp_len = packet_to_bytes((Packet*)&seg, tcp_raw, sizeof(tcp_raw));

    if (!ipv4_create(&ip_pkt, sock->local_ip, sock->remote_ip,
                     IPV4_DEFAULT_TTL, IPV4_PROTOCOL_TCP, tcp_raw, tcp_len)) {
        return 0;
    }
    ip_len = packet_to_bytes((Packet*)&ip_pkt, ip_raw, sizeof(ip_raw));
    if (ip_len == 0) return 0;

    if (!host_send_l3_packet(host, &sock->remote_ip, ETHERNET_TYPE_IPV4, ip_raw, ip_len)) {
        return 0;
    }

    sock->send_seq += seg.payload_len;
    if (flags & TCP_FLAG_FIN) sock->send_seq += 1;
    return 1;
}

int http_server_dispatch(Host* host, TCPSocket* socket)
{
    if (!g_server.running || g_server.bound_host == NULL) return 0;
    if (host == NULL || socket == NULL) return 0;
    if (host != g_server.bound_host) return 0;
    if (socket->local_port != HTTP_SERVER_PORT) return 0;
    if (socket->state != TCP_ESTABLISHED) return 0;
    if (!socket->has_data || socket->last_payload_len == 0) return 0;

    char request_buf[HTTP_BUFFER_SIZE];
    size_t req_len = socket->last_payload_len < sizeof(request_buf) - 1
                     ? socket->last_payload_len
                     : sizeof(request_buf) - 1;
    memcpy(request_buf, socket->last_payload, req_len);
    request_buf[req_len] = '\0';

    socket->has_data = false;
    socket->last_payload_len = 0;

    char response[HTTP_BUFFER_SIZE];
    size_t resp_len = 0;
    if (!http_server_handle_request(request_buf, req_len, response, &resp_len, sizeof(response))) {
        return 0;
    }

    if (!http_server_send_tcp(host, socket, TCP_FLAG_ACK | TCP_FLAG_PSH,
                               (const uint8_t*)response, resp_len)) {
        return 0;
    }

    http_server_send_tcp(host, socket, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    socket->state = TCP_FIN_WAIT_1;

    return 1;
}
