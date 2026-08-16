#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>

#define MAX_EVENTS 64
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 1000

// Key-value store
#define MAX_KEYS 100
char keys[MAX_KEYS][64];
char values[MAX_KEYS][1024];
int key_count = 0;

int find_key(char *key) {
    for (int i = 0; i < key_count; i++) {
        if (strcmp(keys[i], key) == 0) return i;
    }
    return -1;
}

// Set socket to non-blocking
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Execute command (same as before)
void execute_command(char *buffer, char *response) {
    char cmd[64], key[64], value[1024];
    char *p = buffer;
    
    while (*p == ' ') p++;
    
    if (sscanf(p, "SET %63s %1023s", key, value) == 2) {
        int idx = find_key(key);
        if (idx == -1) {
            strcpy(keys[key_count], key);
            strcpy(values[key_count], value);
            key_count++;
        } else {
            strcpy(values[idx], value);
        }
        strcpy(response, "+OK\r\n");
        printf("✅ SET %s = %s\n", key, value);
    }
    else if (sscanf(p, "GET %63s", key) == 1) {
        int idx = find_key(key);
        if (idx == -1) {
            strcpy(response, "$-1\r\n");
            printf("❌ GET %s -> not found\n", key);
        } else {
            sprintf(response, "$%zu\r\n%s\r\n", strlen(values[idx]), values[idx]);
            printf("✅ GET %s -> %s\n", key, values[idx]);
        }
    }
    else if (strncmp(p, "PING", 4) == 0) {
        strcpy(response, "+PONG\r\n");
        printf("🏓 PING\n");
    }
    else if (strncmp(p, "DEL", 3) == 0) {
        if (sscanf(p, "DEL %63s", key) == 1) {
            int idx = find_key(key);
            if (idx == -1) {
                strcpy(response, ":0\r\n");
            } else {
                for (int i = idx; i < key_count - 1; i++) {
                    strcpy(keys[i], keys[i+1]);
                    strcpy(values[i], values[i+1]);
                }
                key_count--;
                strcpy(response, ":1\r\n");
                printf("🗑️ DEL %s\n", key);
            }
        }
    }
    else if (strncmp(p, "KEYS", 4) == 0) {
        char pattern[64];
        if (sscanf(p, "KEYS %63s", pattern) == 1) {
            char result[2048] = "";
            int found = 0;
            for (int i = 0; i < key_count; i++) {
                if (strcmp(pattern, "*") == 0) {
                    char entry[128];
                    sprintf(entry, "$%zu\r\n%s\r\n", strlen(keys[i]), keys[i]);
                    strcat(result, entry);
                    found++;
                }
            }
            if (found == 0) {
                strcpy(response, "*0\r\n");
            } else {
                sprintf(response, "*%d\r\n%s", found, result);
            }
            printf("🔑 KEYS %s -> %d keys\n", pattern, found);
        }
    }
    else if (strncmp(p, "FLUSHALL", 8) == 0) {
        key_count = 0;
        strcpy(response, "+OK\r\n");
        printf("🗑️ FLUSHALL\n");
    }
    else {
        strcpy(response, "-ERR unknown command\r\n");
        printf("❌ Unknown: %s", p);
    }
}

int main() {
    // 1. Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    // 2. Set socket options
    int val = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    
    // 3. Set non-blocking
    if (set_nonblocking(server_fd) < 0) {
        perror("set_nonblocking");
        exit(EXIT_FAILURE);
    }
    
    // 4. Bind
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6379);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    
    // 5. Listen
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    // 6. Create epoll instance
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }
    
    // 7. Add server socket to epoll
    struct epoll_event ev = {
        .events = EPOLLIN,
        .data.fd = server_fd
    };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        perror("epoll_ctl: server_fd");
        exit(EXIT_FAILURE);
    }
    
    printf("🚀 Redis server with epoll on port 6379\n");
    printf("📝 Commands: PING, SET, GET, DEL, KEYS, FLUSHALL\n");
    printf("⚡ High-performance event loop (epoll)\n");
    printf("📡 Waiting for connections...\n\n");
    
    // 8. Event loop
    struct epoll_event events[MAX_EVENTS];
    char buffer[BUFFER_SIZE];
    char response[2048];
    
    int client_count = 0;
    
    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            perror("epoll_wait");
            break;
        }
        
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            
            // New connection
            if (fd == server_fd) {
                int client_fd = accept(server_fd, NULL, NULL);
                if (client_fd < 0) {
                    perror("accept");
                    continue;
                }
                
                // Set client non-blocking
                if (set_nonblocking(client_fd) < 0) {
                    perror("set_nonblocking client");
                    close(client_fd);
                    continue;
                }
                
                // Add client to epoll (edge-triggered)
                struct epoll_event client_ev = {
                    .events = EPOLLIN | EPOLLET,
                    .data.fd = client_fd
                };
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0) {
                    perror("epoll_ctl: client");
                    close(client_fd);
                    continue;
                }
                
                client_count++;
                printf("🔗 New client connected (fd=%d, total=%d)\n", client_fd, client_count);
            }
            // Client data
            else {
                // Read all data (edge-triggered loop)
                while (1) {
                    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
                    if (n <= 0) {
                        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                            perror("read");
                        }
                        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                            // Client disconnected
                            close(fd);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                            client_count--;
                            printf("🔌 Client disconnected (fd=%d, remaining=%d)\n", 
                                   fd, client_count);
                        }
                        break;
                    }
                    
                    buffer[n] = '\0';
                    printf("📨 Client %d: %s", fd, buffer);
                    
                    execute_command(buffer, response);
                    write(fd, response, strlen(response));
                }
            }
        }
    }
    
    close(server_fd);
    close(epoll_fd);
    return 0;
}