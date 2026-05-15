#include "router.h"
#include "icmp.h"

#include "../core/packet.h"
#include "../layer2/ethernet.h"

#include <stdio.h>
#include <string.h>

static int ip_is_zero(const IpAddress* ip)
{
    return ip != NULL &&
           ip->octet[0] == 0 && ip->octet[1] == 0 &&
           ip->octet[2] == 0 && ip->octet[3] == 0;
}

static MacAddress mac_broadcast(void)
{
    MacAddress mac;
    memset(mac.bytes, 0xFF, sizeof(mac.bytes));
    return mac;
}

static void print_ip_address(const IpAddress* ip)
{
    char buffer[32];
    ip_to_string(ip, buffer, sizeof(buffer), false);
    printf("%s", buffer);
}

static void print_mac_address(const MacAddress* mac)
{
    if (mac == NULL) {
        printf("??:??:??:??:??:??");
        return;
    }

    printf("%02X:%02X:%02X:%02X:%02X:%02X",
           mac->bytes[0], mac->bytes[1], mac->bytes[2],
           mac->bytes[3], mac->bytes[4], mac->bytes[5]);
}

static int router_arp_index_for_ip(const Router* router, const IpAddress* ip)
{
    if (router == NULL || ip == NULL) {
        return -1;
    }

    for (size_t i = 0; i < router->arp_table.size; i++) {
        if (ip_octets_equal_public(&router->arp_table.entries[i].ip, ip)) {
            return (int)i;
        }
    }

    return -1;
}

static int router_vlan_for_ip(const Router* router, const IpAddress* ip, int fallback_vlan)
{
    int index = router_arp_index_for_ip(router, ip);

    if (index >= 0 && router->arp_entry_vlans[index] > 0) {
        return router->arp_entry_vlans[index];
    }

    return fallback_vlan;
}

static int router_has_interface_ip(const Router* router, const IpAddress* ip, int* out_port)
{
    if (router == NULL || ip == NULL) {
        return 0;
    }

    for (int i = 0; i < router->base.NUM_INTERFACES; i++) {
        if (router->interface_ips[i].has_ip &&
            ip_octets_equal_public(&router->interface_ips[i].ip_address, ip)) {
            if (out_port != NULL) {
                *out_port = i + 1;
            }
            return 1;
        }
    }

    return 0;
}

static IpAddress router_interface_ip_or_zero(const Router* router, int port)
{
    uint8_t zero[] = {0, 0, 0, 0};

    if (router != NULL && port >= 1 && port <= router->base.NUM_INTERFACES &&
        router->interface_ips[port - 1].has_ip) {
        return router->interface_ips[port - 1].ip_address;
    }

    return ip_init(zero, 0);
}

static int router_send_ethernet_frame(
    Router* router,
    int out_interface,
    int vlan_id,
    const MacAddress* dst_mac,
    uint16_t ethertype,
    const uint8_t* payload,
    size_t payload_len
)
{
    EthernetFrame frame;
    uint8_t raw[ETHERNET_VLAN_HEADER_SIZE + ETHERNET_MAX_PAYLOAD];
    size_t raw_len;
    Interface* iface;

    if (router == NULL || dst_mac == NULL || payload == NULL ||
        out_interface < 1 || out_interface > router->base.NUM_INTERFACES) {
        return 0;
    }

    iface = node_get_interface(&router->base, out_interface);
    if (iface == NULL) {
        return 0;
    }

    if (!ethernet_create(&frame, *dst_mac, iface->Mac_Address, ethertype, payload, payload_len)) {
        return 0;
    }

    if (vlan_id > 0) {
        ethernet_set_vlan(&frame, (uint16_t)vlan_id);
    }

    raw_len = packet_to_bytes((Packet*)&frame, raw, sizeof(raw));
    if (raw_len == 0) {
        return 0;
    }

    send(iface, raw, raw_len);
    return 1;
}

