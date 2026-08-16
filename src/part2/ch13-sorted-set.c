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
#define MAX_LEVEL 16

// ============================================
// Skip List
// ============================================

typedef struct SkipNode {
    char *key;
    double score;
    struct SkipNode **next;
    int level;
} SkipNode;

typedef struct {
    SkipNode *head;
    int max_level;
    int count;
    int level_count[MAX_LEVEL + 1];
} SkipList;

int random_level() {
    int level = 1;
    while ((rand() & 1) && level < MAX_LEVEL) {
        level++;
    }
    return level;
}

SkipList* sl_create() {
    SkipList *sl = malloc(sizeof(SkipList));
    if (!sl) return NULL;
    
    sl->head = malloc(sizeof(SkipNode));
    sl->head->key = NULL;
    sl->head->score = 0;
    sl->head->level = MAX_LEVEL;
    sl->head->next = calloc(MAX_LEVEL + 1, sizeof(SkipNode*));
    
    sl->max_level = 1;
    sl->count = 0;
    memset(sl->level_count, 0, sizeof(sl->level_count));
    
    return sl;
}

void free_node(SkipNode *node) {
    if (node) {
        if (node->key) free(node->key);
        if (node->next) free(node->next);
        free(node);
    }
}

void sl_free(SkipList *sl) {
    if (!sl) return;
    
    SkipNode *current = sl->head->next[0];
    while (current) {
        SkipNode *next = current->next[0];
        free_node(current);
        current = next;
    }
    free_node(sl->head);
    free(sl);
}

SkipNode* sl_find(SkipList *sl, const char *key) {
    if (!sl || !key) return NULL;
    
    SkipNode *current = sl->head->next[0];
    while (current) {
        if (strcmp(current->key, key) == 0) {
            return current;
        }
        current = current->next[0];
    }
    return NULL;
}

void sl_insert(SkipList *sl, const char *key, double score) {
    SkipNode *existing = sl_find(sl, key);
    if (existing) {
        existing->score = score;
        return;
    }
    
    SkipNode *update[MAX_LEVEL + 1];
    SkipNode *current = sl->head;
    
    for (int i = sl->max_level - 1; i >= 0; i--) {
        while (current->next[i] && 
               (current->next[i]->score < score || 
                (current->next[i]->score == score && 
                 strcmp(current->next[i]->key, key) < 0))) {
            current = current->next[i];
        }
        update[i] = current;
    }
    
    int new_level = random_level();
    if (new_level > sl->max_level) {
        for (int i = sl->max_level; i < new_level; i++) {
            update[i] = sl->head;
        }
        sl->max_level = new_level;
    }
    
    SkipNode *new_node = malloc(sizeof(SkipNode));
    new_node->key = strdup(key);
    new_node->score = score;
    new_node->level = new_level;
    new_node->next = calloc(new_level + 1, sizeof(SkipNode*));
    
    for (int i = 0; i < new_level; i++) {
        new_node->next[i] = update[i]->next[i];
        update[i]->next[i] = new_node;
        sl->level_count[i]++;
    }
    
    sl->count++;
}

double sl_get_score(SkipList *sl, const char *key) {
    SkipNode *node = sl_find(sl, key);
    if (node) {
        return node->score;
    }
    return -1;
}

bool sl_delete(SkipList *sl, const char *key) {
    if (!sl || !key) return false;
    
    SkipNode *target = sl_find(sl, key);
    if (!target) {
        return false;
    }
    
    double score = target->score;
    SkipNode *update[MAX_LEVEL + 1];
    SkipNode *current = sl->head;
    
    for (int i = sl->max_level - 1; i >= 0; i--) {
        while (current->next[i] && 
               (current->next[i]->score < score || 
                (current->next[i]->score == score && 
                 strcmp(current->next[i]->key, key) < 0))) {
            current = current->next[i];
        }
        update[i] = current;
    }
    
    current = current->next[0];
    if (!current || strcmp(current->key, key) != 0) {
        return false;
    }
    
    for (int i = 0; i < sl->max_level; i++) {
        if (update[i]->next[i] != current) break;
        update[i]->next[i] = current->next[i];
        sl->level_count[i]--;
    }
    
    while (sl->max_level > 1 && sl->head->next[sl->max_level - 1] == NULL) {
        sl->max_level--;
    }
    
    free_node(current);
    sl->count--;
    return true;
}

SkipNode** sl_range_by_rank(SkipList *sl, int start, int end, int *count) {
    if (start < 0) start = 0;
    if (end >= sl->count) end = sl->count - 1;
    if (start > end || sl->count == 0) {
        *count = 0;
        return NULL;
    }
    
    *count = end - start + 1;
    SkipNode **result = malloc(*count * sizeof(SkipNode*));
    if (!result) return NULL;
    
    SkipNode *current = sl->head->next[0];
    int pos = 0;
    while (current && pos < start) {
        current = current->next[0];
        pos++;
    }
    
    int idx = 0;
    while (current && idx < *count) {
        result[idx++] = current;
        current = current->next[0];
    }
    
    return result;
}

// ============================================
// Redis Server
// ============================================

SkipList *zset;

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// ============================================
// RESP Response Helpers
// ============================================

void send_response(int fd, const char *response) {
    write(fd, response, strlen(response));
}

void send_ok(int fd) {
    send_response(fd, "+OK\r\n");
}

void send_pong(int fd) {
    send_response(fd, "+PONG\r\n");
}

void send_error(int fd, const char *msg) {
    char response[256];
    snprintf(response, sizeof(response), "-ERR %s\r\n", msg);
    send_response(fd, response);
}

