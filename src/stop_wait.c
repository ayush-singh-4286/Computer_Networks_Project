#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "packet.h"

#define RECEIVER_PORT 9000
#define MAX_RETRIES 5
#define TIMEOUT_SECONDS 2

int main(void)
{
    int sockfd;
    struct sockaddr_in receiver_addr;
    struct timeval timeout = {TIMEOUT_SECONDS, 0};

    uint8_t packet[MAX_PACKET_SIZE];
    uint8_t ack[MAX_PACKET_SIZE];
    size_t packet_length;

    const char *message = "Hello from Stop-and-Wait!";
    uint32_t sequence_number = 0;

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

    if (encode_packet(
            packet,
            sizeof(packet),
            sequence_number,
            PACKET_TYPE_DATA,
            (const uint8_t *)message,
            (uint16_t)strlen(message),
            &packet_length) != 0) {
        fprintf(stderr, "Failed to encode DATA packet.\n");
        close(sockfd);
        return 1;
    }

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {

        ssize_t sent = sendto(
            sockfd,
            packet,
            packet_length,
            0,
            (struct sockaddr *)&receiver_addr,
            sizeof(receiver_addr)
        );

        if (sent < 0) {
            perror("sendto");
            close(sockfd);
            return 1;
        }

        printf("Sent DATA sequence %u (attempt %d)\n",
               sequence_number, attempt);

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
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("recvfrom");
                close(sockfd);
                return 1;
            }

            printf("ACK timeout; retrying if attempts remain.\n");
            continue;
        }

        /* Accept ACKs only from the configured receiver. */
        if (from_addr.sin_family != AF_INET ||
            from_addr.sin_addr.s_addr != receiver_addr.sin_addr.s_addr ||
            from_addr.sin_port != receiver_addr.sin_port) {
            printf("ACK received from an unexpected source; ignoring.\n");
            continue;
        }

        if (!verify_packet(ack, (size_t)received)) {
            printf("Invalid ACK packet; retrying.\n");
            continue;
        }

        uint32_t ack_sequence =
            ((uint32_t)ack[4] << 24) |
            ((uint32_t)ack[5] << 16) |
            ((uint32_t)ack[6] << 8) |
            (uint32_t)ack[7];

        if (ack[10] == PACKET_TYPE_ACK &&
            ack_sequence == sequence_number) {
            printf("ACK received for sequence %u. Success!\n",
                   sequence_number);
            close(sockfd);
            return 0;
        }

        printf("Unexpected packet received; retrying.\n");
    }

    printf("Transfer failed: no valid ACK after %d attempts.\n",
           MAX_RETRIES);

    close(sockfd);
    return 1;
}