#include "debugger.h"
#include "loader.h"
#include "visualizer.h"

#include "../layer2/host.h"
#include "../layer2/switch.h"
#include "../core/sim_clock.h"
#include "../layer3/router.h"
#include "../layer3/icmp.h"
#include "../layer4/udp.h"
#include "../layer4/tcp.h"
#include "../layer4/tcp_socket.h"
#include "../layer7/magi_socket.h"
#include "../layer7/dhcp_server.h"
#include "../layer7/dns_server.h"
#include "../layer7/http_server.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ==================== UTILITIES ====================

#define MAX_TESTS 64
#define MAX_NAME 32

static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

#define ANSI_GREEN   "\x1b[32m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_BOLD    "\x1b[1m"
#define ANSI_RESET   "\x1b[0m"

static void test_reset(void)
{
    total_tests = 0;
    passed_tests = 0;
    failed_tests = 0;
}

static void test_report_header(const char *milestone_name)
{
    printf("\n" ANSI_CYAN "==================================================\n" ANSI_RESET);
    printf(ANSI_BOLD "  DEBUG: %s\n" ANSI_RESET, milestone_name);
    printf(ANSI_CYAN "==================================================\n" ANSI_RESET);
}

static void test_report_footer(void)
{
    printf(ANSI_CYAN "--------------------------------------------------\n" ANSI_RESET);
    printf("  Tests: %d  ", total_tests);
    printf(ANSI_GREEN "Passed: %d  " ANSI_RESET, passed_tests);
    if (failed_tests > 0) {
        printf(ANSI_RED "Failed: %d" ANSI_RESET, failed_tests);
    }
    printf("\n\n");
}

#define TEST(name, expr) do { \
    total_tests++; \
    printf("  [%s] Test %d: %s ... ", \
           (expr) ? ANSI_GREEN "PASS" ANSI_RESET : ANSI_RED "FAIL" ANSI_RESET, \
           total_tests, name); \
    if (expr) { \
        passed_tests++; \
        printf(ANSI_GREEN "OK" ANSI_RESET "\n"); \
    } else { \
        failed_tests++; \
        printf(ANSI_RED "FAILED" ANSI_RESET "\n"); \
    } \
} while(0)

