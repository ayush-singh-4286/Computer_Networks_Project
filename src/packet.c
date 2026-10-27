
#include "packet.h"

#include <string.h>

static void write_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint16_t read_u16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static void write_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t read_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           p[3];
}

int encode_packet(
    uint8_t *buffer,
    size_t capacity,
    uint32_t sequence,
    uint8_t type,
    const uint8_t *payload,
    uint16_t payload_length,
    size_t *packet_length)
{
    if (!buffer || !packet_length)
        return -1;

    if (payload_length > MAX_PAYLOAD_SIZE)
        return -1;

    if (payload_length > 0 && !payload)
        return -1;

    if (capacity < (size_t)HEADER_SIZE + payload_length)
        return -1;

    if (type < TYPE_DATA || type > TYPE_FIN_ACK)
        return -1;

    if ((type == TYPE_ACK || type == TYPE_FIN ||
         type == TYPE_FIN_ACK) && payload_length != 0)
        return -1;

    memcpy(buffer, PACKET_MAGIC, 4);

    write_u32(buffer + 4, sequence);
    write_u16(buffer + 8, payload_length);

    buffer[10] = type;
    buffer[11] = 0;

    write_u32(buffer + 12, 0);

    if (payload_length > 0)
        memcpy(buffer + HEADER_SIZE, payload, payload_length);

    size_t total = HEADER_SIZE + payload_length;

    uint32_t checksum = calculate_checksum(buffer, total);

    write_u32(buffer + 12, checksum);

    *packet_length = total;

    return 0;
}

int decode_packet(
    const uint8_t *buffer,
    size_t packet_length,
    uint32_t *sequence,
    uint8_t *type,
    const uint8_t **payload,
    uint16_t *payload_length)
{
    if (!buffer || !sequence || !type ||
        !payload || !payload_length)
        return -1;

    if (packet_length < HEADER_SIZE ||
        packet_length > MAX_PACKET_SIZE)
        return -1;

    if (memcmp(buffer, PACKET_MAGIC, 4) != 0)
        return -1;

    uint16_t len = read_u16(buffer + 8);

    if (len > MAX_PAYLOAD_SIZE ||
        packet_length != (size_t)HEADER_SIZE + len)
        return -1;

    if (buffer[11] != 0)
        return -1;

    if (buffer[10] < TYPE_DATA ||
        buffer[10] > TYPE_FIN_ACK)
        return -1;

    if ((buffer[10] == TYPE_ACK ||
         buffer[10] == TYPE_FIN ||
         buffer[10] == TYPE_FIN_ACK) && len != 0)
        return -1;

    uint32_t received = read_u32(buffer + 12);

    uint8_t copy[MAX_PACKET_SIZE];

    memcpy(copy, buffer, packet_length);
    write_u32(copy + 12, 0);

    uint32_t calculated =
        calculate_checksum(copy, packet_length);

    if (received != calculated)
        return -1;

    *sequence = read_u32(buffer + 4);
    *type = buffer[10];
    *payload_length = len;
    *payload = buffer + HEADER_SIZE;

    return 0;
}