static int router_queue_pending_packet(
    Router* router,
    const IpAddress* next_hop_ip,
    int out_interface,
    int vlan_id,
    const IPv4Packet* packet
)
{
    RouterPendingQueue* queue;
    RouterPendingPacket* item;

    if (router == NULL || next_hop_ip == NULL || packet == NULL) {
        return 0;
    }

    queue = &router->pending_queue;
    if (queue->size >= ROUTER_PENDING_QUEUE_MAX_ENTRIES) {
        return 0;
    }

    item = &queue->items[queue->tail];
    item->next_hop_ip = *next_hop_ip;
    item->out_interface = out_interface;
    item->vlan_id = vlan_id;
    item->packet = *packet;

    queue->tail = (queue->tail + 1) % ROUTER_PENDING_QUEUE_MAX_ENTRIES;
    queue->size++;
    return 1;
}

static int router_dequeue_pending_for_ip(Router* router, const IpAddress* next_hop_ip, RouterPendingPacket* out_packet)
{
    RouterPendingQueue* queue;
    size_t original_size;
    int found = 0;

    if (router == NULL || next_hop_ip == NULL || out_packet == NULL) {
        return 0;
    }

    queue = &router->pending_queue;
    original_size = queue->size;
    for (size_t i = 0; i < original_size; i++) {
        RouterPendingPacket current = queue->items[queue->head];
        queue->head = (queue->head + 1) % ROUTER_PENDING_QUEUE_MAX_ENTRIES;
        queue->size--;

        if (!found && ip_octets_equal_public(&current.next_hop_ip, next_hop_ip)) {
            *out_packet = current;
            found = 1;
            continue;
        }

        queue->items[queue->tail] = current;
        queue->tail = (queue->tail + 1) % ROUTER_PENDING_QUEUE_MAX_ENTRIES;
        queue->size++;
    }

    return found;
}

static int router_send_arp_request(Router* router, int out_interface, const IpAddress* target_ip, int vlan_id)
{
    ARPMessage request;
    uint8_t raw[28];
    size_t raw_len;
    MacAddress broadcast = mac_broadcast();
    Interface* iface;
    IpAddress sender_ip;

    if (router == NULL || target_ip == NULL ||
        out_interface < 1 || out_interface > router->base.NUM_INTERFACES) {
        return 0;
    }

    iface = node_get_interface(&router->base, out_interface);
    sender_ip = router_interface_ip_or_zero(router, out_interface);
    if (iface == NULL || ip_is_zero(&sender_ip)) {
        return 0;
    }

    arp_message_init_request(&request, iface->Mac_Address, sender_ip, *target_ip);
    raw_len = packet_to_bytes((Packet*)&request, raw, sizeof(raw));
    if (raw_len == 0) {
        return 0;
    }

    printf("[%s] Sending ARP Request for ", router->base.NAME);
    print_ip_address(target_ip);
    printf(" on interface %d\n", out_interface);

    return router_send_ethernet_frame(router, out_interface, vlan_id, &broadcast, ETHERNET_TYPE_ARP, raw, raw_len);
}

static int router_send_arp_reply(Router* router, const ARPMessage* request, int out_interface, int vlan_id)
{
    ARPMessage reply;
    uint8_t raw[28];
    size_t raw_len;
    Interface* iface;

    if (router == NULL || request == NULL ||
        out_interface < 1 || out_interface > router->base.NUM_INTERFACES) {
        return 0;
    }

    iface = node_get_interface(&router->base, out_interface);
    if (iface == NULL) {
        return 0;
    }

    arp_message_init_reply(
        &reply,
        iface->Mac_Address,
        request->target_ip,
        request->sender_mac,
        request->sender_ip);

    raw_len = packet_to_bytes((Packet*)&reply, raw, sizeof(raw));
    if (raw_len == 0) {
        return 0;
    }

    printf("[%s] ARP Request received, replying to ", router->base.NAME);
    print_ip_address(&request->sender_ip);
    printf("\n");

    return router_send_ethernet_frame(router, out_interface, vlan_id, &request->sender_mac, ETHERNET_TYPE_ARP, raw, raw_len);
}

