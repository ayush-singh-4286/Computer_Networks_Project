
#include "packet.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define RECEIVER_PORT 9000
#define MAX_RETRIES 5
#define TIMEOUT_SECONDS 2

int main(void)
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct timeval timeout = {TIMEOUT_SECONDS, 0};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        close(sockfd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in receiver_addr = {0};
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(RECEIVER_PORT);
    receiver_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    const char *message = "Hello from Stop-and-Wait!";
    uint8_t packet[MAX_PACKET_SIZE];
    size_t packet_length;

    uint32_t sequence_number = 0;

    if (encode_packet(packet, sizeof(packet),
                      sequence_number, TYPE_DATA,
                      (const uint8_t *)message,
                      (uint16_t)strlen(message),
                      &packet_length) != 0) {
        fprintf(stderr, "Failed to encode DATA packet.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        ssize_t sent = sendto(
            sockfd, packet, packet_length, 0,
            (struct sockaddr *)&receiver_addr,
            sizeof(receiver_addr));

        if (sent != (ssize_t)packet_length) {
            perror("sendto");
            close(sockfd);
            return EXIT_FAILURE;
        }

        printf("Sent DATA sequence %u (attempt %d)\n",
               sequence_number, attempt);

        uint8_t ack[MAX_PACKET_SIZE];
        struct sockaddr_in from_addr = {0};
        socklen_t from_len = sizeof(from_addr);

        ssize_t received = recvfrom(
            sockfd, ack, sizeof(ack), 0,
            (struct sockaddr *)&from_addr, &from_len);

        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("ACK timeout; retrying if attempts remain.\n");
                continue;
            }

            if (errno == EINTR) {
                attempt--;
                continue;
            }

            perror("recvfrom");
            close(sockfd);
            return EXIT_FAILURE;
        }

        if (from_addr.sin_family != AF_INET ||
            from_addr.sin_addr.s_addr != receiver_addr.sin_addr.s_addr ||
            from_addr.sin_port != receiver_addr.sin_port) {
            printf("ACK from unexpected source; ignoring.\n");
            continue;
        }

        uint32_t ack_sequence;
        uint8_t ack_type;
        uint16_t ack_length;
        const uint8_t *ack_payload;

        if (decode_packet(ack, (size_t)received,
                          &ack_sequence, &ack_type,
                          &ack_payload, &ack_length) != 0) {
            printf("Invalid ACK packet; retrying.\n");
            continue;
        }

        if (ack_type == TYPE_ACK &&
            ack_sequence == sequence_number &&
            ack_length == 0) {
            printf("ACK received for sequence %u. Success!\n",
                   sequence_number);
            close(sockfd);
            return EXIT_SUCCESS;
        }

        printf("Unexpected packet received; retrying.\n");
    }

    printf("Transfer failed: no valid ACK after %d attempts.\n",
           MAX_RETRIES);

    close(sockfd);
    return EXIT_FAILURE;
}
