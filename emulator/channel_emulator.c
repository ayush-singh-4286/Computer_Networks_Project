#define _POSIX_C_SOURCE 200809L

#include "channel_emulator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_EMULATOR_PORT 7000
#define DEFAULT_RECEIVER_PORT 9000
#define BUFFER_SIZE 2048

static void sleep_ms(int ms)
{
    net_sleep_ms(ms);
}

static void forward_packet(SOCKET sock, const char *data, int length,
                           const struct sockaddr_in *destination)
{
    if (length <= 0)
        return;

    ssize_t sent = sendto(sock, data, (size_t)length, 0,
                          (const struct sockaddr *)destination,
                          sizeof(*destination));

    if (sent < 0)
        perror("sendto");
    else
        printf("Forwarded %zd bytes.\n", sent);
}

int main(int argc, char *argv[])
{
    int emulator_port = DEFAULT_EMULATOR_PORT;
    int receiver_port = DEFAULT_RECEIVER_PORT;

    ChannelConfig config = {
        .loss_rate = 0.0,
        .duplicate_rate = 0.0,
        .corruption_rate = 0.0,
        .reorder_rate = 0.0,
        .min_delay_ms = 0,
        .max_delay_ms = 0,
        .seed = 1234
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--loss") == 0 && i + 1 < argc) {
            config.loss_rate = atof(argv[++i]);
        } else if (strcmp(argv[i], "--duplicate") == 0 && i + 1 < argc) {
            config.duplicate_rate = atof(argv[++i]);
        } else if (strcmp(argv[i], "--corrupt") == 0 && i + 1 < argc) {
            config.corruption_rate = atof(argv[++i]);
        } else if (strcmp(argv[i], "--reorder") == 0 && i + 1 < argc) {
            config.reorder_rate = atof(argv[++i]);
        } else if (strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            int d = atoi(argv[++i]);
            config.min_delay_ms = d;
            config.max_delay_ms = d;
        } else if (strcmp(argv[i], "--jitter") == 0 && i + 1 < argc) {
            int j = atoi(argv[++i]);
            int base_d = config.min_delay_ms;
            config.min_delay_ms = (base_d >= j) ? (base_d - j) : 0;
            config.max_delay_ms = base_d + j;
        } else if (strcmp(argv[i], "--min-delay") == 0 && i + 1 < argc) {
            config.min_delay_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-delay") == 0 && i + 1 < argc) {
            config.max_delay_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            config.seed = (unsigned int)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            emulator_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--receiver-port") == 0 && i + 1 < argc) {
            receiver_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("  --loss <rate>           Packet drop probability [0.0 - 1.0]\n");
            printf("  --duplicate <rate>      Packet duplication probability [0.0 - 1.0]\n");
            printf("  --corrupt <rate>        Packet corruption probability [0.0 - 1.0]\n");
            printf("  --reorder <rate>        Packet reorder probability [0.0 - 1.0]\n");
            printf("  --delay <ms>            Base one-way delay in milliseconds\n");
            printf("  --jitter <ms>           Delay jitter in milliseconds\n");
            printf("  --min-delay <ms>        Minimum delay in milliseconds\n");
            printf("  --max-delay <ms>        Maximum delay in milliseconds\n");
            printf("  --seed <int>            RNG seed (default: 1234)\n");
            printf("  --port <port>           Emulator listen port (default: 7000)\n");
            printf("  --receiver-port <port>  Target receiver port (default: 9000)\n");
            return EXIT_SUCCESS;
        }
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct timeval timeout = { .tv_sec = 0, .tv_usec = 100000 };
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        close(sock);
        return EXIT_FAILURE;
    }

    struct sockaddr_in emulator_addr = {0};
    struct sockaddr_in receiver_addr = {0};
    struct sockaddr_in sender_addr = {0};

    emulator_addr.sin_family = AF_INET;
    emulator_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    emulator_addr.sin_port = htons((uint16_t)emulator_port);

    if (bind(sock, (struct sockaddr *)&emulator_addr,
             sizeof(emulator_addr)) < 0) {
        perror("bind");
        close(sock);
        return EXIT_FAILURE;
    }

    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    receiver_addr.sin_port = htons((uint16_t)receiver_port);

    int sender_known = 0;

    initialize_channel(&config);

    printf("Channel emulator started on UDP port %d.\n", emulator_port);
    printf("Receiver port: %d\n", receiver_port);
    printf("Fault rates: loss=%.3f duplicate=%.3f corruption=%.3f reorder=%.3f delay=[%d..%d]ms seed=%u\n",
           config.loss_rate, config.duplicate_rate,
           config.corruption_rate, config.reorder_rate,
           config.min_delay_ms, config.max_delay_ms, config.seed);
    fflush(stdout);

    QueuedPacket held = {0};
    int has_held_packet = 0;
    unsigned long long arrival_order = 0;

    while (1) {
        char buffer[BUFFER_SIZE];
        struct sockaddr_in source_addr = {0};
        socklen_t source_len = sizeof(source_addr);

        ssize_t received = recvfrom(
            sock, buffer, sizeof(buffer), 0,
            (struct sockaddr *)&source_addr, &source_len);

        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (has_held_packet) {
                    printf("Reorder timeout: forwarding held packet.\n");
                    forward_packet(sock, held.data, held.length,
                                   &held.destination);
                    has_held_packet = 0;
                }
                continue;
            }
            if (errno == EINTR)
                continue;
            perror("recvfrom");
            continue;
        }

        struct sockaddr_in destination;

        if (source_addr.sin_port == htons((uint16_t)receiver_port)) {
            if (!sender_known) {
                printf("Receiver response ignored: sender unknown.\n");
                continue;
            }
            destination = sender_addr;
            printf("Receiver response received.\n");
        } else {
            sender_addr = source_addr;
            sender_known = 1;
            destination = receiver_addr;
            printf("Sender packet received.\n");
        }

        if (should_drop_packet(&config)) {
            printf("Packet dropped.\n");
            continue;
        }

        if (should_corrupt_packet(&config) && received > 0) {
            size_t position = (size_t)received / 2;
            buffer[position] ^= 0x01;
            printf("Packet corrupted at byte %zu.\n", position);
        }

        int delay_range = config.max_delay_ms - config.min_delay_ms + 1;
        int delay_ms = config.min_delay_ms;

        if (delay_range > 0)
            delay_ms += rand() % delay_range;

        if (delay_ms > 0) {
            printf("Delaying packet by %d ms.\n", delay_ms);
            sleep_ms(delay_ms);
        }

        if (should_reorder_packet(&config)) {
            if (!has_held_packet) {
                memcpy(held.data, buffer, (size_t)received);
                held.length = (int)received;
                held.destination = destination;
                held.arrival_order = arrival_order++;
                has_held_packet = 1;
                printf("Holding packet to reorder delivery.\n");
                continue;
            }

            printf("Reordering: forwarding newer packet first.\n");
            forward_packet(sock, buffer, (int)received, &destination);
            forward_packet(sock, held.data, held.length, &held.destination);
            has_held_packet = 0;
        } else {
            forward_packet(sock, buffer, (int)received, &destination);
        }

        if (should_duplicate_packet(&config)) {
            printf("Duplicating packet.\n");
            forward_packet(sock, buffer, (int)received, &destination);
        }

        fflush(stdout);
    }

    close(sock);
    return EXIT_SUCCESS;
}
