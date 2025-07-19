/*
gcc -Wall -Wextra c_windows_pthread.c -o my_proxy.exe -lws2_32 -lpthread
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define BUFFER_SIZE 4096

typedef struct {
    SOCKET from_fd;
    SOCKET to_fd;
} pipe_args_t;

ssize_t read_n(SOCKET fd, void *buf, size_t n) {
    size_t total = 0;
    char *p = (char*)buf;
    while (total < n) {
        int r = recv(fd, p + total, (int)(n - total), 0);
        if (r <= 0) return r;
        total += r;
    }
    return (ssize_t)total;
}

ssize_t write_n(SOCKET fd, const void *buf, size_t n) {
    size_t total = 0;
    const char *p = (const char*)buf;
    while (total < n) {
        int w = send(fd, p + total, (int)(n - total), 0);
        if (w <= 0) return w;
        total += w;
    }
    return (ssize_t)total;
}

void *pipe_data(void *arg) {
    pipe_args_t *p = (pipe_args_t *)arg;
    char buffer[BUFFER_SIZE];
    ssize_t n;
    while ((n = recv(p->from_fd, buffer, sizeof(buffer), 0)) > 0) {
        ssize_t sent = 0;
        while (sent < n) {
            ssize_t w = send(p->to_fd, buffer + sent, (int)(n - sent), 0);
            if (w <= 0) {
                if (w < 0) {
                    int err = WSAGetLastError();
                    if (err == WSAECONNRESET || err == WSAESHUTDOWN) {
                        goto cleanup;
                    }
                    fprintf(stderr, "send error: %d\n", err);
                }
                goto cleanup;
            }
            sent += w;
        }
    }
    if (n < 0) {
        fprintf(stderr, "recv error: %d\n", WSAGetLastError());
    }
cleanup:
    shutdown(p->to_fd, SD_SEND);
    shutdown(p->from_fd, SD_RECEIVE);
    free(p);
    return NULL;
}

int fragment_data(SOCKET local_fd, SOCKET remote_fd) {
    uint8_t head[5];
    ssize_t n = read_n(local_fd, head, 5);
    if (n != 5) return -1;
    uint8_t data[2048];
    n = recv(local_fd, data, sizeof(data), 0);
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

SOCKET connect_remote(const char *host, const char *port) {
    struct addrinfo hints = {0}, *res, *rp;
    SOCKET sock = INVALID_SOCKET;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        fprintf(stderr, "connect_remote: host='%s', port='%s'\n", host, port);
        return INVALID_SOCKET;
    }
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == INVALID_SOCKET) continue;
        if (connect(sock, rp->ai_addr, (int)rp->ai_addrlen) == 0) break;
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return sock;
}

void *handle_client(void *arg) {
    SOCKET client_fd = *(SOCKET *)arg;
    free(arg);
    char buffer[1500];
    int n = recv(client_fd, buffer, sizeof(buffer), 0);
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
    SOCKET remote_fd = connect_remote(host, port);
    if (remote_fd == INVALID_SOCKET) goto cleanup;
    const char *resp = "HTTP/1.1 200 OK\r\n\r\n";
    if (write_n(client_fd, resp, strlen(resp)) < 0) {
        closesocket(remote_fd);
        goto cleanup;
    }
    if (strcmp(port, "443") == 0) {
        if (fragment_data(client_fd, remote_fd) < 0) {
            closesocket(remote_fd);
            goto cleanup;
        }
    }
    pthread_t t1, t2;
    pipe_args_t *args1 = malloc(sizeof(pipe_args_t));
    pipe_args_t *args2 = malloc(sizeof(pipe_args_t));
    if (!args1 || !args2) {
        closesocket(remote_fd);
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
    closesocket(client_fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    if(argc != 3) {
        fprintf(stderr, "Usage: %s ip port\n", argv[0]);
        return -1;
    }
    char* LISTEN_IP = argv[1];
    uint16_t LISTEN_PORT;
    if(sscanf(argv[2], "%hu", &LISTEN_PORT) != 1) {
        fprintf(stderr, "Invalid port: %s\n", argv[2]);
        return -1;
    }
    WSADATA wsaData;
    int wsaerr = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (wsaerr != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", wsaerr);
        return 1;
    }
    srand((unsigned int)time(NULL));
    SOCKET listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == INVALID_SOCKET) {
        fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    BOOL opt = TRUE;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LISTEN_PORT);
    if (inet_pton(AF_INET, LISTEN_IP, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid listen IP address: %s\n", LISTEN_IP);
        closesocket(listen_fd);
        WSACleanup();
        return 1;
    }
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
        closesocket(listen_fd);
        WSACleanup();
        return 1;
    }
    if (listen(listen_fd, SOMAXCONN) == SOCKET_ERROR) {
        fprintf(stderr, "listen failed: %d\n", WSAGetLastError());
        closesocket(listen_fd);
        WSACleanup();
        return 1;
    }
    printf("Proxy listening on %s:%d\n", LISTEN_IP, LISTEN_PORT);
    while (1) {
        struct sockaddr_storage client_addr;
        int client_len = sizeof(client_addr);
        SOCKET *client_fd = malloc(sizeof(SOCKET));
        if (!client_fd) {
            fprintf(stderr, "malloc failed\n");
            continue;
        }
        *client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (*client_fd == INVALID_SOCKET) {
            fprintf(stderr, "accept failed: %d\n", WSAGetLastError());
            free(client_fd);
            continue;
        }
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, client_fd) != 0) {
            fprintf(stderr, "pthread_create failed\n");
            closesocket(*client_fd);
            free(client_fd);
            continue;
        }
        pthread_detach(tid);
    }
    closesocket(listen_fd);
    WSACleanup();
    return 0;
}
