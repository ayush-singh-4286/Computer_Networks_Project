#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include "packet.h"

#define DEFAULT_RECEIVER_PORT 7000
#define DEFAULT_WINDOW_SIZE 4
#define TIMEOUT_SECONDS 1
#define MAX_RETRIES 10

typedef struct {
    uint8_t data[MAX_PAYLOAD_SIZE];
    uint16_t length;
    uint8_t packet[MAX_PACKET_SIZE];
    size_t packet_len;
    int acknowledged;
    int retries;
} SrPacket;

static double now_seconds(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

int main(int argc, char *argv[])
{
    const char *receiver_ip = "127.0.0.1";
    int port = DEFAULT_RECEIVER_PORT;
    const char *input_file = NULL;
    int window_size = DEFAULT_WINDOW_SIZE;

    if (argc >= 4) {
        receiver_ip = argv[1];
        port = atoi(argv[2]);
        input_file = argv[3];
        if (argc >= 5) {
            window_size = atoi(argv[4]);
            if (window_size <= 0) window_size = DEFAULT_WINDOW_SIZE;
        }
    } else if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("Usage: %s [<receiver_ip> <port> <input_file> [window_size]]\n", argv[0]);
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
    double start_time = now_seconds();

    if (input_file == NULL) {
        /* Teammate demo mode: single string in 8-byte chunks */
        const char *message = "Selective Repeat retransmits only unacknowledged packets.";
        size_t message_length = strlen(message);
        size_t chunk_size = 8;
        uint32_t total_packets = (uint32_t)((message_length + chunk_size - 1) / chunk_size);
        uint8_t packets[32][MAX_PACKET_SIZE];
        size_t packet_lengths[32];
        int acknowledged[32] = {0};
        int retries[32] = {0};

        for (uint32_t seq = 0; seq < total_packets; seq++) {
            size_t offset = (size_t)seq * chunk_size;
            size_t remaining = message_length - offset;
            uint16_t chunk_length = (uint16_t)(remaining < chunk_size ? remaining : chunk_size);
            encode_packet(packets[seq], sizeof(packets[seq]), seq, TYPE_DATA,
                          (const uint8_t *)(message + offset), chunk_length, &packet_lengths[seq]);
        }

        uint32_t base = 0;
        uint32_t next_seq = 0;

        printf("Selective Repeat ARQ: %u packets, window size %d\n", total_packets, window_size);

        while (base < total_packets) {
            while (next_seq < total_packets && next_seq < base + (uint32_t)window_size) {
                sendto(sockfd, packets[next_seq], packet_lengths[next_seq], 0,
                       (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
                printf("Sent DATA sequence %u\n", next_seq);
                next_seq++;
            }

            uint8_t ack[MAX_PACKET_SIZE];
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);

            ssize_t received = recvfrom(sockfd, ack, sizeof(ack), 0,
                                        (struct sockaddr *)&from_addr, &from_len);

            if (received < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("recvfrom");
                    close(sockfd);
                    return EXIT_FAILURE;
                }

                printf("Timeout: checking unacknowledged packets.\n");
                timeouts++;

                for (uint32_t seq = base; seq < next_seq; seq++) {
                    if (acknowledged[seq]) continue;

                    retries[seq]++;
                    if (retries[seq] > MAX_RETRIES) {
                        fprintf(stderr, "Transfer failed: too many retries for sequence %u.\n", seq);
                        close(sockfd);
                        return EXIT_FAILURE;
                    }

                    sendto(sockfd, packets[seq], packet_lengths[seq], 0,
                           (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
                    printf("Retransmitted unacknowledged DATA %u\n", seq);
                    retransmissions++;
                }
                continue;
            }

            uint32_t ack_seq;
            uint8_t ack_type;
            uint16_t ack_length;
            const uint8_t *ack_payload;

            if (decode_packet(ack, (size_t)received, &ack_seq, &ack_type,
                              &ack_payload, &ack_length) != 0 ||
                ack_type != TYPE_ACK || ack_length != 0) {
                continue;
            }

            if (ack_seq >= base && ack_seq < next_seq) {
                if (!acknowledged[ack_seq]) {
                    acknowledged[ack_seq] = 1;
                    printf("Received ACK %u\n", ack_seq);
                }
                while (base < total_packets && acknowledged[base]) {
                    base++;
                }
            }
        }
        total_bytes = message_length;
    } else {
        /* Full file transfer */
        FILE *file = fopen(input_file, "rb");
        if (!file) {
            perror("fopen");
            close(sockfd);
            return EXIT_FAILURE;
        }

        fseek(file, 0, SEEK_END);
        long file_sz = ftell(file);
        fseek(file, 0, SEEK_SET);

        uint32_t total_packets = (file_sz > 0) ? (uint32_t)((file_sz + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE) : 1;
        SrPacket *pkts = (SrPacket *)calloc(total_packets, sizeof(SrPacket));
        if (!pkts) {
            fprintf(stderr, "Out of memory allocating SR packet buffer.\n");
            fclose(file);
            close(sockfd);
            return EXIT_FAILURE;
        }

        for (uint32_t i = 0; i < total_packets; i++) {
            size_t bytes_read = fread(pkts[i].data, 1, MAX_PAYLOAD_SIZE, file);
            pkts[i].length = (uint16_t)bytes_read;
            total_bytes += bytes_read;
            encode_packet(pkts[i].packet, sizeof(pkts[i].packet), i, TYPE_DATA,
                          pkts[i].data, pkts[i].length, &pkts[i].packet_len);
        }
        fclose(file);

        printf("Selective Repeat ARQ: transferring %" PRIu64 " bytes in %u packets, window size %d\n",
               total_bytes, total_packets, window_size);

        uint32_t base = 0;
        uint32_t next_seq = 0;

        while (base < total_packets) {
            while (next_seq < total_packets && next_seq < base + (uint32_t)window_size) {
                ssize_t sent = sendto(sockfd, pkts[next_seq].packet, pkts[next_seq].packet_len, 0,
                                      (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
                if (sent < 0) {
                    perror("sendto");
                    free(pkts);
                    close(sockfd);
                    return EXIT_FAILURE;
                }
                printf("Sent DATA sequence %u\n", next_seq);
                next_seq++;
            }

            uint8_t ack[MAX_PACKET_SIZE];
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);

            ssize_t received = recvfrom(sockfd, ack, sizeof(ack), 0,
                                        (struct sockaddr *)&from_addr, &from_len);

            if (received < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("recvfrom");
                    free(pkts);
                    close(sockfd);
                    return EXIT_FAILURE;
                }

                printf("Timeout: retransmitting unacknowledged packets from sequence %u\n", base);
                timeouts++;

                for (uint32_t seq = base; seq < next_seq; seq++) {
                    if (pkts[seq].acknowledged) continue;

                    pkts[seq].retries++;
                    if (pkts[seq].retries > MAX_RETRIES) {
                        fprintf(stderr, "Transfer failed: too many retries for sequence %u.\n", seq);
                        free(pkts);
                        close(sockfd);
                        return EXIT_FAILURE;
                    }

                    sendto(sockfd, pkts[seq].packet, pkts[seq].packet_len, 0,
                           (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
                    printf("Retransmitted unacknowledged DATA %u (attempt %d)\n", seq, pkts[seq].retries);
                    retransmissions++;
                }
                continue;
            }

            uint32_t ack_seq;
            uint8_t ack_type;
            uint16_t ack_length;
            const uint8_t *ack_payload;

            if (decode_packet(ack, (size_t)received, &ack_seq, &ack_type,
                              &ack_payload, &ack_length) != 0 ||
                ack_type != TYPE_ACK || ack_length != 0) {
                continue;
            }

            if (ack_seq >= base && ack_seq < next_seq) {
                if (!pkts[ack_seq].acknowledged) {
                    pkts[ack_seq].acknowledged = 1;
                    printf("Received individual ACK %u\n", ack_seq);
                }
                while (base < total_packets && pkts[base].acknowledged) {
                    base++;
                }
            }
        }

        /* Send FIN and wait for FIN_ACK */
        uint8_t fin_pkt[MAX_PACKET_SIZE];
        size_t fin_len;
        encode_packet(fin_pkt, sizeof(fin_pkt), total_packets, TYPE_FIN, NULL, 0, &fin_len);

        for (int fin_attempt = 1; fin_attempt <= MAX_RETRIES; fin_attempt++) {
            sendto(sockfd, fin_pkt, fin_len, 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
            printf("Sent FIN sequence %u (attempt %d)\n", total_packets, fin_attempt);

            uint8_t ack[MAX_PACKET_SIZE];
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);

            ssize_t received = recvfrom(sockfd, ack, sizeof(ack), 0,
                                        (struct sockaddr *)&from_addr, &from_len);
            if (received > 0) {
                uint32_t ack_seq;
                uint8_t ack_type;
                uint16_t ack_length;
                const uint8_t *ack_payload;
                if (decode_packet(ack, (size_t)received, &ack_seq, &ack_type,
                                  &ack_payload, &ack_length) == 0 &&
                    ack_seq == total_packets && ack_type == TYPE_FIN_ACK) {
                    printf("Received FIN_ACK. Transfer completed.\n");
                    break;
                }
            }
        }

        free(pkts);
    }

    double end_time = now_seconds();
    double duration = end_time - start_time;
    if (duration <= 0.0) duration = 0.000001;
    double goodput = (double)total_bytes / duration;

    printf("\n--- Selective Repeat Transfer Summary ---\n");
    printf("Total bytes transferred: %" PRIu64 "\n", total_bytes);
    printf("Retransmissions: %" PRIu64 "\n", retransmissions);
    printf("Timeouts: %" PRIu64 "\n", timeouts);
    printf("Duration: %.4f s\n", duration);
    printf("Goodput: %.2f bytes/s (%.2f KiB/s)\n", goodput, goodput / 1024.0);

    close(sockfd);
    return EXIT_SUCCESS;
}
