
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "packet.h"

#define RECEIVER_PORT 9000
#define FIN_WAIT_SECONDS 5

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
    int sockfd,
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
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <output_file>\n", argv[0]);
        return 1;
    }

    FILE *output = fopen(argv[1], "wb");

    if (output == NULL) {
        perror("fopen");
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0) {
        perror("socket");
        fclose(output);
        return 1;
    }

    struct sockaddr_in receiver_addr;
    memset(&receiver_addr, 0, sizeof(receiver_addr));

    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    receiver_addr.sin_port = htons(RECEIVER_PORT);

    if (bind(
            sockfd,
            (struct sockaddr *)&receiver_addr,
            sizeof(receiver_addr)) < 0) {
        perror("bind");
        close(sockfd);
        fclose(output);
        return 1;
    }

    printf("Receiver listening on UDP port %d\n", RECEIVER_PORT);
    printf("Output file: %s\n", argv[1]);

    uint32_t expected_sequence = 0;
    int finished = 0;
    int success = 1;
    int fin_received = 0;

    struct sockaddr_in last_sender;
    memset(&last_sender, 0, sizeof(last_sender));

    uint8_t buffer[MAX_PACKET_SIZE];

    while (!finished) {
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
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (fin_received) {
                    printf("FIN wait timeout. Closing receiver.\n");
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

        if (type == TYPE_DATA) {
            /*
             * A new DATA packet is accepted only if its sequence
             * matches the next expected sequence number.
             */
            if (sequence == expected_sequence) {
                size_t written = fwrite(
                    payload, 1, payload_length, output);

                if (written != payload_length) {
                    perror("fwrite");
                    success = 0;
                    break;
                }

                
expected_sequence++;

if (fflush(output) != 0) {
    perror("fflush");
    success = 0;
    break;
}

printf(
    "Accepted DATA %" PRIu32
    "; next expected=%" PRIu32 "\n",
    sequence, expected_sequence);

            } else if (sequence < expected_sequence) {
                printf(
                    "Duplicate DATA %" PRIu32
                    "; sending ACK again.\n",
                    sequence);
            } else {
                printf(
                    "Out-of-order DATA %" PRIu32
                    "; expected=%" PRIu32 "\n",
                    sequence, expected_sequence);
            }

            /*
             * Cumulative ACK: acknowledge the last consecutive
             * DATA packet received.
             *
             * UINT32_MAX represents ACK -1, meaning no DATA
             * packets have been accepted yet.
             */
            uint32_t ack_sequence =
                (expected_sequence == 0)
                    ? UINT32_MAX
                    : expected_sequence - 1;

            if (send_response(
                    sockfd,
                    &sender,
                    sender_len,
                    ack_sequence,
                    TYPE_ACK) != 0) {
                success = 0;
                break;
            }

            printf("Sent cumulative ACK %" PRIu32 "\n",
                   ack_sequence);
        } else if (type == TYPE_FIN) {

            if (sequence == expected_sequence) {
                if (fflush(output) != 0) {
                    perror("fflush");
                    success = 0;
                    break;
                }

                if (send_response(
                        sockfd,
                        &sender,
                        sender_len,
                        sequence,
                        TYPE_FIN_ACK) != 0) {
                    success = 0;
                    break;
                }

                printf("FIN received. Sent FIN_ACK.\n");

                last_sender = sender;
                fin_received = 1;

                struct timeval timeout;
                timeout.tv_sec = FIN_WAIT_SECONDS;
                timeout.tv_usec = 0;

                if (setsockopt(
                        sockfd,
                        SOL_SOCKET,
                        SO_RCVTIMEO,
                        &timeout,
                        sizeof(timeout)) < 0) {
                    perror("setsockopt");
                    success = 0;
                    break;
                }

                /*
                 * Wait for a possible retransmitted FIN in case
                 * the sender did not receive FIN_ACK.
                 */
            } else if (sequence < expected_sequence) {
                printf("Duplicate FIN received. Sending FIN_ACK again.\n");

                if (send_response(
                        sockfd,
                        &sender,
                        sender_len,
                        sequence,
                        TYPE_FIN_ACK) != 0) {
                    success = 0;
                    break;
                }
            } else {
                printf(
                    "FIN arrived too early: FIN sequence=%" PRIu32
                    ", expected=%" PRIu32 "\n",
                    sequence, expected_sequence);
            }
        } else {
            printf("Unexpected packet type %u ignored.\n", type);
        }

        /*
         * After FIN, the receive timeout lets us close gracefully
         * if no retransmitted FIN arrives. Repeated FIN packets
         * receive another FIN_ACK.
         */
        if (fin_received &&
            sender.sin_addr.s_addr == last_sender.sin_addr.s_addr &&
            sender.sin_port == last_sender.sin_port &&
            type == TYPE_FIN) {
            continue;
        }
    }

    if (fflush(output) != 0) {
        perror("fflush");
        success = 0;
    }

    uint32_t hash = file_hash(output);

    printf("Received DATA packets: %" PRIu32 "\n",
           expected_sequence);
    printf("Output file hash (FNV-1a): %08" PRIx32 "\n", hash);

    if (fclose(output) != 0) {
        perror("fclose");
        success = 0;
    }

    close(sockfd);

    if (success) {
        printf("Receiver closed successfully.\n");
        return 0;
    }

    fprintf(stderr, "Receiver exited with an error.\n");
    return 1;
}
