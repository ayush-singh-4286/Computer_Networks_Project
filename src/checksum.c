
#include "packet.h"

uint32_t calculate_checksum(const uint8_t *data, size_t length)
{
    uint32_t hash = 2166136261u;

    for (size_t i = 0; i < length; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }

    return hash;
}
