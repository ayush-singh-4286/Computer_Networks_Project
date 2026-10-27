#include "packet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#define DEFAULT_RECEIVER_PORT 9000
#define MAX_RETRIES 10
#define TIMEOUT_SECONDS 1

static double now_seconds(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static int send_and_wait_ack(
    SOCKET sockfd,
    struct sockaddr_in *receiver,
    socklen_t receiver_len,
    uint32_t sequence,
    uint8_t type,
    const uint8_t *payload,
    uint16_t payload_length,
    uint64_t *retransmissions,
    uint64_t *timeouts)
{
    uint8_t packet[MAX_PACKET_SIZE];
    size_t packet_length;

    if (encode_packet(packet, sizeof(packet), sequence, type,
                      payload, payload_length, &packet_length) != 0)
        return -1;

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        ssize_t sent = sendto(
            sockfd, packet, packet_length, 0,
            (struct sockaddr *)receiver, receiver_len);

        if (sent != (ssize_t)packet_length) {
            perror("sendto");
            return -1;
        }

        if (attempt > 1) {
            (*retransmissions)++;
            printf("Retransmitted DATA sequence %u (attempt %d)\n", sequence, attempt);
        } else {
            printf("Sent DATA sequence %u (attempt 1)\n", sequence);
        }

        uint8_t ack[MAX_PACKET_SIZE];
        struct sockaddr_in from_addr = {0};
        socklen_t from_len = sizeof(from_addr);

        ssize_t received = recvfrom(
            sockfd, ack, sizeof(ack), 0,
            (struct sockaddr *)&from_addr, &from_len);

        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                (*timeouts)++;
                printf("ACK timeout for sequence %u; retrying.\n", sequence);
                continue;
            }
            if (errno == EINTR) {
                attempt--;
                continue;
            }
            perror("recvfrom");
            return -1;
        }

        if (from_addr.sin_family != AF_INET ||
            from_addr.sin_addr.s_addr != receiver->sin_addr.s_addr ||
            from_addr.sin_port != receiver->sin_port) {
            continue;
        }

        uint32_t ack_sequence;
        uint8_t ack_type;
        uint16_t ack_length;
        const uint8_t *ack_payload;

        if (decode_packet(ack, (size_t)received, &ack_sequence, &ack_type,
                          &ack_payload, &ack_length) != 0) {
            printf("Invalid ACK packet; retrying.\n");
            continue;
        }

        if (ack_sequence == sequence && ack_length == 0 &&
            ((type == TYPE_DATA && ack_type == TYPE_ACK) ||
             (type == TYPE_FIN && ack_type == TYPE_FIN_ACK))) {
            printf("ACK received for sequence %u.\n", sequence);
            return 0;
        }
    }

    fprintf(stderr, "Transfer failed: no valid ACK for sequence %u after %d attempts.\n",
            sequence, MAX_RETRIES);
    return -1;
}

int main(int argc, char *argv[])
{
    const char *receiver_ip = "127.0.0.1";
    int port = DEFAULT_RECEIVER_PORT;
    const char *input_file = NULL;

    if (argc >= 4) {
        receiver_ip = argv[1];
        port = atoi(argv[2]);
        input_file = argv[3];
    } else if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("Usage: %s [<receiver_ip> <port> <input_file>]\n", argv[0]);
        return EXIT_SUCCESS;
    }

    SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == INVALID_SOCKET) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct timeval timeout = {TIMEOUT_SECONDS, 0};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        close(sockfd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in receiver_addr = {0};
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, receiver_ip, &receiver_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid receiver IP: %s\n", receiver_ip);
        close(sockfd);
        return EXIT_FAILURE;
    }

    uint64_t retransmissions = 0;
    uint64_t timeouts = 0;
    uint64_t total_bytes = 0;
    uint32_t data_packets = 0;
    double start_time = now_seconds();

    if (input_file == NULL) {
        /* Backward compatibility: single demo string transfer */
        const char *message = "Hello from Stop-and-Wait!";
        if (send_and_wait_ack(sockfd, &receiver_addr, sizeof(receiver_addr),
                              0, TYPE_DATA, (const uint8_t *)message,
                              (uint16_t)strlen(message), &retransmissions, &timeouts) != 0) {
            close(sockfd);
            return EXIT_FAILURE;
        }
        data_packets = 1;
        total_bytes = strlen(message);
    } else {
        /* Full file transfer */
        FILE *file = fopen(input_file, "rb");
        if (!file) {
            perror("fopen");
            close(sockfd);
            return EXIT_FAILURE;
        }

        uint8_t payload[MAX_PAYLOAD_SIZE];
        uint32_t sequence = 0;
        int success = 1;

        while (1) {
            size_t bytes_read = fread(payload, 1, sizeof(payload), file);
            if (bytes_read > 0) {
                if (send_and_wait_ack(sockfd, &receiver_addr, sizeof(receiver_addr),
                                      sequence, TYPE_DATA, payload,
                                      (uint16_t)bytes_read, &retransmissions, &timeouts) != 0) {
                    success = 0;
                    break;
                }
                sequence++;
                data_packets++;
                total_bytes += bytes_read;
            }
            if (bytes_read < sizeof(payload)) {
                if (ferror(file)) {
                    perror("fread");
                    success = 0;
                }
                break;
            }
        }

        if (success) {
            if (send_and_wait_ack(sockfd, &receiver_addr, sizeof(receiver_addr),
                                  sequence, TYPE_FIN, NULL, 0,
                                  &retransmissions, &timeouts) != 0) {
                success = 0;
            }
        }

        fclose(file);
        if (!success) {
            close(sockfd);
            return EXIT_FAILURE;
        }
    }

    double end_time = now_seconds();
    double duration = end_time - start_time;
    if (duration <= 0.0) duration = 0.000001;
    double goodput = (double)total_bytes / duration;

    printf("\n--- Stop-and-Wait Transfer Summary ---\n");
    printf("DATA packets sent: %u\n", data_packets);
    printf("Total bytes transferred: %" PRIu64 "\n", total_bytes);
    printf("Retransmissions: %" PRIu64 "\n", retransmissions);
    printf("Timeouts: %" PRIu64 "\n", timeouts);
    printf("Duration: %.4f s\n", duration);
    printf("Goodput: %.2f bytes/s (%.2f KiB/s)\n", goodput, goodput / 1024.0);

    close(sockfd);
    return EXIT_SUCCESS;
}
