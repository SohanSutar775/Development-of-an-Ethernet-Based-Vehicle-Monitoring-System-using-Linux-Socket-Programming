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

#define BUF_SIZE 512

static volatile sig_atomic_t running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}

static void parse_and_display(const char *line, const char *proto, const char *src_ip, uint16_t src_port) {
    unsigned int seq = 0;
    double speed = 0.0;
    int rpm = 0;
    double steer = 0.0;

    int parsed = sscanf(line, "SEQ=%u;SPEED=%lf;RPM=%d;STEER=%lf",
                        &seq, &speed, &rpm, &steer);
    if (parsed == 4) {
        printf("[%s] From %s:%u | Seq: %u | Speed: %.2f km/h | RPM: %d | Steering Angle: %.2f deg\n",
               proto, src_ip, src_port, seq, speed, rpm, steer);
    } else {
        printf("[%s] From %s:%u | Raw message: %s\n", proto, src_ip, src_port, line);
    }
    fflush(stdout);
}

static int run_udp(const char *bind_ip, uint16_t port) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket (UDP) failed");
        return EXIT_FAILURE;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid bind IPv4 address: %s\n", bind_ip);
        close(sockfd);
        return EXIT_FAILURE;
    }

    if (bind(sockfd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind (UDP) failed");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("[Dashboard ECU][UDP] Listening on %s:%u. Press Ctrl+C to stop.\n", bind_ip, port);

    char buf[BUF_SIZE];
    while (running) {
        struct sockaddr_in peer_addr;
        socklen_t peer_len = sizeof(peer_addr);
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&peer_addr, &peer_len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recvfrom failed");
            break;
        }
        buf[n] = '\0';
        char *nl = strpbrk(buf, "\r\n");
        if (nl) {
            *nl = '\0';
        }

        char src_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer_addr.sin_addr, src_ip, sizeof(src_ip));
        uint16_t src_port = ntohs(peer_addr.sin_port);

        parse_and_display(buf, "UDP", src_ip, src_port);
    }

    close(sockfd);
    printf("\n[Dashboard ECU][UDP] Shutdown complete.\n");
    return EXIT_SUCCESS;
}

static void handle_tcp_client(int client_fd, const char *client_ip, uint16_t client_port) {
    char line_buf[BUF_SIZE];
    size_t line_len = 0;
    char recv_buf[BUF_SIZE];

    while (running) {
        ssize_t n = recv(client_fd, recv_buf, sizeof(recv_buf), 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv failed");
            break;
        }
        if (n == 0) {
            printf("[Dashboard ECU][TCP] Vehicle ECU %s:%u closed connection.\n",
                   client_ip, client_port);
            break;
        }

        for (ssize_t i = 0; i < n; i++) {
            char c = recv_buf[i];
            if (c == '\n') {
                line_buf[line_len] = '\0';
                if (line_len > 0 && line_buf[line_len - 1] == '\r') {
                    line_buf[line_len - 1] = '\0';
                }
                parse_and_display(line_buf, "TCP", client_ip, client_port);
                line_len = 0;
            } else if (line_len + 1 < sizeof(line_buf)) {
                line_buf[line_len++] = c;
            } else {
                line_buf[sizeof(line_buf) - 1] = '\0';
                parse_and_display(line_buf, "TCP", client_ip, client_port);
                line_len = 0;
            }
        }
    }
}

static int run_tcp(const char *bind_ip, uint16_t port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket (TCP) failed");
        return EXIT_FAILURE;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid bind IPv4 address: %s\n", bind_ip);
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (bind(listen_fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind (TCP) failed");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, 4) < 0) {
        perror("listen failed");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    printf("[Dashboard ECU][TCP] Listening on %s:%u. Press Ctrl+C to stop.\n", bind_ip, port);

    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept failed");
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        uint16_t client_port = ntohs(client_addr.sin_port);
        printf("[Dashboard ECU][TCP] Vehicle ECU connected from %s:%u\n", client_ip, client_port);

        handle_tcp_client(client_fd, client_ip, client_port);
        close(client_fd);
    }

    close(listen_fd);
    printf("\n[Dashboard ECU][TCP] Shutdown complete.\n");
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <udp|tcp> <bind_ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *proto = argv[1];
    const char *bind_ip = argv[2];
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

    if (strcmp(proto, "udp") == 0) {
        return run_udp(bind_ip, port);
    } else if (strcmp(proto, "tcp") == 0) {
        return run_tcp(bind_ip, port);
    } else {
        fprintf(stderr, "Unsupported protocol '%s'. Use 'udp' or 'tcp'.\n", proto);
        return EXIT_FAILURE;
    }
}