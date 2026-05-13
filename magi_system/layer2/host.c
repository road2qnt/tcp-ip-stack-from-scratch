#include "host.h"
#include <string.h>

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
    host->has_ip = false;
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

        if (!found && ip_equal(&current.target_ip, (IpAddress*)target_ip)) {
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
        if (ip_equal((IpAddress*)&queue->items[index].target_ip, (IpAddress*)target_ip)) {
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
