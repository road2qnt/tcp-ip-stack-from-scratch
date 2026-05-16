#include "cli.h"
#include "loader.h"
#include "visualizer.h"
#include "debugger.h"
#include "../layer2/host.h"
#include "../layer3/router.h"
#include "../layer3/icmp.h"
#include "../layer4/udp.h"
#include "../layer4/tcp.h"

#define MAX_COMMAND_LENGTH 100

static Simulator simulator;

static void print_help(void)
{
    printf("Commands:\n");
    printf("  create <host|switch|router> <name> [ports]\n");
    printf("  link <device[:port]> <device[:port]> [delay_ms]\n");
    printf("  unlink <device[:port]> <device[:port]>\n");
    printf("  save [filename]\n");
    printf("  load [filename]\n");
    printf("  topology\n");
    printf("  <host> ping <ip|host>\n");
    printf("  <host> traceroute <ip|host>\n");
    printf("  <router> route\n");
    printf("  <host> tcp_connect <ip> <port>\n");
    printf("  <host> tcp_listen <port>\n");
    printf("  <host> tcp_send <data>\n");
    printf("  <host> tcp_recv\n");
    printf("  <host> tcp_close\n");
    printf("  <host> udp_send <ip> <dstport> <data>\n");
    printf("  <host> tcp_status\n");
    printf("  exit | quit\n");
}

static int resolve_ip_target(const char* token, IpAddress* out_ip)
{
    int idx;

    if (token == NULL || out_ip == NULL) {
        return 0;
    }

    if (ip_parse(token, out_ip)) {
        return 1;
    }

    idx = simulator_find_node(&simulator, token);
    if (idx < 0) {
        return 0;
    }

    if (simulator.nodes[idx].type == SIM_NODE_HOST) {
        Host* host = (Host*)simulator.nodes[idx].node;
        if (host->has_ip) {
            *out_ip = host->ip_address;
            return 1;
        }
    } else if (simulator.nodes[idx].type == SIM_NODE_ROUTER) {
        Router* router = (Router*)simulator.nodes[idx].node;
        for (int i = 0; i < router->base.NUM_INTERFACES; i++) {
            if (router->interface_ips[i].has_ip) {
                *out_ip = router->interface_ips[i].ip_address;
                return 1;
            }
        }
    }

    return 0;
}

static int cli_find_host(const char* name, Host** out_host)
{
    int idx;

    if (out_host != NULL) {
        *out_host = NULL;
    }

    idx = simulator_find_node(&simulator, name);
    if (idx < 0 || simulator.nodes[idx].type != SIM_NODE_HOST) {
        return 0;
    }

    if (out_host != NULL) {
        *out_host = (Host*)simulator.nodes[idx].node;
    }
    return 1;
}

static int cli_find_router(const char* name, Router** out_router)
{
    int idx;

    if (out_router != NULL) {
        *out_router = NULL;
    }

    idx = simulator_find_node(&simulator, name);
    if (idx < 0 || simulator.nodes[idx].type != SIM_NODE_ROUTER) {
        return 0;
    }

    if (out_router != NULL) {
        *out_router = (Router*)simulator.nodes[idx].node;
    }
    return 1;
}

