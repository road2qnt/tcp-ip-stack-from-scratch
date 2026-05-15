#include "host.h"
#include "../core/packet.h"
#include "../layer3/icmp.h"
#include <stdio.h>
#include <string.h>

static int ip_octets_equal(const IpAddress* a, const IpAddress* b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }

    return memcmp(a->octet, b->octet, 4) == 0;
}

static uint32_t ip_to_u32(const IpAddress* ip)
{
    return ((uint32_t)ip->octet[0] << 24) |
           ((uint32_t)ip->octet[1] << 16) |
           ((uint32_t)ip->octet[2] << 8) |
           (uint32_t)ip->octet[3];
}

static uint32_t prefix_to_mask(uint8_t prefix)
{
    if (prefix == 0) {
        return 0;
    }

    if (prefix >= 32) {
        return 0xFFFFFFFFu;
    }

    return 0xFFFFFFFFu << (32 - prefix);
}

static int host_is_same_subnet(const Host* host, const IpAddress* target_ip)
{
    uint32_t mask;

    if (host == NULL || target_ip == NULL || !host->has_ip) {
        return 0;
    }

    mask = prefix_to_mask(host->ip_address.prefix);
    return (ip_to_u32(&host->ip_address) & mask) == (ip_to_u32(target_ip) & mask);
}

static MacAddress mac_broadcast(void)
{
    MacAddress mac;
    memset(mac.bytes, 0xFF, sizeof(mac.bytes));
    return mac;
}

