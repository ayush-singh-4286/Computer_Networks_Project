
#include "packet.h"

#include <arpa/inet.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define RECEIVER_PORT 9000

static uint32_t file_hash(FILE *file)
{
    uint32_t hash = 2166136261u;
    uint8_t buffer[4096];

    rewind(file);

    size_t n;

    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        for (size_t i = 0; i < n; i++) {
            hash ^= buffer[i];
            hash *= 16777619u;
        }
    }

    return hash;
}

static int send_response(
    int sockfd,
    struct sockaddr_in *sender,
    socklen_t sender_len,
    uint32_t sequence,
    uint8_t type)
{
    uint8_t packet[MAX_PACKET_SIZE];
    size_t packet_length;

    if (encode_packet(
            packet, sizeof(packet),
            sequence, type,
            NULL, 0,
            &packet_length) != 0)
        return -1;

    ssize_t sent = sendto(
        sockfd,
        packet,
        packet_length,
        0,
        (struct sockaddr *)sender,
        sender_len);

    return sent == (ssize_t)packet_length ? 0 : -1;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        printf("Usage: %s <output_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE *output = fopen(argv[1], "wb");

    if (!output) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0) {
        perror("socket");
        fclose(output);
        return EXIT_FAILURE;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(RECEIVER_PORT);

    if (bind(sockfd,
             (struct sockaddr *)&server,
             sizeof(server)) < 0) {
        perror("bind");
        fclose(output);
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Receiver listening on UDP port %d...\n",
           RECEIVER_PORT);

    uint32_t expected_sequence = 0;
    int finished = 0;
    int success = 1;

    while (!finished) {

        uint8_t buffer[MAX_PACKET_SIZE];

        struct sockaddr_in sender;
        socklen_t sender_len = sizeof(sender);

        ssize_t received = recvfrom(
            sockfd,
            buffer,
            sizeof(buffer),
            0,
            (struct sockaddr *)&sender,
            &sender_len);

        if (received < 0) {
            perror("recvfrom");
            success = 0;
            break;
        }

        uint32_t sequence;
        uint8_t type;
        uint16_t payload_length;
        const uint8_t *payload;

        if (decode_packet(
                buffer,
                (size_t)received,
                &sequence,
                &type,
                &payload,
                &payload_length) != 0) {

            printf("Invalid or corrupted packet discarded.\n");
            continue;
        }

        printf("Received packet: seq=%" PRIu32
               ", type=%u\n",
               sequence, (unsigned)type);

        if (type == TYPE_DATA) {

            if (sequence == expected_sequence) {

                if (fwrite(payload, 1,
                           payload_length, output) != payload_length) {
                    perror("fwrite");
                    success = 0;
                    break;
                }

                expected_sequence++;

                printf("Data written. Expected next seq=%" PRIu32 "\n",
                       expected_sequence);

            } else if (sequence < expected_sequence) {

                printf("Duplicate packet. Sending ACK again.\n");

            } else {

                printf("Out-of-order packet. Expected=%" PRIu32 "\n",
                       expected_sequence);
                continue;
            }

            if (send_response(
                    sockfd, &sender, sender_len,
                    sequence, TYPE_ACK) != 0) {
                perror("ACK sendto");
                success = 0;
                break;
            }
        }

        else if (type == TYPE_FIN) {

            if (sequence == expected_sequence) {

                if (fflush(output) != 0) {
                    perror("fflush");
                    success = 0;
                    break;
                }

                if (send_response(
                        sockfd, &sender, sender_len,
                        sequence, TYPE_FIN_ACK) != 0) {
                    perror("FIN ACK sendto");
                    success = 0;
                    break;
                }

                finished = 1;

            } else if (sequence < expected_sequence) {

                /* Repeat FIN ACK if the previous one was lost. */
                if (send_response(
                        sockfd, &sender, sender_len,
                        sequence, TYPE_FIN_ACK) != 0) {
                    success = 0;
                    break;
                }

            } else {

                printf("FIN received before all data packets.\n");
            }
        }
    }

    if (fclose(output) != 0) {
        perror("fclose");
        success = 0;
    }

    if (success) {
        FILE *check = fopen(argv[1], "rb");

        if (!check) {
            perror("fopen");
            success = 0;
        } else {
            uint32_t hash = file_hash(check);

            if (ferror(check)) {
                fprintf(stderr, "Error reading reconstructed file.\n");
                success = 0;
            } else {
                printf("\nFile reconstructed successfully.\n");
                printf("Final file hash: %08" PRIx32 "\n", hash);
            }

            fclose(check);
        }
    }

    close(sockfd);

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