void cli(){
    printf("=== Magi System ===\n");
    bool loop = true;

    simulator_init(&simulator);

    char input[MAX_COMMAND_LENGTH];
    while (loop){
        printf("magi> ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        input[strcspn(input, "\n")] = '\0';
        loop = process(input);
    }

    simulator_clear(&simulator);
    printf("=== END ===\n");
}

bool process(char* command){
    char buffer[MAX_COMMAND_LENGTH];
    char *tokens[8];
    int count = 0;
    char *token;

    if (command == NULL) {
        return true;
    }

    strncpy(buffer, command, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    token = strtok(buffer, " \t");
    while (token != NULL && count < 8) {
        tokens[count++] = token;
        token = strtok(NULL, " \t");
    }

    if (count == 0) {
        return true;
    }

    if (strcmp(tokens[0], "create") == 0){
        int ports = 0;

        if (count < 3) {
            printf("Usage: create <host|switch|router> <name> [ports]\n");
            return true;
        }

        if (count >= 4) {
            ports = atoi(tokens[3]);
        }

        if (simulator_create_node(&simulator, tokens[1], tokens[2], ports)) {
            printf("[SIM] Created %s %s\n", tokens[1], tokens[2]);
        } else {
            printf("[SIM] Failed to create %s %s\n", tokens[1], tokens[2]);
        }
        return true;
    } else if (strcmp(tokens[0], "link") == 0){
        float delay = 0;

        if (count < 3) {
            printf("Usage: link <device[:port]> <device[:port]> [delay_ms]\n");
            return true;
        }

        if (count >= 4) {
            delay = (float)atof(tokens[3]);
        }

        if (simulator_link(&simulator, tokens[1], tokens[2], delay)) {
            printf("[SIM] Linked %s <-> %s delay=%.0fms\n", tokens[1], tokens[2], delay);
        } else {
            printf("[SIM] Failed to link %s <-> %s\n", tokens[1], tokens[2]);
        }
        return true;
    } else if (strcmp(tokens[0], "unlink") == 0){
        if (count < 3) {
            printf("Usage: unlink <device[:port]> <device[:port]>\n");
            return true;
        }

        if (simulator_unlink(&simulator, tokens[1], tokens[2])) {
            printf("[SIM] Unlinked %s <-> %s\n", tokens[1], tokens[2]);
        } else {
            printf("[SIM] Failed to unlink %s <-> %s\n", tokens[1], tokens[2]);
        }
        return true;
    } else if (strcmp(tokens[0], "save") == 0){
        const char *filename = count >= 2 ? tokens[1] : "magi_system/topology.json";

        if (simulator_save(&simulator, filename)) {
            printf("[SIM] Saved topology to %s\n", filename);
        } else {
            printf("[SIM] Failed to save topology to %s\n", filename);
        }
        return true;
    } else if (strcmp(tokens[0], "load") == 0){
        const char *filename = count >= 2 ? tokens[1] : "magi_system/topology.json";

        if (simulator_load(&simulator, filename)) {
            printf("[SIM] Loaded topology from %s\n", filename);
        } else {
            printf("[SIM] Failed to load topology from %s\n", filename);
        }
        return true;
    } else if (strcmp(tokens[0], "debug") == 0){
        const char *target = count >= 2 ? tokens[1] : "help";

        if (strcmp(target, "m0") == 0 || strcmp(target, "0") == 0) {
            debug_milestone_0(&simulator);
        } else if (strcmp(target, "m1") == 0 || strcmp(target, "1") == 0) {
            debug_milestone_1(&simulator);
        } else if (strcmp(target, "m2") == 0 || strcmp(target, "2") == 0) {
            debug_milestone_2(&simulator);
        } else if (strcmp(target, "m3") == 0 || strcmp(target, "3") == 0) {
            debug_milestone_3(&simulator);
        } else if (strcmp(target, "m4") == 0 || strcmp(target, "4") == 0) {
            debug_milestone_4(&simulator);
        } else if (strcmp(target, "all") == 0) {
            debug_run_all(&simulator);
        } else {
            printf("Debug targets:\n");
            printf("  debug m0   - Milestone 0: Fondasi Simulasi\n");
            printf("  debug m1   - Milestone 1: Data Link Layer\n");
            printf("  debug m2   - Milestone 2: Network Layer\n");
            printf("  debug m3   - Milestone 3: Transport Layer\n");
            printf("  debug m4   - Milestone 4: Application Layer\n");
            printf("  debug all  - Run all milestone tests\n");
        }
        return true;
    } else if (strcmp(tokens[0], "topology") == 0){
        simulator_print_topology(&simulator);
        return true;
    } else if (count >= 2 && strcmp(tokens[1], "ping") == 0) {
        Host* host;
        IpAddress target_ip;

        if (count < 3) {
            printf("Usage: <host> ping <ip|host>\n");
            return true;
        }

        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        if (!resolve_ip_target(tokens[2], &target_ip)) {
            printf("Invalid target: %s\n", tokens[2]);
            return true;
        }

        host_send_icmp_echo_request(host, &target_ip, IPV4_DEFAULT_TTL, 1);
        return true;
    } else if (count >= 2 && strcmp(tokens[1], "traceroute") == 0) {
        Host* host;
        IpAddress target_ip;
        char target[32];

        if (count < 3) {
            printf("Usage: <host> traceroute <ip|host>\n");
            return true;
        }

        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        if (!resolve_ip_target(tokens[2], &target_ip)) {
            printf("Invalid target: %s\n", tokens[2]);
            return true;
        }

        ip_to_string(&target_ip, target, sizeof(target), false);
        printf("traceroute to %s\n", target);
        for (uint8_t ttl = 1; ttl <= 30; ttl++) {
            host_send_icmp_echo_request(host, &target_ip, ttl, ttl);
            if (!host->has_last_icmp) {
                printf("%u  *\n", ttl);
                continue;
            }

            char hop[32];
            ip_to_string(&host->last_icmp_source, hop, sizeof(hop), false);
            printf("%u  %s  0ms\n", ttl, hop);
            if (host->last_icmp_type == ICMP_ECHO_REPLY) {
                break;
            }
            if (host->last_icmp_type == ICMP_DEST_UNREACHABLE) {
                break;
            }
        }
        return true;
    } else if (count >= 2 && strcmp(tokens[1], "route") == 0) {
        Router* router;

        if (!cli_find_router(tokens[0], &router)) {
            printf("Unknown router: %s\n", tokens[0]);
            return true;
        }

        router_print_routes(router);
        return true;
    } else if (count >= 2 && strcmp(tokens[1], "tcp_connect") == 0) {
        // <host> tcp_connect <ip> <port>
        Host* host;
        IpAddress target_ip;

        if (count < 4) {
            printf("Usage: <host> tcp_connect <ip> <port>\n");
            return true;
        }

        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        if (!ip_parse(tokens[2], &target_ip)) {
            printf("Invalid IP: %s\n", tokens[2]);
            return true;
        }

        int dst_port = atoi(tokens[3]);
        if (dst_port <= 0 || dst_port > 65535) {
            printf("Invalid port: %d\n", dst_port);
            return true;
        }

        // Allocate a TCP socket
        int sock_idx = tcp_socket_alloc(host->tcp_sockets, TCP_MAX_SOCKETS);
        if (sock_idx < 0) {
            printf("[%s] No free TCP socket slots\n", host->base.NAME);
            return true;
        }

        TCPSocket* sock = &host->tcp_sockets[sock_idx];
        tcp_socket_connect(sock, &host->ip_address, (uint16_t)dst_port + 1000,
                           &target_ip, (uint16_t)dst_port);

        char ip_buf[20];
        ip_to_string(&target_ip, ip_buf, sizeof(ip_buf), false);
        printf("[%s] TCP connect initiated to %s:%d\n", host->base.NAME, ip_buf, dst_port);

        // Build SYN packet
        uint8_t tcp_raw[TCP_HEADER_SIZE];
        TCPSegment syn_seg;
        tcp_init(&syn_seg);
        syn_seg.src_port = sock->local_port;
        syn_seg.dst_port = sock->remote_port;
        syn_seg.seq_num = sock->send_seq;
        syn_seg.flags = TCP_FLAG_SYN;
        syn_seg.window = TCP_WINDOW_SIZE;

        size_t tcp_len = packet_to_bytes((Packet*)&syn_seg, tcp_raw, sizeof(tcp_raw));
        if (tcp_len > 0) {
            syn_seg.checksum = tcp_compute_checksum(tcp_raw, tcp_len,
                                                     &sock->local_ip, &sock->remote_ip);
            tcp_len = packet_to_bytes((Packet*)&syn_seg, tcp_raw, sizeof(tcp_raw));

            uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
            IPv4Packet ip_pkt;
            if (ipv4_create(&ip_pkt, sock->local_ip, sock->remote_ip,
                            IPV4_DEFAULT_TTL, IPV4_PROTOCOL_TCP, tcp_raw, tcp_len)) {
                size_t ip_len = packet_to_bytes((Packet*)&ip_pkt, ip_raw, sizeof(ip_raw));
                if (ip_len > 0) {
                    host_send_l3_packet(host, &sock->remote_ip, ETHERNET_TYPE_IPV4, ip_raw, ip_len);
                }
            }
        }

        return true;
    } else if (count >= 2 && strcmp(tokens[1], "tcp_listen") == 0) {
        // <host> tcp_listen <port>
        Host* host;

        if (count < 3) {
            printf("Usage: <host> tcp_listen <port>\n");
            return true;
        }

        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        int port = atoi(tokens[2]);
        if (port <= 0 || port > 65535) {
            printf("Invalid port: %d\n", port);
            return true;
        }

        int sock_idx = tcp_socket_alloc(host->tcp_sockets, TCP_MAX_SOCKETS);
        if (sock_idx < 0) {
            printf("[%s] No free TCP socket slots\n", host->base.NAME);
            return true;
        }

        TCPSocket* sock = &host->tcp_sockets[sock_idx];
        tcp_socket_listen(sock, &host->ip_address, (uint16_t)port);

        return true;
    } else if (count >= 2 && strcmp(tokens[1], "tcp_send") == 0) {
        // <host> tcp_send <data>
        Host* host;

        if (count < 3) {
            printf("Usage: <host> tcp_send <data>\n");
            return true;
        }

        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        // Find first ESTABLISHED socket on this host
        TCPSocket* sock = NULL;
        for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
            if (host->tcp_sockets[i].in_use &&
                host->tcp_sockets[i].state == TCP_ESTABLISHED) {
                sock = &host->tcp_sockets[i];
                break;
            }
        }

        if (sock == NULL) {
            printf("[%s] No established TCP connection\n", host->base.NAME);
            return true;
        }

        const char* data = tokens[2];
        size_t data_len = strlen(data);

        if (data_len > TCP_MAX_PAYLOAD) {
            data_len = TCP_MAX_PAYLOAD;
        }

        tcp_socket_send(sock, (const uint8_t*)data, data_len);

        // Send data packet
        uint8_t tcp_raw[TCP_HEADER_SIZE + TCP_MAX_PAYLOAD];
        TCPSegment data_seg;
        tcp_init(&data_seg);
        data_seg.src_port = sock->local_port;
        data_seg.dst_port = sock->remote_port;
        data_seg.seq_num = sock->send_seq;
        data_seg.ack_num = sock->recv_seq;
        data_seg.flags = TCP_FLAG_ACK | TCP_FLAG_PSH;
        data_seg.window = TCP_WINDOW_SIZE;
        data_seg.payload_len = data_len;
        memcpy(data_seg.payload, data, data_len);

        size_t tcp_len = packet_to_bytes((Packet*)&data_seg, tcp_raw, sizeof(tcp_raw));
        if (tcp_len > 0) {
            data_seg.checksum = tcp_compute_checksum(tcp_raw, tcp_len,
                                                     &sock->local_ip, &sock->remote_ip);
            tcp_len = packet_to_bytes((Packet*)&data_seg, tcp_raw, sizeof(tcp_raw));

            uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
            IPv4Packet ip_pkt;
            if (ipv4_create(&ip_pkt, sock->local_ip, sock->remote_ip,
                            IPV4_DEFAULT_TTL, IPV4_PROTOCOL_TCP, tcp_raw, tcp_len)) {
                size_t ip_len = packet_to_bytes((Packet*)&ip_pkt, ip_raw, sizeof(ip_raw));
                if (ip_len > 0) {
                    host_send_l3_packet(host, &sock->remote_ip, ETHERNET_TYPE_IPV4, ip_raw, ip_len);
                    sock->send_seq += data_len;
                    printf("[%s] Sent %zu bytes via TCP\n", host->base.NAME, data_len);
                }
            }
        }

        return true;
    } else if (count >= 2 && strcmp(tokens[1], "tcp_recv") == 0) {
        // <host> tcp_recv
        Host* host;

        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        // Find first ESTABLISHED socket on this host
        TCPSocket* sock = NULL;
        for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
            if (host->tcp_sockets[i].in_use &&
                (host->tcp_sockets[i].state == TCP_ESTABLISHED ||
                 host->tcp_sockets[i].has_data)) {
                sock = &host->tcp_sockets[i];
                break;
            }
        }

        if (sock == NULL) {
            printf("[%s] No TCP connection with data\n", host->base.NAME);
            return true;
        }

        if (!sock->has_data) {
            printf("[%s] No data available\n", host->base.NAME);
            return true;
        }

        uint8_t buffer[TCP_MAX_PAYLOAD];
        int received = tcp_socket_recv(sock, buffer, sizeof(buffer));
        if (received > 0) {
            buffer[received] = '\0';
            printf("[%s] Received %d bytes: %s\n", host->base.NAME, received, (char*)buffer);
        }

        return true;
    } else if (count >= 2 && strcmp(tokens[1], "tcp_close") == 0) {
        // <host> tcp_close
        Host* host;

        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        // Find first ESTABLISHED socket
        TCPSocket* sock = NULL;
        for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
            if (host->tcp_sockets[i].in_use &&
                host->tcp_sockets[i].state == TCP_ESTABLISHED) {
                sock = &host->tcp_sockets[i];
                break;
            }
        }

        if (sock == NULL) {
            printf("[%s] No established TCP connection\n", host->base.NAME);
            return true;
        }

        // Send FIN
        uint8_t tcp_raw[TCP_HEADER_SIZE];
        TCPSegment fin_seg;
        tcp_init(&fin_seg);
        fin_seg.src_port = sock->local_port;
        fin_seg.dst_port = sock->remote_port;
        fin_seg.seq_num = sock->send_seq;
        fin_seg.ack_num = sock->recv_seq;
        fin_seg.flags = TCP_FLAG_FIN | TCP_FLAG_ACK;
        fin_seg.window = TCP_WINDOW_SIZE;

        size_t tcp_len = packet_to_bytes((Packet*)&fin_seg, tcp_raw, sizeof(tcp_raw));
        if (tcp_len > 0) {
            fin_seg.checksum = tcp_compute_checksum(tcp_raw, tcp_len,
                                                     &sock->local_ip, &sock->remote_ip);
            tcp_len = packet_to_bytes((Packet*)&fin_seg, tcp_raw, sizeof(tcp_raw));

            uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
            IPv4Packet ip_pkt;
            if (ipv4_create(&ip_pkt, sock->local_ip, sock->remote_ip,
                            IPV4_DEFAULT_TTL, IPV4_PROTOCOL_TCP, tcp_raw, tcp_len)) {
                size_t ip_len = packet_to_bytes((Packet*)&ip_pkt, ip_raw, sizeof(ip_raw));
                if (ip_len > 0) {
                    host_send_l3_packet(host, &sock->remote_ip, ETHERNET_TYPE_IPV4, ip_raw, ip_len);
                    tcp_socket_close(sock);
                    printf("[%s] TCP connection closing (FIN sent)\n", host->base.NAME);
                }
            }
        }

        return true;
    } else if (count >= 2 && strcmp(tokens[1], "tcp_status") == 0) {
        // <host> tcp_status
        Host* host;

        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        printf("[%s] TCP sockets:\n", host->base.NAME);
        for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
            TCPSocket* s = &host->tcp_sockets[i];
            if (!s->in_use) continue;

            char local_ip[20], remote_ip[20];
            ip_to_string(&s->local_ip, local_ip, sizeof(local_ip), false);
            ip_to_string(&s->remote_ip, remote_ip, sizeof(remote_ip), false);

            printf("  #%d: %s:%u <-> %s:%u [%s]%s%s\n",
                   i, local_ip, s->local_port, remote_ip, s->remote_port,
                   tcp_state_name(s->state),
                   s->is_listening ? " LISTENING" : "",
                   s->has_data ? " DATA" : "");
        }

        return true;
    } else if (count >= 3 && strcmp(tokens[1], "udp_send") == 0) {
        // <host> udp_send <ip> <dstport> <data>
        Host* host;
        IpAddress target_ip;

        if (count < 5) {
            printf("Usage: <host> udp_send <ip> <dstport> <data>\n");
            return true;
        }

        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        if (!ip_parse(tokens[2], &target_ip)) {
            printf("Invalid IP: %s\n", tokens[2]);
            return true;
        }

        int dst_port = atoi(tokens[3]);
        if (dst_port <= 0 || dst_port > 65535) {
            printf("Invalid port: %d\n", dst_port);
            return true;
        }

        const char* data = tokens[4];
        size_t data_len = strlen(data);
        if (data_len > UDP_MAX_PAYLOAD) {
            data_len = UDP_MAX_PAYLOAD;
        }

        UDPDatagram udp;
        udp_init(&udp);
        udp_create(&udp, (uint16_t)(dst_port + 2000), (uint16_t)dst_port,
                   (const uint8_t*)data, data_len);

        uint8_t udp_raw[UDP_HEADER_SIZE + UDP_MAX_PAYLOAD];
        size_t udp_len = packet_to_bytes((Packet*)&udp, udp_raw, sizeof(udp_raw));
        if (udp_len > 0) {
            udp.checksum = udp_compute_checksum(udp_raw, udp_len,
                                                 &host->ip_address, &target_ip);
            udp_len = packet_to_bytes((Packet*)&udp, udp_raw, sizeof(udp_raw));

            uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
            IPv4Packet ip_pkt;
            if (ipv4_create(&ip_pkt, host->ip_address, target_ip,
                            IPV4_DEFAULT_TTL, IPV4_PROTOCOL_UDP, udp_raw, udp_len)) {
                size_t ip_len = packet_to_bytes((Packet*)&ip_pkt, ip_raw, sizeof(ip_raw));
                if (ip_len > 0) {
                    host_send_l3_packet(host, &target_ip, ETHERNET_TYPE_IPV4, ip_raw, ip_len);
                    char udp_ip_buf[20];
                    ip_to_string(&target_ip, udp_ip_buf, sizeof(udp_ip_buf), false);
                    printf("[%s] Sent UDP datagram to %s:%d (%zu bytes)\n",
                           host->base.NAME, udp_ip_buf, dst_port, data_len);
                }
            }
        }

        return true;
    } else if (strcmp(tokens[0], "help") == 0){
        print_help();
        return true;
    } else if (strcmp(tokens[0], "exit") == 0 || strcmp(tokens[0], "quit") == 0){
        return false;
    } else {
        printf("Unknown command: %s\n", command);
        print_help();
        return true;
    }
}
