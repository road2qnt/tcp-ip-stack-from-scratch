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

#define RIP_OK            0
#define RIP_BAD_FORMAT    1
#define RIP_NOT_FOUND     2
#define RIP_NO_IP         3
#define RIP_SWITCH_NO_IP  4

static int resolve_ip_target_diag(const char* token, IpAddress* out_ip, int* err)
{
    if (err) *err = RIP_OK;
    if (token == NULL || out_ip == NULL) {
        if (err) *err = RIP_BAD_FORMAT;
        return 0;
    }
    if (ip_parse(token, out_ip)) return 1;

    int idx = simulator_find_node(&simulator, token);
    if (idx < 0) {
        if (err) *err = RIP_NOT_FOUND;
        return 0;
    }

    SimulatorNodeType t = simulator.nodes[idx].type;
    if (t == SIM_NODE_HOST) {
        Host* host = (Host*)simulator.nodes[idx].node;
        if (host->has_ip) {
            *out_ip = host->ip_address;
            return 1;
        }
        if (err) *err = RIP_NO_IP;
        return 0;
    }
    if (t == SIM_NODE_ROUTER) {
        Router* router = (Router*)simulator.nodes[idx].node;
        for (int i = 0; i < router->base.NUM_INTERFACES; i++) {
            if (router->interface_ips[i].has_ip) {
                *out_ip = router->interface_ips[i].ip_address;
                return 1;
            }
        }
        if (err) *err = RIP_NO_IP;
        return 0;
    }
    if (err) *err = RIP_SWITCH_NO_IP;
    return 0;
}

static void print_resolve_err(const char* token, int err)
{
    switch (err) {
        case RIP_NOT_FOUND:
            printf("[SIM] target '%s' not found - not an IP and not a node name (case-sensitive)\n", token);
            break;
        case RIP_NO_IP:
            printf("[SIM] node '%s' has no IP configured (configure via topology.json or dhcp_discover)\n",
                   token);
            break;
        case RIP_SWITCH_NO_IP:
            printf("[SIM] '%s' is a switch and has no IP - use a host or router as target\n", token);
            break;
        default:
            printf("[SIM] invalid target '%s'\n", token);
            break;
    }
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
    uint8_t bcast[] = {255, 255, 255, 255};
    IpAddress broadcast_ip = ip_init(bcast, 0);

    // Use 0.0.0.0 as source if no IP yet (correct per RFC 2131)
    IpAddress src_ip;
    if (host->has_ip) {
        src_ip = host->ip_address;
    } else {
        uint8_t zeros[] = {0, 0, 0, 0};
        src_ip = ip_init(zeros, 0);
    }

    int sockfd = magi_socket(AF_INET, SOCK_DGRAM);
    if (sockfd < 0) return 0;
    if (magi_socket_attach_host(sockfd, host) < 0) {
        magi_close(sockfd);
        return 0;
    }
    if (magi_bind(sockfd, &src_ip, DHCP_CLIENT_PORT) < 0) {
        magi_close(sockfd);
        return 0;
    }

    host->has_last_udp = false;
    int r = magi_sendto(sockfd, dhcp_raw, dhcp_len, &broadcast_ip, DHCP_SERVER_PORT);
    magi_close(sockfd);
    return r > 0 ? 1 : 0;
}