static void print_ip_address(const IpAddress* ip)
{
    if (ip == NULL) {
        printf("?.?.?.?");
        return;
    }

    printf("%u.%u.%u.%u", ip->octet[0], ip->octet[1], ip->octet[2], ip->octet[3]);
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

static int host_send_ethernet_frame(
    Host* host,
    const MacAddress* dst_mac,
    uint16_t ethertype,
    const uint8_t* payload,
    size_t payload_len
)
{
    EthernetFrame frame;
    uint8_t raw[ETHERNET_VLAN_HEADER_SIZE + ETHERNET_MAX_PAYLOAD];
    size_t raw_len;

    if (host == NULL || dst_mac == NULL || payload == NULL) {
        return 0;
    }

    if (!ethernet_create(
            &frame,
            *dst_mac,
            host->base.interfaces[0].Mac_Address,
            ethertype,
            payload,
            payload_len)) {
        return 0;
    }

    raw_len = packet_to_bytes((Packet*)&frame, raw, sizeof(raw));
    if (raw_len == 0) {
        return 0;
    }

    send(&host->base.interfaces[0], raw, raw_len);
    return 1;
}

static int host_send_arp_request(Host* host, const IpAddress* target_ip)
{
    ARPMessage request;
    uint8_t arp_raw[28];
    size_t arp_len;
    MacAddress broadcast = mac_broadcast();

    if (host == NULL || target_ip == NULL || !host->has_ip) {
        return 0;
    }

    arp_message_init_request(
        &request,
        host->base.interfaces[0].Mac_Address,
        host->ip_address,
        *target_ip);

    arp_len = packet_to_bytes((Packet*)&request, arp_raw, sizeof(arp_raw));
    if (arp_len == 0) {
        return 0;
    }

    printf("[%s] Sending ARP Request for ", host->base.NAME);
    print_ip_address(target_ip);
    printf("\n");

    return host_send_ethernet_frame(host, &broadcast, ETHERNET_TYPE_ARP, arp_raw, arp_len);
}

static int host_send_arp_reply(Host* host, const ARPMessage* request)
{
    ARPMessage reply;
    uint8_t arp_raw[28];
    size_t arp_len;

    if (host == NULL || request == NULL || !host->has_ip) {
        return 0;
    }

    arp_message_init_reply(
        &reply,
        host->base.interfaces[0].Mac_Address,
        host->ip_address,
        request->sender_mac,
        request->sender_ip);

    arp_len = packet_to_bytes((Packet*)&reply, arp_raw, sizeof(arp_raw));
    if (arp_len == 0) {
        return 0;
    }

    printf("[%s] ARP Request received, replying to ", host->base.NAME);
    print_ip_address(&request->sender_ip);
    printf("\n");

    return host_send_ethernet_frame(host, &request->sender_mac, ETHERNET_TYPE_ARP, arp_raw, arp_len);
}

static void host_receive(Node* self, Interface* in_interface, const uint8_t* raw, size_t raw_len)
{
    Host* host = (Host*)self;
    EthernetFrame frame;
    ARPMessage arp;
    IPv4Packet ip_packet;
    ICMPMessage icmp;
    MacAddress* host_mac;

    (void)in_interface;

    if (host == NULL || raw == NULL) {
        return;
    }

    ethernet_init(&frame);
    if (!packet_from_bytes((Packet*)&frame, raw, raw_len)) {
        printf("[%s] Dropped invalid Ethernet frame\n", host->base.NAME);
        return;
    }

    host_mac = &host->base.interfaces[0].Mac_Address;
    if (!ethernet_is_broadcast(&frame.dst_mac) && !mac_equal(&frame.dst_mac, host_mac)) {
        return;
    }

    if (frame.has_vlan) {
        printf("[%s] Dropped tagged Ethernet frame on host interface\n", host->base.NAME);
        return;
    }

    if (frame.ethertype == ETHERNET_TYPE_ARP) {
        arp_message_init(&arp);
        if (!packet_from_bytes((Packet*)&arp, frame.payload, frame.payload_len)) {
            printf("[%s] Dropped invalid ARP message\n", host->base.NAME);
            return;
        }

        host_learn_arp(host, &arp);

        if (arp.opcode == ARP_OPCODE_REQUEST && host->has_ip && ip_octets_equal(&arp.target_ip, &host->ip_address)) {
            host_send_arp_reply(host, &arp);
        } else if (arp.opcode == ARP_OPCODE_REPLY) {
            printf("[%s] ARP Reply received: ", host->base.NAME);
            print_ip_address(&arp.sender_ip);
            printf(" is ");
            print_mac_address(&arp.sender_mac);
            printf("\n");
            host_flush_pending_packets(host, &arp.sender_ip);
        }

        return;
    }

    if (frame.ethertype == ETHERNET_TYPE_IPV4) {
        ipv4_init(&ip_packet);
        if (!packet_from_bytes((Packet*)&ip_packet, frame.payload, frame.payload_len) ||
            !ipv4_validate_checksum(&ip_packet)) {
            printf("[%s] Dropped invalid IPv4 packet\n", host->base.NAME);
            return;
        }

        if (!host->has_ip || !ip_octets_equal_public(&ip_packet.dst_ip, &host->ip_address)) {
            return;
        }

        if (ip_packet.protocol != IPV4_PROTOCOL_ICMP) {
            printf("[%s] IPv4 protocol %u not supported\n", host->base.NAME, ip_packet.protocol);
            return;
        }

        icmp_init(&icmp);
        if (!packet_from_bytes((Packet*)&icmp, ip_packet.payload, ip_packet.payload_len) ||
            !icmp_validate_checksum(&icmp)) {
            printf("[%s] Dropped invalid ICMP message\n", host->base.NAME);
            return;
        }

        host->last_icmp_type = icmp.type;
        host->last_icmp_source = ip_packet.src_ip;
        host->last_icmp_sequence = icmp.sequence;
        host->has_last_icmp = true;

        if (icmp.type == ICMP_ECHO_REQUEST) {
            ICMPMessage reply;
            uint8_t icmp_raw[ICMP_HEADER_SIZE + ICMP_MAX_PAYLOAD];
            size_t icmp_len;
            IPv4Packet response;
            uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
            size_t ip_len;

            printf("[%s] ICMP Echo Request received from ", host->base.NAME);
            print_ip_address(&ip_packet.src_ip);
            printf("\n");

            if (!icmp_create(&reply, ICMP_ECHO_REPLY, 0, icmp.identifier, icmp.sequence,
                             icmp.payload, icmp.payload_len)) {
                return;
            }

            icmp_len = packet_to_bytes((Packet*)&reply, icmp_raw, sizeof(icmp_raw));
            if (icmp_len == 0 ||
                !ipv4_create(&response, host->ip_address, ip_packet.src_ip, IPV4_DEFAULT_TTL,
                             IPV4_PROTOCOL_ICMP, icmp_raw, icmp_len)) {
                return;
            }

            ip_len = packet_to_bytes((Packet*)&response, ip_raw, sizeof(ip_raw));
            if (ip_len == 0) {
                return;
            }

            host_send_l3_packet(host, &ip_packet.src_ip, ETHERNET_TYPE_IPV4, ip_raw, ip_len);
        } else if (icmp.type == ICMP_ECHO_REPLY) {
            printf("%s: Reply from ", host->base.NAME);
            print_ip_address(&ip_packet.src_ip);
            printf(": bytes=%zu time=0ms TTL=%u\n", icmp.payload_len, ip_packet.ttl);
        } else if (icmp.type == ICMP_TIME_EXCEEDED) {
            printf("%s: ICMP Time Exceeded from ", host->base.NAME);
            print_ip_address(&ip_packet.src_ip);
            printf("\n");
        } else if (icmp.type == ICMP_DEST_UNREACHABLE) {
            printf("%s: ICMP Destination Unreachable from ", host->base.NAME);
            print_ip_address(&ip_packet.src_ip);
            printf("\n");
        } else {
            printf("[%s] ICMP type %u received\n", host->base.NAME, icmp.type);
        }
        return;
    }

    printf("[%s] Dropped unknown ethertype 0x%04X\n", host->base.NAME, frame.ethertype);
}

static const NodeVTable HOST_VTABLE = {
    .receive = host_receive
};

void host_init(Host* host, int num_interfaces)
{
    host_init_with_macs(host, num_interfaces, NULL);
}

void host_init_with_macs(Host* host, int num_interfaces, const MacAddress* mac_addresses)
{
    if (host == NULL) {
        return;
    }

    node_init_with_macs(&host->base, NODE_HOST, num_interfaces, mac_addresses);
    node_set_vtable(&host->base, &HOST_VTABLE);
    host->has_ip = false;
    host->has_last_icmp = false;
    host->last_icmp_type = 0;
    host->last_icmp_sequence = 0;
    arp_table_init(&host->arp_table);
    host_pending_queue_init(&host->pending_queue);
}

void host_pending_queue_init(HostPendingQueue* queue)
{
    if (queue == NULL) {
        return;
    }

    queue->head = 0;
    queue->tail = 0;
    queue->size = 0;
}

int host_queue_pending_packet(Host* host, const IpAddress* target_ip, uint16_t ethertype, const uint8_t* payload, size_t payload_len)
{
    HostPendingQueue* queue;
    HostPendingPacket* item;

    if (host == NULL || target_ip == NULL || payload == NULL || payload_len > ETHERNET_MAX_PAYLOAD) {
        return 0;
    }

    queue = &host->pending_queue;
    if (queue->size >= HOST_PENDING_QUEUE_MAX_ENTRIES) {
        return 0;
    }

    item = &queue->items[queue->tail];
    item->target_ip = *target_ip;
    item->ethertype = ethertype;
    memcpy(item->payload, payload, payload_len);
    item->payload_len = payload_len;

    queue->tail = (queue->tail + 1) % HOST_PENDING_QUEUE_MAX_ENTRIES;
    queue->size++;

    return 1;
}

int host_dequeue_pending_packet_for_ip(Host* host, const IpAddress* target_ip, HostPendingPacket* out_packet)
{
    HostPendingQueue* queue;
    size_t original_size;
    int found = 0;

    if (host == NULL || target_ip == NULL || out_packet == NULL) {
        return 0;
    }

    queue = &host->pending_queue;
    original_size = queue->size;

    for (size_t i = 0; i < original_size; i++) {
        HostPendingPacket current = queue->items[queue->head];
        queue->head = (queue->head + 1) % HOST_PENDING_QUEUE_MAX_ENTRIES;
        queue->size--;

        if (!found && ip_octets_equal(&current.target_ip, target_ip)) {
            *out_packet = current;
            found = 1;
            continue;
        }

        queue->items[queue->tail] = current;
        queue->tail = (queue->tail + 1) % HOST_PENDING_QUEUE_MAX_ENTRIES;
        queue->size++;
    }

    return found;
}

size_t host_pending_count_for_ip(const Host* host, const IpAddress* target_ip)
{
    const HostPendingQueue* queue;
    size_t count = 0;
    size_t index;

    if (host == NULL || target_ip == NULL) {
        return 0;
    }

    queue = &host->pending_queue;
    index = queue->head;
    for (size_t i = 0; i < queue->size; i++) {
        if (ip_octets_equal(&queue->items[index].target_ip, target_ip)) {
            count++;
        }
        index = (index + 1) % HOST_PENDING_QUEUE_MAX_ENTRIES;
    }

    return count;
}

int host_learn_arp(Host* host, const ARPMessage* message)
{
    if (host == NULL || message == NULL) {
        return 0;
    }

    return arp_table_set(&host->arp_table, &message->sender_ip, &message->sender_mac);
}

int host_send_l3_packet(Host* host, const IpAddress* target_ip, uint16_t ethertype, const uint8_t* payload, size_t payload_len)
{
    const IpAddress* next_hop_ip;
    const MacAddress* next_hop_mac;

    if (host == NULL || target_ip == NULL || payload == NULL || payload_len > ETHERNET_MAX_PAYLOAD || !host->has_ip) {
        return 0;
    }

    next_hop_ip = target_ip;
    if (!host_is_same_subnet(host, target_ip)) {
        next_hop_ip = &host->default_gateway;
    }

    next_hop_mac = arp_table_get_const(&host->arp_table, next_hop_ip);
    if (next_hop_mac != NULL) {
        printf("[%s] ARP cache hit for ", host->base.NAME);
        print_ip_address(next_hop_ip);
        printf(", sending Ethernet frame\n");
        return host_send_ethernet_frame(host, next_hop_mac, ethertype, payload, payload_len);
    }

    printf("[%s] ARP cache miss for ", host->base.NAME);
    print_ip_address(next_hop_ip);
    printf(", queueing packet\n");

    if (!host_queue_pending_packet(host, next_hop_ip, ethertype, payload, payload_len)) {
        return 0;
    }

    return host_send_arp_request(host, next_hop_ip);
}

int host_flush_pending_packets(Host* host, const IpAddress* resolved_ip)
{
    HostPendingPacket packet;
    const MacAddress* mac;
    int sent = 0;

    if (host == NULL || resolved_ip == NULL) {
        return 0;
    }

    mac = arp_table_get_const(&host->arp_table, resolved_ip);
    if (mac == NULL) {
        return 0;
    }

    while (host_dequeue_pending_packet_for_ip(host, resolved_ip, &packet)) {
        printf("[%s] Flushing pending packet to ", host->base.NAME);
        print_ip_address(resolved_ip);
        printf("\n");

        if (host_send_ethernet_frame(host, mac, packet.ethertype, packet.payload, packet.payload_len)) {
            sent++;
        }
    }

    return sent;
}

int host_send_icmp_echo_request(Host* host, const IpAddress* target_ip, uint8_t ttl, uint16_t sequence)
{
    uint8_t payload[32];
    ICMPMessage icmp;
    uint8_t icmp_raw[ICMP_HEADER_SIZE + ICMP_MAX_PAYLOAD];
    size_t icmp_len;
    IPv4Packet packet;
    uint8_t ip_raw[IPV4_HEADER_SIZE + IPV4_MAX_PAYLOAD];
    size_t ip_len;

    if (host == NULL || target_ip == NULL || !host->has_ip) {
        return 0;
    }

    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)i;
    }

    host->has_last_icmp = false;
    if (!icmp_create(&icmp, ICMP_ECHO_REQUEST, 0, 0x1234, sequence, payload, sizeof(payload))) {
        return 0;
    }

    icmp_len = packet_to_bytes((Packet*)&icmp, icmp_raw, sizeof(icmp_raw));
    if (icmp_len == 0) {
        return 0;
    }

    if (!ipv4_create(&packet, host->ip_address, *target_ip, ttl, IPV4_PROTOCOL_ICMP, icmp_raw, icmp_len)) {
        return 0;
    }

    ip_len = packet_to_bytes((Packet*)&packet, ip_raw, sizeof(ip_raw));
    if (ip_len == 0) {
        return 0;
    }

    return host_send_l3_packet(host, target_ip, ETHERNET_TYPE_IPV4, ip_raw, ip_len);
}
