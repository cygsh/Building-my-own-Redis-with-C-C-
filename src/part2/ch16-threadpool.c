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
#include <pthread.h>

#define MAX_EVENTS 64
#define BUFFER_SIZE 1024
#define MAX_KEYS 1000
#define MAX_KEY_LEN 64
#define MAX_VAL_LEN 1024
#define THREAD_COUNT 4
#define QUEUE_SIZE 100

// ============================================
// Key-Value Store with Expiration
// ============================================

typedef struct {
    char key[MAX_KEY_LEN];
    char value[MAX_VAL_LEN];
    time_t expire_time;
} KeyValue;

KeyValue store[MAX_KEYS];
int key_count = 0;
pthread_mutex_t store_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================
// Thread Pool
// ============================================

typedef struct Task {
    int client_fd;
    char buffer[BUFFER_SIZE];
    struct Task *next;
} Task;

typedef struct {
    Task *head;
    Task *tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int shutdown;
} TaskQueue;

typedef struct {
    pthread_t thread;
    int id;
    TaskQueue *queue;
} Worker;

TaskQueue task_queue;
Worker workers[THREAD_COUNT];

// ============================================
// Task Queue Functions
// ============================================

void queue_init(TaskQueue *queue) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->shutdown = 0;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
}

void queue_push(TaskQueue *queue, int client_fd, const char *buffer) {
    Task *task = malloc(sizeof(Task));
    task->client_fd = client_fd;
    strncpy(task->buffer, buffer, BUFFER_SIZE - 1);
    task->buffer[BUFFER_SIZE - 1] = '\0';
    task->next = NULL;
    
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->tail) {
        queue->tail->next = task;
        queue->tail = task;
    } else {
        queue->head = task;
        queue->tail = task;
    }
    queue->count++;
    
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

Task* queue_pop(TaskQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    
    while (queue->head == NULL && !queue->shutdown) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }
    
    if (queue->shutdown && queue->head == NULL) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }
    
    Task *task = queue->head;
    queue->head = task->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    queue->count--;
    
    pthread_mutex_unlock(&queue->mutex);
    return task;
}

