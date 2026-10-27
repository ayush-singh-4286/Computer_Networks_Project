
#define _POSIX_C_SOURCE 200809L

#include "channel_emulator.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>

#define EMULATOR_PORT 7000
#define RECEIVER_PORT 9000
#define BUFFER_SIZE 2048

static void sleep_ms(int ms)
{
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
}

static void forward_packet(int sock, const char *data, int length,
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

int main(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
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
    emulator_addr.sin_port = htons(EMULATOR_PORT);

    if (bind(sock, (struct sockaddr *)&emulator_addr,
             sizeof(emulator_addr)) < 0) {
        perror("bind");
        close(sock);
        return EXIT_FAILURE;
    }

    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    receiver_addr.sin_port = htons(RECEIVER_PORT);

    int sender_known = 0;

    ChannelConfig config = {
        .loss_rate = 0.0,
        .duplicate_rate = 0.0,
        .corruption_rate = 0.0,
        .reorder_rate = 1.0,
        .min_delay_ms = 0,
        .max_delay_ms = 0,
        .seed = 1234
    };

    initialize_channel(&config);

    printf("Channel emulator started on UDP port %d.\n", EMULATOR_PORT);
    printf("Receiver port: %d\n", RECEIVER_PORT);
    printf("Fault rates: loss=%.2f duplicate=%.2f corruption=%.2f reorder=%.2f\n",
           config.loss_rate, config.duplicate_rate,
           config.corruption_rate, config.reorder_rate);
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

        if (source_addr.sin_port == htons(RECEIVER_PORT)) {
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