void send_integer(int fd, int value) {
    char response[64];
    snprintf(response, sizeof(response), ":%d\r\n", value);
    send_response(fd, response);
}

void send_bulk_string(int fd, const char *str) {
    if (str) {
        char response[256];
        snprintf(response, sizeof(response), "$%zu\r\n%s\r\n", strlen(str), str);
        send_response(fd, response);
    } else {
        send_response(fd, "$-1\r\n");
    }
}

void send_array_start(int fd, int count) {
    char response[64];
    snprintf(response, sizeof(response), "*%d\r\n", count);
    send_response(fd, response);
}

// ============================================
// Command Handlers
// ============================================

void handle_zadd(char *args, int fd) {
    char key[64], member[64];
    double score;
    
    if (sscanf(args, "%63s %lf %63s", key, &score, member) == 3) {
        sl_insert(zset, member, score);
        send_integer(fd, 1);
        printf("✅ ZADD %s = %.2f\n", member, score);
    } else {
        send_error(fd, "wrong number of arguments for ZADD");
    }
}

void handle_zrange(char *args, int fd) {
    char key[64];
    int start, end;
    
    if (sscanf(args, "%63s %d %d", key, &start, &end) == 3) {
        int count;
        SkipNode **nodes = sl_range_by_rank(zset, start, end, &count);
        
        if (count > 0 && nodes) {
            send_array_start(fd, count * 2);
            for (int i = 0; i < count; i++) {
                send_bulk_string(fd, nodes[i]->key);
                char score_str[64];
                snprintf(score_str, sizeof(score_str), "%.2f", nodes[i]->score);
                send_bulk_string(fd, score_str);
            }
            free(nodes);
            printf("📊 ZRANGE %d-%d -> %d elements\n", start, end, count);
        } else {
            send_response(fd, "*0\r\n");
            printf("📊 ZRANGE %d-%d -> 0 elements\n", start, end);
        }
    } else {
        send_error(fd, "wrong number of arguments for ZRANGE");
    }
}

void handle_zscore(char *args, int fd) {
    char key[64], member[64];
    
    if (sscanf(args, "%63s %63s", key, member) == 2) {
        double score = sl_get_score(zset, member);
        if (score >= 0) {
            char response[64];
            snprintf(response, sizeof(response), "$%d\r\n%.2f\r\n", 
                     snprintf(NULL, 0, "%.2f", score), score);
            send_response(fd, response);
            printf("✅ ZSCORE %s -> %.2f\n", member, score);
        } else {
            send_response(fd, "$-1\r\n");
            printf("❌ ZSCORE %s -> not found\n", member);
        }
    } else {
        send_error(fd, "wrong number of arguments for ZSCORE");
    }
}

void handle_zrem(char *args, int fd) {
    char key[64], member[64];
    
    if (sscanf(args, "%63s %63s", key, member) == 2) {
        if (sl_delete(zset, member)) {
            send_integer(fd, 1);
            printf("🗑️ ZREM %s\n", member);
        } else {
            send_integer(fd, 0);
            printf("❌ ZREM %s -> not found\n", member);
        }
    } else {
        send_error(fd, "wrong number of arguments for ZREM");
    }
}

void handle_zcard(char *args, int fd) {
    send_integer(fd, zset->count);
    printf("📊 ZCARD -> %d elements\n", zset->count);
}

void handle_info(int fd) {
    char response[256];
    snprintf(response, sizeof(response), 
             "+SortedSet: count=%d, max_level=%d, memory_usage=unknown\r\n", 
             zset->count, zset->max_level);
    send_response(fd, response);
    printf("📊 INFO\n");
}

// ============================================
// Parse and Execute Command (FIXED)
// ============================================

void execute_command(char *buffer, int fd) {
    char cmd[64];
    char *args = buffer;
    
    // Skip leading spaces
    while (*args == ' ') args++;
    
    // Remove trailing \r\n
    char *end = args + strlen(args) - 1;
    while (end >= args && (*end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    
    // Extract command (first word)
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
        send_pong(fd);
        printf("🏓 PONG\n");
    }
    else if (strcasecmp(cmd, "ZADD") == 0) {
        handle_zadd(args, fd);
    }
    else if (strcasecmp(cmd, "ZRANGE") == 0) {
        handle_zrange(args, fd);
    }
    else if (strcasecmp(cmd, "ZSCORE") == 0) {
        handle_zscore(args, fd);
    }
    else if (strcasecmp(cmd, "ZREM") == 0) {
        handle_zrem(args, fd);
    }
    else if (strcasecmp(cmd, "ZCARD") == 0) {
        handle_zcard(args, fd);
    }
    else if (strcasecmp(cmd, "INFO") == 0) {
        handle_info(fd);
    }
    else {
        send_error(fd, "unknown command");
        printf("❌ Unknown: '%s'\n", cmd);
    }
}

// ============================================
// Main Server
// ============================================

int main() {
    srand(time(NULL));
    
    zset = sl_create();
    if (!zset) {
        printf("❌ Failed to create sorted set\n");
        return 1;
    }
    
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
    
    printf("\n🚀 Redis with Sorted Sets on port 6379\n");
    printf("📝 Commands: PING, ZADD, ZRANGE, ZSCORE, ZREM, ZCARD, INFO\n");
    printf("⚡ Using skip lists (O(log N) operations)\n");
    printf("📡 Waiting for connections...\n\n");
    
    struct epoll_event events[MAX_EVENTS];
    char buffer[BUFFER_SIZE];
    
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
                    execute_command(buffer, fd);
                }
            }
        }
    }
    
    sl_free(zset);
    close(server_fd);
    close(epoll_fd);
    return 0;
}
