#include "ethernet.h"

#include <string.h>

static uint16_t read_u16_be(const uint8_t* raw)
{
    return (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
}

static void write_u16_be(uint8_t* out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)(value & 0xFF);
}

static size_t ethernet_to_bytes(const Packet* self, uint8_t* out, size_t out_size)
{
    const EthernetFrame* frame = (const EthernetFrame*)self;
    size_t header_size;
    size_t offset = 0;

    if (frame == NULL || out == NULL) {
        return 0;
    }

    header_size = frame->has_vlan ? ETHERNET_VLAN_HEADER_SIZE : ETHERNET_HEADER_SIZE;
    if (frame->payload_len > ETHERNET_MAX_PAYLOAD || out_size < header_size + frame->payload_len) {
        return 0;
    }

    memcpy(out + offset, frame->dst_mac.bytes, 6);
    offset += 6;
    memcpy(out + offset, frame->src_mac.bytes, 6);
    offset += 6;

    if (frame->has_vlan) {
        write_u16_be(out + offset, ETHERNET_TYPE_VLAN);
        offset += 2;
        write_u16_be(out + offset, frame->vlan_id & ETHERNET_VLAN_ID_MASK);
        offset += 2;
    }

    write_u16_be(out + offset, frame->ethertype);
    offset += 2;

    memcpy(out + offset, frame->payload, frame->payload_len);
    offset += frame->payload_len;

    return offset;
}

static int ethernet_from_bytes(Packet* self, const uint8_t* raw, size_t raw_len)
{
    EthernetFrame* frame = (EthernetFrame*)self;
    size_t offset = 0;
    uint16_t type_or_vlan;

    if (frame == NULL || raw == NULL || raw_len < ETHERNET_HEADER_SIZE) {
        return 0;
    }

    memcpy(frame->dst_mac.bytes, raw + offset, 6);
    offset += 6;
    memcpy(frame->src_mac.bytes, raw + offset, 6);
    offset += 6;

    type_or_vlan = read_u16_be(raw + offset);
    offset += 2;

    frame->has_vlan = false;
    frame->vlan_id = 0;
    if (type_or_vlan == ETHERNET_TYPE_VLAN) {
        if (raw_len < ETHERNET_VLAN_HEADER_SIZE) {
            return 0;
        }
        frame->has_vlan = true;
        frame->vlan_id = read_u16_be(raw + offset) & ETHERNET_VLAN_ID_MASK;
        offset += 2;
        frame->ethertype = read_u16_be(raw + offset);
        offset += 2;
    } else {
        frame->ethertype = type_or_vlan;
    }

    if (raw_len - offset > ETHERNET_MAX_PAYLOAD) {
        return 0;
    }

    frame->payload_len = raw_len - offset;
    memcpy(frame->payload, raw + offset, frame->payload_len);

    return 1;
}

static const PacketVTable ETHERNET_VTABLE = {
    .to_bytes = ethernet_to_bytes,
    .from_bytes = ethernet_from_bytes
};

void ethernet_init(EthernetFrame *frame)
{
    if (frame == NULL) {
        return;
    }

    memset(frame, 0, sizeof(*frame));
    frame->base.vtable = &ETHERNET_VTABLE;
    frame->base.type = ETHERNET;
}

int ethernet_create(
    EthernetFrame *frame,
    MacAddress dst_mac,
    MacAddress src_mac,
    uint16_t ethertype,
    const uint8_t *payload,
    size_t payload_len
)
{
    if (frame == NULL || payload_len > ETHERNET_MAX_PAYLOAD) {
        return 0;
    }

    if (payload_len > 0 && payload == NULL) {
        return 0;
    }

    ethernet_init(frame);
    frame->dst_mac = dst_mac;
    frame->src_mac = src_mac;
    frame->ethertype = ethertype;
    frame->payload_len = payload_len;

    if (payload_len > 0) {
        memcpy(frame->payload, payload, payload_len);
    }

    return 1;
}

int ethernet_set_vlan(EthernetFrame *frame, uint16_t vlan_id)
{
    if (frame == NULL || vlan_id == 0 || vlan_id > ETHERNET_VLAN_ID_MASK) {
        return 0;
    }

    frame->has_vlan = true;
    frame->vlan_id = vlan_id;
    return 1;
}

void ethernet_clear_vlan(EthernetFrame *frame)
{
    if (frame == NULL) {
        return;
    }

    frame->has_vlan = false;
    frame->vlan_id = 0;
}

bool ethernet_is_broadcast(const MacAddress *mac)
{
    if (mac == NULL) {
        return false;
    }

    for (int i = 0; i < 6; i++) {
        if (mac->bytes[i] != 0xFF) {
            return false;
        }
    }

    return true;
}