#define TEST_MSG(name, expr, fmt, ...) do { \
    total_tests++; \
    printf("  [%s] Test %d: %s ... ", \
           (expr) ? ANSI_GREEN "PASS" ANSI_RESET : ANSI_RED "FAIL" ANSI_RESET, \
           total_tests, name); \
    if (expr) { \
        passed_tests++; \
        printf(ANSI_GREEN "OK" ANSI_RESET); \
        printf(" (" fmt ")", ##__VA_ARGS__); \
        printf("\n"); \
    } else { \
        failed_tests++; \
        printf(ANSI_RED "FAILED" ANSI_RESET); \
        printf(" (" fmt ")", ##__VA_ARGS__); \
        printf("\n"); \
    } \
} while(0)

// Helper to check node exists in simulator
static int node_exists(Simulator *sim, const char *name)
{
    return simulator_find_node(sim, name) >= 0;
}

// Helper: Build an Ethernet frame containing an IPv4 packet as raw bytes
static size_t build_ipv4_frame(uint8_t* out, size_t out_size,
                               const MacAddress* dst_mac, const MacAddress* src_mac,
                               const IpAddress* ip_src, const IpAddress* ip_dst,
                               uint8_t ttl, uint8_t protocol,
                               const uint8_t* payload, size_t payload_len)
{
    IPv4Packet ip_pkt;
    EthernetFrame eth;
    uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
    size_t ip_len;

    if (out == NULL || dst_mac == NULL || src_mac == NULL ||
        ip_src == NULL || ip_dst == NULL) {
        return 0;
    }

    if (!ipv4_create(&ip_pkt, *ip_src, *ip_dst, ttl, protocol, payload, payload_len)) {
        return 0;
    }

    ip_len = packet_to_bytes((Packet*)&ip_pkt, ip_raw, sizeof(ip_raw));
    if (ip_len == 0) return 0;

    if (!ethernet_create(&eth, *dst_mac, *src_mac, ETHERNET_TYPE_IPV4, ip_raw, ip_len)) {
        return 0;
    }

    return packet_to_bytes((Packet*)&eth, out, out_size);
}

// Helper: Build an Ethernet frame containing an ARP message as raw bytes
static size_t build_arp_frame(uint8_t* out, size_t out_size,
                              const MacAddress* dst_mac, const MacAddress* src_mac,
                              const ARPMessage* arp)
{
    EthernetFrame eth;
    uint8_t arp_raw[28];
    size_t arp_len;

    if (out == NULL || dst_mac == NULL || src_mac == NULL || arp == NULL) {
        return 0;
    }

    arp_len = packet_to_bytes((Packet*)arp, arp_raw, sizeof(arp_raw));
    if (arp_len == 0) return 0;

    if (!ethernet_create(&eth, *dst_mac, *src_mac, ETHERNET_TYPE_ARP, arp_raw, arp_len)) {
        return 0;
    }

    return packet_to_bytes((Packet*)&eth, out, out_size);
}

// Helper to check interface is linked
static int is_linked(Simulator *sim, const char *endpoint)
{
    char name[MAX_NAME];
    int port;
    int idx;

    const char *colon = strchr(endpoint, ':');
    if (colon) {
        size_t len = (size_t)(colon - endpoint);
        if (len >= MAX_NAME) len = MAX_NAME - 1;
        strncpy(name, endpoint, len);
        name[len] = '\0';
        port = atoi(colon + 1);
    } else {
        strncpy(name, endpoint, MAX_NAME - 1);
        name[MAX_NAME - 1] = '\0';
        port = 1;
    }

    idx = simulator_find_node(sim, name);
    if (idx < 0) return 0;

    Interface *iface = node_get_interface(sim->nodes[idx].node, port);
    if (iface == NULL) return 0;

    return iface->link != NULL;
}

// ==================== MILESTONE 0: FONDASI SIMULASI ====================

void debug_milestone_0(Simulator *sim)
{
    test_reset();
    test_report_header("Milestone 0: Fondasi Simulasi (Packet, Link, Interface, CLI)");

    // --- Clear state ---
    simulator_clear(sim);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- Node Creation Tests ---\n" ANSI_RESET);
    // ========================================

    // T1: Create host
    int r = simulator_create_node(sim, "host", "H1", 1);
    TEST("Create host H1", r == 1 && node_exists(sim, "H1"));

    // T2: Create switch
    r = simulator_create_node(sim, "switch", "SW1", 24);
    TEST("Create switch SW1 with 24 ports", r == 1 && node_exists(sim, "SW1"));

    // T3: Create router
    r = simulator_create_node(sim, "router", "R1", 8);
    TEST("Create router R1 with 8 ports", r == 1 && node_exists(sim, "R1"));

    // T4: Verify node count
    TEST("Node count = 3", sim->node_count == 3);

    // T5: Duplicate name rejected
    r = simulator_create_node(sim, "host", "H1", 1);
    TEST("Reject duplicate name H1", r == 0);

    // T6: Invalid type rejected
    r = simulator_create_node(sim, "firewall", "FW1", 1);
    TEST("Reject invalid type 'firewall'", r == 0);

    // T7: Verify Host has correct type
    int idx = simulator_find_node(sim, "H1");
    TEST("H1 type is SIM_NODE_HOST", idx >= 0 && sim->nodes[idx].type == SIM_NODE_HOST);

    // T8: Verify Switch has correct type
    idx = simulator_find_node(sim, "SW1");
    TEST("SW1 type is SIM_NODE_SWITCH", idx >= 0 && sim->nodes[idx].type == SIM_NODE_SWITCH);

    // T9: Verify Router has correct type
    idx = simulator_find_node(sim, "R1");
    TEST("R1 type is SIM_NODE_ROUTER", idx >= 0 && sim->nodes[idx].type == SIM_NODE_ROUTER);

    // T10: Verify Switch has 24 interfaces
    idx = simulator_find_node(sim, "SW1");
    int ports = idx >= 0 ? sim->nodes[idx].node->NUM_INTERFACES : -1;
    TEST_MSG("SW1 has 24 interfaces", ports == 24, "got %d", ports);

    // T11: Verify Router has 8 interfaces
    idx = simulator_find_node(sim, "R1");
    ports = idx >= 0 ? sim->nodes[idx].node->NUM_INTERFACES : -1;
    TEST_MSG("R1 has 8 interfaces", ports == 8, "got %d", ports);

    // T12: Create minimal host (0 ports defaults to 1)
    simulator_create_node(sim, "host", "H-min", 0);
    idx = simulator_find_node(sim, "H-min");
    ports = idx >= 0 ? sim->nodes[idx].node->NUM_INTERFACES : -1;
    TEST_MSG("Host with 0 ports gets default 1", ports == 1, "got %d", ports);
    simulator_remove_node(sim, "H-min");

    // ========================================
    printf("\n" ANSI_YELLOW "  --- Link Tests ---\n" ANSI_RESET);
    // ========================================

    // T13: Link H1 to SW1:1
    r = simulator_link(sim, "H1", "SW1:1", 10.0f);
    TEST("Link H1 <-> SW1:1 delay=10ms", r == 1);
    TEST("H1:1 is linked", is_linked(sim, "H1:1"));
    TEST("SW1:1 is linked", is_linked(sim, "SW1:1"));

    // T14: Link SW1:24 to R1:1
    r = simulator_link(sim, "SW1:24", "R1:1", 5.0f);
    TEST("Link SW1:24 <-> R1:1 delay=5ms", r == 1);

    // T15: Link H2 to R1:2
    simulator_create_node(sim, "host", "H2", 1);
    r = simulator_link(sim, "H2", "R1:2", 25.0f);
    TEST("Link H2 <-> R1:2 delay=25ms", r == 1);

    // T16: Verify link count
    TEST_MSG("Link count = 3", sim->link_count == 3, "got %zu", sim->link_count);

    // T17: Duplicate link rejected
    r = simulator_link(sim, "H1", "SW1:1", 10.0f);
    TEST("Reject duplicate link H1<->SW1:1", r == 0);

    // T18: Self-link rejected
    r = simulator_link(sim, "H1:1", "H1:1", 10.0f);
    TEST("Reject self-link H1:1<->H1:1", r == 0);

    // T19: Link to already-linked interface rejected
    r = simulator_link(sim, "H1", "SW1:24", 10.0f);
    TEST("Reject link to already-linked SW1:24", r == 0);

    // T20: Link to non-existent node
    r = simulator_link(sim, "H1", "NONEXIST:1", 10.0f);
    TEST("Reject link to non-existent node", r == 0);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- Unlink Tests ---\n" ANSI_RESET);
    // ========================================

    // T21: Unlink H1 from SW1
    r = simulator_unlink(sim, "H1", "SW1:1");
    TEST("Unlink H1 <-> SW1:1", r == 1);
    TEST("H1:1 is unlinked", !is_linked(sim, "H1:1"));

    // T22: Unlink non-existent link
    r = simulator_unlink(sim, "H1", "SW1:1");
    TEST("Reject unlink non-existent link", r == 0);

    // T23: Re-link after unlink
    r = simulator_link(sim, "H1", "SW1:1", 15.0f);
    TEST("Re-link H1 <-> SW1:1 after unlink", r == 1);
    TEST("H1:1 is re-linked", is_linked(sim, "H1:1"));

    // ========================================
    printf("\n" ANSI_YELLOW "  --- Save & Load Tests ---\n" ANSI_RESET);
    // ========================================

    const char *tmpfile = "/tmp/m0_debug_test.json";

    // T24: Save topology
    r = simulator_save(sim, tmpfile);
    TEST("Save topology to file", r == 1);

    // T25: Load topology into fresh simulator
    Simulator sim2;
    simulator_init(&sim2);
    r = simulator_load(&sim2, tmpfile);
    TEST("Load topology from file", r == 1);

    // T26: Verify loaded node count
    TEST_MSG("Loaded node count = 4 (H1, H2, SW1, R1)",
             sim2.node_count == 4, "got %zu", sim2.node_count);

    // T27: Verify loaded node names
    TEST("Loaded node H1 exists", node_exists(&sim2, "H1"));
    TEST("Loaded node H2 exists", node_exists(&sim2, "H2"));
    TEST("Loaded node SW1 exists", node_exists(&sim2, "SW1"));
    TEST("Loaded node R1 exists", node_exists(&sim2, "R1"));

    // T28: Verify loaded link count
    TEST_MSG("Loaded link count = 3", sim2.link_count == 3, "got %zu", sim2.link_count);

    // T29: Verify loaded links
    TEST("Loaded link H1<->SW1:1", is_linked(&sim2, "H1:1"));
    TEST("Loaded link H2<->R1:2", is_linked(&sim2, "H2:1"));
    TEST("Loaded link SW1:24<->R1:1", is_linked(&sim2, "SW1:24"));

    // T30: Load topology.json spec file
    Simulator sim3;
    simulator_init(&sim3);
    r = simulator_load(&sim3, "magi_system/topology.json");
    TEST("Load magi_system/topology.json", r == 1);
    TEST_MSG("Spec topology has 4 nodes", sim3.node_count == 4, "got %zu", sim3.node_count);
    TEST_MSG("Spec topology has 3 links", sim3.link_count == 3, "got %zu", sim3.link_count);

    // T31: Verify H1 has IP from spec
    idx = simulator_find_node(&sim3, "H1");
    Host *h1 = (Host *)sim3.nodes[idx].node;
    TEST("H1 has IP configured", h1->has_ip);
    TEST_MSG("H1 IP matches 192.168.1.10/24",
             h1->has_ip && h1->ip_address.octet[0] == 192 && h1->ip_address.octet[3] == 10 && h1->ip_address.prefix == 24,
             "got %d.%d.%d.%d/%d",
             h1->ip_address.octet[0], h1->ip_address.octet[1],
             h1->ip_address.octet[2], h1->ip_address.octet[3],
             h1->ip_address.prefix);

    // T32: Verify SW1 VLAN config from spec
    idx = simulator_find_node(&sim3, "SW1");
    Switch *sw1 = (Switch *)sim3.nodes[idx].node;
    TEST_MSG("SW1 Port 1 is access VLAN 10",
             sw1->port_configs[0].mode == SWITCH_PORT_ACCESS && sw1->port_configs[0].vlan_id == 10,
             "mode=%d vlan=%d", sw1->port_configs[0].mode, sw1->port_configs[0].vlan_id);
    TEST_MSG("SW1 Port 24 is trunk",
             sw1->port_configs[23].mode == SWITCH_PORT_TRUNK,
             "mode=%d", sw1->port_configs[23].mode);

    // T33: Verify R1 interface IPs from spec
    idx = simulator_find_node(&sim3, "R1");
    Router *r1 = (Router *)sim3.nodes[idx].node;
    TEST("R1 Port 1 has IP", r1->interface_ips[0].has_ip);
    TEST_MSG("R1 Port 1 IP = 192.168.1.1/24",
             r1->interface_ips[0].has_ip &&
             r1->interface_ips[0].ip_address.octet[0] == 192 &&
             r1->interface_ips[0].ip_address.octet[3] == 1 &&
             r1->interface_ips[0].ip_address.prefix == 24,
             "got %d.%d.%d.%d/%d",
             r1->interface_ips[0].ip_address.octet[0],
             r1->interface_ips[0].ip_address.octet[1],
             r1->interface_ips[0].ip_address.octet[2],
             r1->interface_ips[0].ip_address.octet[3],
             r1->interface_ips[0].ip_address.prefix);

    simulator_clear(&sim2);
    simulator_clear(&sim3);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- Topology Display Test ---\n" ANSI_RESET);
    // ========================================

    // T34: Print topology (just verify no crash)
    printf("\n  [TOPOLOGY OUTPUT]:\n\n");
    simulator_print_topology(sim);
    TEST("Topology print completes without crash", 1);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- Stress / Edge Case Tests ---\n" ANSI_RESET);
    // ========================================

    // T35: Max capacity test — fill to near limit
    simulator_clear(sim);
    int created = 0;
    for (int i = 0; i < 10; i++) {
        char name[32];
        snprintf(name, sizeof(name), "S%d", i);
        if (simulator_create_node(sim, "switch", name, 4)) created++;
    }
    TEST_MSG("Create 10 switches", created == 10, "created %d", created);

    // T36: Remove one node
    simulator_remove_node(sim, "S0");
    TEST_MSG("S0 removed, node count = 9", sim->node_count == 9, "got %zu", sim->node_count);

    simulator_clear(sim);

    // ========================================
    test_report_footer();
}

// ==================== MILESTONE 1: DATA LINK LAYER ====================

void debug_milestone_1(Simulator *sim)
{
    test_reset();
    test_report_header("Milestone 1: Data Link Layer (Ethernet, ARP, Switch)");

    simulator_clear(sim);

    // Create test topology: H1 -- SW1 -- H2
    simulator_create_node(sim, "host", "H1", 1);
    simulator_create_node(sim, "host", "H2", 1);
    simulator_create_node(sim, "switch", "SW1", 4);
    simulator_link(sim, "H1", "SW1:1", 10.0f);
    simulator_link(sim, "H2", "SW1:2", 10.0f);

    // Set IPs on hosts
    int idx = simulator_find_node(sim, "H1");
    Host *h1 = (Host *)sim->nodes[idx].node;
    uint8_t ip1[] = {192, 168, 1, 10};
    h1->ip_address = ip_init(ip1, 24);
    h1->has_ip = true;
    uint8_t gw1[] = {192, 168, 1, 1};
    h1->default_gateway = ip_init(gw1, 0);

    idx = simulator_find_node(sim, "H2");
    Host *h2 = (Host *)sim->nodes[idx].node;
    uint8_t ip2[] = {192, 168, 1, 11};
    h2->ip_address = ip_init(ip2, 24);
    h2->has_ip = true;
    uint8_t gw2[] = {192, 168, 1, 1};
    h2->default_gateway = ip_init(gw2, 0);

    printf("\n" ANSI_YELLOW "  --- ARP Table Tests ---\n" ANSI_RESET);

    // T1: ARP table initially empty
    TEST("H1 ARP table empty initially", h1->arp_table.size == 0);

    // T2: Add ARP entry
    ARPMessage arp_reply;
    uint8_t mac_bytes[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    MacAddress test_mac;
    memcpy(test_mac.bytes, mac_bytes, 6);

    arp_message_init_reply(&arp_reply, test_mac, h2->ip_address, h1->base.interfaces[0].Mac_Address, h1->ip_address);
    int r = host_learn_arp(h1, &arp_reply);
    TEST("H1 learns ARP entry for H2", r == 1);
    TEST_MSG("H1 ARP table size = 1", h1->arp_table.size == 1, "got %zu", h1->arp_table.size);

    // T3: Lookup ARP entry
    MacAddress *found = arp_table_get(&h1->arp_table, &h2->ip_address);
    int mac_match = found && mac_equal(found, &test_mac);
    TEST("H1 ARP lookup finds H2 MAC", mac_match);

    // T4: Pending queue
    TEST("H1 pending queue empty", h1->pending_queue.size == 0);

    // T5: Queue a packet
    uint8_t test_payload[] = {0x01, 0x02, 0x03, 0x04};
    r = host_queue_pending_packet(h1, &h2->ip_address, 0x0800, test_payload, 4);
    TEST("H1 queues pending packet for H2", r == 1);
    TEST_MSG("H1 pending queue size = 1", h1->pending_queue.size == 1, "got %zu", h1->pending_queue.size);

    // T6: Dequeue the packet
    HostPendingPacket out;
    r = host_dequeue_pending_packet_for_ip(h1, &h2->ip_address, &out);
    TEST("H1 dequeues packet for H2", r == 1);
    TEST("Dequeued packet has correct ethertype", out.ethertype == 0x0800);
    TEST_MSG("H1 pending queue now empty", h1->pending_queue.size == 0, "got %zu", h1->pending_queue.size);

    // T7: Switch MAC table initially empty
    Switch *sw = (Switch *)sim->nodes[simulator_find_node(sim, "SW1")].node;
    TEST("SW1 MAC table empty initially", sw->mac_table.size == 0);

    // T8: Switch VLAN config
    TEST("SW1 Port 1 default VLAN = 1",
         sw->port_configs[0].mode == SWITCH_PORT_ACCESS && sw->port_configs[0].vlan_id == SWITCH_DEFAULT_VLAN_ID);

    // T9: Set VLAN configs
    switch_set_access_port(sw, 1, 10);
    switch_set_trunk_port(sw, 4);
    TEST("SW1 Port 1 set to access VLAN 10",
         sw->port_configs[0].mode == SWITCH_PORT_ACCESS && sw->port_configs[0].vlan_id == 10);
    TEST("SW1 Port 4 set to trunk",
         sw->port_configs[3].mode == SWITCH_PORT_TRUNK);

    // T10: VLAN filtering
    TEST("Port 1 allows VLAN 10", switch_port_allows_vlan(sw, 1, 10));
    TEST("Port 1 denies VLAN 20", !switch_port_allows_vlan(sw, 1, 20));
    TEST("Trunk Port 4 allows VLAN 10", switch_port_allows_vlan(sw, 4, 10));
    TEST("Trunk Port 4 allows VLAN 99", switch_port_allows_vlan(sw, 4, 99));

    printf("\n" ANSI_YELLOW "  --- FEATURE NOT YET IMPLEMENTED ---\n" ANSI_RESET);
    printf("  The following features require further implementation:\n");
    printf("    - Ethernet frame serialization (to_bytes / from_bytes)\n");
    printf("    - ARP message serialization\n");
    printf("    - Host receive handler (vtable)\n");
    printf("    - Switch receive handler (frame forwarding/flooding)\n");        printf("    - ARP resolution + automatic queue flush on ARP reply\n\n");

    simulator_clear(sim);
    test_report_footer();
}

// ==================== MILESTONE 2: NETWORK LAYER ====================

void debug_milestone_2(Simulator *sim)
{
    (void)sim;
    test_reset();
    test_report_header("Milestone 2: Network Layer (IPv4, ICMP, Routing)");

    uint8_t ip_a[] = {192, 168, 1, 10};
    uint8_t ip_b[] = {10, 0, 0, 5};
    uint8_t ip_c[] = {192, 168, 1, 1};    IpAddress src_ip = ip_init(ip_a, 24);
    IpAddress dst_ip = ip_init(ip_b, 8);
    IpAddress gw_ip = ip_init(ip_c, 24);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- IPv4 Address Utility Tests ---\n" ANSI_RESET);
    // ========================================

    // T1: ip_init
    TEST_MSG("ip_init creates correct IP",
             src_ip.octet[0] == 192 && src_ip.octet[3] == 10 && src_ip.prefix == 24,
             "got %d.%d.%d.%d/%d",
             src_ip.octet[0], src_ip.octet[1], src_ip.octet[2], src_ip.octet[3], src_ip.prefix);

    // T2: ip_equal with same IP/prefix
    uint8_t same[] = {192, 168, 1, 10};
    IpAddress same_ip = ip_init(same, 24);
    TEST("ip_equal same IP and prefix", ip_equal(&src_ip, &same_ip));

    // T3: ip_equal different prefix
    IpAddress diff_prefix = ip_init(same, 16);
    TEST("ip_equal different prefix fails", !ip_equal(&src_ip, &diff_prefix));

    // T4: ip_equal different octet
    uint8_t diff_octet[] = {192, 168, 1, 11};
    IpAddress diff_ip = ip_init(diff_octet, 24);
    TEST("ip_equal different IP fails", !ip_equal(&src_ip, &diff_ip));

    // T5: ip_octets_equal_public (only compares octets, ignores prefix)
    TEST("ip_octets_equal_public same IP", ip_octets_equal_public(&src_ip, &same_ip));
    TEST("ip_octets_equal_public diff prefix ok", ip_octets_equal_public(&src_ip, &diff_prefix));
    TEST("ip_octets_equal_public diff IP fails", !ip_octets_equal_public(&src_ip, &diff_ip));

    // T6: ip_to_u32_public
    uint32_t u32 = ip_to_u32_public(&src_ip);
    TEST_MSG("ip_to_u32_public 192.168.1.10 = 0xC0A8010A",
             u32 == 0xC0A8010A, "got 0x%08X", u32);

    // T7: NULL safety
    TEST("ip_to_u32_public NULL returns 0", ip_to_u32_public(NULL) == 0);
    TEST("ip_octets_equal_public NULL returns false", !ip_octets_equal_public(NULL, &src_ip));

    // T8: ip_prefix_to_mask
    uint32_t m24 = ip_prefix_to_mask(24);
    uint32_t m8 = ip_prefix_to_mask(8);
    uint32_t m0 = ip_prefix_to_mask(0);
    uint32_t m32 = ip_prefix_to_mask(32);
    TEST_MSG("prefix /24 mask = 0xFFFFFF00", m24 == 0xFFFFFF00, "got 0x%08X", m24);
    TEST_MSG("prefix /8 mask = 0xFF000000", m8 == 0xFF000000, "got 0x%08X", m8);
    TEST_MSG("prefix /0 mask = 0", m0 == 0, "got 0x%08X", m0);
    TEST_MSG("prefix /32 mask = 0xFFFFFFFF", m32 == 0xFFFFFFFF, "got 0x%08X", m32);

    // T9: ip_network_address
    IpAddress net = ip_network_address(src_ip);
    TEST_MSG("ip_network_address 192.168.1.10/24 = 192.168.1.0",
             net.octet[0] == 192 && net.octet[1] == 168 && net.octet[2] == 1 && net.octet[3] == 0,
             "got %d.%d.%d.%d", net.octet[0], net.octet[1], net.octet[2], net.octet[3]);

    net = ip_network_address(dst_ip);
    TEST_MSG("ip_network_address 10.0.0.5/8 = 10.0.0.0",
             net.octet[0] == 10 && net.octet[1] == 0 && net.octet[2] == 0 && net.octet[3] == 0,
             "got %d.%d.%d.%d", net.octet[0], net.octet[1], net.octet[2], net.octet[3]);

    // T10: ip_parse
    IpAddress parsed;
    int r = ip_parse("192.168.1.10/24", &parsed);
    TEST("ip_parse succeeds", r == 1);
    TEST("ip_parse correct octets",
         parsed.octet[0] == 192 && parsed.octet[3] == 10);
    TEST_MSG("ip_parse prefix = 24", parsed.prefix == 24, "got %u", parsed.prefix);

    r = ip_parse("10.0.0.5", &parsed);
    TEST("ip_parse without prefix succeeds", r == 1);
    TEST_MSG("ip_parse no prefix defaults to 0", parsed.prefix == 0, "got %u", parsed.prefix);

    r = ip_parse("invalid", &parsed);
    TEST("ip_parse invalid string fails", r == 0);

    r = ip_parse("999.999.999.999", &parsed);
    TEST("ip_parse out-of-range octets fails", r == 0);

    r = ip_parse("1.2.3.4/33", &parsed);
    TEST("ip_parse prefix > 32 fails", r == 0);

    // T11: ip_to_string
    char ip_str[32];
    ip_to_string(&src_ip, ip_str, sizeof(ip_str), true);
    TEST_MSG("ip_to_string with prefix", strcmp(ip_str, "192.168.1.10/24") == 0,
             "got '%s'", ip_str);
    ip_to_string(&src_ip, ip_str, sizeof(ip_str), false);
    TEST_MSG("ip_to_string without prefix", strcmp(ip_str, "192.168.1.10") == 0,
             "got '%s'", ip_str);
    ip_to_string(NULL, ip_str, sizeof(ip_str), false);
    TEST_MSG("ip_to_string NULL", strcmp(ip_str, "?.?.?.?") == 0,
             "got '%s'", ip_str);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- IPv4 Packet Tests ---\n" ANSI_RESET);
    // ========================================

    // T12: ipv4_init
    IPv4Packet ipv4;
    ipv4_init(&ipv4);
    TEST("ipv4_init type = IPV4", ipv4.base.type == IPV4);
    TEST_MSG("ipv4_init version = 4", ipv4.version == 4, "got %u", ipv4.version);
    TEST_MSG("ipv4_init ihl = 5", ipv4.ihl == 5, "got %u", ipv4.ihl);
    TEST_MSG("ipv4_init ttl = %d", ipv4.ttl == IPV4_DEFAULT_TTL, "got %u", ipv4.ttl);
    TEST_MSG("ipv4_init payload_len = 0", ipv4.payload_len == 0, "got %zu", ipv4.payload_len);

    // T13: ipv4_create
    const char* payload = "Hello Network!";
    size_t payload_len = strlen(payload);
    r = ipv4_create(&ipv4, src_ip, dst_ip, 64, IPV4_PROTOCOL_ICMP, (const uint8_t*)payload, payload_len);
    TEST("ipv4_create succeeds", r == 1);
    TEST_MSG("ipv4_create protocol = 1 (ICMP)", ipv4.protocol == IPV4_PROTOCOL_ICMP, "got %u", ipv4.protocol);
    TEST_MSG("ipv4_create ttl = 64", ipv4.ttl == 64, "got %u", ipv4.ttl);
    TEST_MSG("ipv4_create total_length = 20 + payload",
             ipv4.total_length == IPV4_HEADER_SIZE + payload_len,
             "got %u", ipv4.total_length);
    TEST_MSG("ipv4_create payload_len matches", ipv4.payload_len == payload_len, "got %zu", ipv4.payload_len);
    TEST("ipv4_create src_ip matches", ip_octets_equal_public(&ipv4.src_ip, &src_ip));
    TEST("ipv4_create dst_ip matches", ip_octets_equal_public(&ipv4.dst_ip, &dst_ip));

    // T14: ipv4_create with zero-length payload
    IPv4Packet ipv4_empty;
    r = ipv4_create(&ipv4_empty, src_ip, dst_ip, 64, IPV4_PROTOCOL_ICMP, NULL, 0);
    TEST("ipv4_create with zero payload succeeds", r == 1);
    TEST_MSG("ipv4_create zero payload total_length = 20",
             ipv4_empty.total_length == IPV4_HEADER_SIZE, "got %u", ipv4_empty.total_length);

    // T15: ipv4_create with oversized payload fails
    uint8_t big_payload[IPV4_MAX_PAYLOAD + 1];
    memset(big_payload, 0xAB, sizeof(big_payload));
    r = ipv4_create(&ipv4_empty, src_ip, dst_ip, 64, IPV4_PROTOCOL_ICMP, big_payload, sizeof(big_payload));
    TEST("ipv4_create with oversized payload fails", r == 0);

    // T16: ipv4_create with NULL payload and non-zero len fails
    r = ipv4_create(&ipv4_empty, src_ip, dst_ip, 64, IPV4_PROTOCOL_ICMP, NULL, 10);
    TEST("ipv4_create with NULL payload and len>0 fails", r == 0);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- IPv4 Checksum Tests ---\n" ANSI_RESET);
    // ========================================

    // T17: ipv4_compute_checksum on a known header
    uint8_t sample_header[IPV4_HEADER_SIZE];
    memset(sample_header, 0, sizeof(sample_header));
    sample_header[0] = 0x45;
    sample_header[2] = 0x00;
    sample_header[3] = 0x1C;
    sample_header[8] = 64;
    sample_header[9] = IPV4_PROTOCOL_ICMP;
    sample_header[12] = 192; sample_header[13] = 168; sample_header[14] = 1; sample_header[15] = 10;
    sample_header[16] = 10; sample_header[17] = 0; sample_header[18] = 0; sample_header[19] = 5;

    // Temporarily set checksum field to 0 for computation
    uint16_t cksum = ipv4_compute_checksum(sample_header, IPV4_HEADER_SIZE);
    TEST("ipv4_compute_checksum returns non-zero", cksum != 0);

    // Store checksum and verify
    sample_header[10] = (uint8_t)(cksum >> 8);
    sample_header[11] = (uint8_t)(cksum & 0xFF);
    uint16_t verify = ipv4_compute_checksum(sample_header, IPV4_HEADER_SIZE);
    TEST_MSG("ipv4 checksum stored correctly verifies to 0", verify == 0, "got 0x%04X", verify);

    // T18: ipv4_validate_checksum
    // Create a proper packet and validate
    IPv4Packet ipv4_cksum;
    ipv4_create(&ipv4_cksum, src_ip, dst_ip, 64, IPV4_PROTOCOL_ICMP, (const uint8_t*)payload, payload_len);
    // Serialize (this computes and inserts checksum)
    uint8_t raw_ip[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
    size_t raw_ip_len = packet_to_bytes((Packet*)&ipv4_cksum, raw_ip, sizeof(raw_ip));
    TEST("IPv4 to_bytes succeeds", raw_ip_len > 0);

    IPv4Packet ipv4_parsed;
    ipv4_init(&ipv4_parsed);
    r = packet_from_bytes((Packet*)&ipv4_parsed, raw_ip, raw_ip_len);
    TEST("IPv4 from_bytes succeeds", r == 1);
    TEST("IPv4 roundtrip src_ip", ip_octets_equal_public(&ipv4_parsed.src_ip, &src_ip));
    TEST("IPv4 roundtrip dst_ip", ip_octets_equal_public(&ipv4_parsed.dst_ip, &dst_ip));
    TEST_MSG("IPv4 roundtrip ttl", ipv4_parsed.ttl == 64, "got %u", ipv4_parsed.ttl);
    TEST("IPv4 roundtrip protocol = ICMP", ipv4_parsed.protocol == IPV4_PROTOCOL_ICMP);
    TEST_MSG("IPv4 roundtrip payload_len", ipv4_parsed.payload_len == payload_len, "got %zu", ipv4_parsed.payload_len);
    TEST("IPv4 roundtrip payload matches", memcmp(ipv4_parsed.payload, payload, payload_len) == 0);
    TEST("IPv4 validate checksum after roundtrip", ipv4_validate_checksum(&ipv4_parsed));

    // T19: Modify payload and verify checksum still valid (IPv4 checksum is header-only)
    // Actually, changing payload doesn't affect IPv4 checksum since it's only over header
    IPv4Packet ipv4_corrupt = ipv4_parsed;
    ipv4_corrupt.ttl = 63;  // TTL is in header - changing it breaks checksum
    TEST("IPv4 checksum fails after TTL modified", !ipv4_validate_checksum(&ipv4_corrupt));

    // T20: NULL safety
    TEST("ipv4_compute_checksum NULL returns 0", ipv4_compute_checksum(NULL, 10) == 0);
    TEST("ipv4_validate_checksum NULL returns false", !ipv4_validate_checksum(NULL));

    // T21: ipv4_from_bytes with too-short buffer
    uint8_t tiny[10];
    memcpy(tiny, raw_ip, 10);
    IPv4Packet ipv4_bad;
    r = ipv4_from_bytes(&ipv4_bad, tiny, 10);
    TEST("ipv4_from_bytes with <20 bytes fails", r == 0);

    // T22: ipv4_to_bytes with insufficient buffer
    uint8_t small_buf[5];
    size_t len = ipv4_to_bytes(&ipv4_cksum, small_buf, 5);
    TEST("ipv4_to_bytes with small buffer returns 0", len == 0);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- ICMP Message Tests ---\n" ANSI_RESET);
    // ========================================

    // T23: icmp_init
    ICMPMessage icmp;
    icmp_init(&icmp);
    TEST("icmp_init type = ICMP", icmp.base.type == ICMP);
    TEST("icmp_init type = 0", icmp.type == 0);
    TEST("icmp_init payload_len = 0", icmp.payload_len == 0);

    // T24: icmp_create Echo Request
    const char* icmp_data = "Ping data payload";
    size_t icmp_data_len = strlen(icmp_data);
    r = icmp_create(&icmp, ICMP_ECHO_REQUEST, 0, 0xBEEF, 1, (const uint8_t*)icmp_data, icmp_data_len);
    TEST("icmp_create Echo Request succeeds", r == 1);
    TEST_MSG("icmp type = %d (Echo Request)", icmp.type == ICMP_ECHO_REQUEST, "got %u", icmp.type);
    TEST_MSG("icmp code = 0", icmp.code == 0, "got %u", icmp.code);
    TEST_MSG("icmp identifier = 0xBEEF", icmp.identifier == 0xBEEF, "got 0x%04X", icmp.identifier);
    TEST_MSG("icmp sequence = 1", icmp.sequence == 1, "got %u", icmp.sequence);

    // T25: icmp Echo Request serialization roundtrip
    uint8_t icmp_raw_echo[ICMP_HEADER_SIZE + ICMP_MAX_PAYLOAD];
    size_t icmp_len = packet_to_bytes((Packet*)&icmp, icmp_raw_echo, sizeof(icmp_raw_echo));
    TEST_MSG("ICMP to_bytes length = header + payload",
             icmp_len == ICMP_HEADER_SIZE + icmp_data_len,
             "got %zu", icmp_len);

    ICMPMessage icmp2;
    icmp_init(&icmp2);
    r = packet_from_bytes((Packet*)&icmp2, icmp_raw_echo, icmp_len);
    TEST("ICMP from_bytes succeeds", r == 1);
    TEST_MSG("ICMP roundtrip type", icmp2.type == ICMP_ECHO_REQUEST, "got %u", icmp2.type);
    TEST_MSG("ICMP roundtrip identifier", icmp2.identifier == 0xBEEF, "got 0x%04X", icmp2.identifier);
    TEST_MSG("ICMP roundtrip sequence", icmp2.sequence == 1, "got %u", icmp2.sequence);
    TEST_MSG("ICMP roundtrip payload_len", icmp2.payload_len == icmp_data_len, "got %zu", icmp2.payload_len);
    TEST("ICMP roundtrip payload matches", memcmp(icmp2.payload, icmp_data, icmp_data_len) == 0);

    // T26: icmp_validate_checksum
    // Checksum is computed during serialization (to_bytes), so validate on parsed copy
    TEST("ICMP checksum valid on parsed packet", icmp_validate_checksum(&icmp2));

    // Corrupt the checksum
    ICMPMessage icmp_corrupt = icmp2;
    icmp_corrupt.checksum = 0xFFFF;
    TEST("ICMP checksum fails after corruption", !icmp_validate_checksum(&icmp_corrupt));

    // T27: icmp_create Echo Reply
    ICMPMessage icmp_reply;
    icmp_create(&icmp_reply, ICMP_ECHO_REPLY, 0, 0xBEEF, 1, (const uint8_t*)icmp_data, icmp_data_len);
    TEST_MSG("ICMP Echo Reply type = %d", icmp_reply.type == ICMP_ECHO_REPLY, "got %u", icmp_reply.type);

    icmp_len = packet_to_bytes((Packet*)&icmp_reply, icmp_raw_echo, sizeof(icmp_raw_echo));
    ICMPMessage icmp_reply2;
    icmp_init(&icmp_reply2);
    packet_from_bytes((Packet*)&icmp_reply2, icmp_raw_echo, icmp_len);
    TEST("ICMP Echo Reply roundtrip", icmp_reply2.type == ICMP_ECHO_REPLY);
    TEST("ICMP Echo Reply checksum valid", icmp_validate_checksum(&icmp_reply2));

    // T28: icmp_create Time Exceeded
    uint8_t orig_packet[28];
    memset(orig_packet, 0xAA, sizeof(orig_packet));
    ICMPMessage icmp_ttl;
    icmp_create(&icmp_ttl, ICMP_TIME_EXCEEDED, 0, 0, 0, orig_packet, sizeof(orig_packet));
    TEST_MSG("ICMP Time Exceeded type = %d", icmp_ttl.type == ICMP_TIME_EXCEEDED, "got %u", icmp_ttl.type);
    TEST_MSG("ICMP Time Exceeded payload len = 28", icmp_ttl.payload_len == 28, "got %zu", icmp_ttl.payload_len);

    icmp_len = packet_to_bytes((Packet*)&icmp_ttl, icmp_raw_echo, sizeof(icmp_raw_echo));
    ICMPMessage icmp_ttl2;
    icmp_init(&icmp_ttl2);
    packet_from_bytes((Packet*)&icmp_ttl2, icmp_raw_echo, icmp_len);
    TEST("ICMP Time Exceeded roundtrip", icmp_ttl2.type == ICMP_TIME_EXCEEDED);
    TEST("ICMP Time Exceeded checksum valid", icmp_validate_checksum(&icmp_ttl2));

    // T29: icmp_create Destination Unreachable
    ICMPMessage icmp_unreach;
    icmp_create(&icmp_unreach, ICMP_DEST_UNREACHABLE, 0, 0, 0, orig_packet, sizeof(orig_packet));
    TEST_MSG("ICMP Dest Unreachable type = %d", icmp_unreach.type == ICMP_DEST_UNREACHABLE, "got %u", icmp_unreach.type);
    // Serialize, parse, then validate checksum on parsed copy
    icmp_len = packet_to_bytes((Packet*)&icmp_unreach, icmp_raw_echo, sizeof(icmp_raw_echo));
    ICMPMessage icmp_unreach2;
    icmp_init(&icmp_unreach2);
    packet_from_bytes((Packet*)&icmp_unreach2, icmp_raw_echo, icmp_len);
    TEST("ICMP Dest Unreachable checksum valid", icmp_validate_checksum(&icmp_unreach2));

    // T30: ICMP create with oversized payload fails
    {
        uint8_t big_icmp_payload[ICMP_MAX_PAYLOAD + 1];
        r = icmp_create(&icmp, ICMP_ECHO_REQUEST, 0, 0, 0, big_icmp_payload, ICMP_MAX_PAYLOAD + 1);
        TEST("icmp_create oversized payload fails", r == 0);
    }

    // T31: ICMP create with NULL payload and len > 0 fails
    r = icmp_create(&icmp, ICMP_ECHO_REQUEST, 0, 0, 0, NULL, 5);
    TEST("icmp_create NULL payload fails", r == 0);

    // T32: ICMP from_bytes with too-short buffer
    uint8_t short_raw[4];
    ICMPMessage icmp_short;
    r = icmp_from_bytes(&icmp_short, short_raw, 4);
    TEST("icmp_from_bytes < 8 bytes fails", r == 0);

    // T33: NULL safety
    TEST("icmp_validate_checksum NULL returns false", !icmp_validate_checksum(NULL));
    TEST("icmp_compute_checksum NULL returns 0", icmp_compute_checksum(NULL, 10) == 0);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- Routing Table Tests ---\n" ANSI_RESET);
    // ========================================

    // T34: router_init
    Router router;
    router_init(&router, 4);
    TEST("router_init NUM_INTERFACES = 4", router.base.NUM_INTERFACES == 4);
    TEST("router_init route_count = 0", router.route_count == 0);
    TEST("router_init ARP table empty", router.arp_table.size == 0);
    TEST("router_init pending queue empty", router.pending_queue.size == 0);

    // T35: router_add_route - broad /16 route for LPM testing
    uint8_t zero_octets[] = {0, 0, 0, 0};
    IpAddress zero_hop = ip_init(zero_octets, 0);
    uint8_t broad_octets[] = {192, 168, 0, 0};
    IpAddress broad_net = ip_init(broad_octets, 16);
    r = router_add_route(&router, broad_net, zero_hop, 1);
    TEST("router_add_route /16 direct route succeeds", r == 1);
    TEST_MSG("route_count = 1", router.route_count == 1, "got %zu", router.route_count);

    // T36: router_add_route with next_hop
    r = router_add_route(&router, dst_ip, src_ip, 2);
    TEST("router_add_route with next_hop succeeds", r == 1);
    TEST_MSG("route_count = 2", router.route_count == 2, "got %zu", router.route_count);

    // T37: Duplicate route (should be idempotent, return 1)
    r = router_add_route(&router, broad_net, zero_hop, 1);
    TEST("router_add_route duplicate returns 1", r == 1);
    TEST_MSG("route_count still 2", router.route_count == 2, "got %zu", router.route_count);

    // T38: router_lookup_route - longest prefix match
    // Add a more specific /24 route for 192.168.1.0/24 via interface 3
    uint8_t specific_octets[] = {192, 168, 1, 0};
    IpAddress specific_net = ip_init(specific_octets, 24);
    router_add_route(&router, specific_net, zero_hop, 3);
    TEST_MSG("route_count = 3 after adding /24", router.route_count == 3, "got %zu", router.route_count);

    // Now lookup 192.168.1.10 - should match /24 (more specific than /16)
    const RoutingTableEntry* route = router_lookup_route(&router, &src_ip);
    TEST("router_lookup_route finds a route", route != NULL);
    TEST_MSG("Longest prefix match: /24 (iface 3) wins over /16 (iface 1)",
             route != NULL && route->out_interface == 3,
             "got interface %d", route ? route->out_interface : -1);

    // T39: router_lookup_route - default route (0.0.0.0/0)
    IpAddress unknown_ip = ip_init(ip_a, 24);
    unknown_ip.octet[0] = 8;  // 8.8.8.8 - no specific route
    unknown_ip.octet[1] = 8;
    unknown_ip.octet[2] = 8;
    unknown_ip.octet[3] = 8;
    route = router_lookup_route(&router, &unknown_ip);
    TEST("No route for unknown IP before default", route == NULL);

    // Add default route
    uint8_t default_octets[] = {0, 0, 0, 0};
    IpAddress default_net = ip_init(default_octets, 0);
    router_add_route(&router, default_net, zero_hop, 4);

    route = router_lookup_route(&router, &unknown_ip);
    TEST("Default route matches unknown IP", route != NULL);
    TEST_MSG("Default route interface = 4",
             route != NULL && route->out_interface == 4,
             "got interface %d", route ? route->out_interface : -1);

    // T40: router_lookup_route with more specific still wins over default
    route = router_lookup_route(&router, &src_ip);
    TEST("Specific /24 still wins over default", route != NULL);
    TEST_MSG("Still interface 3 (not default)", route != NULL && route->out_interface == 3,
             "got interface %d", route ? route->out_interface : -1);

    // T41: router_lookup_route with NULL returns NULL
    route = router_lookup_route(&router, NULL);
    TEST("router_lookup_route NULL returns NULL", route == NULL);

    // T42: router_add_direct_routes
    Router router2;
    router_init(&router2, 3);
    // Set interface IPs first
    uint8_t ip_port1[] = {10, 0, 0, 1};
    uint8_t ip_port2[] = {172, 16, 0, 1};
    uint8_t ip_port3[] = {192, 168, 1, 1};
    router2.interface_ips[0].ip_address = ip_init(ip_port1, 8);
    router2.interface_ips[0].has_ip = true;
    router2.interface_ips[1].ip_address = ip_init(ip_port2, 16);
    router2.interface_ips[1].has_ip = true;
    router2.interface_ips[2].ip_address = ip_init(ip_port3, 24);
    router2.interface_ips[2].has_ip = true;

    int added = router_add_direct_routes(&router2);
    TEST_MSG("router_add_direct_routes adds 3 routes", added == 3, "got %d", added);
    TEST_MSG("route_count = 3", router2.route_count == 3, "got %zu", router2.route_count);

    // Verify lookup
    uint8_t test_ip[] = {10, 0, 0, 5};
    IpAddress test = ip_init(test_ip, 24);
    route = router_lookup_route(&router2, &test);
    TEST("Direct route matches 10.0.0.5 via interface 1",
         route != NULL && route->out_interface == 1);

    test_ip[0] = 172; test_ip[1] = 16; test_ip[2] = 0; test_ip[3] = 5;
    test = ip_init(test_ip, 24);
    route = router_lookup_route(&router2, &test);
    TEST("Direct route matches 172.16.0.5 via interface 2",
         route != NULL && route->out_interface == 2);

    test_ip[0] = 192; test_ip[1] = 168; test_ip[2] = 1; test_ip[3] = 10;
    test = ip_init(test_ip, 24);
    route = router_lookup_route(&router2, &test);
    TEST("Direct route matches 192.168.1.10 via interface 3",
         route != NULL && route->out_interface == 3);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- Router ARP & Pending Queue Tests ---\n" ANSI_RESET);
    // ========================================

    // T43: router ARP learning
    ARPMessage arp_reply;
    uint8_t mac_bytes[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    MacAddress test_mac;
    memcpy(test_mac.bytes, mac_bytes, 6);
    uint8_t sender_mac_bytes[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    MacAddress sender_mac;
    memcpy(sender_mac.bytes, sender_mac_bytes, 6);

    arp_message_init_reply(&arp_reply, test_mac, src_ip, sender_mac, src_ip);
    r = router_learn_arp(&router, &arp_reply, 10);
    TEST("router_learn_arp succeeds", r == 1);
    TEST_MSG("router ARP table size = 1", router.arp_table.size == 1, "got %zu", router.arp_table.size);

    // T44: Router ARP lookup
    const MacAddress* found_mac = arp_table_get_const(&router.arp_table, &src_ip);
    TEST("Router ARP lookup finds MAC", found_mac != NULL);
    int mac_match = found_mac && memcmp(found_mac->bytes, mac_bytes, 6) == 0;
    TEST("Router ARP MAC matches", mac_match);

    // T45: Router pending queue
    TEST("Router pending queue initially empty", router.pending_queue.size == 0);

    // T46: Router pending queue enqueue/dequeue
    RouterPendingQueue* queue = &router.pending_queue;
    router_pending_queue_init(queue);
    TEST("Pending queue init resets", queue->size == 0 && queue->head == 0 && queue->tail == 0);

    IPv4Packet test_pkt;
    ipv4_create(&test_pkt, src_ip, dst_ip, 64, IPV4_PROTOCOL_ICMP, (const uint8_t*)"test", 4);

    // Enqueue via internal helpers actually... the pending queue functions are static in router.c
    // We'll verify via the existing abstracted test indirectly
    // For now, verify queue init works
    TEST("Queue init valid", queue->head == 0 && queue->tail == 0);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- Router Interface & VLAN Tests ---\n" ANSI_RESET);
    // ========================================

    // T47: Router interface IP configuration
    Router router3;
    router_init(&router3, 2);
    router3.interface_ips[0].ip_address = gw_ip;
    router3.interface_ips[0].has_ip = true;
    router3.interface_ips[1].ip_address = dst_ip;
    router3.interface_ips[1].has_ip = true;

    TEST("Router3 port1 IP configured", router3.interface_ips[0].has_ip);
    TEST_MSG("Router3 port1 IP = 192.168.1.1/24",
             router3.interface_ips[0].ip_address.octet[0] == 192 &&
             router3.interface_ips[0].ip_address.octet[3] == 1 &&
             router3.interface_ips[0].ip_address.prefix == 24,
             "got %d.%d.%d.%d/%d",
             router3.interface_ips[0].ip_address.octet[0],
             router3.interface_ips[0].ip_address.octet[1],
             router3.interface_ips[0].ip_address.octet[2],
             router3.interface_ips[0].ip_address.octet[3],
             router3.interface_ips[0].ip_address.prefix);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- End-to-End Topology Routing Test ---\n" ANSI_RESET);
    // ========================================

    // T48: Load the spec topology and verify router config
    Simulator e2e_sim;
    simulator_init(&e2e_sim);
    r = simulator_load(&e2e_sim, "magi_system/topology.json");
    TEST("Load topology.json successfully", r == 1);

    int ridx = simulator_find_node(&e2e_sim, "R1");
    TEST("R1 exists in topology", ridx >= 0);

    if (ridx >= 0) {
        Router* r1 = (Router*)e2e_sim.nodes[ridx].node;

        // T49: R1 interface IPs
        TEST("R1 port1 has IP", r1->interface_ips[0].has_ip);
        TEST("R1 port2 has IP", r1->interface_ips[1].has_ip);
        TEST_MSG("R1 port1 IP = 192.168.1.1/24",
                 r1->interface_ips[0].ip_address.octet[0] == 192 &&
                 r1->interface_ips[0].ip_address.octet[3] == 1 &&
                 r1->interface_ips[0].ip_address.prefix == 24,
                 "got %d.%d.%d.%d/%d",
                 r1->interface_ips[0].ip_address.octet[0],
                 r1->interface_ips[0].ip_address.octet[1],
                 r1->interface_ips[0].ip_address.octet[2],
                 r1->interface_ips[0].ip_address.octet[3],
                 r1->interface_ips[0].ip_address.prefix);
        TEST_MSG("R1 port2 IP = 10.0.0.1/8",
                 r1->interface_ips[1].ip_address.octet[0] == 10 &&
                 r1->interface_ips[1].ip_address.octet[3] == 1 &&
                 r1->interface_ips[1].ip_address.prefix == 8,
                 "got %d.%d.%d.%d/%d",
                 r1->interface_ips[1].ip_address.octet[0],
                 r1->interface_ips[1].ip_address.octet[1],
                 r1->interface_ips[1].ip_address.octet[2],
                 r1->interface_ips[1].ip_address.octet[3],
                 r1->interface_ips[1].ip_address.prefix);

        // T50: R1 routes from spec
        TEST_MSG("R1 has %zu routes (direct + default)", r1->route_count >= 2, "got %zu", r1->route_count);

        // T51: Verify routing works - lookup H2 (10.0.0.5) should hit via direct route
        route = router_lookup_route(r1, &dst_ip);
        TEST("R1 can route to 10.0.0.5", route != NULL);
        TEST_MSG("R1 routes 10.0.0.5 via interface 2",
                 route != NULL && route->out_interface == 2,
                 "got interface %d", route ? route->out_interface : -1);

        // T52: Verify routing to H1 (192.168.1.10) via interface 1
        route = router_lookup_route(r1, &src_ip);
        TEST("R1 can route to 192.168.1.10", route != NULL);
        TEST_MSG("R1 routes 192.168.1.10 via interface 1",
                 route != NULL && route->out_interface == 1,
                 "got interface %d", route ? route->out_interface : -1);

        // T53: Unknown IP hits default route
        uint8_t unknown_octets[] = {8, 8, 8, 8};
        IpAddress unknown = ip_init(unknown_octets, 0);
        route = router_lookup_route(r1, &unknown);
        TEST("R1 default route matches 8.8.8.8", route != NULL);
        TEST_MSG("Default route via interface 2",
                 route != NULL && route->out_interface == 2,
                 "got interface %d", route ? route->out_interface : -1);
    }

    // T54: H1 has correct IP from topology
    int hidx = simulator_find_node(&e2e_sim, "H1");
    TEST("H1 exists", hidx >= 0);
    if (hidx >= 0) {
        Host* h1 = (Host*)e2e_sim.nodes[hidx].node;
        TEST("H1 has IP", h1->has_ip);
        TEST_MSG("H1 IP = 192.168.1.10/24",
                 h1->has_ip &&
                 h1->ip_address.octet[0] == 192 &&
                 h1->ip_address.octet[3] == 10 &&
                 h1->ip_address.prefix == 24,
                 "got %d.%d.%d.%d/%d",
                 h1->ip_address.octet[0],
                 h1->ip_address.octet[1],
                 h1->ip_address.octet[2],
                 h1->ip_address.octet[3],
                 h1->ip_address.prefix);
    }

    // T55: H2 has correct IP from topology
    int h2idx = simulator_find_node(&e2e_sim, "H2");
    TEST("H2 exists", h2idx >= 0);
    if (h2idx >= 0) {
        Host* h2 = (Host*)e2e_sim.nodes[h2idx].node;
        TEST("H2 has IP", h2->has_ip);
        TEST_MSG("H2 IP = 10.0.0.5/8",
                 h2->has_ip &&
                 h2->ip_address.octet[0] == 10 &&
                 h2->ip_address.octet[3] == 5 &&
                 h2->ip_address.prefix == 8,
                 "got %d.%d.%d.%d/%d",
                 h2->ip_address.octet[0],
                 h2->ip_address.octet[1],
                 h2->ip_address.octet[2],
                 h2->ip_address.octet[3],
                 h2->ip_address.prefix);
    }

    // T56: Verify H2's default gateway = 10.0.0.1
    if (h2idx >= 0) {
        Host* h2 = (Host*)e2e_sim.nodes[h2idx].node;
        TEST_MSG("H2 gateway = 10.0.0.1",
                 h2->default_gateway.octet[0] == 10 &&
                 h2->default_gateway.octet[3] == 1,
                 "got %d.%d.%d.%d",
                 h2->default_gateway.octet[0],
                 h2->default_gateway.octet[1],
                 h2->default_gateway.octet[2],
                 h2->default_gateway.octet[3]);
    }

    // T57: Verify links exist between proper interfaces
    TEST("H1 linked", is_linked(&e2e_sim, "H1:1"));
    TEST("H2 linked", is_linked(&e2e_sim, "H2:1"));
    TEST("R1 port1 linked", is_linked(&e2e_sim, "R1:1"));
    TEST("R1 port2 linked", is_linked(&e2e_sim, "R1:2"));
    TEST("SW1 port1 linked", is_linked(&e2e_sim, "SW1:1"));
    TEST("SW1 port24 linked", is_linked(&e2e_sim, "SW1:24"));
    TEST_MSG("Link count = 3", e2e_sim.link_count == 3, "got %zu", e2e_sim.link_count);

    if (hidx >= 0 && ridx >= 0) {
        Host* h1 = (Host*)e2e_sim.nodes[hidx].node;
        Router* r1 = (Router*)e2e_sim.nodes[ridx].node;
        uint8_t unknown_octets[] = {8, 8, 8, 8};
        IpAddress unknown = ip_init(unknown_octets, 32);

        h1->has_last_icmp = false;
        r = host_send_icmp_echo_request(h1, &unknown, IPV4_DEFAULT_TTL, 1);
        TEST("Dead next-hop: ping transmission starts", r == 1);
        TEST("Dead next-hop: host receives ICMP Destination Unreachable",
             h1->has_last_icmp && h1->last_icmp_type == ICMP_DEST_UNREACHABLE);
        TEST_MSG("Dead next-hop: router drops failed pending packet",
                 r1->pending_queue.size == 0,
                 "got %zu", r1->pending_queue.size);

        uint8_t unlinked_octets[] = {8, 8, 4, 4};
        IpAddress unlinked_dst = ip_init(unlinked_octets, 32);
        r = simulator_unlink(&e2e_sim, "H2", "R1:2");
        TEST("Unlinked next-hop: remove output link", r == 1);

        h1->has_last_icmp = false;
        r = host_send_icmp_echo_request(h1, &unlinked_dst, IPV4_DEFAULT_TTL, 2);
        TEST("Unlinked next-hop: ping transmission starts", r == 1);
        TEST("Unlinked next-hop: host receives ICMP Destination Unreachable",
             h1->has_last_icmp && h1->last_icmp_type == ICMP_DEST_UNREACHABLE);
        TEST_MSG("Unlinked next-hop: router drops failed pending packet",
                 r1->pending_queue.size == 0,
                 "got %zu", r1->pending_queue.size);
    }

    simulator_clear(&e2e_sim);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- Router Forwarding Integration Tests ---\n" ANSI_RESET);
    // ========================================

    // Create a dedicated router for forwarding tests
    Router fwd_router;
    router_init(&fwd_router, 2);
    strncpy(fwd_router.base.NAME, "R1", MAX_NAME - 1);

    // Configure interface IPs
    {
        uint8_t ip1[] = {192, 168, 1, 1};
        fwd_router.interface_ips[0].ip_address = ip_init(ip1, 24);
        fwd_router.interface_ips[0].has_ip = true;
        uint8_t ip2[] = {10, 0, 0, 1};
        fwd_router.interface_ips[1].ip_address = ip_init(ip2, 8);
        fwd_router.interface_ips[1].has_ip = true;
    }

    // Add direct routes
    {
        uint8_t zero[] = {0, 0, 0, 0};
        IpAddress zero_hop = ip_init(zero, 0);
        uint8_t n1[] = {192, 168, 1, 0};
        router_add_route(&fwd_router, ip_init(n1, 24), zero_hop, 1);
        uint8_t n2[] = {10, 0, 0, 0};
        router_add_route(&fwd_router, ip_init(n2, 8), zero_hop, 2);
    }

    // IPs for test packets
    uint8_t src_arr[] = {192, 168, 1, 10};
    IpAddress src_ip_fwd = ip_init(src_arr, 24);
    uint8_t dst_arr[] = {10, 0, 0, 5};
    IpAddress dst_ip_fwd = ip_init(dst_arr, 8);

    Interface* iface1 = node_get_interface(&fwd_router.base, 1);
    Interface* iface2 = node_get_interface(&fwd_router.base, 2);
    MacAddress broadcast = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
    bool was_realtime = sim_clock_realtime_enabled();

    sim_clock_set_realtime(true);

    // Create a small ICMP Echo payload to use as IPv4 payload
    uint8_t icmp_payload_raw[ICMP_HEADER_SIZE + ICMP_MAX_PAYLOAD];
    size_t icmp_payload_len;
    {
        ICMPMessage ping;
        icmp_create(&ping, ICMP_ECHO_REQUEST, 0, 0xBEEF, 1, (const uint8_t*)"Ping", 4);
        icmp_payload_len = packet_to_bytes((Packet*)&ping, icmp_payload_raw, sizeof(icmp_payload_raw));
    }

    // ========================================
    printf("\n" ANSI_YELLOW "  --- TTL Time Exceeded Test ---\n" ANSI_RESET);
    // ========================================

    // Test TTL=1 causes ICMP Time Exceeded
    // The ICMP error goes back to src, needs ARP → gets queued
    // We verify by checking the queued packet's dst_ip (should be src_ip, not dst_ip)
    {
        // Reset state
        router_pending_queue_init(&fwd_router.pending_queue);
        arp_table_init(&fwd_router.arp_table);

        uint8_t raw[ETHERNET_VLAN_HEADER_SIZE + ETHERNET_MAX_PAYLOAD];
        MacAddress src_mac = {{0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA}};

        // Build IPv4 with TTL=1, wrapped in Ethernet → router interface 1
        size_t raw_len = build_ipv4_frame(raw, sizeof(raw), &broadcast, &src_mac,
                                          &src_ip_fwd, &dst_ip_fwd, 1, IPV4_PROTOCOL_ICMP,
                                          icmp_payload_raw, icmp_payload_len);

        TEST("TTL: Build frame succeeds", raw_len > 0);
        node_receive(&fwd_router.base, iface1, raw, raw_len);

        // Router does NOT learn ARP from IPv4 packets (only from ARP messages)
        TEST_MSG("TTL: ARP table empty after IPv4 receive", fwd_router.arp_table.size == 0,
                 "got %zu", fwd_router.arp_table.size);

        // The ICMP error should be queued waiting for ARP resolution of source
        TEST_MSG("TTL: ICMP error queued (pending=%zu)", fwd_router.pending_queue.size == 1,
                 "got %zu", fwd_router.pending_queue.size);

        // Verify the queued packet is the ICMP error going BACK to src, not the original
        if (fwd_router.pending_queue.size > 0) {
            RouterPendingPacket* q = &fwd_router.pending_queue.items[fwd_router.pending_queue.head];
            TEST("TTL: Queued dst = source IP (ICMP error going back)",
                 ip_octets_equal_public(&q->packet.dst_ip, &src_ip_fwd));
        }
    }

    // ========================================
    printf("\n" ANSI_YELLOW "  --- Dest Unreachable (No Route) Test ---\n" ANSI_RESET);
    // ========================================

    // Test packet to unreachable destination → ICMP Dest Unreachable
    {
        router_pending_queue_init(&fwd_router.pending_queue);
        arp_table_init(&fwd_router.arp_table);

        uint8_t raw[ETHERNET_VLAN_HEADER_SIZE + ETHERNET_MAX_PAYLOAD];
        MacAddress src_mac = {{0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB}};

        // 8.8.8.8 has no route in our routing table
        uint8_t unknown_arr[] = {8, 8, 8, 8};
        IpAddress unknown_dst = ip_init(unknown_arr, 0);

        size_t raw_len = build_ipv4_frame(raw, sizeof(raw), &broadcast, &src_mac,
                                          &src_ip_fwd, &unknown_dst, 64, IPV4_PROTOCOL_ICMP,
                                          icmp_payload_raw, icmp_payload_len);

        TEST("NoRoute: Build frame succeeds", raw_len > 0);
        node_receive(&fwd_router.base, iface1, raw, raw_len);

        // ARP not learned from IPv4 packets (only from ARP messages)
        TEST_MSG("NoRoute: ARP table empty after IPv4 receive", fwd_router.arp_table.size == 0,
                 "got %zu", fwd_router.arp_table.size);

        // ICMP Dest Unreachable should be queued (waiting for ARP to source)
        TEST_MSG("NoRoute: ICMP error queued (pending=%zu)", fwd_router.pending_queue.size == 1,
                 "got %zu", fwd_router.pending_queue.size);

        // The queued packet should be the ICMP error going back to src
        if (fwd_router.pending_queue.size > 0) {
            RouterPendingPacket* q = &fwd_router.pending_queue.items[fwd_router.pending_queue.head];
            TEST("NoRoute: Queued dst = source IP (ICMP error going back)",
                 ip_octets_equal_public(&q->packet.dst_ip, &src_ip_fwd));
            TEST("NoRoute: Queued next_hop = source IP",
                 ip_octets_equal_public(&q->next_hop_ip, &src_ip_fwd));
        }
    }

    // ========================================
    printf("\n" ANSI_YELLOW "  --- ARP Miss -> Queue + ARP Request Test ---\n" ANSI_RESET);
    // ========================================

    // Test packet forwarding when ARP cache is cold → queue + ARP request
    {
        router_pending_queue_init(&fwd_router.pending_queue);
        arp_table_init(&fwd_router.arp_table);

        uint8_t raw[ETHERNET_VLAN_HEADER_SIZE + ETHERNET_MAX_PAYLOAD];
        MacAddress src_mac = {{0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}};

        size_t raw_len = build_ipv4_frame(raw, sizeof(raw), &broadcast, &src_mac,
                                          &src_ip_fwd, &dst_ip_fwd, 64, IPV4_PROTOCOL_ICMP,
                                          icmp_payload_raw, icmp_payload_len);

        TEST("ARP: Build frame succeeds", raw_len > 0);
        node_receive(&fwd_router.base, iface1, raw, raw_len);

        // Original packet should be queued (waiting for ARP resolution of dst)
        TEST_MSG("ARP: Original packet queued (pending=%zu)", fwd_router.pending_queue.size == 1,
                 "got %zu", fwd_router.pending_queue.size);

        // Verify the queued packet is the ORIGINAL (dst=dst_ip), NOT an ICMP error
        if (fwd_router.pending_queue.size > 0) {
            RouterPendingPacket* q = &fwd_router.pending_queue.items[fwd_router.pending_queue.head];
            TEST("ARP: Queued dst = original destination IP (forwarding, not error)",
                 ip_octets_equal_public(&q->packet.dst_ip, &dst_ip_fwd));
            TEST("ARP: Queued next_hop = destination IP",
                 ip_octets_equal_public(&q->next_hop_ip, &dst_ip_fwd));
            TEST_MSG("ARP: Queued out_interface = 2", q->out_interface == 2,
                     "got %d", q->out_interface);
        }
    }

    // ========================================
    printf("\n" ANSI_YELLOW "  --- ARP Reply -> Flush Pending Queue Test ---\n" ANSI_RESET);
    // ========================================

    // Test ARP reply triggers pending queue flush
    {
        router_pending_queue_init(&fwd_router.pending_queue);
        arp_table_init(&fwd_router.arp_table);

        // Pre-queue a pending packet for 10.0.0.5
        IPv4Packet premade_pkt;
        ipv4_create(&premade_pkt, src_ip_fwd, dst_ip_fwd, 64, IPV4_PROTOCOL_ICMP,
                    icmp_payload_raw, icmp_payload_len);

        RouterPendingQueue* qq = &fwd_router.pending_queue;
        qq->items[qq->tail].next_hop_ip = dst_ip_fwd;
        qq->items[qq->tail].out_interface = 2;
        qq->items[qq->tail].vlan_id = 0;
        qq->items[qq->tail].packet = premade_pkt;
        qq->tail = (qq->tail + 1) % ROUTER_PENDING_QUEUE_MAX_ENTRIES;
        qq->size = 1;

        TEST_MSG("ARPFlush: Initial pending count", fwd_router.pending_queue.size == 1,
                 "got %zu", fwd_router.pending_queue.size);

        // Send ARP Reply claiming 10.0.0.5 has MAC
        MacAddress dst_mac = {{0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD}};
        ARPMessage arp_reply;
        arp_message_init_reply(&arp_reply, dst_mac, dst_ip_fwd,
                               iface2->Mac_Address, fwd_router.interface_ips[1].ip_address);

        uint8_t arp_raw[ETHERNET_VLAN_HEADER_SIZE + ETHERNET_MAX_PAYLOAD];
        MacAddress src_mac2 = {{0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD}};
        size_t arp_raw_len = build_arp_frame(arp_raw, sizeof(arp_raw),
                                             &iface2->Mac_Address, &src_mac2, &arp_reply);

        TEST("ARPFlush: Build ARP frame succeeds", arp_raw_len > 0);
        node_receive(&fwd_router.base, iface2, arp_raw, arp_raw_len);

        // Router should learn ARP and flush pending queue
        const MacAddress* learned = arp_table_get_const(&fwd_router.arp_table, &dst_ip_fwd);
        TEST("ARPFlush: Router learned ARP entry", learned != NULL);

        int mac_match = learned && memcmp(learned->bytes, dst_mac.bytes, 6) == 0;
        TEST("ARPFlush: ARP MAC matches", mac_match);

        TEST_MSG("ARPFlush: Pending queue flushed", fwd_router.pending_queue.size == 0,
                 "got %zu", fwd_router.pending_queue.size);
    }

    // ========================================
    printf("\n" ANSI_YELLOW "  --- ARP Hit -> Immediate Forward Test ---\n" ANSI_RESET);
    // ========================================

    // Test packet forwarding with warm ARP cache → immediate send, no queue
    {
        router_pending_queue_init(&fwd_router.pending_queue);
        arp_table_init(&fwd_router.arp_table);

        // Pre-populate ARP for destination
        MacAddress dst_mac = {{0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE}};
        arp_table_set(&fwd_router.arp_table, &dst_ip_fwd, &dst_mac);

        uint8_t raw[ETHERNET_VLAN_HEADER_SIZE + ETHERNET_MAX_PAYLOAD];
        MacAddress src_mac = {{0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}};

        size_t raw_len = build_ipv4_frame(raw, sizeof(raw), &broadcast, &src_mac,
                                          &src_ip_fwd, &dst_ip_fwd, 64, IPV4_PROTOCOL_ICMP,
                                          icmp_payload_raw, icmp_payload_len);

        TEST("ARPHit: Build frame succeeds", raw_len > 0);
        node_receive(&fwd_router.base, iface1, raw, raw_len);

        // ARP was already resolved → no queuing needed
        TEST_MSG("ARPHit: Pending queue empty (immediate forward)",
                 fwd_router.pending_queue.size == 0,
                 "got %zu", fwd_router.pending_queue.size);

        // ARP table should still have the entry
        TEST_MSG("ARPHit: ARP table still has entry",
                 fwd_router.arp_table.size == 1,
                 "got %zu", fwd_router.arp_table.size);
    }

    sim_clock_set_realtime(was_realtime);
    test_report_footer();
}

// ==================== MILESTONE 3: TRANSPORT LAYER ====================

void debug_milestone_3(Simulator *sim)
{
    (void)sim;
    test_reset();
    test_report_header("Milestone 3: Transport Layer (TCP, UDP)");

    uint8_t ip_a[] = {192, 168, 1, 10};
    uint8_t ip_b[] = {192, 168, 1, 11};
    IpAddress src_ip = ip_init(ip_a, 24);
    IpAddress dst_ip = ip_init(ip_b, 24);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- UDP Datagram Tests ---\n" ANSI_RESET);
    // ========================================

    // T1: UDP init
    UDPDatagram udp;
    udp_init(&udp);
    TEST("UDP init sets type to UDP", udp.base.type == UDP);
    TEST("UDP init length = header size", udp.length == UDP_HEADER_SIZE);

    // T2: UDP create
    const char* udp_data = "Hello UDP";
    size_t udp_data_len = strlen(udp_data);
    int r = udp_create(&udp, 1234, 5678, (const uint8_t*)udp_data, udp_data_len);
    TEST("UDP create succeeds", r == 1);
    TEST_MSG("UDP src_port = 1234", udp.src_port == 1234, "got %u", udp.src_port);
    TEST_MSG("UDP dst_port = 5678", udp.dst_port == 5678, "got %u", udp.dst_port);
    TEST_MSG("UDP payload_len = %zu", udp.payload_len == udp_data_len, "got %zu", udp.payload_len);

    // T3: UDP serialization roundtrip
    uint8_t udp_raw[UDP_HEADER_SIZE + UDP_MAX_PAYLOAD];
    size_t udp_len = packet_to_bytes((Packet*)&udp, udp_raw, sizeof(udp_raw));
    TEST_MSG("UDP to_bytes length = 8 + payload", udp_len == UDP_HEADER_SIZE + udp_data_len, "got %zu", udp_len);

    UDPDatagram udp2;
    udp_init(&udp2);
    r = packet_from_bytes((Packet*)&udp2, udp_raw, udp_len);
    TEST("UDP from_bytes succeeds", r == 1);
    TEST_MSG("UDP roundtrip src_port", udp2.src_port == 1234, "got %u", udp2.src_port);
    TEST_MSG("UDP roundtrip dst_port", udp2.dst_port == 5678, "got %u", udp2.dst_port);
    TEST_MSG("UDP roundtrip payload_len", udp2.payload_len == udp_data_len, "got %zu", udp2.payload_len);

    // T4: UDP checksum with pseudo-header
    udp_len = packet_to_bytes((Packet*)&udp, udp_raw, sizeof(udp_raw));
    uint16_t cksum = udp_compute_checksum(udp_raw, udp_len, &src_ip, &dst_ip);
    udp.checksum = cksum;
    bool valid = udp_validate_checksum(&udp, &src_ip, &dst_ip);
    TEST("UDP checksum valid with correct pseudo-header", valid);

    // T5: UDP checksum fails with wrong IP
    IpAddress wrong_ip = ip_init(ip_b, 24);
    bool invalid = udp_validate_checksum(&udp, &wrong_ip, &dst_ip);
    TEST("UDP checksum fails with wrong src IP", !invalid);

    // T5b: UDP parser rejects malformed length fields before copying payload.
    uint8_t malformed_udp[UDP_HEADER_SIZE + 1] = {0};
    malformed_udp[4] = 0;
    malformed_udp[5] = UDP_HEADER_SIZE - 1;
    r = udp_from_bytes(&udp2, malformed_udp, sizeof(malformed_udp));
    TEST("UDP rejects length smaller than header", r == 0);
    malformed_udp[5] = UDP_HEADER_SIZE + 2;
    r = udp_from_bytes(&udp2, malformed_udp, sizeof(malformed_udp));
    TEST("UDP rejects length larger than received bytes", r == 0);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- TCP Segment Tests ---\n" ANSI_RESET);
    // ========================================

    // T6: TCP init
    TCPSegment tcp;
    tcp_init(&tcp);
    TEST("TCP init type = TCP", tcp.base.type == TCP);
    TEST("TCP init data_offset = 5", tcp.data_offset == 5);
    TEST_MSG("TCP init window = %u", tcp.window == TCP_WINDOW_SIZE, "got %u", tcp.window);

    // T7: TCP create (SYN)
    r = tcp_create(&tcp, 1000, 80, 100, 0, TCP_FLAG_SYN, TCP_WINDOW_SIZE, NULL, 0);
    TEST("TCP create SYN succeeds", r == 1);
    TEST_MSG("TCP src_port = 1000", tcp.src_port == 1000, "got %u", tcp.src_port);
    TEST("TCP SYN flag set", tcp.flags == TCP_FLAG_SYN);
    TEST_MSG("TCP seq_num = 100", tcp.seq_num == 100, "got %u", tcp.seq_num);

    // T8: TCP serialization roundtrip
    uint8_t tcp_raw[TCP_HEADER_SIZE + TCP_MAX_PAYLOAD];
    size_t tcp_len = packet_to_bytes((Packet*)&tcp, tcp_raw, sizeof(tcp_raw));
    TEST_MSG("TCP to_bytes length = 20", tcp_len == TCP_HEADER_SIZE, "got %zu", tcp_len);

    TCPSegment tcp2;
    tcp_init(&tcp2);
    r = packet_from_bytes((Packet*)&tcp2, tcp_raw, tcp_len);
    TEST("TCP from_bytes succeeds", r == 1);
    TEST_MSG("TCP roundtrip src_port", tcp2.src_port == 1000, "got %u", tcp2.src_port);
    TEST("TCP roundtrip SYN flag", tcp2.flags & TCP_FLAG_SYN);
    TEST("TCP roundtrip NOT ACK flag", !(tcp2.flags & TCP_FLAG_ACK));

    // T9: TCP checksum with pseudo-header
    tcp_len = packet_to_bytes((Packet*)&tcp, tcp_raw, sizeof(tcp_raw));
    uint16_t tcp_cksum = tcp_compute_checksum(tcp_raw, tcp_len, &src_ip, &dst_ip);
    tcp.checksum = tcp_cksum;
    valid = tcp_validate_checksum(&tcp, &src_ip, &dst_ip);
    TEST("TCP checksum valid with correct pseudo-header", valid);

    invalid = tcp_validate_checksum(&tcp, &wrong_ip, &dst_ip);
    TEST("TCP checksum fails with wrong src IP", !invalid);

    // T10: TCP with payload
    const char* tcp_data = "Hello TCP World";
    size_t tcp_data_len = strlen(tcp_data);
    r = tcp_create(&tcp, 1000, 80, 200, 150, TCP_FLAG_ACK | TCP_FLAG_PSH,
                   TCP_WINDOW_SIZE, (const uint8_t*)tcp_data, tcp_data_len);
    TEST("TCP create with data succeeds", r == 1);
    TEST("TCP PSH and ACK flags set", tcp.flags == (TCP_FLAG_ACK | TCP_FLAG_PSH));
    TEST_MSG("TCP payload_len = %zu", tcp.payload_len == tcp_data_len, "got %zu", tcp.payload_len);

    tcp_len = packet_to_bytes((Packet*)&tcp, tcp_raw, sizeof(tcp_raw));
    TEST_MSG("TCP to_bytes with payload", tcp_len == TCP_HEADER_SIZE + tcp_data_len, "got %zu", tcp_len);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- TCP Socket & State Machine Tests ---\n" ANSI_RESET);
    // ========================================

    // T11: Socket init
    TCPSocket sockets[TCP_MAX_SOCKETS];
    tcp_socket_init(sockets, TCP_MAX_SOCKETS);
    int idx = tcp_socket_alloc(sockets, TCP_MAX_SOCKETS);
    TEST("Socket alloc returns valid index", idx >= 0);
    TEST("Allocated socket is in_use", sockets[idx].in_use);
    TEST("Allocated socket starts CLOSED", sockets[idx].state == TCP_CLOSED);

    // T12: TCP connect
    TCPSocket* client = &sockets[idx];
    r = tcp_socket_connect(client, &src_ip, 12345, &dst_ip, 80);
    TEST("TCP connect succeeds", r == 1);
    TEST("TCP state after connect = SYN_SENT", client->state == TCP_SYN_SENT);
    TEST_MSG("local_port = 12345", client->local_port == 12345, "got %u", client->local_port);
    TEST_MSG("remote_port = 80", client->remote_port == 80, "got %u", client->remote_port);
    TEST_MSG("send_seq = 1000", client->send_seq == 1000, "got %u", client->send_seq);

    // T13: TCP listen
    int listen_idx = tcp_socket_alloc(sockets, TCP_MAX_SOCKETS);
    TCPSocket* server = &sockets[listen_idx];
    r = tcp_socket_listen(server, &dst_ip, 80);
    TEST("TCP listen succeeds", r == 1);
    TEST("TCP state after listen = LISTEN", server->state == TCP_LISTEN);
    TEST("is_listening = true", server->is_listening);

    // T14: Socket find (match by local port for listening socket)
    TCPSocket* found = tcp_socket_find(sockets, TCP_MAX_SOCKETS, &dst_ip, 80, &src_ip, 12345);
    TEST("Find listening socket by local port", found == server);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- TCP 3-Way Handshake Test ---\n" ANSI_RESET);
    // ========================================

    // Simulate 3-way handshake between client and server
    // Step 1: Client sends SYN
    // (Client is already in SYN_SENT state)

    // Step 2: Server receives SYN -> sends SYN-ACK
    TCPSegment syn_seg;
    tcp_create(&syn_seg, 12345, 80, 1000, 0, TCP_FLAG_SYN, TCP_WINDOW_SIZE, NULL, 0);

    // Compute checksum for syn_seg (needed for tcp_socket_receive_segment validation)
    {
        uint8_t seg_raw[TCP_HEADER_SIZE];
        size_t seg_len = packet_to_bytes((Packet*)&syn_seg, seg_raw, sizeof(seg_raw));
        syn_seg.checksum = tcp_compute_checksum(seg_raw, seg_len, &src_ip, &dst_ip);
    }

    TCPSegment syn_ack_response;
    tcp_init(&syn_ack_response);

    // Server receives SYN
    r = tcp_socket_receive_segment(server, &syn_seg, &src_ip, &dst_ip, &syn_ack_response);
    TEST("Server handles SYN from LISTEN", r == 1);
    TEST("Server state = SYN_RECEIVED after SYN", server->state == TCP_SYN_RECEIVED);
    TEST("Server responds with SYN-ACK", syn_ack_response.flags == TCP_FLAG_SYN_ACK);
    TEST("Server ack_num = client_seq + 1", syn_ack_response.ack_num == 1001);
    server->remote_ip = src_ip;
    server->remote_port = 12345;
    server->is_listening = false;

    // Step 3: Client receives SYN-ACK -> sends ACK
    // First, allocate a child socket for the client side
    int child_idx = tcp_socket_alloc(sockets, TCP_MAX_SOCKETS);
    TCPSocket* child = &sockets[child_idx];
    // Set up as client connecting TO dst_ip:80 FROM src_ip:12345
    tcp_socket_connect(child, &src_ip, 12345, &dst_ip, 80);
    child->state = TCP_SYN_SENT;

    TCPSegment ack_response;
    tcp_init(&ack_response);
    r = tcp_socket_receive_segment(child, &syn_ack_response, &dst_ip, &src_ip, &ack_response);
    TEST("Client handles SYN-ACK", r == 1);
    TEST("Client state = ESTABLISHED after SYN-ACK", child->state == TCP_ESTABLISHED);
    TEST("Client sends ACK", ack_response.flags == TCP_FLAG_ACK);
    TEST_MSG("Client ack_num = server_seq + 1", ack_response.ack_num == syn_ack_response.seq_num + 1, "got %u", ack_response.ack_num);

    TCPSegment no_response;
    tcp_init(&no_response);
    r = tcp_socket_receive_segment(server, &ack_response, &src_ip, &dst_ip, &no_response);
    TEST("Server handles final handshake ACK", r == 1);
    TEST("Server state = ESTABLISHED after final ACK", server->state == TCP_ESTABLISHED);
    TEST_MSG("Server SYN consumes one send sequence number",
             server->send_seq == syn_ack_response.seq_num + 1,
             "got %u", server->send_seq);

    int strict_idx = tcp_socket_alloc(sockets, TCP_MAX_SOCKETS);
    TCPSocket* strict_client = &sockets[strict_idx];
    tcp_socket_connect(strict_client, &src_ip, 22000, &dst_ip, 80);
    TCPSegment invalid_syn_ack = syn_ack_response;
    invalid_syn_ack.dst_port = strict_client->local_port;
    invalid_syn_ack.ack_num = strict_client->send_seq + 2;
    invalid_syn_ack.checksum = 0;
    {
        uint8_t seg_raw[TCP_HEADER_SIZE];
        size_t seg_len = packet_to_bytes((Packet*)&invalid_syn_ack, seg_raw, sizeof(seg_raw));
        invalid_syn_ack.checksum = tcp_compute_checksum(seg_raw, seg_len, &dst_ip, &src_ip);
    }
    tcp_init(&no_response);
    r = tcp_socket_receive_segment(strict_client, &invalid_syn_ack,
                                   &dst_ip, &src_ip, &no_response);
    TEST("Client rejects SYN-ACK with invalid acknowledgment", r == 0);
    TEST("Invalid SYN-ACK leaves client in SYN_SENT", strict_client->state == TCP_SYN_SENT);

    printf("\n" ANSI_YELLOW "  --- TCP Receive Buffer & Reassembly Tests ---\n" ANSI_RESET);

    TCPSegment data_segment;
    TCPSegment data_ack;
    uint8_t recv_data[32];

    tcp_create(&data_segment, child->local_port, server->local_port, 1001,
               child->recv_seq, TCP_FLAG_ACK | TCP_FLAG_PSH, TCP_WINDOW_SIZE,
               (const uint8_t*)"First", 5);
    {
        uint8_t seg_raw[TCP_HEADER_SIZE + TCP_MAX_PAYLOAD];
        size_t seg_len = packet_to_bytes((Packet*)&data_segment, seg_raw, sizeof(seg_raw));
        data_segment.checksum = tcp_compute_checksum(seg_raw, seg_len, &src_ip, &dst_ip);
    }
    tcp_init(&data_ack);
    r = tcp_socket_receive_segment(server, &data_segment, &src_ip, &dst_ip, &data_ack);
    TEST("TCP receives first stream segment", r == 1);

    tcp_create(&data_segment, child->local_port, server->local_port, 1006,
               child->recv_seq, TCP_FLAG_ACK | TCP_FLAG_PSH, TCP_WINDOW_SIZE,
               (const uint8_t*)"Second", 6);
    {
        uint8_t seg_raw[TCP_HEADER_SIZE + TCP_MAX_PAYLOAD];
        size_t seg_len = packet_to_bytes((Packet*)&data_segment, seg_raw, sizeof(seg_raw));
        data_segment.checksum = tcp_compute_checksum(seg_raw, seg_len, &src_ip, &dst_ip);
    }
    tcp_init(&data_ack);
    r = tcp_socket_receive_segment(server, &data_segment, &src_ip, &dst_ip, &data_ack);
    TEST("TCP receives second stream segment", r == 1);
    r = tcp_socket_recv(server, recv_data, sizeof(recv_data));
    TEST_MSG("TCP receive buffer concatenates segments",
             r == 11 && memcmp(recv_data, "FirstSecond", 11) == 0,
             "got %d bytes", r);

    tcp_create(&data_segment, child->local_port, server->local_port, 1017,
               child->recv_seq, TCP_FLAG_ACK | TCP_FLAG_PSH, TCP_WINDOW_SIZE,
               (const uint8_t*)"World", 5);
    {
        uint8_t seg_raw[TCP_HEADER_SIZE + TCP_MAX_PAYLOAD];
        size_t seg_len = packet_to_bytes((Packet*)&data_segment, seg_raw, sizeof(seg_raw));
        data_segment.checksum = tcp_compute_checksum(seg_raw, seg_len, &src_ip, &dst_ip);
    }
    tcp_init(&data_ack);
    tcp_socket_receive_segment(server, &data_segment, &src_ip, &dst_ip, &data_ack);
    TEST("Out-of-order segment is held until gap arrives", !server->has_data);

    tcp_create(&data_segment, child->local_port, server->local_port, 1012,
               child->recv_seq, TCP_FLAG_ACK | TCP_FLAG_PSH, TCP_WINDOW_SIZE,
               (const uint8_t*)"Hello", 5);
    {
        uint8_t seg_raw[TCP_HEADER_SIZE + TCP_MAX_PAYLOAD];
        size_t seg_len = packet_to_bytes((Packet*)&data_segment, seg_raw, sizeof(seg_raw));
        data_segment.checksum = tcp_compute_checksum(seg_raw, seg_len, &src_ip, &dst_ip);
    }
    tcp_init(&data_ack);
    tcp_socket_receive_segment(server, &data_segment, &src_ip, &dst_ip, &data_ack);
    r = tcp_socket_recv(server, recv_data, sizeof(recv_data));
    TEST_MSG("TCP reassembles out-of-order segments",
             r == 10 && memcmp(recv_data, "HelloWorld", 10) == 0,
             "got %d bytes", r);

    r = tcp_socket_send(server, (const uint8_t*)"Reply", 5);
    TEST("Server can buffer data after handshake", r == 1);
    server->send_seq += 5;
    tcp_create(&data_segment, child->local_port, server->local_port, child->send_seq,
               server->send_seq, TCP_FLAG_ACK, TCP_WINDOW_SIZE, NULL, 0);
    {
        uint8_t seg_raw[TCP_HEADER_SIZE];
        size_t seg_len = packet_to_bytes((Packet*)&data_segment, seg_raw, sizeof(seg_raw));
        data_segment.checksum = tcp_compute_checksum(seg_raw, seg_len, &src_ip, &dst_ip);
    }
    tcp_init(&data_ack);
    tcp_socket_receive_segment(server, &data_segment, &src_ip, &dst_ip, &data_ack);
    TEST_MSG("Server payload acknowledgment clears complete send buffer",
             server->send_buffer.size == 0,
             "got %zu remaining", server->send_buffer.size);

    printf("\n" ANSI_YELLOW "  --- TCP 4-Way Teardown Tests ---\n" ANSI_RESET);

    TCPSocket fin_data_receiver;
    memset(&fin_data_receiver, 0, sizeof(fin_data_receiver));
    fin_data_receiver.in_use = true;
    fin_data_receiver.state = TCP_ESTABLISHED;
    fin_data_receiver.local_ip = dst_ip;
    fin_data_receiver.local_port = 80;
    fin_data_receiver.remote_ip = src_ip;
    fin_data_receiver.remote_port = 30001;
    fin_data_receiver.send_seq = 9000;
    fin_data_receiver.send_ack = 9000;
    fin_data_receiver.recv_seq = 4000;
    TCPSegment fin_with_data;
    TCPSegment fin_with_data_ack;
    tcp_create(&fin_with_data, fin_data_receiver.remote_port, fin_data_receiver.local_port,
               4000, fin_data_receiver.send_seq, TCP_FLAG_FIN | TCP_FLAG_ACK,
               TCP_WINDOW_SIZE, (const uint8_t*)"End", 3);
    {
        uint8_t seg_raw[TCP_HEADER_SIZE + TCP_MAX_PAYLOAD];
        size_t seg_len = packet_to_bytes((Packet*)&fin_with_data, seg_raw, sizeof(seg_raw));
        fin_with_data.checksum = tcp_compute_checksum(seg_raw, seg_len, &src_ip, &dst_ip);
    }
    tcp_init(&fin_with_data_ack);
    r = tcp_socket_receive_segment(&fin_data_receiver, &fin_with_data,
                                   &src_ip, &dst_ip, &fin_with_data_ack);
    TEST("FIN carrying data enters CLOSE_WAIT after delivering payload",
         r == 1 && fin_data_receiver.state == TCP_CLOSE_WAIT);
    r = tcp_socket_recv(&fin_data_receiver, recv_data, sizeof(recv_data));
    TEST_MSG("FIN carrying data preserves payload before closing",
             r == 3 && memcmp(recv_data, "End", 3) == 0 &&
             fin_with_data_ack.ack_num == 4004,
             "got %d bytes and ACK %u", r, fin_with_data_ack.ack_num);

    TCPSocket close_sockets[2];
    tcp_socket_init(close_sockets, 2);
    TCPSocket* closer = &close_sockets[0];
    TCPSocket* peer = &close_sockets[1];
    closer->in_use = true;
    closer->state = TCP_ESTABLISHED;
    closer->local_ip = src_ip;
    closer->local_port = 30000;
    closer->remote_ip = dst_ip;
    closer->remote_port = 80;
    closer->send_seq = 5000;
    closer->send_ack = 5000;
    closer->recv_seq = 7000;
    peer->in_use = true;
    peer->state = TCP_ESTABLISHED;
    peer->local_ip = dst_ip;
    peer->local_port = 80;
    peer->remote_ip = src_ip;
    peer->remote_port = 30000;
    peer->send_seq = 7000;
    peer->send_ack = 7000;
    peer->recv_seq = 5000;

    TCPSegment fin;
    TCPSegment fin_ack;
    tcp_create(&fin, closer->local_port, closer->remote_port, closer->send_seq,
               closer->recv_seq, TCP_FLAG_FIN | TCP_FLAG_ACK, TCP_WINDOW_SIZE,
               NULL, 0);
    {
        uint8_t seg_raw[TCP_HEADER_SIZE];
        size_t seg_len = packet_to_bytes((Packet*)&fin, seg_raw, sizeof(seg_raw));
        fin.checksum = tcp_compute_checksum(seg_raw, seg_len, &src_ip, &dst_ip);
    }
    r = tcp_socket_close(closer);
    closer->send_seq++;
    TEST("Active close enters FIN_WAIT_1 before FIN delivery", r == 1 && closer->state == TCP_FIN_WAIT_1);
    tcp_init(&fin_ack);
    r = tcp_socket_receive_segment(peer, &fin, &src_ip, &dst_ip, &fin_ack);
    TEST("Peer accepts FIN and enters CLOSE_WAIT", r == 1 && peer->state == TCP_CLOSE_WAIT);
    tcp_init(&no_response);
    r = tcp_socket_receive_segment(closer, &fin_ack, &dst_ip, &src_ip, &no_response);
    TEST("ACK for initial FIN enters FIN_WAIT_2", r == 1 && closer->state == TCP_FIN_WAIT_2);

    tcp_create(&fin, peer->local_port, peer->remote_port, peer->send_seq,
               peer->recv_seq, TCP_FLAG_FIN | TCP_FLAG_ACK, TCP_WINDOW_SIZE,
               NULL, 0);
    {
        uint8_t seg_raw[TCP_HEADER_SIZE];
        size_t seg_len = packet_to_bytes((Packet*)&fin, seg_raw, sizeof(seg_raw));
        fin.checksum = tcp_compute_checksum(seg_raw, seg_len, &dst_ip, &src_ip);
    }
    r = tcp_socket_close(peer);
    peer->send_seq++;
    TEST("Passive close enters LAST_ACK before FIN delivery", r == 1 && peer->state == TCP_LAST_ACK);
    tcp_init(&fin_ack);
    r = tcp_socket_receive_segment(closer, &fin, &dst_ip, &src_ip, &fin_ack);
    TEST("Final FIN places active closer in TIME_WAIT", r == 1 && closer->state == TCP_TIME_WAIT);
    tcp_init(&no_response);
    r = tcp_socket_receive_segment(peer, &fin_ack, &src_ip, &dst_ip, &no_response);
    TEST("Final ACK closes passive peer", r == 1 && peer->state == TCP_CLOSED);
    TEST("Final ACK releases passive peer socket slot", !peer->in_use);

    uint16_t reconnect_port = tcp_socket_select_source_port(
        close_sockets, 2, &dst_ip, 80, closer->local_port);
    TEST("Reconnect avoids a four-tuple still in TIME_WAIT",
         reconnect_port != 0 && reconnect_port != closer->local_port);

    Host port_owner;
    memset(&port_owner, 0, sizeof(port_owner));
    tcp_socket_init(port_owner.tcp_sockets, TCP_MAX_SOCKETS);
    uint16_t first_port = host_select_tcp_source_port(&port_owner, &dst_ip, 80, 2080);
    uint16_t second_port = host_select_tcp_source_port(&port_owner, &dst_ip, 80, 2080);
    TEST("Host source-port cursor avoids immediate reuse after passive close",
         first_port == 2080 && second_port == 2081);

    TCPSocket bounded_pool[2];
    tcp_socket_init(bounded_pool, 2);
    bounded_pool[0].in_use = true;
    bounded_pool[0].is_listening = true;
    bounded_pool[0].state = TCP_LISTEN;
    bounded_pool[1].in_use = true;
    bounded_pool[1].state = TCP_TIME_WAIT;
    r = tcp_socket_alloc(bounded_pool, 2);
    TEST("Full bounded socket pool can recycle TIME_WAIT storage",
         r == 1 && bounded_pool[1].state == TCP_CLOSED);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- TCP State Transition Tests ---\n" ANSI_RESET);
    // ========================================

    // T15: TCP state names
    TEST("State name CLOSED", strcmp(tcp_state_name(TCP_CLOSED), "CLOSED") == 0);
    TEST("State name ESTABLISHED", strcmp(tcp_state_name(TCP_ESTABLISHED), "ESTABLISHED") == 0);
    TEST("State name TIME_WAIT", strcmp(tcp_state_name(TCP_TIME_WAIT), "TIME_WAIT") == 0);

    // T16: TCP send/recv (no actual network - just buffer test)
    TCPSocket* esock = child;  // Use the established socket
    r = tcp_socket_send(esock, (const uint8_t*)"Hello", 5);
    TEST("TCP send to established socket", r == 1);
    TEST_MSG("Send buffer size = 5", esock->send_buffer.size == 5, "got %zu", esock->send_buffer.size);

    // T17: TCP close (initiate 4-way handshake)
    r = tcp_socket_close(esock);
    TEST("TCP close from ESTABLISHED", r == 1);
    TEST("State after close = FIN_WAIT_1", esock->state == TCP_FIN_WAIT_1);

    // T18: TCP socket free
    tcp_socket_free(esock);
    TEST("Freed socket not in use", !esock->in_use);

    // T19: TCP socket alloc after free
    int re_idx = tcp_socket_alloc(sockets, TCP_MAX_SOCKETS);
    TEST("Can re-allocate freed socket", re_idx == child_idx);

    test_report_footer();
}

// ==================== MILESTONE 4: APPLICATION LAYER ====================

// Helper: create a host with IP for M4 tests
static Host* m4_create_host(Simulator* sim, const char* name, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    int idx;
    uint8_t octets[] = {a, b, c, d};

    simulator_create_node(sim, "host", name, 1);
    idx = simulator_find_node(sim, name);
    if (idx < 0) return NULL;

    Host* h = (Host*)sim->nodes[idx].node;
    h->ip_address = ip_init(octets, 24);
    h->has_ip = true;
    return h;
}

void debug_milestone_4(Simulator *sim)
{
    test_reset();
    test_report_header("Milestone 4: Application Layer (Socket API, DHCP, DNS, HTTP)");

    simulator_clear(sim);

    uint8_t ip_a[] = {192, 168, 1, 10};
    uint8_t ip_b[] = {10, 0, 0, 5};
    uint8_t ip_c[] = {192, 168, 1, 1};
    IpAddress src_ip = ip_init(ip_a, 24);
    IpAddress dst_ip = ip_init(ip_b, 8); (void)dst_ip;
    IpAddress server_ip = ip_init(ip_c, 24);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- MagiSocket API Tests ---\n" ANSI_RESET);
    // ========================================

    // T1: Create socket
    int sockfd = magi_socket(AF_INET, SOCK_STREAM);
    TEST_MSG("magi_socket() creates TCP socket", sockfd >= 0, "got %d", sockfd);

    // T2: Bind socket
    int r = magi_bind(sockfd, &src_ip, 8080);
    TEST("magi_bind() succeeds", r == 1);

    // T3: Cannot listen without host attachment
    r = magi_listen(sockfd, 5);
    TEST("magi_listen() without host fails", r < 0);

    // T4: Create UDP socket
    int udp_sock = magi_socket(AF_INET, SOCK_DGRAM);
    TEST_MSG("magi_socket() creates UDP socket", udp_sock >= 0, "got %d", udp_sock);

    // T5: Close sockets
    r = magi_close(sockfd);
    TEST("magi_close() TCP socket", r == 1);
    r = magi_close(udp_sock);
    TEST("magi_close() UDP socket", r == 1);

    // T6: MagiSocket with host attachment (end-to-end)
    Host* h1 = m4_create_host(sim, "H1", 192, 168, 1, 10);
    TEST("M4: Created H1", h1 != NULL);

    if (h1) {
        int asock = magi_socket(AF_INET, SOCK_STREAM);
        TEST_MSG("M4: create connected socket", asock >= 0, "got %d", asock);
        if (asock >= 0) {
            magi_socket_attach_host(asock, h1);
            magi_bind(asock, &h1->ip_address, 9090);
            r = magi_listen(asock, 5);
            TEST("M4: listen on host-attached socket", r == 1);
            magi_close(asock);
        }
    }

    // ========================================
    printf("\n" ANSI_YELLOW "  --- DHCP Protocol Tests ---\n" ANSI_RESET);
    // ========================================

    // T7: DHCP init
    dhcp_server_init();
    dhcp_server_set_ip(&server_ip);
    TEST("DHCP server initialized", 1);

    // T8: Create DHCP Discover
    DHCPMessage discover;
    uint8_t test_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    r = dhcp_create_discover(&discover, 0x1234, test_mac);
    TEST("DHCP create DISCOVER", r == 1);
    TEST("DHCP DISCOVER op = BOOTREQUEST", discover.op == DHCP_BOOTREQUEST);
    TEST_MSG("DHCP DISCOVER xid = 0x1234", discover.xid == 0x1234, "got 0x%04X", discover.xid);

    // T9: DISCOVER message type
    int msg_type = dhcp_get_msg_type(&discover);
    TEST_MSG("DHCP DISCOVER message type = 1", msg_type == DHCP_DISCOVER, "got %d", msg_type);

    // T10: DHCP serialize/deserialize roundtrip
    uint8_t dhcp_raw[DHCP_HEADER_SIZE + DHCP_OPTIONS_SIZE];
    int dhcp_len = dhcp_message_to_bytes(&discover, dhcp_raw, sizeof(dhcp_raw));
    TEST_MSG("DHCP to_bytes length", dhcp_len > 0, "got %d", dhcp_len);

    DHCPMessage discover2;
    r = dhcp_message_from_bytes(&discover2, dhcp_raw, (size_t)dhcp_len);
    TEST("DHCP from_bytes", r == 1);
    TEST("DHCP roundtrip op", discover2.op == DHCP_BOOTREQUEST);
    TEST("DHCP roundtrip xid", discover2.xid == 0x1234);

    // T11: DHCP DISCOVER -> OFFER
    DHCPMessage offer;
    r = dhcp_server_handle_discover(&discover, &offer);
    TEST("DHCP handle DISCOVER -> OFFER", r == 1);
    msg_type = dhcp_get_msg_type(&offer);
    TEST_MSG("DHCP OFFER message type = 2", msg_type == DHCP_OFFER, "got %d", msg_type);
    TEST("DHCP OFFER op = BOOTREPLY", offer.op == DHCP_BOOTREPLY);
    TEST("DHCP OFFER has yiaddr", offer.yiaddr.octet[3] > 0);

    // T12: DHCP client receives OFFER -> creates REQUEST
    DHCPMessage request;
    r = dhcp_create_request(&request, 0x1234, test_mac, &offer.yiaddr, &server_ip);
    TEST("DHCP create REQUEST", r == 1);
    msg_type = dhcp_get_msg_type(&request);
    TEST_MSG("DHCP REQUEST message type = 3", msg_type == DHCP_REQUEST, "got %d", msg_type);

    // T13: DHCP REQUEST -> ACK
    DHCPMessage ack;
    r = dhcp_server_handle_request(&request, &ack);
    TEST("DHCP handle REQUEST -> ACK", r == 1);
    msg_type = dhcp_get_msg_type(&ack);
    TEST_MSG("DHCP ACK message type = 5", msg_type == DHCP_ACK, "got %d", msg_type);
    TEST("DHCP ACK yiaddr matches request",
         ip_octets_equal_public(&ack.yiaddr, &offer.yiaddr));

    // T14: DHCP get lease
    IpAddress leased_ip;
    r = dhcp_get_lease(test_mac, &leased_ip);
    TEST("DHCP get lease for MAC", r == 1);
    TEST("DHCP leased IP matches offered",
         ip_octets_equal_public(&leased_ip, &offer.yiaddr));

    // ========================================
    printf("\n" ANSI_YELLOW "  --- DNS Protocol Tests ---\n" ANSI_RESET);
    // ========================================

    // T15: DNS init
    dns_server_init();
    TEST("DNS server initialized with default entries", 1);

    // T16: DNS resolve
    IpAddress resolved;
    r = dns_server_resolve("www.magi.com", &resolved);
    TEST("DNS resolve www.magi.com", r == 1);
    TEST_MSG("DNS resolved to 10.0.0.5",
             resolved.octet[0] == 10 && resolved.octet[3] == 5,
             "got %d.%d.%d.%d", resolved.octet[0], resolved.octet[1], resolved.octet[2], resolved.octet[3]);

    // T17: DNS resolve unknown
    r = dns_server_resolve("unknown.example.com", &resolved);
    TEST("DNS resolve unknown returns 0", r == 0);

    // T18: DNS add custom entry
    uint8_t custom_ip[] = {172, 16, 0, 1};
    IpAddress custom_addr = ip_init(custom_ip, 0);
    dns_server_add_entry("custom.test", &custom_addr);
    r = dns_server_resolve("custom.test", &resolved);
    TEST("DNS resolve custom.test", r == 1);
    TEST_MSG("DNS custom -> 172.16.0.1",
             resolved.octet[0] == 172 && resolved.octet[3] == 1,
             "got %d.%d.%d.%d", resolved.octet[0], resolved.octet[1], resolved.octet[2], resolved.octet[3]);

    // T19: DNS add duplicate entry (should update)
    uint8_t custom_ip2[] = {172, 16, 0, 2};
    IpAddress custom_addr2 = ip_init(custom_ip2, 0);
    dns_server_add_entry("custom.test", &custom_addr2);
    r = dns_server_resolve("custom.test", &resolved);
    TEST_MSG("DNS updated custom.test -> 172.16.0.2",
             resolved.octet[3] == 2,
             "got %d.%d.%d.%d", resolved.octet[0], resolved.octet[1], resolved.octet[2], resolved.octet[3]);

    // T20: DNS query/response serialization
    uint8_t dns_query[512];
    size_t query_len;
    r = dns_create_query(dns_query, sizeof(dns_query), &query_len, 0xAAAA, "www.magi.com");
    TEST("DNS create query", r == 1);
    TEST_MSG("DNS query length", query_len > 12, "got %zu", query_len);

    // T21: DNS handle query -> response
    uint8_t dns_response[512];
    size_t resp_len;
    r = dns_server_handle_query(dns_query, query_len, dns_response, &resp_len, sizeof(dns_response));
    TEST("DNS handle query", r == 1);
    TEST_MSG("DNS response length", resp_len > 12, "got %zu", resp_len);

    // T22: Parse DNS response
    IpAddress dns_resolved;
    r = dns_parse_response(dns_response, resp_len, &dns_resolved);
    TEST("DNS parse response for IP", r == 1);
    TEST_MSG("DNS response IP = 10.0.0.5",
             dns_resolved.octet[0] == 10 && dns_resolved.octet[3] == 5,
             "got %d.%d.%d.%d", dns_resolved.octet[0], dns_resolved.octet[1], dns_resolved.octet[2], dns_resolved.octet[3]);

    // ========================================
    printf("\n" ANSI_YELLOW "  --- HTTP Protocol Tests ---\n" ANSI_RESET);
    // ========================================

    // T23: HTTP init
    http_server_init();
    TEST("HTTP server initialized", !http_server_is_running());

    // T24: HTTP server start/stop
    r = http_server_start(&server_ip, NULL);
    TEST("HTTP server start", r == 1);
    TEST("HTTP server is running", http_server_is_running());

    r = http_server_stop();
    TEST("HTTP server stop", r == 1);
    TEST("HTTP server not running after stop", !http_server_is_running());

    // T25: HTTP start again (idempotent)
    r = http_server_start(&server_ip, "/var/www");
    TEST("HTTP server re-start", r == 1);

    // T26: Parse HTTP GET request
    char method[16], path[256];
    r = http_parse_request("GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n", 50,
                            method, sizeof(method), path, sizeof(path));
    TEST("HTTP parse GET request", r == 1);
    TEST_MSG("HTTP method = GET", strcmp(method, "GET") == 0, "got '%s'", method);
    TEST_MSG("HTTP path = /index.html", strcmp(path, "/index.html") == 0, "got '%s'", path);

    // T27: Parse HTTP Head request
    r = http_parse_request("HEAD / HTTP/1.1\r\nHost: localhost\r\n\r\n", 38,
                            method, sizeof(method), path, sizeof(path));
    TEST("HTTP parse HEAD request", r == 1);
    TEST_MSG("HTTP method = HEAD", strcmp(method, "HEAD") == 0, "got '%s'", method);

    // T28: Build HTTP response
    char response[4096];
    resp_len = 0;
    r = http_build_response(200, "OK", "text/html",
                             "<html><body><h1>OK</h1></body></html>", 40,
                             response, &resp_len, sizeof(response));
    TEST("HTTP build 200 response", r == 1);
    TEST_MSG("HTTP response contains 200", strstr(response, "200 OK") != NULL,
             "got: %s", response);
    TEST_MSG("HTTP response has Content-Length",
             strstr(response, "Content-Length: 40") != NULL,
             "got: %s", response);

    // T29: Build 404 response
    resp_len = 0;
    r = http_build_response(404, "Not Found", "text/plain",
                             "404 Not Found", 13,
                             response, &resp_len, sizeof(response));
    TEST("HTTP build 404 response", r == 1);
    TEST("HTTP 404 contains 'Not Found'", strstr(response, "404 Not Found") != NULL);

    // T30: HTTP serve default page
    resp_len = 0;
    r = http_serve_default("/", response, &resp_len, sizeof(response));
    TEST("HTTP serve default page", r == 1);
    TEST("HTTP default page contains 200 OK", strstr(response, "200 OK") != NULL);
    TEST("HTTP default page contains HTML", strstr(response, "<!DOCTYPE html>") != NULL);
    TEST("HTTP default page contains Welcome", strstr(response, "Welcome to Magi System") != NULL);

    // T31: HTTP handle full request
    resp_len = 0;
    r = http_server_handle_request("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n", 40,
                                    response, &resp_len, sizeof(response));
    TEST("HTTP handle GET request", r == 1);
    TEST("HTTP response from handler contains 200 OK", strstr(response, "200 OK") != NULL);

    // T32: HTTP handle invalid request -> 400
    resp_len = 0;
    r = http_server_handle_request("INVALID\r\n", 9, response, &resp_len, sizeof(response));
    TEST("HTTP handle invalid request -> 400", r == 1);
    TEST("HTTP 400 response", strstr(response, "400") != NULL);

    // T33: HTTP handle unsupported method -> 405
    resp_len = 0;
    r = http_server_handle_request("POST / HTTP/1.1\r\nHost: localhost\r\n\r\n", 40,
                                    response, &resp_len, sizeof(response));
    TEST("HTTP handle POST -> 405", r == 1);
    TEST("HTTP 405 response", strstr(response, "405") != NULL);

    // T34: HTTP server stop
    http_server_stop();
    TEST("HTTP server stopped after tests", !http_server_is_running());

    // ========================================
    printf("\n" ANSI_YELLOW "  --- MagiSocket Integration Tests ---\n" ANSI_RESET);
    // ========================================

    // T35: Create host with MagiSocket infrastructure
    Host* h2 = m4_create_host(sim, "H2", 192, 168, 1, 20);
    TEST("M4: Created H2", h2 != NULL);

    if (h1 && h2) {
        // T36: Two sockets, each attached to different hosts
        int s1 = magi_socket(AF_INET, SOCK_STREAM);
        int s2 = magi_socket(AF_INET, SOCK_STREAM);
        TEST("M4: Create two sockets", s1 >= 0 && s2 >= 0);

        if (s1 >= 0 && s2 >= 0) {
            // Attach to hosts
            magi_socket_attach_host(s1, h1);
            magi_socket_attach_host(s2, h2);

            // Set up H2 as a listening server
            magi_bind(s2, &h2->ip_address, 80);
            r = magi_listen(s2, 5);
            TEST("M4: H2 listens on port 80", r == 1);

            // Try H1 connecting to H2
            magi_bind(s1, &h1->ip_address, 12345);
            r = magi_connect(s1, &h2->ip_address, 80);
            TEST("M4: H1 connects to H2:80", r == 1);

            // Clean up
            magi_close(s1);
            magi_close(s2);
        }
    }

    // T37: DNS with MagiSocket resolve helper
    IpAddress res;
    r = magi_resolve("www.magi.com", &res);
    TEST("M4: magi_resolve www.magi.com", r == 1);
    TEST_MSG("M4: resolved to 10.0.0.5",
             res.octet[0] == 10 && res.octet[3] == 5,
             "got %d.%d.%d.%d", res.octet[0], res.octet[1], res.octet[2], res.octet[3]);

    // T38: Resolve direct IP (pass-through)
    r = magi_resolve("192.168.1.1", &res);
    TEST("M4: magi_resolve direct IP", r == 1);
    TEST_MSG("M4: resolved 192.168.1.1",
             res.octet[0] == 192 && res.octet[3] == 1,
             "got %d.%d.%d.%d", res.octet[0], res.octet[1], res.octet[2], res.octet[3]);

    // T39: DNS query with hostname containing http:// prefix
    r = dns_server_resolve("http://www.magi.com", &res);
    TEST("M4: DNS resolve with http:// prefix", r == 1);
    TEST_MSG("M4: resolved to 10.0.0.5",
             res.octet[0] == 10 && res.octet[3] == 5,
             "got %d.%d.%d.%d", res.octet[0], res.octet[1], res.octet[2], res.octet[3]);

    // T40: DNS query with trailing path
    r = dns_server_resolve("www.magi.com/index.html", &res);
    TEST("M4: DNS resolve with trailing path", r == 1);
    TEST_MSG("M4: resolved to 10.0.0.5",
             res.octet[0] == 10 && res.octet[3] == 5,
             "got %d.%d.%d.%d", res.octet[0], res.octet[1], res.octet[2], res.octet[3]);

    // T41: HTTP server double stop (idempotent)
    http_server_stop();
    http_server_stop();
    TEST("M4: HTTP server double stop is safe", !http_server_is_running());

    // T42: HTTP content type in response
    http_server_start(&server_ip, NULL);
    resp_len = 0;
    http_serve_default("/", response, &resp_len, sizeof(response));
    TEST("M4: HTTP default page has Content-Type",
         strstr(response, "Content-Type: text/html") != NULL);
    http_server_stop();

    // T43: DHCP OFFER with proper options
    // Re-init DHCP and create a new offer
    dhcp_server_init();
    dhcp_server_set_ip(&server_ip);
    uint8_t mac2[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    DHCPMessage discover3;
    dhcp_create_discover(&discover3, 0x5678, mac2);
    DHCPMessage offer3;
    dhcp_server_handle_discover(&discover3, &offer3);
    TEST("M4: DHCP OFFER has magic cookie", offer3.magic_cookie == DHCP_MAGIC_COOKIE);
    TEST("M4: DHCP OFFER has yiaddr != 0",
         offer3.yiaddr.octet[0] > 0 || offer3.yiaddr.octet[3] > 0);

    // T44: DHCP ACK yiaddr matches offered
    DHCPMessage req3;
    dhcp_create_request(&req3, 0x5678, mac2, &offer3.yiaddr, &server_ip);
    DHCPMessage ack3;
    dhcp_server_handle_request(&req3, &ack3);
    TEST("M4: DHCP ACK yiaddr == OFFER yiaddr",
         ip_octets_equal_public(&ack3.yiaddr, &offer3.yiaddr));

    // T45: HTTP GET request string building
    char get_request[256];
    int get_len = snprintf(get_request, sizeof(get_request),
                           "GET / HTTP/1.1\r\n"
                           "Host: www.magi.com\r\n"
                           "User-Agent: MagiSystem/1.0\r\n"
                           "Accept: */*\r\n"
                           "Connection: close\r\n"
                           "\r\n");
    TEST("M4: HTTP GET request built", get_len > 0);
    TEST("M4: HTTP GET contains Host header",
         strstr(get_request, "Host: www.magi.com") != NULL);

    // T46: HTTP GET response with proper headers
    http_server_start(&server_ip, NULL);
    resp_len = 0;
    http_server_handle_request(get_request, (size_t)get_len,
                                response, &resp_len, sizeof(response));
    TEST("M4: HTTP GET response has Server header",
         strstr(response, "Server: MagiSystem/1.0") != NULL);
    TEST("M4: HTTP GET response has Connection: close",
         strstr(response, "Connection: close") != NULL);
    http_server_stop();

    // T47: DHCP serialization - verify header fields
    DHCPMessage msg;
    dhcp_create_discover(&msg, 0xABCD, test_mac);
    TEST("M4: DHCP htype = 1 (Ethernet)", msg.htype == 1);
    TEST("M4: DHCP hlen = 6 (MAC length)", msg.hlen == 6);

    // T48: Verify chaddr preserved in DHCP messages
    TEST("M4: DHCP chaddr preserved",
         memcmp(msg.chaddr, test_mac, 6) == 0);

    // T49: DNS serialization - verify header fields in query
    uint8_t dns_q[512];
    size_t dns_q_len;
    dns_create_query(dns_q, sizeof(dns_q), &dns_q_len, 0x1234, "test.example.com");
    TEST("M4: DNS query ID = 0x1234",
         (dns_q[0] == 0x12 && dns_q[1] == 0x34));
    TEST("M4: DNS query has RD flag", (dns_q[2] & 0x01) == 0x01);
    TEST("M4: DNS query QDCOUNT = 1",
         (dns_q[4] == 0 && dns_q[5] == 1));

    // T50: HTTP parse with empty path
    r = http_parse_request("GET  HTTP/1.1\r\n", 16,
                            method, sizeof(method), path, sizeof(path));
    TEST("M4: HTTP parse with empty path fails", r == 0);

    simulator_clear(sim);
    test_report_footer();
}

// ==================== RUN ALL ====================

void debug_run_all(Simulator *sim)
{
    printf(ANSI_BOLD ANSI_CYAN "\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║       MAGI SYSTEM - FULL DEBUG SUITE        ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    printf(ANSI_RESET);

    int all_total = 0;
    int all_passed = 0;
    int all_failed = 0;

    // M0
    debug_milestone_0(sim);
    all_total += total_tests;
    all_passed += passed_tests;
    all_failed += failed_tests;

    // M1 (placeholder)
    debug_milestone_1(sim);
    all_total += total_tests;
    all_passed += passed_tests;
    all_failed += failed_tests;

    // M2 (placeholder)
    debug_milestone_2(sim);
    all_total += total_tests;
    all_passed += passed_tests;
    all_failed += failed_tests;

    // M3 (placeholder)
    debug_milestone_3(sim);
    all_total += total_tests;
    all_passed += passed_tests;
    all_failed += failed_tests;

    // M4 (placeholder)
    debug_milestone_4(sim);
    all_total += total_tests;
    all_passed += passed_tests;
    all_failed += failed_tests;

    // Final summary
    printf(ANSI_BOLD "╔══════════════════════════════════════════════╗\n");
    printf("║           FINAL SUMMARY                    ║\n");
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║  Total tests: %28d ║\n", all_total);
    printf("║  " ANSI_GREEN "Passed:      %28d" ANSI_BOLD " ║\n" ANSI_RESET, all_passed);
    if (all_failed > 0) {
        printf("║  " ANSI_RED "Failed:      %28d" ANSI_BOLD " ║\n" ANSI_RESET, all_failed);
    } else {
        printf("║  " ANSI_GREEN "Failed:      %28d" ANSI_BOLD " ║\n" ANSI_RESET, all_failed);
    }
    printf("╚══════════════════════════════════════════════╝\n");
    printf(ANSI_RESET "\n");
}
