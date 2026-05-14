#include "debugger.h"
#include "loader.h"
#include "visualizer.h"

#include "../layer2/host.h"
#include "../layer2/switch.h"
#include "../layer3/router.h"

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

    printf("\n" ANSI_YELLOW "  --- FEATURE NOT YET IMPLEMENTED ---\n" ANSI_RESET);
    printf("  Planned tests for Milestone 2:\n");
    printf("    - IPv4 packet serialization/checksum\n");
    printf("    - Router routing table + longest prefix match\n");
    printf("    - ICMP Echo Request/Reply (ping)\n");
    printf("    - ICMP Time Exceeded (traceroute)\n");
    printf("    - ICMP Destination Unreachable\n");
    printf("    - TTL decrement and checksum recalculation\n\n");

    test_report_footer();
}

// ==================== MILESTONE 3: TRANSPORT LAYER ====================

void debug_milestone_3(Simulator *sim)
{
    (void)sim;
    test_reset();
    test_report_header("Milestone 3: Transport Layer (TCP, UDP)");

    printf("\n" ANSI_YELLOW "  --- FEATURE NOT YET IMPLEMENTED ---\n" ANSI_RESET);
    printf("  Planned tests for Milestone 3:\n");
    printf("    - TCP segment serialization\n");
    printf("    - UDP datagram serialization\n");
    printf("    - TCP 3-Way Handshake (SYN -> SYN-ACK -> ACK)\n");
    printf("    - TCP state machine transitions\n");
    printf("    - TCP receive buffer reassembly\n");
    printf("    - TCP 4-Way Teardown (FIN/ACK)\n");
    printf("    - Pseudo-header checksum calculation\n\n");

    test_report_footer();
}

// ==================== MILESTONE 4: APPLICATION LAYER ====================

void debug_milestone_4(Simulator *sim)
{
    (void)sim;
    test_reset();
    test_report_header("Milestone 4: Application Layer (Socket API, DHCP, DNS, HTTP)");

    printf("\n" ANSI_YELLOW "  --- FEATURE NOT YET IMPLEMENTED ---\n" ANSI_RESET);
    printf("  Planned tests for Milestone 4:\n");
    printf("    - MagiSocket API (bind, listen, accept, connect, send, recv, close)\n");
    printf("    - DHCP DORA (Discover, Offer, Request, Acknowledge)\n");
    printf("    - DNS domain name resolution\n");
    printf("    - HTTP GET request / response\n");
    printf("    - HTTP server (start/stop)\n\n");

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
