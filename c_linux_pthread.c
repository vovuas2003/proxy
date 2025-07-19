/*
Simple compilation:
gcc -Wall -Wextra c_linux_pthread.c -o my_proxy -lpthread
Recommended compilation (for local use):
gcc -Wall -Wextra -DDEBUG c_linux_pthread.c -o my_proxy -lpthread

List of possible defines:
DEBUG - show error and info messages (perror, printf and fprintf to stderr)
DAEMON - server will start as background process (for more info check daemonize function below)
BUFFER_SIZE=N - set size of pipe buffers to N bytes (default 4096)

Compilation with all defines (just as an example):
gcc -Wall -Wextra -DDEBUG -DDAEMON -DBUFFER_SIZE=1024 c_linux_pthread.c -o my_proxy -lpthread
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>

#include <fcntl.h>
#include <sys/stat.h>

#ifndef BUFFER_SIZE
#define BUFFER_SIZE 4096
#endif

#ifdef DAEMON
void daemonize(void) {
    pid_t pid;
    pid = fork();
    if (pid < 0) {
#ifdef DEBUG
        perror("fork");
#endif
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    if (setsid() < 0) {
#ifdef DEBUG
        perror("setsid");
#endif
        exit(EXIT_FAILURE);
    }
    pid = fork();
    if (pid < 0) {
#ifdef DEBUG
        perror("fork");
#endif
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    if (setpgid(0, 0) < 0) {
#ifdef DEBUG
        perror("setpgid");
#endif
        exit(EXIT_FAILURE);
    }
    if (chdir("/") < 0) {
#ifdef DEBUG
        perror("chdir");
#endif
        exit(EXIT_FAILURE);
    }
    umask(0);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
#ifdef DEBUG
        perror("open");
#endif
        exit(EXIT_FAILURE);
    }
    if (dup2(fd, STDIN_FILENO) < 0) exit(EXIT_FAILURE);
    if (dup2(fd, STDOUT_FILENO) < 0) exit(EXIT_FAILURE);
    if (dup2(fd, STDERR_FILENO) < 0) exit(EXIT_FAILURE);
    if (fd > STDERR_FILENO) {
        close(fd);
    }
}
#endif

typedef struct {
    int from_fd;
    int to_fd;
} pipe_args_t;

ssize_t read_n(int fd, void *buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t r = read(fd, (char*)buf + total, n - total);
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

ssize_t write_n(int fd, const void *buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t w = write(fd, (const char*)buf + total, n - total);
        if (w <= 0) return w;
        total += w;
    }
    return total;
}

void *pipe_data(void *arg) {
    pipe_args_t *p = (pipe_args_t *)arg;
    char buffer[BUFFER_SIZE];
    ssize_t n;
    while ((n = read(p->from_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t sent = 0;
        while (sent < n) {
            ssize_t w = write(p->to_fd, buffer + sent, n - sent);
            if (w <= 0) {
                if (w < 0) {
                    if (errno == EPIPE || errno == ECONNRESET) {
                        goto cleanup;
                    }
#ifdef DEBUG
                    perror("write");
#endif
                }
                goto cleanup;
            }
            sent += w;
        }
    }
#ifdef DEBUG
    if (n < 0) {
        perror("read");
    }
#endif
cleanup:
    shutdown(p->to_fd, SHUT_WR);
    shutdown(p->from_fd, SHUT_RD);
    free(p);
    return NULL;
}

int fragment_data(int local_fd, int remote_fd) {
    uint8_t head[5];
    ssize_t n = read_n(local_fd, head, 5);
    if (n != 5) return -1;
    uint8_t data[2048];
    n = read(local_fd, data, sizeof(data));
    if (n <= 0) return -1;
    size_t data_len = (size_t)n;
    size_t offset = 0;
    uint8_t part_buf[3 + 2 + 2048];
    uint8_t *zero_ptr = memchr(data, 0x00, data_len);
    if (zero_ptr) {
        size_t host_end_index = zero_ptr - data;
        part_buf[0] = 0x16;
        part_buf[1] = 0x03;
        part_buf[2] = 0x04;
        uint16_t len_be = htons((uint16_t)(host_end_index + 1));
        memcpy(part_buf + 3, &len_be, 2);
        memcpy(part_buf + 5, data, host_end_index + 1);
        if (write_n(remote_fd, part_buf, 5 + host_end_index + 1) < 0) return -1;
        offset = host_end_index + 1;
        data_len -= offset;
    }
    while (data_len > 0) {
        size_t max_len = data_len;
        size_t part_len = (rand() % max_len) + 1;
        part_buf[0] = 0x16;
        part_buf[1] = 0x03;
        part_buf[2] = 0x04;
        uint16_t len_be = htons((uint16_t)part_len);
        memcpy(part_buf + 3, &len_be, 2);
        memcpy(part_buf + 5, data + offset, part_len);
        if (write_n(remote_fd, part_buf, 5 + part_len) < 0) return -1;
        offset += part_len;
        data_len -= part_len;
    }
    return 0;
}

int connect_remote(const char *host, const char *port) {
    struct addrinfo hints = {0}, *res, *rp;
    int sock = -1;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
#ifdef DEBUG
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        fprintf(stderr, "connect_remote: host='%s', port='%s'\n", host, port);
#endif
        return -1;
    }
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) continue;
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    return sock;
}

void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);
    char buffer[1500];
    ssize_t n = read(client_fd, buffer, sizeof(buffer));
    if (n <= 0) goto cleanup;
    char *line_end = memchr(buffer, '\n', n);
    if (!line_end) goto cleanup;
    size_t line_len = line_end - buffer;
    char line[1024];
    if (line_len >= sizeof(line)) goto cleanup;
    memcpy(line, buffer, line_len);
    line[line_len] = 0;
    char method[16], target[256];
    if (sscanf(line, "%15s %255s", method, target) != 2) goto cleanup;
    if (strcmp(method, "CONNECT") != 0) goto cleanup;
    char *colon = strchr(target, ':');
    if (!colon) goto cleanup;
    *colon = 0;
    const char *host = target;
    const char *port = colon + 1;
    int remote_fd = connect_remote(host, port);
    if (remote_fd < 0) goto cleanup;
    const char *resp = "HTTP/1.1 200 OK\r\n\r\n";
    if (write_n(client_fd, resp, strlen(resp)) < 0) {
        close(remote_fd);
        goto cleanup;
    }
    if (strcmp(port, "443") == 0) {
        if (fragment_data(client_fd, remote_fd) < 0) {
            close(remote_fd);
            goto cleanup;
        }
    }
    pthread_t t1, t2;
    pipe_args_t *args1 = malloc(sizeof(pipe_args_t));
    pipe_args_t *args2 = malloc(sizeof(pipe_args_t));
    if (!args1 || !args2) {
        close(remote_fd);
        goto cleanup;
    }
    args1->from_fd = client_fd;
    args1->to_fd = remote_fd;
    args2->from_fd = remote_fd;
    args2->to_fd = client_fd;
    pthread_create(&t1, NULL, pipe_data, args1);
    pthread_create(&t2, NULL, pipe_data, args2);
    pthread_detach(t1);
    pthread_detach(t2);
    return NULL;
cleanup:
    close(client_fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    if(argc != 3) {
#ifdef DEBUG
        fprintf(stderr, "Usage: %s ip port\n", argv[0]);
#endif
        return -1;
    }
    char* LISTEN_IP = argv[1];
    uint16_t LISTEN_PORT;
    if(sscanf(argv[2], "%hu", &LISTEN_PORT) != 1) {
#ifdef DEBUG
        fprintf(stderr, "Invalid port: %s\n", argv[2]);
#endif
        return -1;
    }
#ifdef DAEMON
    daemonize();
#endif
    signal(SIGPIPE, SIG_IGN);
    srand(time(NULL));
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
#ifdef DEBUG
        perror("socket");
#endif
        exit(1);
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LISTEN_PORT);
    if (inet_pton(AF_INET, LISTEN_IP, &addr.sin_addr) != 1) {
#ifdef DEBUG
        fprintf(stderr, "Invalid listen IP address: %s\n", LISTEN_IP);
#endif
        exit(1);
    }
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
#ifdef DEBUG
        perror("bind");
#endif
        close(listen_fd);
        exit(1);
    }
    if (listen(listen_fd, 128) < 0) {
#ifdef DEBUG
        perror("listen");
#endif
        close(listen_fd);
        exit(1);
    }
#ifdef DEBUG
    printf("Proxy listening on %s:%d\n", LISTEN_IP, LISTEN_PORT);
#endif
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) {
#ifdef DEBUG
            perror("malloc");
#endif
            continue;
        }
        *client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (*client_fd < 0) {
#ifdef DEBUG
            perror("accept");
#endif
            free(client_fd);
            continue;
        }
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, client_fd) != 0) {
#ifdef DEBUG
            perror("pthread_create");
#endif
            close(*client_fd);
            free(client_fd);
            continue;
        }
        pthread_detach(tid);
    }
    close(listen_fd);
    return 0;
}
