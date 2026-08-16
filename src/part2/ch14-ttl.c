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
#include <time.h>

#define MAX_EVENTS 64
#define BUFFER_SIZE 1024
#define MAX_KEYS 1000
#define MAX_KEY_LEN 64
#define MAX_VAL_LEN 1024

// ============================================
// Key-Value Store with Expiration
// ============================================

typedef struct {
    char key[MAX_KEY_LEN];
    char value[MAX_VAL_LEN];
    time_t expire_time;  // 0 = no expiration
} KeyValue;

KeyValue store[MAX_KEYS];
int key_count = 0;

// ============================================
// Forward declarations
// ============================================

void send_response(int fd, const char *response);
void cleanup_expired_keys();

// ============================================
// Hash Table (Simplified for TTL demo)
// ============================================

int find_key(const char *key) {
    for (int i = 0; i < key_count; i++) {
        if (strcmp(store[i].key, key) == 0) {
            // Check if expired
            if (store[i].expire_time > 0 && time(NULL) > store[i].expire_time) {
                // Key expired - remove it
                for (int j = i; j < key_count - 1; j++) {
                    store[j] = store[j + 1];
                }
                key_count--;
                return -1;
            }
            return i;
        }
    }
    return -1;
}

// ============================================
// Cleanup expired keys (run periodically)
// ============================================

void cleanup_expired_keys() {
    int cleaned = 0;
    int i = 0;
    while (i < key_count) {
        if (store[i].expire_time > 0 && time(NULL) > store[i].expire_time) {
            // Remove expired key
            for (int j = i; j < key_count - 1; j++) {
                store[j] = store[j + 1];
            }
            key_count--;
            cleaned++;
        } else {
            i++;
        }
    }
    if (cleaned > 0) {
        printf("🧹 Cleaned up %d expired keys\n", cleaned);
    }
}

// ============================================
// TTL Functions
// ============================================

long long get_ttl(const char *key) {
    int idx = find_key(key);
    if (idx == -1) {
        return -2;  // Key doesn't exist
    }
    
    if (store[idx].expire_time == 0) {
        return -1;  // No expiration
    }
    
    long long remaining = store[idx].expire_time - time(NULL);
    if (remaining < 0) {
        // Key expired - remove it
        for (int j = idx; j < key_count - 1; j++) {
            store[j] = store[j + 1];
        }
        key_count--;
        return -2;
    }
    
    return remaining;
}

// ============================================
// Redis Commands
// ============================================

void handle_ping(int fd) {
    send_response(fd, "+PONG\r\n");
    printf("🏓 PING\n");
}

void handle_set(char *args, int fd) {
    char key[MAX_KEY_LEN], value[MAX_VAL_LEN];
    if (sscanf(args, "%63s %1023s", key, value) == 2) {
        int idx = find_key(key);
        if (idx == -1) {
            if (key_count < MAX_KEYS) {
                strcpy(store[key_count].key, key);
                strcpy(store[key_count].value, value);
                store[key_count].expire_time = 0;
                key_count++;
            } else {
                send_response(fd, "-ERR max keys reached\r\n");
                return;
            }
        } else {
            strcpy(store[idx].value, value);
        }
        send_response(fd, "+OK\r\n");
        printf("✅ SET %s = %s\n", key, value);
    } else {
        send_response(fd, "-ERR wrong number of arguments\r\n");
    }
}

void handle_get(char *args, int fd) {
    char key[MAX_KEY_LEN];
    if (sscanf(args, "%63s", key) == 1) {
        int idx = find_key(key);
        if (idx == -1) {
            send_response(fd, "$-1\r\n");
            printf("❌ GET %s -> not found\n", key);
        } else {
            char response[1024];
            snprintf(response, sizeof(response), "$%zu\r\n%s\r\n", 
                     strlen(store[idx].value), store[idx].value);
            send_response(fd, response);
            printf("✅ GET %s -> %s\n", key, store[idx].value);
        }
    } else {
        send_response(fd, "-ERR wrong number of arguments\r\n");
    }
}

void handle_expire(char *args, int fd) {
    char key[MAX_KEY_LEN];
    int seconds;
    if (sscanf(args, "%63s %d", key, &seconds) == 2) {
        int idx = find_key(key);
        if (idx == -1) {
            send_response(fd, ":0\r\n");
            printf("❌ EXPIRE %s -> key not found\n", key);
        } else {
            if (seconds <= 0) {
                store[idx].expire_time = 0;
                send_response(fd, ":1\r\n");
                printf("✅ PERSIST %s\n", key);
            } else {
                store[idx].expire_time = time(NULL) + seconds;
                send_response(fd, ":1\r\n");
                printf("✅ EXPIRE %s -> %d seconds\n", key, seconds);
            }
        }
    } else {
        send_response(fd, "-ERR wrong number of arguments\r\n");
    }
}

void handle_ttl(char *args, int fd) {
    char key[MAX_KEY_LEN];
    if (sscanf(args, "%63s", key) == 1) {
        long long ttl = get_ttl(key);
        char response[64];
        snprintf(response, sizeof(response), ":%lld\r\n", ttl);
        send_response(fd, response);
        if (ttl == -2) {
            printf("❌ TTL %s -> key not found\n", key);
        } else if (ttl == -1) {
            printf("❌ TTL %s -> no expiration\n", key);
        } else {
            printf("✅ TTL %s -> %lld seconds\n", key, ttl);
        }
    } else {
        send_response(fd, "-ERR wrong number of arguments\r\n");
    }
}

