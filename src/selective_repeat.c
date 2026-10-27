#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "packet.h"

#define RECEIVER_PORT 7000
#define WINDOW_SIZE 4
#define TIMEOUT_SECONDS 1
#define CHUNK_SIZE 8
#define MAX_PACKETS 32
#define MAX_RETRIES 5

int main(void)
{
    int sockfd;
    struct sockaddr_in receiver_addr;
    struct timeval timeout = {TIMEOUT_SECONDS, 0};

    const char *message =
        "Selective Repeat retransmits only unacknowledged packets.";

    size_t message_length = strlen(message);
    uint32_t total_packets =
        (uint32_t)((message_length + CHUNK_SIZE - 1) / CHUNK_SIZE);

    uint8_t packets[MAX_PACKETS][MAX_PACKET_SIZE];
    size_t packet_lengths[MAX_PACKETS];
    int acknowledged[MAX_PACKETS] = {0};
    int retries[MAX_PACKETS] = {0};

    uint32_t base = 0;
    uint32_t next_seq = 0;

    if (total_packets > MAX_PACKETS) {
        fprintf(stderr, "Message requires too many packets.\n");
        return 1;
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        close(sockfd);
        return 1;
    }

    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(RECEIVER_PORT);
    receiver_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    /* Prepare all DATA packets. */
    for (uint32_t seq = 0; seq < total_packets; seq++) {
        size_t offset = (size_t)seq * CHUNK_SIZE;
        size_t remaining = message_length - offset;

        uint16_t chunk_length = (uint16_t)(
            remaining < CHUNK_SIZE ? remaining : CHUNK_SIZE
        );

        if (encode_packet(
                packets[seq],
                sizeof(packets[seq]),
                seq,
                TYPE_DATA,
                (const uint8_t *)(message + offset),
                chunk_length,
                &packet_lengths[seq]) != 0) {
            fprintf(stderr, "Failed to encode packet %u.\n", seq);
            close(sockfd);
            return 1;
        }
    }

    printf("Selective Repeat ARQ: %u packets, window size %d\n",
           total_packets, WINDOW_SIZE);

    while (base < total_packets) {

        /* Send new packets while they fit in the window. */
        while (next_seq < total_packets &&
               next_seq < base + WINDOW_SIZE) {

            ssize_t sent = sendto(
                sockfd,
                packets[next_seq],
                packet_lengths[next_seq],
                0,
                (struct sockaddr *)&receiver_addr,
                sizeof(receiver_addr)
            );

            if (sent < 0) {
                perror("sendto");
                close(sockfd);
                return 1;
            }

            printf("Sent DATA sequence %u\n", next_seq);
            next_seq++;
        }

        uint8_t ack[MAX_PACKET_SIZE];
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        ssize_t received = recvfrom(
            sockfd,
            ack,
            sizeof(ack),
            0,
            (struct sockaddr *)&from_addr,
            &from_len
        );

        if (received < 0) {

            /* Handle socket errors separately from timeouts. */
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("recvfrom");
                close(sockfd);
                return 1;
            }

            printf("Timeout: checking unacknowledged packets.\n");

            /* Retransmit only packets that have not been ACKed. */
            for (uint32_t seq = base; seq < next_seq; seq++) {

                if (acknowledged[seq]) {
                    continue;
                }

                retries[seq]++;

                if (retries[seq] > MAX_RETRIES) {
                    fprintf(stderr,
                            "Transfer failed: too many retries for "
                            "sequence %u.\n", seq);
                    close(sockfd);
                    return 1;
                }

                ssize_t sent = sendto(
                    sockfd,
                    packets[seq],
                    packet_lengths[seq],
                    0,
                    (struct sockaddr *)&receiver_addr,
                    sizeof(receiver_addr)
                );

                if (sent < 0) {
                    perror("sendto");
                    close(sockfd);
                    return 1;
                }

                printf("Retransmitted unacknowledged DATA %u\n", seq);
            }

            continue;
        }

        uint32_t decoded_seq;
uint8_t decoded_type;
uint16_t decoded_length;
const uint8_t *decoded_payload;

if (decode_packet(
        ack,
        (size_t)received,
        &decoded_seq,
        &decoded_type,
        &decoded_payload,
        &decoded_length) != 0 ||
    decoded_type != TYPE_ACK ||
    decoded_length != 0) {
    printf("Invalid or unexpected packet ignored.\n");
    continue;
}

        uint32_t ack_seq = decoded_seq;

        if (ack_seq >= base && ack_seq < next_seq) {

            if (!acknowledged[ack_seq]) {
                acknowledged[ack_seq] = 1;
                printf("Received ACK %u\n", ack_seq);
            }

            /* Advance the window over consecutively ACKed packets. */
            while (base < total_packets && acknowledged[base]) {
                base++;
            }

        } else {
            printf("Ignoring ACK %u outside the current window.\n",
                   ack_seq);
        }
    }

    printf("All %u packets acknowledged successfully.\n",
           total_packets);

    close(sockfd);
    return 0;
}