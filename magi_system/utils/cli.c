#include "cli.h"
#include "loader.h"
#include "visualizer.h"
#include "debugger.h"
#include "../layer2/host.h"
#include "../layer2/switch.h"
#include "../layer3/router.h"
#include "../layer3/icmp.h"
#include "../layer4/udp.h"
#include "../layer4/tcp.h"
#include "../layer7/magi_socket.h"
#include "../layer7/dhcp_server.h"
#include "../layer7/dns_server.h"
#include "../layer7/http_server.h"

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
    printf("  <switch> mac\n");
    printf("  <host|router> arp\n");
    printf("  <host> tcp_connect <ip> <port>\n");
    printf("  <host> tcp_listen <port>\n");
    printf("  <host> tcp_send <data>\n");
    printf("  <host> tcp_recv\n");
    printf("  <host> tcp_close\n");
    printf("  <host> udp_send <ip> <dstport> <data>\n");
    printf("  <host> tcp_status\n");
    printf("  <host> magi_sock_connect <ip> <port>\n");
    printf("  <host> magi_sock_send <data>\n");
    printf("  <host> magi_sock_recv\n");
    printf("  <host> magi_sock_close\n");
    printf("  <host> dhcp_discover\n");
    printf("  <host> dhcp_server start\n");
    printf("  <host> dhcp_server stop\n");
    printf("  <host> dns_resolve <hostname> [server_ip]\n");
    printf("  <host> dns_server start\n");
    printf("  <host> dns_server stop\n");
    printf("  <host> http_get <url>\n");
    printf("  <host> http_server start [dir]\n");
    printf("  <host> http_server stop\n");
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