void handle_persist(char *args, int fd) {
    char key[MAX_KEY_LEN];
    if (sscanf(args, "%63s", key) == 1) {
        int idx = find_key(key);
        if (idx == -1) {
            send_response(fd, ":0\r\n");
            printf("❌ PERSIST %s -> key not found\n", key);
        } else {
            store[idx].expire_time = 0;
            send_response(fd, ":1\r\n");
            printf("✅ PERSIST %s\n", key);
        }
    } else {
        send_response(fd, "-ERR wrong number of arguments\r\n");
    }
}

void handle_keys(char *args, int fd) {
    char pattern[MAX_KEY_LEN];
    if (sscanf(args, "%63s", pattern) == 1) {
        char result[4096] = "*";
        int count = 0;
        
        // Cleanup expired keys first
        cleanup_expired_keys();
        
        for (int i = 0; i < key_count; i++) {
            if (strcmp(pattern, "*") == 0 || strcmp(store[i].key, pattern) == 0) {
                char entry[128];
                snprintf(entry, sizeof(entry), "$%zu\r\n%s\r\n", 
                         strlen(store[i].key), store[i].key);
                strcat(result, entry);
                count++;
            }
        }
        char response[4096];
        snprintf(response, sizeof(response), "*%d\r\n%s", count, result + 1);
        send_response(fd, response);
        printf("🔑 KEYS %s -> %d keys\n", pattern, count);
    }
}

void handle_flushall(int fd) {
    key_count = 0;
    send_response(fd, "+OK\r\n");
    printf("🗑️ FLUSHALL\n");
}

void handle_info(int fd) {
    char response[512];
    int expired_count = 0;
    for (int i = 0; i < key_count; i++) {
        if (store[i].expire_time > 0) expired_count++;
    }
    snprintf(response, sizeof(response), 
             "+Keys: %d, With TTL: %d, No TTL: %d\r\n", 
             key_count, expired_count, key_count - expired_count);
    send_response(fd, response);
    printf("📊 INFO\n");
}

// ============================================
// Send Response (DEFINED BEFORE USE)
// ============================================

void send_response(int fd, const char *response) {
    write(fd, response, strlen(response));
}

// ============================================
// Command Parser
// ============================================

void execute_command(char *buffer, int fd) {
    char cmd[64];
    char *args = buffer;
    
    while (*args == ' ') args++;
    
    char *end = args + strlen(args) - 1;
    while (end >= args && (*end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    
    char *space = strchr(args, ' ');
    if (space) {
        int cmd_len = space - args;
        if (cmd_len > 0 && cmd_len < 64) {
            strncpy(cmd, args, cmd_len);
            cmd[cmd_len] = '\0';
        } else {
            strcpy(cmd, args);
        }
        args = space + 1;
        while (*args == ' ') args++;
    } else {
        strcpy(cmd, args);
        args = "";
    }
    
    printf("📨 Command: '%s'\n", cmd);
    
    if (strcasecmp(cmd, "PING") == 0) {
        handle_ping(fd);
    }
    else if (strcasecmp(cmd, "SET") == 0) {
        handle_set(args, fd);
    }
    else if (strcasecmp(cmd, "GET") == 0) {
        handle_get(args, fd);
    }
    else if (strcasecmp(cmd, "EXPIRE") == 0) {
        handle_expire(args, fd);
    }
    else if (strcasecmp(cmd, "TTL") == 0) {
        handle_ttl(args, fd);
    }
    else if (strcasecmp(cmd, "PERSIST") == 0) {
        handle_persist(args, fd);
    }
    else if (strcasecmp(cmd, "KEYS") == 0) {
        handle_keys(args, fd);
    }
    else if (strcasecmp(cmd, "FLUSHALL") == 0) {
        handle_flushall(fd);
    }
    else if (strcasecmp(cmd, "INFO") == 0) {
        handle_info(fd);
    }
    else {
        send_response(fd, "-ERR unknown command\r\n");
        printf("❌ Unknown: '%s'\n", cmd);
    }
}

// ============================================
// Server Setup
// ============================================

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    srand(time(NULL));
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    int val = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    set_nonblocking(server_fd);
    
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
    
    int epoll_fd = epoll_create1(0);
    struct epoll_event ev = {.events = EPOLLIN, .data.fd = server_fd};
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);
    
    printf("\n🚀 Redis with TTL/Expiration on port 6379\n");
    printf("📝 Commands: PING, SET, GET, EXPIRE, TTL, PERSIST, KEYS, FLUSHALL, INFO\n");
    printf("⏰ Keys expire automatically!\n");
    printf("📡 Waiting for connections...\n\n");
    
    struct epoll_event events[MAX_EVENTS];
    char buffer[BUFFER_SIZE];
    
    // Timer for cleanup (every 5 seconds)
    time_t last_cleanup = time(NULL);
    
    while (1) {
        // Periodic cleanup of expired keys
        time_t now = time(NULL);
        if (now - last_cleanup > 5) {
            cleanup_expired_keys();
            last_cleanup = now;
        }
        
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == server_fd) {
                int client_fd = accept(server_fd, NULL, NULL);
                set_nonblocking(client_fd);
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
                printf("🔗 New client connected (fd=%d)\n", client_fd);
            } else {
                int fd = events[i].data.fd;
                while (1) {
                    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
                    if (n <= 0) {
                        if (n == 0 || (n < 0 && errno != EAGAIN)) {
                            close(fd);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                            printf("🔌 Client disconnected (fd=%d)\n", fd);
                        }
                        break;
                    }
                    buffer[n] = '\0';
                    execute_command(buffer, fd);
                }
            }
        }
    }
    
    close(server_fd);
    close(epoll_fd);
    return 0;
}
