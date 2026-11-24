#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF_SIZE 256
#define TX_INTERVAL_SEC 1

static volatile sig_atomic_t running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}

static void generate_telemetry(unsigned int tick, double *speed_kmh, int *rpm, double *steering_deg) {
    /* Deterministic realistic waveform for repeatable testing */
    int phase = (int)(tick % 20);
    if (phase < 10) {
        *speed_kmh = 20.0 + phase * 6.5;
        *rpm = 1200 + phase * 320;
    } else {
        *speed_kmh = 85.0 - (phase - 10) * 5.0;
        *rpm = 4400 - (phase - 10) * 240;
    }
    *steering_deg = (phase % 2 == 0) ? (double)(phase - 10) * 2.5 : -(double)(phase - 10) * 1.5;
}

static int run_udp(const char *ip, uint16_t port) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket (UDP) failed");
        return EXIT_FAILURE;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &dest_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", ip);
        close(sockfd);
        return EXIT_FAILURE;
    }

    unsigned int seq = 0;
    char payload[BUF_SIZE];

    printf("[Vehicle ECU][UDP] Sending to %s:%u every %d s. Press Ctrl+C to stop.\n",
           ip, port, TX_INTERVAL_SEC);

    while (running) {
        double speed_kmh = 0.0;
        int rpm = 0;
        double steering_deg = 0.0;
        generate_telemetry(seq, &speed_kmh, &rpm, &steering_deg);

        int len = snprintf(payload, sizeof(payload),
                           "SEQ=%u;SPEED=%.2f;RPM=%d;STEER=%.2f\n",
                           seq, speed_kmh, rpm, steering_deg);
        if (len < 0 || len >= (int)sizeof(payload)) {
            fprintf(stderr, "Payload formatting error\n");
            break;
        }

        ssize_t sent = sendto(sockfd, payload, (size_t)len, 0,
                              (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (sent < 0) {
            perror("sendto failed");
        } else {
            printf("[TX][UDP] %s", payload);
            fflush(stdout);
        }

        seq++;
        sleep(TX_INTERVAL_SEC);
    }

    close(sockfd);
    printf("\n[Vehicle ECU][UDP] Shutdown complete.\n");
    return EXIT_SUCCESS;
}

static int connect_tcp_with_retry(const char *ip, uint16_t port) {
    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &srv_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", ip);
        return -1;
    }

    while (running) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            perror("socket (TCP) failed");
            return -1;
        }

        if (connect(sockfd, (const struct sockaddr *)&srv_addr, sizeof(srv_addr)) == 0) {
            printf("[Vehicle ECU][TCP] Connected to Dashboard ECU at %s:%u\n", ip, port);
            return sockfd;
        }

        fprintf(stderr, "[Vehicle ECU][TCP] connect() to %s:%u failed (%s); retrying in %d s...\n",
                ip, port, strerror(errno), TX_INTERVAL_SEC);
        close(sockfd);
        sleep(TX_INTERVAL_SEC);
    }

    return -1;
}

static int run_tcp(const char *ip, uint16_t port) {
    unsigned int seq = 0;
    char payload[BUF_SIZE];

    while (running) {
        int sockfd = connect_tcp_with_retry(ip, port);
        if (sockfd < 0) {
            break;
        }

        while (running) {
            double speed_kmh = 0.0;
            int rpm = 0;
            double steering_deg = 0.0;
            generate_telemetry(seq, &speed_kmh, &rpm, &steering_deg);

            int len = snprintf(payload, sizeof(payload),
                               "SEQ=%u;SPEED=%.2f;RPM=%d;STEER=%.2f\n",
                               seq, speed_kmh, rpm, steering_deg);
            if (len < 0 || len >= (int)sizeof(payload)) {
                fprintf(stderr, "Payload formatting error\n");
                running = 0;
                break;
            }

            size_t total_sent = 0;
            bool peer_closed = false;
            while (total_sent < (size_t)len && running) {
                ssize_t n = send(sockfd, payload + total_sent, (size_t)len - total_sent, 0);
                if (n < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno == EPIPE || errno == ECONNRESET) {
                        fprintf(stderr, "[Vehicle ECU][TCP] Connection lost (%s); reconnecting...\n",
                                strerror(errno));
                        peer_closed = true;
                        break;
                    }
                    perror("send failed");
                    peer_closed = true;
                    break;
                }
                total_sent += (size_t)n;
            }

            if (peer_closed) {
                close(sockfd);
                break;
            }

            if (total_sent == (size_t)len) {
                printf("[TX][TCP] %s", payload);
                fflush(stdout);
                seq++;
            }

            sleep(TX_INTERVAL_SEC);
        }

        if (running) {
            close(sockfd);
        }
    }

    printf("\n[Vehicle ECU][TCP] Shutdown complete.\n");
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <udp|tcp> <dest_ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *proto = argv[1];
    const char *ip = argv[2];
    long port_val = strtol(argv[3], NULL, 10);
    if (port_val <= 0 || port_val > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[3]);
        return EXIT_FAILURE;
    }
    uint16_t port = (uint16_t)port_val;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    if (strcmp(proto, "udp") == 0) {
        return run_udp(ip, port);
    } else if (strcmp(proto, "tcp") == 0) {
        return run_tcp(ip, port);
    } else {
        fprintf(stderr, "Unsupported protocol '%s'. Use 'udp' or 'tcp'.\n", proto);
        return EXIT_FAILURE;
    }
}