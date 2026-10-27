
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "packet.h"

#define RECEIVER_PORT 9000
#define WINDOW_SIZE 4
#define EXPECTED_PACKETS 8

int main(void)
{
    int sockfd;
    FILE *output;
    struct sockaddr_in receiver_addr;

    uint32_t expected_sequence = 0;
    int received_packet[EXPECTED_PACKETS] = {0};
    uint16_t received_length[EXPECTED_PACKETS] = {0};
    uint8_t received_data[EXPECTED_PACKETS][MAX_PAYLOAD_SIZE];

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    receiver_addr.sin_port = htons(RECEIVER_PORT);

    if (bind(sockfd, (struct sockaddr *)&receiver_addr,
             sizeof(receiver_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    output = fopen("sr_output.txt", "wb");
    if (output == NULL) {
        perror("fopen");
        close(sockfd);
        return 1;
    }

    printf("Selective Repeat receiver listening on port %d\n",
           RECEIVER_PORT);
    printf("Output file: sr_output.txt\n");

    while (expected_sequence < EXPECTED_PACKETS) {
        uint8_t buffer[MAX_PACKET_SIZE];
        struct sockaddr_in sender;
        socklen_t sender_len = sizeof(sender);

        ssize_t received = recvfrom(
            sockfd, buffer, sizeof(buffer), 0,
            (struct sockaddr *)&sender, &sender_len
        );

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("recvfrom");
            fclose(output);
            close(sockfd);
            return 1;
        }

        uint32_t sequence;
        uint8_t type;
        uint16_t payload_length;
        const uint8_t *payload;

        if (decode_packet(buffer, (size_t)received,
                          &sequence, &type, &payload,
                          &payload_length) != 0) {
            printf("Invalid packet discarded.\n");
            continue;
        }

        if (type != TYPE_DATA || sequence >= EXPECTED_PACKETS) {
            printf("Unexpected packet ignored.\n");
            continue;
        }

        /* Accept packets only inside the current receive window. */
        if (sequence >= expected_sequence &&
            sequence < expected_sequence + WINDOW_SIZE) {

            if (!received_packet[sequence]) {
                memcpy(received_data[sequence], payload, payload_length);
                received_length[sequence] = payload_length;
                received_packet[sequence] = 1;

                printf("Buffered DATA %" PRIu32 "\n", sequence);
            } else {
                printf("Duplicate DATA %" PRIu32 "\n", sequence);
            }

            /* Selective Repeat ACKs this individual packet. */
            uint8_t ack[MAX_PACKET_SIZE];
            size_t ack_length;

            if (encode_packet(ack, sizeof(ack), sequence,
                              TYPE_ACK, NULL, 0, &ack_length) != 0) {
                fprintf(stderr, "Failed to encode ACK.\n");
                fclose(output);
                close(sockfd);
                return 1;
            }

            if (sendto(sockfd, ack, ack_length, 0,
                       (struct sockaddr *)&sender, sender_len)
                != (ssize_t)ack_length) {
                perror("sendto ACK");
                fclose(output);
                close(sockfd);
                return 1;
            }

            printf("Sent ACK %" PRIu32 "\n", sequence);

        } else if (sequence < expected_sequence) {
            /* The sender may have retransmitted an already delivered packet. */
            uint8_t ack[MAX_PACKET_SIZE];
            size_t ack_length;

            if (encode_packet(ack, sizeof(ack), sequence,
                              TYPE_ACK, NULL, 0, &ack_length) != 0) {
                fprintf(stderr, "Failed to encode duplicate ACK.\n");
                fclose(output);
                close(sockfd);
                return 1;
            }

            if (sendto(sockfd, ack, ack_length, 0,
                       (struct sockaddr *)&sender, sender_len)
                != (ssize_t)ack_length) {
                perror("sendto duplicate ACK");
                fclose(output);
                close(sockfd);
                return 1;
            }

            printf("Re-sent ACK for old DATA %" PRIu32 "\n", sequence);

        } else {
            printf("DATA %" PRIu32 " is outside the receive window.\n",
                   sequence);
        }

        /* Deliver buffered packets to the file in sequence order. */
        while (expected_sequence < EXPECTED_PACKETS &&
               received_packet[expected_sequence]) {

            if (fwrite(received_data[expected_sequence], 1,
                       received_length[expected_sequence], output)
                != received_length[expected_sequence]) {
                perror("fwrite");
                fclose(output);
                close(sockfd);
                return 1;
            }

            printf("Wrote DATA %" PRIu32 " in order\n",
                   expected_sequence);
            expected_sequence++;
        }

        fflush(output);
    }

    printf("All %d packets received and written in order.\n",
           EXPECTED_PACKETS);

    fclose(output);
    close(sockfd);
    return 0;
}