static int dhcp_client_send_broadcast(Host* host, const uint8_t* dhcp_raw, size_t dhcp_len)
{
    IpAddress broadcast_ip;
    uint8_t bcast[] = {255, 255, 255, 255};
    broadcast_ip = ip_init(bcast, 0);

    UDPDatagram udp;
    udp_init(&udp);
    udp_create(&udp, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, dhcp_raw, dhcp_len);

    uint8_t udp_raw[UDP_HEADER_SIZE + UDP_MAX_PAYLOAD];
    size_t udp_len = packet_to_bytes((Packet*)&udp, udp_raw, sizeof(udp_raw));
    if (udp_len == 0) return 0;

    IpAddress src_ip = host->has_ip ? host->ip_address : broadcast_ip;
    udp.checksum = udp_compute_checksum(udp_raw, udp_len, &src_ip, &broadcast_ip);
    udp_len = packet_to_bytes((Packet*)&udp, udp_raw, sizeof(udp_raw));

    uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
    IPv4Packet ip_pkt;
    if (!ipv4_create(&ip_pkt, src_ip, broadcast_ip,
                     IPV4_DEFAULT_TTL, IPV4_PROTOCOL_UDP, udp_raw, udp_len)) {
        return 0;
    }
    size_t ip_len = packet_to_bytes((Packet*)&ip_pkt, ip_raw, sizeof(ip_raw));
    if (ip_len == 0) return 0;

    host->has_last_udp = false;
    return host_send_l3_packet(host, &broadcast_ip, ETHERNET_TYPE_IPV4, ip_raw, ip_len);
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
            printf("%u  %s  %.1fms\n", ttl, hop, host->last_icmp_rtt_ms);
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
    } else if (count >= 2 && strcmp(tokens[1], "mac") == 0) {
        int idx = simulator_find_node(&simulator, tokens[0]);
        if (idx < 0 || simulator.nodes[idx].type != SIM_NODE_SWITCH) {
            printf("Unknown switch: %s\n", tokens[0]);
            return true;
        }

        Switch* sw = (Switch*)simulator.nodes[idx].node;
        printf("[%s] MAC Address Table (%zu entries):\n",
               sw->base.NAME, sw->mac_table.size);
        printf("  %-6s %-6s %-20s\n", "VLAN", "Port", "MAC Address");
        for (size_t i = 0; i < sw->mac_table.size; i++) {
            const SwitchMacEntry* e = &sw->mac_table.entries[i];
            char mac_buf[20];
            mac_to_string(&e->mac, mac_buf, sizeof(mac_buf));
            printf("  %-6d %-6d %-20s\n", e->vlan_id, e->port_number, mac_buf);
        }
        return true;
    } else if (count >= 2 && strcmp(tokens[1], "arp") == 0) {
        int idx = simulator_find_node(&simulator, tokens[0]);
        if (idx < 0) {
            printf("Unknown node: %s\n", tokens[0]);
            return true;
        }

        const ArpTable* arp = NULL;
        const char* node_name = simulator.nodes[idx].name;
        if (simulator.nodes[idx].type == SIM_NODE_HOST) {
            arp = &((Host*)simulator.nodes[idx].node)->arp_table;
        } else if (simulator.nodes[idx].type == SIM_NODE_ROUTER) {
            arp = &((Router*)simulator.nodes[idx].node)->arp_table;
        } else {
            printf("ARP table only available on host or router: %s\n", tokens[0]);
            return true;
        }

        printf("[%s] ARP Cache (%zu entries):\n", node_name, arp->size);
        printf("  %-18s %-20s\n", "IP Address", "MAC Address");
        for (size_t i = 0; i < arp->size; i++) {
            const ArpTableEntry* e = &arp->entries[i];
            char ip_buf[20];
            char mac_buf[20];
            ip_to_string(&e->ip, ip_buf, sizeof(ip_buf), false);
            mac_to_string(&e->mac, mac_buf, sizeof(mac_buf));
            printf("  %-18s %-20s\n", ip_buf, mac_buf);
        }
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
    } else if (count >= 2 && strcmp(tokens[1], "magi_sock_connect") == 0) {
        // <host> magi_sock_connect <ip> <port>
        Host* host;
        IpAddress target_ip;

        if (count < 4) {
            printf("Usage: <host> magi_sock_connect <ip> <port>\n");
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

        int port = atoi(tokens[3]);
        if (port <= 0 || port > 65535) {
            printf("Invalid port: %d\n", port);
            return true;
        }

        int sockfd = magi_socket(AF_INET, SOCK_STREAM);
        if (sockfd < 0) {
            printf("[%s] Failed to create MagiSocket\n", host->base.NAME);
            return true;
        }

        magi_socket_attach_host(sockfd, host);
        magi_connect(sockfd, &target_ip, (uint16_t)port);
        host->magi_sock_fd = sockfd;

        char ip_buf[20];
        ip_to_string(&target_ip, ip_buf, sizeof(ip_buf), false);
        printf("[%s] MagiSocket connected to %s:%d (fd=%d)\n",
               host->base.NAME, ip_buf, port, sockfd);
        return true;

    } else if (count >= 2 && strcmp(tokens[1], "magi_sock_send") == 0) {
        Host* host;
        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }
        if (count < 3) {
            printf("Usage: <host> magi_sock_send <data>\n");
            return true;
        }
        int r = magi_send(host->magi_sock_fd, (const uint8_t*)tokens[2], strlen(tokens[2]));
        if (r > 0) {
            printf("[%s] Sent %d bytes\n", host->base.NAME, r);
        } else {
            printf("[%s] Failed to send data\n", host->base.NAME);
        }
        return true;

    } else if (count >= 2 && strcmp(tokens[1], "magi_sock_recv") == 0) {
        Host* host;
        uint8_t buf[MAGI_SOCKET_BUFFER_SIZE];

        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        int r = magi_recv(host->magi_sock_fd, buf, sizeof(buf) - 1);
        if (r > 0) {
            buf[r] = '\0';
            printf("[%s] Received %d bytes: %s\n", host->base.NAME, r, (char*)buf);
        } else {
            printf("[%s] No data available\n", host->base.NAME);
        }
        return true;

    } else if (count >= 2 && strcmp(tokens[1], "magi_sock_close") == 0) {
        Host* host;
        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }
        magi_close(host->magi_sock_fd);
        host->magi_sock_fd = -1;
        printf("[%s] MagiSocket closed\n", host->base.NAME);
        return true;

    } else if (count >= 2 && strcmp(tokens[1], "dhcp_server") == 0) {
        if (count < 3) {
            printf("Usage: <host> dhcp_server start|stop\n");
            return true;
        }
        if (strcmp(tokens[2], "start") == 0) {
            Host* host;
            if (!cli_find_host(tokens[0], &host)) {
                printf("Unknown host: %s\n", tokens[0]);
                return true;
            }
            if (!host->has_ip) {
                printf("[DHCP] %s must have an IP before starting DHCP server\n", host->base.NAME);
                return true;
            }
            if (!dhcp_server_attach_host(host)) {
                printf("[DHCP] Failed to start DHCP server on %s\n", host->base.NAME);
                return true;
            }
            char ip_buf[20];
            ip_to_string(&host->ip_address, ip_buf, sizeof(ip_buf), false);
            printf("[DHCP] Server started on %s (%s:%u)\n",
                   host->base.NAME, ip_buf, DHCP_SERVER_PORT);
        } else if (strcmp(tokens[2], "stop") == 0) {
            dhcp_server_detach_host();
            printf("[DHCP] Server stopped\n");
        } else {
            printf("Unknown dhcp_server subcommand: %s\n", tokens[2]);
        }
        return true;

    } else if (count >= 2 && strcmp(tokens[1], "dhcp_discover") == 0) {
        Host* host;
        DHCPMessage discover, request_msg, response_msg;
        uint8_t dhcp_raw[DHCP_HEADER_SIZE + DHCP_OPTIONS_SIZE];
        int dhcp_len;
        uint32_t xid = (uint32_t)(rand() % 100000);

        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        printf("[%s] DHCP Discover (xid=%u)...\n", host->base.NAME, xid);
        dhcp_create_discover(&discover, xid, host->base.interfaces[0].Mac_Address.bytes);
        dhcp_len = dhcp_message_to_bytes(&discover, dhcp_raw, sizeof(dhcp_raw));
        if (dhcp_len <= 0) {
            printf("[DHCP] Failed to serialize discover\n");
            return true;
        }
        if (!dhcp_client_send_broadcast(host, dhcp_raw, (size_t)dhcp_len)) {
            printf("[DHCP] Failed to send DISCOVER\n");
            return true;
        }

        if (!host->has_last_udp || host->last_udp.src_port != DHCP_SERVER_PORT) {
            printf("[%s] No DHCP OFFER received\n", host->base.NAME);
            return true;
        }
        if (!dhcp_message_from_bytes(&response_msg,
                                      host->last_udp.payload,
                                      host->last_udp.payload_len)) {
            printf("[DHCP] Failed to parse OFFER\n");
            return true;
        }
        if (dhcp_get_msg_type(&response_msg) != DHCP_OFFER) {
            printf("[DHCP] Expected OFFER, got msg-type=%d\n", dhcp_get_msg_type(&response_msg));
            return true;
        }
        IpAddress offered_ip = response_msg.yiaddr;
        IpAddress server_ip = response_msg.siaddr;
        char ip_buf[20];
        ip_to_string(&offered_ip, ip_buf, sizeof(ip_buf), false);
        printf("[%s] Received OFFER for %s\n", host->base.NAME, ip_buf);

        printf("[%s] DHCP Request for %s...\n", host->base.NAME, ip_buf);
        dhcp_create_request(&request_msg, xid,
                            host->base.interfaces[0].Mac_Address.bytes,
                            &offered_ip, &server_ip);
        dhcp_len = dhcp_message_to_bytes(&request_msg, dhcp_raw, sizeof(dhcp_raw));
        if (dhcp_len <= 0) {
            printf("[DHCP] Failed to serialize REQUEST\n");
            return true;
        }
        if (!dhcp_client_send_broadcast(host, dhcp_raw, (size_t)dhcp_len)) {
            printf("[DHCP] Failed to send REQUEST\n");
            return true;
        }

        if (!host->has_last_udp || host->last_udp.src_port != DHCP_SERVER_PORT) {
            printf("[%s] No DHCP ACK received\n", host->base.NAME);
            return true;
        }
        if (!dhcp_message_from_bytes(&response_msg,
                                      host->last_udp.payload,
                                      host->last_udp.payload_len)) {
            printf("[DHCP] Failed to parse ACK\n");
            return true;
        }
        if (dhcp_get_msg_type(&response_msg) != DHCP_ACK) {
            printf("[DHCP] Expected ACK, got msg-type=%d\n", dhcp_get_msg_type(&response_msg));
            return true;
        }

        host->ip_address = response_msg.yiaddr;
        host->has_ip = true;
        ip_to_string(&host->ip_address, ip_buf, sizeof(ip_buf), false);
        printf("[%s] DHCP lease acquired: %s\n", host->base.NAME, ip_buf);

        return true;

    } else if (count >= 2 && strcmp(tokens[1], "dns_server") == 0) {
        if (count < 3) {
            printf("Usage: <host> dns_server start|stop\n");
            return true;
        }
        if (strcmp(tokens[2], "start") == 0) {
            Host* host;
            if (!cli_find_host(tokens[0], &host)) {
                printf("Unknown host: %s\n", tokens[0]);
                return true;
            }
            if (!host->has_ip) {
                printf("[DNS] %s must have an IP before starting DNS server\n", host->base.NAME);
                return true;
            }
            if (!dns_server_attach_host(host)) {
                printf("[DNS] Failed to start DNS server on %s\n", host->base.NAME);
                return true;
            }
            char ip_buf[20];
            ip_to_string(&host->ip_address, ip_buf, sizeof(ip_buf), false);
            printf("[DNS] Server started on %s (%s:%u)\n",
                   host->base.NAME, ip_buf, DNS_SERVER_PORT);
        } else if (strcmp(tokens[2], "stop") == 0) {
            dns_server_detach_host();
            printf("[DNS] Server stopped\n");
        } else {
            printf("Unknown dns_server subcommand: %s\n", tokens[2]);
        }
        return true;

    } else if (count >= 2 && strcmp(tokens[1], "dns_resolve") == 0) {
        IpAddress resolved;

        if (count < 3) {
            printf("Usage: <host> dns_resolve <hostname> [server_ip]\n");
            return true;
        }

        if (count < 4) {
            if (dns_server_resolve(tokens[2], &resolved)) {
                char ip_buf[20];
                ip_to_string(&resolved, ip_buf, sizeof(ip_buf), false);
                printf("[DNS] %s -> %s\n", tokens[2], ip_buf);
            } else {
                printf("[DNS] Could not resolve: %s\n", tokens[2]);
            }
            return true;
        }

        Host* host;
        IpAddress server_ip;
        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }
        if (!ip_parse(tokens[3], &server_ip)) {
            printf("Invalid server IP: %s\n", tokens[3]);
            return true;
        }

        uint8_t query_raw[512];
        size_t query_len = 0;
        uint16_t query_id = (uint16_t)((rand() % 0xFFFF) + 1);
        if (!dns_create_query(query_raw, sizeof(query_raw), &query_len, query_id, tokens[2])) {
            printf("[DNS] Failed to build query for %s\n", tokens[2]);
            return true;
        }

        UDPDatagram udp;
        udp_init(&udp);
        udp_create(&udp, 5353, DNS_SERVER_PORT, query_raw, query_len);

        uint8_t udp_raw[UDP_HEADER_SIZE + UDP_MAX_PAYLOAD];
        size_t udp_len = packet_to_bytes((Packet*)&udp, udp_raw, sizeof(udp_raw));
        if (udp_len == 0) {
            printf("[DNS] UDP serialization failed\n");
            return true;
        }
        udp.checksum = udp_compute_checksum(udp_raw, udp_len, &host->ip_address, &server_ip);
        udp_len = packet_to_bytes((Packet*)&udp, udp_raw, sizeof(udp_raw));

        uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
        IPv4Packet ip_pkt;
        if (!ipv4_create(&ip_pkt, host->ip_address, server_ip,
                         IPV4_DEFAULT_TTL, IPV4_PROTOCOL_UDP, udp_raw, udp_len)) {
            printf("[DNS] IP packet build failed\n");
            return true;
        }
        size_t ip_len = packet_to_bytes((Packet*)&ip_pkt, ip_raw, sizeof(ip_raw));
        if (ip_len == 0) {
            printf("[DNS] IP serialization failed\n");
            return true;
        }

        host->has_last_udp = false;
        if (!host_send_l3_packet(host, &server_ip, ETHERNET_TYPE_IPV4, ip_raw, ip_len)) {
            printf("[DNS] Failed to send query\n");
            return true;
        }

        if (!host->has_last_udp || host->last_udp.src_port != DNS_SERVER_PORT) {
            printf("[DNS] No DNS response received from %s\n", tokens[3]);
            return true;
        }

        if (!dns_parse_response(host->last_udp.payload, host->last_udp.payload_len, &resolved)) {
            printf("[DNS] Failed to parse response (NXDOMAIN?)\n");
            return true;
        }

        char ip_buf[20];
        ip_to_string(&resolved, ip_buf, sizeof(ip_buf), false);
        printf("[DNS] %s -> %s\n", tokens[2], ip_buf);
        return true;

    } else if (count >= 2 && strcmp(tokens[1], "http_get") == 0) {
        // <host> http_get <url>
        Host* host;
        char hostname[256];
        char path[512];
        IpAddress resolved;

        if (count < 3) {
            printf("Usage: <host> http_get <url>\n");
            printf("Examples:\n");
            printf("  H1 http_get www.magi.com\n");
            printf("  H1 http_get www.magi.com/index.html\n");
            return true;
        }

        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        // Parse URL into hostname and path
        const char* url = tokens[2];
        const char* h = url;
        if (strncmp(h, "http://", 7) == 0) h += 7;

        // Extract hostname
        const char* slash = strchr(h, '/');
        if (slash) {
            size_t host_len = (size_t)(slash - h);
            if (host_len >= sizeof(hostname)) host_len = sizeof(hostname) - 1;
            strncpy(hostname, h, host_len);
            hostname[host_len] = '\0';
            strncpy(path, slash, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
        } else {
            strncpy(hostname, h, sizeof(hostname) - 1);
            hostname[sizeof(hostname) - 1] = '\0';
            strncpy(path, "/", sizeof(path) - 1);
        }

        // Resolve hostname
        if (!dns_server_resolve(hostname, &resolved)) {
            // Try as IP
            if (!ip_parse(hostname, &resolved)) {
                printf("[HTTP] Could not resolve hostname: %s\n", hostname);
                return true;
            }
        }

        char ip_buf[20];
        ip_to_string(&resolved, ip_buf, sizeof(ip_buf), false);
        printf("[HTTP] Resolved %s -> %s\n", hostname, ip_buf);

        // Create MagiSocket and connect
        int sockfd = magi_socket(AF_INET, SOCK_STREAM);
        if (sockfd < 0) {
            printf("[%s] Failed to create socket\n", host->base.NAME);
            return true;
        }

        magi_socket_attach_host(sockfd, host);
        magi_connect(sockfd, &resolved, HTTP_SERVER_PORT);

        // Build HTTP GET request
        char request[1024];
        int req_len = snprintf(request, sizeof(request),
                               "GET %s HTTP/1.1\r\n"
                               "Host: %s\r\n"
                               "User-Agent: MagiSystem/1.0\r\n"
                               "Accept: */*\r\n"
                               "Connection: close\r\n"
                               "\r\n",
                               path, hostname);

        if (req_len > 0) {
            magi_send(sockfd, (const uint8_t*)request, (size_t)req_len);
            printf("[HTTP] GET %s -> %s:%d\n", path, ip_buf, HTTP_SERVER_PORT);
        }

        uint8_t response[HTTP_BUFFER_SIZE];
        int resp_n = magi_recv(sockfd, response, sizeof(response));
        if (resp_n > 0) {
            printf("\n=== HTTP Response ===\n");
            fwrite(response, 1, resp_n > 512 ? 512 : (size_t)resp_n, stdout);
            printf("...\n=== End ===\n\n");
        } else {
            printf("[HTTP] No response received\n");
        }

        magi_close(sockfd);
        return true;

    } else if (count >= 2 && strcmp(tokens[1], "http_server") == 0) {
        // <host> http_server start|stop [dir]
        if (count < 3) {
            printf("Usage: <host> http_server start [dir]\n");
            printf("       <host> http_server stop\n");
            return true;
        }

        if (strcmp(tokens[2], "start") == 0) {
            Host* host;
            if (!cli_find_host(tokens[0], &host)) {
                printf("Unknown host: %s\n", tokens[0]);
                return true;
            }

            const char* root_dir = count >= 4 ? tokens[3] : NULL;
            http_server_start(&host->ip_address, root_dir);
            if (!http_server_attach_host(host)) {
                printf("[HTTP] Failed to bind listening socket on %s:80\n", tokens[0]);
                http_server_stop();
                return true;
            }
            printf("[HTTP] Server started on %s:80\n", tokens[0]);
        } else if (strcmp(tokens[2], "stop") == 0) {
            if (http_server_stop()) {
                printf("[HTTP] Server stopped\n");
            } else {
                printf("[HTTP] No server running\n");
            }
        } else {
            printf("Unknown http_server subcommand: %s\n", tokens[2]);
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
