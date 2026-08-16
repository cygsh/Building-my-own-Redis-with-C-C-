#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>

#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

// Key-value store (simple array for now)
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

// Execute command (inline parser)
void execute_command(char *buffer, char *response) {
    char cmd[64], key[64], value[1024];
    char *p = buffer;
    
    // Skip leading whitespace
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
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    int val = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6379);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    printf("🚀 Redis server with select() on port 6379\n");
    printf("📝 Commands: PING, SET, GET, DEL, KEYS, FLUSHALL\n");
    printf("📡 Multiple clients supported concurrently!\n\n");
    
    // Client tracking
    int clients[MAX_CLIENTS] = {0};
    int client_count = 0;
    
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        int max_fd = server_fd;
        
        // Add clients to fd_set
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] > 0) {
                FD_SET(clients[i], &readfds);
                if (clients[i] > max_fd) max_fd = clients[i];
            }
        }
        
        // Wait for activity (no timeout)
        int activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            continue;
        }
        
        // New connection
        if (FD_ISSET(server_fd, &readfds)) {
            int client_fd = accept(server_fd, NULL, NULL);
            if (client_fd < 0) {
                perror("accept");
                continue;
            }
            
            // Add to client list
            int i;
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i] == 0) {
                    clients[i] = client_fd;
                    client_count++;
                    printf("🔗 New client (fd=%d, slot=%d, total=%d)\n", 
                           client_fd, i, client_count);
                    break;
                }
            }
            if (i == MAX_CLIENTS) {
                printf("❌ Too many clients! Max is %d\n", MAX_CLIENTS);
                close(client_fd);
            }
        }
        
        // Client activity
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int fd = clients[i];
            if (fd > 0 && FD_ISSET(fd, &readfds)) {
                char buffer[BUFFER_SIZE] = {};
                ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
                
                if (n <= 0) {
                    // Client disconnected
                    close(fd);
                    clients[i] = 0;
                    client_count--;
                    printf("🔌 Client disconnected (slot=%d, remaining=%d)\n", 
                           i, client_count);
                } else {
                    buffer[n] = '\0';
                    printf("📨 Client %d: %s", i, buffer);
                    
                    char response[2048];
                    execute_command(buffer, response);
                    write(fd, response, strlen(response));
                }
            }
        }
    }
    
    close(server_fd);
    return 0;
}