// Send a DNS query from 'requester' to 'server_ip' and parse the response.
// Returns 1 and fills out_ip on success; 0 on failure.
static int cli_dns_query(Host* requester, const char* hostname,
                          const IpAddress* server_ip, IpAddress* out_ip)
{
    if (requester == NULL || hostname == NULL || server_ip == NULL || out_ip == NULL) return 0;
    if (!requester->has_ip) return 0;

    uint8_t query_raw[512];
    size_t query_len = 0;
    uint16_t query_id = (uint16_t)((rand() % 0xFFFF) + 1);
    if (!dns_create_query(query_raw, sizeof(query_raw), &query_len, query_id, hostname)) {
        return 0;
    }

    int sockfd = magi_socket(AF_INET, SOCK_DGRAM);
    if (sockfd < 0) return 0;
    if (magi_socket_attach_host(sockfd, requester) < 0) {
        magi_close(sockfd);
        return 0;
    }
    if (magi_bind(sockfd, &requester->ip_address, 5353) < 0) {
        magi_close(sockfd);
        return 0;
    }

    requester->has_last_udp = false;
    int s = magi_sendto(sockfd, query_raw, query_len, server_ip, DNS_SERVER_PORT);
    if (s <= 0) {
        magi_close(sockfd);
        return 0;
    }

    // Simulator is synchronous: DNS response arrives before magi_sendto() returns.
    uint8_t response[512];
    IpAddress src_ip;
    uint16_t src_port;
    int r = magi_recvfrom(sockfd, response, sizeof(response), &src_ip, &src_port);
    magi_close(sockfd);

    if (r <= 0) return 0;
    return dns_parse_response(response, (size_t)r, out_ip);
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

Simulator* cli_simulator(void){
    return &simulator;
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
            printf("  e.g. link H1 SW1:24 10   (no brackets; use a colon for ports)\n");
            return true;
        }

        if (count >= 4) {
            delay = (float)atof(tokens[3]);
        }

        /* Diagnose common failure modes before calling simulator_link so
         * the user knows which endpoint / port is the problem. */
        for (int i = 0; i < 2; i++) {
            const char* ep = tokens[1 + i];
            char name[MAX_NAME];
            int port = 1;
            const char* colon = strchr(ep, ':');
            size_t name_len = colon ? (size_t)(colon - ep) : strlen(ep);
            if (name_len == 0 || name_len >= sizeof(name)) {
                printf("[SIM] Invalid endpoint '%s'\n", ep);
                return true;
            }
            memcpy(name, ep, name_len);
            name[name_len] = '\0';
            if (colon) port = atoi(colon + 1);

            if (strchr(name, '[') || strchr(name, ']')) {
                printf("[SIM] '%s' has illegal '[' or ']' - syntax is name:port (no brackets)\n", ep);
                return true;
            }

            int idx = simulator_find_node(&simulator, name);
            if (idx < 0) {
                printf("[SIM] node '%s' not found (names are case-sensitive)\n", name);
                return true;
            }
            Node* nd = simulator.nodes[idx].node;
            if (port < 1 || port > nd->NUM_INTERFACES) {
                printf("[SIM] %s has no port %d (valid: 1..%d)\n",
                       name, port, nd->NUM_INTERFACES);
                return true;
            }
            if (nd->interfaces[port - 1].link != NULL) {
                printf("[SIM] %s:%d is already linked\n", name, port);
                return true;
            }
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

        if (!host->has_ip) {
            printf("[SIM] source host '%s' has no IP (configure via topology.json or dhcp_discover)\n",
                   tokens[0]);
            return true;
        }

        int err = 0;
        if (!resolve_ip_target_diag(tokens[2], &target_ip, &err)) {
            print_resolve_err(tokens[2], err);
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

        if (!host->has_ip) {
            printf("[SIM] source host '%s' has no IP (configure via topology.json or dhcp_discover)\n",
                   tokens[0]);
            return true;
        }

        int err = 0;
        if (!resolve_ip_target_diag(tokens[2], &target_ip, &err)) {
            print_resolve_err(tokens[2], err);
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
        uint32_t preferred_port = (uint32_t)dst_port + 1000u;
        if (preferred_port > 65535u) preferred_port = 49152u;
        uint16_t source_port = host_select_tcp_source_port(
            host, &target_ip, (uint16_t)dst_port,
            (uint16_t)preferred_port);
        if (source_port == 0) {
            tcp_socket_free(sock);
            printf("[%s] No available TCP source port\n", host->base.NAME);
            return true;
        }

        tcp_socket_connect(sock, &host->ip_address, source_port,
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

        if (!tcp_socket_send(sock, (const uint8_t*)data, data_len)) {
            return true;
        }

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
                    sock->send_seq += (uint32_t)data_len;
                    host_send_l3_packet(host, &sock->remote_ip, ETHERNET_TYPE_IPV4, ip_raw, ip_len);
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

        uint8_t buffer[TCP_MAX_PAYLOAD + 1];
        int received = tcp_socket_recv(sock, buffer, sizeof(buffer) - 1);
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

        // Find a socket that can actively or passively finish closing.
        TCPSocket* sock = NULL;
        for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
            if (host->tcp_sockets[i].in_use &&
                (host->tcp_sockets[i].state == TCP_ESTABLISHED ||
                 host->tcp_sockets[i].state == TCP_CLOSE_WAIT)) {
                sock = &host->tcp_sockets[i];
                break;
            }
        }

        if (sock == NULL) {
            printf("[%s] No TCP connection available to close\n", host->base.NAME);
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
                    if (!tcp_socket_close(sock)) {
                        return true;
                    }
                    sock->send_seq++;
                    host_send_l3_packet(host, &sock->remote_ip, ETHERNET_TYPE_IPV4, ip_raw, ip_len);
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
        int conn_result = magi_connect(sockfd, &target_ip, (uint16_t)port);

        char ip_buf[20];
        ip_to_string(&target_ip, ip_buf, sizeof(ip_buf), false);
        if (conn_result < 0) {
            printf("[%s] MagiSocket connect failed to %s:%d\n",
                   host->base.NAME, ip_buf, port);
            magi_close(sockfd);
        } else {
            host->magi_sock_fd = sockfd;
            printf("[%s] MagiSocket connected to %s:%d (fd=%d)\n",
                   host->base.NAME, ip_buf, port, sockfd);
        }
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

        Host* host;
        if (!cli_find_host(tokens[0], &host)) {
            printf("Unknown host: %s\n", tokens[0]);
            return true;
        }

        IpAddress server_ip;
        bool has_server = false;

        if (count >= 4) {
            if (!ip_parse(tokens[3], &server_ip)) {
                printf("Invalid server IP: %s\n", tokens[3]);
                return true;
            }
            has_server = true;
        } else {
            // Use the running DNS server's IP if one is attached
            Host* dns_host = dns_server_get_bound_host();
            if (dns_host != NULL) {
                server_ip = dns_host->ip_address;
                has_server = true;
            }
        }

        int ok = 0;
        if (has_server) {
            ok = cli_dns_query(host, tokens[2], &server_ip, &resolved);
            if (!ok) {
                printf("[DNS] No response or NXDOMAIN for: %s\n", tokens[2]);
                return true;
            }
        } else {
            // No server available: fall back to direct table lookup
            ok = dns_server_resolve(tokens[2], &resolved);
            if (!ok) {
                printf("[DNS] Could not resolve: %s\n", tokens[2]);
                return true;
            }
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

        // Resolve hostname: IP literal → network DNS query → direct table fallback
        bool resolved_ok = false;
        if (ip_parse(hostname, &resolved)) {
            resolved_ok = true;
        } else {
            Host* dns_host = dns_server_get_bound_host();
            if (dns_host != NULL) {
                resolved_ok = cli_dns_query(host, hostname, &dns_host->ip_address, &resolved);
            }
            if (!resolved_ok) {
                resolved_ok = dns_server_resolve(hostname, &resolved);
            }
        }
        if (!resolved_ok) {
            printf("[HTTP] Could not resolve hostname: %s\n", hostname);
            return true;
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
        if (magi_connect(sockfd, &resolved, HTTP_SERVER_PORT) < 0) {
            printf("[HTTP] Connection to %s:%d failed\n", ip_buf, HTTP_SERVER_PORT);
            magi_close(sockfd);
            return true;
        }

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
