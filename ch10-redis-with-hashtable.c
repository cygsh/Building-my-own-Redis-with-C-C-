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

// ============================================
// Hash Table from Chapter 9-10
// ============================================

typedef struct HEntry {
    char *key;
    char *value;
    struct HEntry *next;
} HEntry;

typedef struct {
    HEntry **buckets;
    int size;
    int count;
    int threshold;
    float load_factor;
} HashTable;

unsigned long hash_function(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

HashTable* ht_create(int initial_size) {
    HashTable *ht = malloc(sizeof(HashTable));
    ht->size = initial_size > 0 ? initial_size : 16;
    ht->count = 0;
    ht->load_factor = 0.75;
    ht->threshold = ht->size * ht->load_factor;
    ht->buckets = calloc(ht->size, sizeof(HEntry*));
    return ht;
}

void ht_resize(HashTable *ht, int new_size) {
    HEntry **old_buckets = ht->buckets;
    int old_size = ht->size;
    
    ht->size = new_size;
    ht->threshold = ht->size * ht->load_factor;
    ht->buckets = calloc(ht->size, sizeof(HEntry*));
    ht->count = 0;
    
    for (int i = 0; i < old_size; i++) {
        HEntry *entry = old_buckets[i];
        while (entry) {
            HEntry *next = entry->next;
            unsigned long hash = hash_function(entry->key);
            int index = hash % ht->size;
            entry->next = ht->buckets[index];
            ht->buckets[index] = entry;
            ht->count++;
            entry = next;
        }
    }
    free(old_buckets);
}

void ht_set(HashTable *ht, const char *key, const char *value) {
    if (ht->count >= ht->threshold) {
        ht_resize(ht, ht->size * 2);
    }
    
    unsigned long hash = hash_function(key);
    int index = hash % ht->size;
    
    HEntry *entry = ht->buckets[index];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            free(entry->value);
            entry->value = strdup(value);
            return;
        }
        entry = entry->next;
    }
    
    HEntry *new_entry = malloc(sizeof(HEntry));
    new_entry->key = strdup(key);
    new_entry->value = strdup(value);
    new_entry->next = ht->buckets[index];
    ht->buckets[index] = new_entry;
    ht->count++;
}

char* ht_get(HashTable *ht, const char *key) {
    unsigned long hash = hash_function(key);
    int index = hash % ht->size;
    
    HEntry *entry = ht->buckets[index];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    return NULL;
}

bool ht_delete(HashTable *ht, const char *key) {
    unsigned long hash = hash_function(key);
    int index = hash % ht->size;
    
    HEntry *entry = ht->buckets[index];
    HEntry *prev = NULL;
    
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            if (prev) {
                prev->next = entry->next;
            } else {
                ht->buckets[index] = entry->next;
            }
            free(entry->key);
            free(entry->value);
            free(entry);
            ht->count--;
            return true;
        }
        prev = entry;
        entry = entry->next;
    }
    return false;
}

char** ht_get_keys(HashTable *ht, int *count) {
    char **keys = malloc(ht->count * sizeof(char*));
    int idx = 0;
    for (int i = 0; i < ht->size; i++) {
        HEntry *entry = ht->buckets[i];
        while (entry) {
            keys[idx++] = entry->key;
            entry = entry->next;
        }
    }
    *count = ht->count;
    return keys;
}

void ht_free(HashTable *ht) {
    for (int i = 0; i < ht->size; i++) {
        HEntry *entry = ht->buckets[i];
        while (entry) {
            HEntry *next = entry->next;
            free(entry->key);
            free(entry->value);
            free(entry);
            entry = next;
        }
    }
    free(ht->buckets);
    free(ht);
}

// ============================================
// Redis Server with Hash Table
// ============================================

HashTable *store;  // Global key-value store

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void execute_command(char *buffer, char *response) {
    char cmd[64], key[64], value[1024];
    char *p = buffer;
    
    while (*p == ' ') p++;
    
    if (sscanf(p, "SET %63s %1023s", key, value) == 2) {
        ht_set(store, key, value);
        strcpy(response, "+OK\r\n");
        printf("✅ SET %s = %s\n", key, value);
    }
    else if (sscanf(p, "GET %63s", key) == 1) {
        char *val = ht_get(store, key);
        if (val) {
            sprintf(response, "$%zu\r\n%s\r\n", strlen(val), val);
            printf("✅ GET %s -> %s\n", key, val);
        } else {
            strcpy(response, "$-1\r\n");
            printf("❌ GET %s -> not found\n", key);
        }
    }
    else if (strncmp(p, "PING", 4) == 0) {
        strcpy(response, "+PONG\r\n");
        printf("🏓 PING\n");
    }
    else if (strncmp(p, "DEL", 3) == 0) {
        if (sscanf(p, "DEL %63s", key) == 1) {
            if (ht_delete(store, key)) {
                strcpy(response, ":1\r\n");
                printf("🗑️ DEL %s\n", key);
            } else {
                strcpy(response, ":0\r\n");
                printf("❌ DEL %s -> not found\n", key);
            }
        }
    }
    else if (strncmp(p, "KEYS", 4) == 0) {
        char pattern[64];
        if (sscanf(p, "KEYS %63s", pattern) == 1) {
            int count;
            char **keys = ht_get_keys(store, &count);
            char result[2048] = "";
            int found = 0;
            
            for (int i = 0; i < count; i++) {
                if (strcmp(pattern, "*") == 0) {
                    char entry[128];
                    sprintf(entry, "$%zu\r\n%s\r\n", strlen(keys[i]), keys[i]);
                    strcat(result, entry);
                    found++;
                }
            }
            free(keys);
            
            if (found == 0) {
                strcpy(response, "*0\r\n");
            } else {
                sprintf(response, "*%d\r\n%s", found, result);
            }
            printf("🔑 KEYS %s -> %d keys\n", pattern, found);
        }
    }
    else if (strncmp(p, "FLUSHALL", 8) == 0) {
        ht_free(store);
        store = ht_create(16);
        strcpy(response, "+OK\r\n");
        printf("🗑️ FLUSHALL\n");
    }
    else if (strncmp(p, "INFO", 4) == 0) {
        sprintf(response, "+HashTable size: %d, count: %d, load: %.2f\r\n", 
                store->size, store->count, (float)store->count / store->size);
        printf("📊 INFO\n");
    }
    else {
        strcpy(response, "-ERR unknown command\r\n");
        printf("❌ Unknown: %s", p);
    }
}

int main() {
    // Initialize hash table
    store = ht_create(16);
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int val = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    set_nonblocking(server_fd);
    
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6379);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, SOMAXCONN);
    
    int epoll_fd = epoll_create1(0);
    struct epoll_event ev = {.events = EPOLLIN, .data.fd = server_fd};
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);
    
    printf("🚀 Redis with Hash Table on port 6379\n");
    printf("📝 Commands: PING, SET, GET, DEL, KEYS, FLUSHALL, INFO\n");
    printf("⚡ Using O(1) hash table storage!\n");
    printf("📡 Waiting for connections...\n\n");
    
    struct epoll_event events[MAX_EVENTS];
    char buffer[BUFFER_SIZE];
    char response[2048];
    
    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
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
                    printf("📨 Client %d: %s", fd, buffer);
                    execute_command(buffer, response);
                    write(fd, response, strlen(response));
                }
            }
        }
    }
    
    ht_free(store);
    close(server_fd);
    close(epoll_fd);
    return 0;
}
