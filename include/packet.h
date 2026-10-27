
#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include <stddef.h>

#define PACKET_MAGIC "RDT1"
#define HEADER_SIZE 16
#define MAX_PAYLOAD_SIZE 1024
#define MAX_PACKET_SIZE (HEADER_SIZE + MAX_PAYLOAD_SIZE)

#define TYPE_DATA 1
#define TYPE_ACK  2
#define TYPE_FIN  3
#define TYPE_FIN_ACK 4

uint32_t calculate_checksum(const uint8_t *data, size_t length);

int encode_packet(
    uint8_t *buffer,
    size_t capacity,
    uint32_t sequence,
    uint8_t type,
    const uint8_t *payload,
    uint16_t payload_length,
    size_t *packet_length
);

int decode_packet(
    const uint8_t *buffer,
    size_t packet_length,
    uint32_t *sequence,
    uint8_t *type,
    const uint8_t **payload,
    uint16_t *payload_length
);

#endif
