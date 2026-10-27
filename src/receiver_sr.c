#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include "packet.h"

#define DEFAULT_RECEIVER_PORT 9000
#define DEFAULT_WINDOW_SIZE 16
#define MAX_SR_WINDOW 256
#define FIN_WAIT_SECONDS 1

typedef struct {
    int received;
    uint16_t length;
    uint8_t data[MAX_PAYLOAD_SIZE];
} SrSlot;

static uint32_t file_hash(FILE *file)
{
    uint32_t hash = 2166136261u;
    int ch;

    rewind(file);

    while ((ch = fgetc(file)) != EOF) {
        hash ^= (uint8_t)ch;
        hash *= 16777619u;
    }

    return hash;
}

static int send_response(
    SOCKET sockfd,
    struct sockaddr_in *sender,
    socklen_t sender_len,
    uint32_t sequence,
    uint8_t type)
{
    uint8_t packet[MAX_PACKET_SIZE];
    size_t packet_length = 0;

    if (encode_packet(
            packet,
            sizeof(packet),
            sequence,
            type,
            NULL,
            0,
            &packet_length) != 0) {
        fprintf(stderr, "Failed to encode response packet.\n");
        return -1;
    }

    ssize_t sent = sendto(
        sockfd,
        packet,
        packet_length,
        0,
        (struct sockaddr *)sender,
        sender_len);

    if (sent != (ssize_t)packet_length) {
        perror("sendto");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    const char *out_filename = (argc >= 2) ? argv[1] : "sr_output.txt";
    int port = (argc >= 3) ? atoi(argv[2]) : DEFAULT_RECEIVER_PORT;
    int window_size = (argc >= 4) ? atoi(argv[3]) : DEFAULT_WINDOW_SIZE;

    if (window_size <= 0 || window_size > MAX_SR_WINDOW)
        window_size = DEFAULT_WINDOW_SIZE;

    FILE *output = fopen(out_filename, "w+b");
    if (output == NULL) {
        perror("fopen");
        return 1;
    }

    SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == INVALID_SOCKET) {
        perror("socket");
        fclose(output);
        return 1;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in receiver_addr;
    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    receiver_addr.sin_port = htons((uint16_t)port);

    if (bind(sockfd, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr)) < 0) {
        perror("bind");
        close(sockfd);
        fclose(output);
        return 1;
    }

    printf("Selective Repeat receiver listening on port %d\n", port);
    printf("Output file: %s | Window size: %d\n", out_filename, window_size);

    SrSlot slots[MAX_SR_WINDOW];
    memset(slots, 0, sizeof(slots));

    uint32_t expected_sequence = 0;
    int finished = 0;
    int success = 1;
    int fin_received = 0;
    struct sockaddr_in last_sender = {0};

    uint8_t buffer[MAX_PACKET_SIZE];

    while (!finished) {
        struct sockaddr_in sender;
        socklen_t sender_len = sizeof(sender);

        ssize_t received = recvfrom(
            sockfd, buffer, sizeof(buffer), 0,
            (struct sockaddr *)&sender, &sender_len);

        if (received < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (fin_received) {
                    printf("FIN wait timeout. Closing SR receiver.\n");
                    break;
                }
                continue;
            }
            perror("recvfrom");
            success = 0;
            break;
        }

        uint32_t sequence = 0;
        uint8_t type = 0;
        const uint8_t *payload = NULL;
        uint16_t payload_length = 0;

        if (decode_packet(buffer, (size_t)received, &sequence, &type,
                          &payload, &payload_length) != 0) {
            printf("Invalid packet discarded.\n");
            continue;
        }

        if (type == TYPE_DATA) {
            if (sequence >= expected_sequence && sequence < expected_sequence + (uint32_t)window_size) {
                int slot = (int)(sequence % MAX_SR_WINDOW);
                if (!slots[slot].received) {
                    memcpy(slots[slot].data, payload, payload_length);
                    slots[slot].length = payload_length;
                    slots[slot].received = 1;
                    printf("Buffered DATA %" PRIu32 "\n", sequence);
                } else {
                    printf("Duplicate DATA %" PRIu32 "\n", sequence);
                }

                send_response(sockfd, &sender, sender_len, sequence, TYPE_ACK);
                printf("Sent individual ACK %" PRIu32 "\n", sequence);

                while (slots[expected_sequence % MAX_SR_WINDOW].received) {
                    int cur_slot = (int)(expected_sequence % MAX_SR_WINDOW);
                    if (output) {
                        if (fwrite(slots[cur_slot].data, 1, slots[cur_slot].length, output) != slots[cur_slot].length) {
                            perror("fwrite");
                            success = 0;
                            break;
                        }
                        fflush(output);
                    }
                    slots[cur_slot].received = 0;
                    printf("Wrote DATA %" PRIu32 " in order\n", expected_sequence);
                    expected_sequence++;
                }

            } else if (sequence < expected_sequence) {
                send_response(sockfd, &sender, sender_len, sequence, TYPE_ACK);
                printf("Re-sent ACK for old DATA %" PRIu32 "\n", sequence);
            } else {
                printf("DATA %" PRIu32 " outside receive window (expected %" PRIu32 ")\n",
                       sequence, expected_sequence);
            }

        } else if (type == TYPE_FIN) {
            if (sequence == expected_sequence) {
                if (output) {
                    fflush(output);
                    uint32_t hash = file_hash(output);
                    printf("Delivered DATA packets: %" PRIu32 "\n", expected_sequence);
                    printf("Output file hash (FNV-1a): %08" PRIx32 "\n", hash);
                    fclose(output);
                    output = NULL;
                }

                send_response(sockfd, &sender, sender_len, sequence, TYPE_FIN_ACK);
                printf("FIN received. Sent FIN_ACK.\n");

                last_sender = sender;
                fin_received = 1;

                struct timeval timeout = { .tv_sec = FIN_WAIT_SECONDS, .tv_usec = 0 };
                setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            } else if (sequence < expected_sequence) {
                send_response(sockfd, &sender, sender_len, sequence, TYPE_FIN_ACK);
                printf("Duplicate FIN received. Sent FIN_ACK.\n");
            } else {
                printf("FIN arrived too early: %" PRIu32 ", expected: %" PRIu32 "\n",
                       sequence, expected_sequence);
            }
        }

        if (fin_received &&
            sender.sin_addr.s_addr == last_sender.sin_addr.s_addr &&
            sender.sin_port == last_sender.sin_port &&
            type == TYPE_FIN) {
            continue;
        }
    }

    if (output) {
        fflush(output);
        uint32_t hash = file_hash(output);
        printf("Delivered DATA packets: %" PRIu32 "\n", expected_sequence);
        printf("Output file hash (FNV-1a): %08" PRIx32 "\n", hash);
        fclose(output);
        output = NULL;
    }

    close(sockfd);

    if (success) {
        printf("Selective Repeat receiver closed successfully.\n");
        return 0;
    }
    return 1;
}