void queue_shutdown(TaskQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = 1;
    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

// ============================================
// Cleanup Expired Keys
// ============================================

void cleanup_expired_keys() {
    pthread_mutex_lock(&store_mutex);
    int cleaned = 0;
    int i = 0;
    while (i < key_count) {
        if (store[i].expire_time > 0 && time(NULL) > store[i].expire_time) {
            for (int j = i; j < key_count - 1; j++) {
                store[j] = store[j + 1];
            }
            key_count--;
            cleaned++;
        } else {
            i++;
        }
    }
    pthread_mutex_unlock(&store_mutex);
    if (cleaned > 0) {
        printf("🧹 Cleaned up %d expired keys\n", cleaned);
    }
}

// ============================================
// Redis Command Handlers
// ============================================

void send_response(int fd, const char *response) {
    write(fd, response, strlen(response));
}

void handle_ping(int fd) {
    send_response(fd, "+PONG\r\n");
    printf("🏓 PING\n");
}

void handle_set(char *args, int fd) {
    char key[MAX_KEY_LEN], value[MAX_VAL_LEN];
    if (sscanf(args, "%63s %1023s", key, value) == 2) {
        pthread_mutex_lock(&store_mutex);
        
        int idx = -1;
        for (int i = 0; i < key_count; i++) {
            if (strcmp(store[i].key, key) == 0) {
                idx = i;
                break;
            }
        }
        
        if (idx == -1) {
            if (key_count < MAX_KEYS) {
                strcpy(store[key_count].key, key);
                strcpy(store[key_count].value, value);
                store[key_count].expire_time = 0;
                key_count++;
            } else {
                send_response(fd, "-ERR max keys reached\r\n");
                pthread_mutex_unlock(&store_mutex);
                return;
            }
        } else {
            strcpy(store[idx].value, value);
        }
        
        pthread_mutex_unlock(&store_mutex);
        send_response(fd, "+OK\r\n");
        printf("✅ SET %s = %s\n", key, value);
    } else {
        send_response(fd, "-ERR wrong number of arguments\r\n");
    }
}

void handle_get(char *args, int fd) {
    char key[MAX_KEY_LEN];
    if (sscanf(args, "%63s", key) == 1) {
        pthread_mutex_lock(&store_mutex);
        
        int idx = -1;
        for (int i = 0; i < key_count; i++) {
            if (strcmp(store[i].key, key) == 0) {
                if (store[i].expire_time > 0 && time(NULL) > store[i].expire_time) {
                    for (int j = i; j < key_count - 1; j++) {
                        store[j] = store[j + 1];
                    }
                    key_count--;
                    idx = -1;
                    break;
                }
                idx = i;
                break;
            }
        }
        
        if (idx == -1) {
            send_response(fd, "$-1\r\n");
            printf("❌ GET %s -> not found\n", key);
        } else {
            char response[2048];
            snprintf(response, sizeof(response), "$%zu\r\n%s\r\n", 
                     strlen(store[idx].value), store[idx].value);
            send_response(fd, response);
            printf("✅ GET %s -> %s\n", key, store[idx].value);
        }
        
        pthread_mutex_unlock(&store_mutex);
    } else {
        send_response(fd, "-ERR wrong number of arguments\r\n");
    }
}

void handle_expire(char *args, int fd) {
    char key[MAX_KEY_LEN];
    int seconds;
    if (sscanf(args, "%63s %d", key, &seconds) == 2) {
        pthread_mutex_lock(&store_mutex);
        
        int idx = -1;
        for (int i = 0; i < key_count; i++) {
            if (strcmp(store[i].key, key) == 0) {
                idx = i;
                break;
            }
        }
        
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
        
        pthread_mutex_unlock(&store_mutex);
    } else {
        send_response(fd, "-ERR wrong number of arguments\r\n");
    }
}

void handle_ttl(char *args, int fd) {
    char key[MAX_KEY_LEN];
    if (sscanf(args, "%63s", key) == 1) {
        pthread_mutex_lock(&store_mutex);
        
        int idx = -1;
        for (int i = 0; i < key_count; i++) {
            if (strcmp(store[i].key, key) == 0) {
                idx = i;
                break;
            }
        }
        
        long long ttl;
        if (idx == -1) {
            ttl = -2;
        } else if (store[idx].expire_time == 0) {
            ttl = -1;
        } else {
            ttl = store[idx].expire_time - time(NULL);
            if (ttl < 0) {
                for (int j = idx; j < key_count - 1; j++) {
                    store[j] = store[j + 1];
                }
                key_count--;
                ttl = -2;
            }
        }
        
        pthread_mutex_unlock(&store_mutex);
        
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
        pthread_mutex_lock(&store_mutex);
        
        int idx = -1;
        for (int i = 0; i < key_count; i++) {
            if (strcmp(store[i].key, key) == 0) {
                idx = i;
                break;
            }
        }
        
        if (idx == -1) {
            send_response(fd, ":0\r\n");
            printf("❌ PERSIST %s -> key not found\n", key);
        } else {
            store[idx].expire_time = 0;
            send_response(fd, ":1\r\n");
            printf("✅ PERSIST %s\n", key);
        }
        
        pthread_mutex_unlock(&store_mutex);
    } else {
        send_response(fd, "-ERR wrong number of arguments\r\n");
    }
}

void handle_keys(char *args, int fd) {
    char pattern[MAX_KEY_LEN];
    if (sscanf(args, "%63s", pattern) == 1) {
        pthread_mutex_lock(&store_mutex);
        
        cleanup_expired_keys();
        
        char result[4096] = "*";
        int count = 0;
        
        for (int i = 0; i < key_count; i++) {
            if (strcmp(pattern, "*") == 0 || strcmp(store[i].key, pattern) == 0) {
                char entry[128];
                snprintf(entry, sizeof(entry), "$%zu\r\n%s\r\n", 
                         strlen(store[i].key), store[i].key);
                strcat(result, entry);
                count++;
            }
        }
        
        pthread_mutex_unlock(&store_mutex);
        
        char response[4096];
        snprintf(response, sizeof(response), "*%d\r\n%s", count, result + 1);
        send_response(fd, response);
        printf("🔑 KEYS %s -> %d keys\n", pattern, count);
    }
}

void handle_flushall(int fd) {
    pthread_mutex_lock(&store_mutex);
    key_count = 0;
    pthread_mutex_unlock(&store_mutex);
    send_response(fd, "+OK\r\n");
    printf("🗑️ FLUSHALL\n");
}

void handle_info(int fd) {
    pthread_mutex_lock(&store_mutex);
    int expired_count = 0;
    for (int i = 0; i < key_count; i++) {
        if (store[i].expire_time > 0) expired_count++;
    }
    pthread_mutex_unlock(&store_mutex);
    
    char response[512];
    snprintf(response, sizeof(response), 
             "+Keys: %d, With TTL: %d, No TTL: %d, Threads: %d\r\n", 
             key_count, expired_count, key_count - expired_count, THREAD_COUNT);
    send_response(fd, response);
    printf("📊 INFO\n");
}

// ============================================
// Execute Command (Worker Thread)
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
    
    printf("📨 Thread processing: '%s'\n", cmd);
    
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
// Worker Thread Function
// ============================================

void* worker_thread(void *arg) {
    Worker *worker = (Worker*)arg;
    TaskQueue *queue = worker->queue;
    
    printf("🧵 Worker %d started\n", worker->id);
    
    while (1) {
        Task *task = queue_pop(queue);
        if (task == NULL) {
            break;
        }
        
        printf("🧵 Worker %d processing client %d\n", worker->id, task->client_fd);
        execute_command(task->buffer, task->client_fd);
        close(task->client_fd);
        free(task);
    }
    
    printf("🧵 Worker %d stopped\n", worker->id);
    return NULL;
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
    
    // Initialize task queue
    queue_init(&task_queue);
    
    // Create worker threads
    for (int i = 0; i < THREAD_COUNT; i++) {
        workers[i].id = i;
        workers[i].queue = &task_queue;
        pthread_create(&workers[i].thread, NULL, worker_thread, &workers[i]);
    }
    
    // Create socket
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
    
    printf("\n🚀 Redis with Thread Pool on port 6379\n");
    printf("📝 Commands: PING, SET, GET, EXPIRE, TTL, PERSIST, KEYS, FLUSHALL, INFO\n");
    printf("🧵 Thread pool: %d workers\n", THREAD_COUNT);
    printf("📡 Waiting for connections...\n\n");
    
    struct epoll_event events[MAX_EVENTS];
    char buffer[BUFFER_SIZE];
    
    time_t last_cleanup = time(NULL);
    
    while (1) {
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
                    
                    // Push task to queue instead of processing directly
                    queue_push(&task_queue, fd, buffer);
                    printf("📤 Task queued for client %d (queue: %d)\n", fd, task_queue.count);
                    
                    // Don't close fd here - worker will handle it
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                }
            }
        }
    }
    
    // Cleanup
    queue_shutdown(&task_queue);
    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_join(workers[i].thread, NULL);
    }
    
    close(server_fd);
    close(epoll_fd);
    return 0;
}