static int router_send_packet_to_next_hop(
    Router* router,
    IPv4Packet* packet,
    const IpAddress* next_hop_ip,
    int out_interface
)
{
    const MacAddress* next_hop_mac;
    uint8_t raw_ip[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
    size_t raw_ip_len;
    int vlan_id = 0;

    if (router == NULL || packet == NULL || next_hop_ip == NULL) {
        return 0;
    }

    if (out_interface < 1 || out_interface > router->base.NUM_INTERFACES) {
        return 0;
    }

    vlan_id = router_vlan_for_ip(router, next_hop_ip, router->interface_vlans[out_interface - 1]);
    next_hop_mac = arp_table_get_const(&router->arp_table, next_hop_ip);
    if (next_hop_mac != NULL) {
        raw_ip_len = packet_to_bytes((Packet*)packet, raw_ip, sizeof(raw_ip));
        if (raw_ip_len == 0) {
            return 0;
        }

        printf("[%s] Forwarding IPv4 packet to ", router->base.NAME);
        print_ip_address(next_hop_ip);
        printf(" via interface %d (", out_interface);
        print_mac_address(next_hop_mac);
        printf(")\n");

        return router_send_ethernet_frame(
            router,
            out_interface,
            vlan_id,
            next_hop_mac,
            ETHERNET_TYPE_IPV4,
            raw_ip,
            raw_ip_len);
    }

    printf("[%s] ARP cache miss for ", router->base.NAME);
    print_ip_address(next_hop_ip);
    printf(", queueing packet\n");

    if (!router_queue_pending_packet(router, next_hop_ip, out_interface, vlan_id, packet)) {
        return 0;
    }

    return router_send_arp_request(router, out_interface, next_hop_ip, vlan_id);
}

static int router_flush_pending_packets(Router* router, const IpAddress* resolved_ip)
{
    RouterPendingPacket pending;
    int sent = 0;

    if (router == NULL || resolved_ip == NULL) {
        return 0;
    }

    while (router_dequeue_pending_for_ip(router, resolved_ip, &pending)) {
        if (router_send_packet_to_next_hop(
                router,
                &pending.packet,
                &pending.next_hop_ip,
                pending.out_interface)) {
            sent++;
        }
    }

    return sent;
}

static void router_send_icmp_error(Router* router, const IPv4Packet* original, uint8_t type, uint8_t code)
{
    uint8_t original_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
    uint8_t icmp_payload[28];
    size_t original_len;
    size_t payload_len;
    ICMPMessage icmp;
    uint8_t icmp_raw[ICMP_HEADER_SIZE + ICMP_MAX_PAYLOAD];
    size_t icmp_len;
    IPv4Packet response;
    const RoutingTableEntry* route;
    IpAddress src_ip;

    if (router == NULL || original == NULL) {
        return;
    }

    route = router_lookup_route(router, &original->src_ip);
    if (route == NULL) {
        printf("[%s] Cannot send ICMP error, no route back to source\n", router->base.NAME);
        return;
    }

    src_ip = router_interface_ip_or_zero(router, route->out_interface);
    if (ip_is_zero(&src_ip)) {
        return;
    }

    original_len = packet_to_bytes((Packet*)original, original_raw, sizeof(original_raw));
    payload_len = original_len < sizeof(icmp_payload) ? original_len : sizeof(icmp_payload);
    memcpy(icmp_payload, original_raw, payload_len);

    if (!icmp_create(&icmp, type, code, 0, 0, icmp_payload, payload_len)) {
        return;
    }

    icmp_len = packet_to_bytes((Packet*)&icmp, icmp_raw, sizeof(icmp_raw));
    if (icmp_len == 0) {
        return;
    }

    if (!ipv4_create(&response, src_ip, original->src_ip, IPV4_DEFAULT_TTL, IPV4_PROTOCOL_ICMP, icmp_raw, icmp_len)) {
        return;
    }

    router_send_ipv4_packet(router, &response);
}

static void router_forward_ipv4_packet(Router* router, IPv4Packet* packet)
{
    const RoutingTableEntry* route;
    IpAddress next_hop_ip;

    if (router == NULL || packet == NULL) {
        return;
    }

    if (packet->ttl <= 1) {
        printf("[%s] TTL expired, sending ICMP Time Exceeded\n", router->base.NAME);
        router_send_icmp_error(router, packet, ICMP_TIME_EXCEEDED, 0);
        return;
    }

    packet->ttl--;
    route = router_lookup_route(router, &packet->dst_ip);
    if (route == NULL) {
        printf("[%s] No route to ", router->base.NAME);
        print_ip_address(&packet->dst_ip);
        printf(", sending ICMP Destination Unreachable\n");
        router_send_icmp_error(router, packet, ICMP_DEST_UNREACHABLE, 0);
        return;
    }

    next_hop_ip = ip_is_zero(&route->next_hop) ? packet->dst_ip : route->next_hop;
    router_send_packet_to_next_hop(router, packet, &next_hop_ip, route->out_interface);
}

static void router_handle_local_ipv4(Router* router, const IPv4Packet* packet)
{
    ICMPMessage request;
    ICMPMessage reply;
    uint8_t icmp_raw[ICMP_HEADER_SIZE + ICMP_MAX_PAYLOAD];
    size_t icmp_len;
    IPv4Packet response;

    if (router == NULL || packet == NULL) {
        return;
    }

    if (packet->protocol != IPV4_PROTOCOL_ICMP) {
        printf("[%s] IPv4 protocol %u not supported locally\n", router->base.NAME, packet->protocol);
        return;
    }

    icmp_init(&request);
    if (!packet_from_bytes((Packet*)&request, packet->payload, packet->payload_len) ||
        !icmp_validate_checksum(&request)) {
        printf("[%s] Dropped invalid ICMP message\n", router->base.NAME);
        return;
    }

    if (request.type == ICMP_ECHO_REQUEST) {
        if (!icmp_create(&reply, ICMP_ECHO_REPLY, 0, request.identifier, request.sequence,
                         request.payload, request.payload_len)) {
            return;
        }

        icmp_len = packet_to_bytes((Packet*)&reply, icmp_raw, sizeof(icmp_raw));
        if (icmp_len == 0) {
            return;
        }

        if (!ipv4_create(&response, packet->dst_ip, packet->src_ip, IPV4_DEFAULT_TTL,
                         IPV4_PROTOCOL_ICMP, icmp_raw, icmp_len)) {
            return;
        }

        router_send_ipv4_packet(router, &response);
    }
}

static void router_receive(Node* self, Interface* in_interface, const uint8_t* raw, size_t raw_len)
{
    Router* router = (Router*)self;
    EthernetFrame frame;
    ARPMessage arp;
    IPv4Packet ip_packet;
    int in_port;
    int vlan_id = 0;
    MacAddress* iface_mac;

    if (router == NULL || in_interface == NULL || raw == NULL) {
        return;
    }

    ethernet_init(&frame);
    if (!packet_from_bytes((Packet*)&frame, raw, raw_len)) {
        printf("[%s] Dropped invalid Ethernet frame\n", router->base.NAME);
        return;
    }

    in_port = in_interface->portNumber;
    vlan_id = frame.has_vlan ? (int)frame.vlan_id : 0;
    if (vlan_id > 0) {
        router->interface_vlans[in_port - 1] = vlan_id;
    }

    iface_mac = &in_interface->Mac_Address;
    if (!ethernet_is_broadcast(&frame.dst_mac) && !mac_equal(&frame.dst_mac, iface_mac)) {
        return;
    }

    if (frame.ethertype == ETHERNET_TYPE_ARP) {
        arp_message_init(&arp);
        if (!packet_from_bytes((Packet*)&arp, frame.payload, frame.payload_len)) {
            printf("[%s] Dropped invalid ARP message\n", router->base.NAME);
            return;
        }

        router_learn_arp(router, &arp, vlan_id);

        if (arp.opcode == ARP_OPCODE_REQUEST && router_has_interface_ip(router, &arp.target_ip, NULL)) {
            router_send_arp_reply(router, &arp, in_port, vlan_id);
        } else if (arp.opcode == ARP_OPCODE_REPLY) {
            printf("[%s] ARP Reply received: ", router->base.NAME);
            print_ip_address(&arp.sender_ip);
            printf(" is ");
            print_mac_address(&arp.sender_mac);
            printf("\n");
            router_flush_pending_packets(router, &arp.sender_ip);
        }
        return;
    }

    if (frame.ethertype == ETHERNET_TYPE_IPV4) {
        ipv4_init(&ip_packet);
        if (!packet_from_bytes((Packet*)&ip_packet, frame.payload, frame.payload_len) ||
            !ipv4_validate_checksum(&ip_packet)) {
            printf("[%s] Dropped invalid IPv4 packet\n", router->base.NAME);
            return;
        }

        if (router_has_interface_ip(router, &ip_packet.dst_ip, NULL)) {
            router_handle_local_ipv4(router, &ip_packet);
        } else {
            router_forward_ipv4_packet(router, &ip_packet);
        }
        return;
    }

    printf("[%s] Dropped unknown ethertype 0x%04X\n", router->base.NAME, frame.ethertype);
}

static const NodeVTable ROUTER_VTABLE = {
    .receive = router_receive
};

void router_init(Router* router, int num_interfaces)
{
    router_init_with_macs(router, num_interfaces, NULL);
}

void router_init_with_macs(Router* router, int num_interfaces, const MacAddress* mac_addresses)
{
    if (router == NULL) {
        return;
    }

    node_init_with_macs(&router->base, NODE_ROUTER, num_interfaces, mac_addresses);
    node_set_vtable(&router->base, &ROUTER_VTABLE);

    for (int i = 0; i < MAX_PORT; i++) {
        router->interface_ips[i].portNumber = i + 1;
        router->interface_ips[i].has_ip = false;
        router->interface_vlans[i] = 0;
    }
    router->route_count = 0;
    arp_table_init(&router->arp_table);
    memset(router->arp_entry_vlans, 0, sizeof(router->arp_entry_vlans));
    router_pending_queue_init(&router->pending_queue);
}

void router_pending_queue_init(RouterPendingQueue* queue)
{
    if (queue == NULL) {
        return;
    }

    queue->head = 0;
    queue->tail = 0;
    queue->size = 0;
}

int router_add_route(Router* router, IpAddress destination, IpAddress next_hop, int out_interface)
{
    if (router == NULL || out_interface < 1 || out_interface > router->base.NUM_INTERFACES ||
        destination.prefix > 32) {
        return 0;
    }

    destination = ip_network_address(destination);

    for (size_t i = 0; i < router->route_count; i++) {
        RoutingTableEntry* entry = &router->routing_table[i];
        if (entry->out_interface == out_interface &&
            entry->destination.prefix == destination.prefix &&
            ip_octets_equal_public(&entry->destination, &destination) &&
            ip_octets_equal_public(&entry->next_hop, &next_hop)) {
            return 1;
        }
    }

    if (router->route_count >= ROUTER_MAX_ROUTES) {
        return 0;
    }

    router->routing_table[router->route_count].destination = destination;
    router->routing_table[router->route_count].next_hop = next_hop;
    router->routing_table[router->route_count].out_interface = out_interface;
    router->route_count++;
    return 1;
}

int router_add_direct_routes(Router* router)
{
    uint8_t zero_bytes[] = {0, 0, 0, 0};
    IpAddress zero = ip_init(zero_bytes, 0);
    int added = 0;

    if (router == NULL) {
        return 0;
    }

    for (int i = 0; i < router->base.NUM_INTERFACES; i++) {
        if (router->interface_ips[i].has_ip) {
            if (router_add_route(router, router->interface_ips[i].ip_address, zero, i + 1)) {
                added++;
            }
        }
    }

    return added;
}

const RoutingTableEntry* router_lookup_route(const Router* router, const IpAddress* dst_ip)
{
    const RoutingTableEntry* best = NULL;
    int best_prefix = -1;
    uint32_t dst_value;

    if (router == NULL || dst_ip == NULL) {
        return NULL;
    }

    dst_value = ip_to_u32_public(dst_ip);
    for (size_t i = 0; i < router->route_count; i++) {
        const RoutingTableEntry* entry = &router->routing_table[i];
        uint32_t mask = ip_prefix_to_mask(entry->destination.prefix);
        uint32_t route_value = ip_to_u32_public(&entry->destination);

        if ((dst_value & mask) == (route_value & mask) &&
            (int)entry->destination.prefix > best_prefix) {
            best = entry;
            best_prefix = entry->destination.prefix;
        }
    }

    return best;
}

int router_learn_arp(Router* router, const ARPMessage* message, int vlan_id)
{
    int index;

    if (router == NULL || message == NULL) {
        return 0;
    }

    if (!arp_table_set(&router->arp_table, &message->sender_ip, &message->sender_mac)) {
        return 0;
    }

    index = router_arp_index_for_ip(router, &message->sender_ip);
    if (index >= 0) {
        router->arp_entry_vlans[index] = vlan_id;
    }

    return 1;
}

int router_send_ipv4_packet(Router* router, IPv4Packet* packet)
{
    const RoutingTableEntry* route;
    IpAddress next_hop_ip;

    if (router == NULL || packet == NULL) {
        return 0;
    }

    route = router_lookup_route(router, &packet->dst_ip);
    if (route == NULL) {
        return 0;
    }

    next_hop_ip = ip_is_zero(&route->next_hop) ? packet->dst_ip : route->next_hop;
    return router_send_packet_to_next_hop(router, packet, &next_hop_ip, route->out_interface);
}

int router_send_icmp_echo_request(Router* router, const IpAddress* target_ip, uint8_t ttl, uint16_t sequence)
{
    uint8_t payload[32];
    ICMPMessage icmp;
    uint8_t icmp_raw[ICMP_HEADER_SIZE + ICMP_MAX_PAYLOAD];
    size_t icmp_len;
    IPv4Packet packet;
    const RoutingTableEntry* route;
    IpAddress src_ip;

    if (router == NULL || target_ip == NULL) {
        return 0;
    }

    route = router_lookup_route(router, target_ip);
    if (route == NULL) {
        return 0;
    }

    src_ip = router_interface_ip_or_zero(router, route->out_interface);
    if (ip_is_zero(&src_ip)) {
        return 0;
    }

    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)i;
    }

    if (!icmp_create(&icmp, ICMP_ECHO_REQUEST, 0, 0xBEEF, sequence, payload, sizeof(payload))) {
        return 0;
    }
    icmp_len = packet_to_bytes((Packet*)&icmp, icmp_raw, sizeof(icmp_raw));
    if (icmp_len == 0) {
        return 0;
    }
    if (!ipv4_create(&packet, src_ip, *target_ip, ttl, IPV4_PROTOCOL_ICMP, icmp_raw, icmp_len)) {
        return 0;
    }

    return router_send_ipv4_packet(router, &packet);
}

void router_print_routes(const Router* router)
{
    char destination[32];
    char next_hop[32];

    if (router == NULL) {
        return;
    }

    printf("[%s] Routing table:\n", router->base.NAME);
    printf("  Destination        Next hop        Interface\n");
    for (size_t i = 0; i < router->route_count; i++) {
        const RoutingTableEntry* entry = &router->routing_table[i];
        ip_to_string(&entry->destination, destination, sizeof(destination), true);
        if (ip_is_zero(&entry->next_hop)) {
            snprintf(next_hop, sizeof(next_hop), "direct");
        } else {
            ip_to_string(&entry->next_hop, next_hop, sizeof(next_hop), false);
        }
        printf("  %-18s %-15s %d\n", destination, next_hop, entry->out_interface);
    }
}
