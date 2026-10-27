
#ifndef CHANNEL_EMULATOR_H
#define CHANNEL_EMULATOR_H

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

typedef struct
{
    double loss_rate;
    double duplicate_rate;
    double corruption_rate;
    double reorder_rate;

    int min_delay_ms;
    int max_delay_ms;

    unsigned int seed;
} ChannelConfig;

void initialize_channel(ChannelConfig *config);

int should_drop_packet(const ChannelConfig *config);
int should_duplicate_packet(const ChannelConfig *config);
int should_corrupt_packet(const ChannelConfig *config);
int should_reorder_packet(const ChannelConfig *config);

#define MAX_QUEUE 100
#define MAX_PACKET_SIZE 2048

typedef struct
{
    char data[MAX_PACKET_SIZE];
    int length;
    unsigned long long arrival_order;
    unsigned long long delivery_time_ms;
    struct sockaddr_in destination;
} QueuedPacket;

#endif
