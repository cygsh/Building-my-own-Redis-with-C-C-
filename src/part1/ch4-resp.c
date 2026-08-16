#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <ctype.h>

#define MAX_MSG 4096

// RESP types
#define RESP_SIMPLE_STRING 1
#define RESP_ERROR 2
#define RESP_INTEGER 3
#define RESP_BULK_STRING 4
#define RESP_ARRAY 5
#define RESP_NULL 6

// Forward declarations
typedef struct RespValue RespValue;

struct RespValue {
    int type;
    int len;
    int count;
    char *str;
    struct RespValue **array;
};

// Parse inline command (e.g., "PING\r\n" or "SET key value\r\n")
RespValue* parse_inline(const char *data, size_t *pos) {
    // Skip leading spaces
    while (data[*pos] == ' ') (*pos)++;
    
    // Count arguments
    int argc = 0;
    const char *p = data + *pos;
    while (*p && *p != '\r' && *p != '\n') {
        while (*p == ' ') p++;
        if (*p == '\r' || *p == '\n' || *p == '\0') break;
        argc++;
        while (*p && *p != ' ' && *p != '\r' && *p != '\n') p++;
    }
    
    if (argc == 0) return NULL;
    
    // Create RESP array
    RespValue *val = malloc(sizeof(RespValue));
    val->type = RESP_ARRAY;
    val->count = argc;
    val->str = NULL;
    val->array = malloc(argc * sizeof(RespValue*));
    
    // Parse each argument
    p = data + *pos;
    for (int i = 0; i < argc; i++) {
        while (*p == ' ') p++;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\r' && *p != '\n') p++;
        int len = p - start;
        
        RespValue *arg = malloc(sizeof(RespValue));
        arg->type = RESP_BULK_STRING;
        arg->len = len;
        arg->str = malloc(len + 1);
        strncpy(arg->str, start, len);
        arg->str[len] = '\0';
        arg->array = NULL;
        arg->count = 0;
        val->array[i] = arg;
    }
    
    // Skip trailing \r\n
    while (data[*pos] == ' ' || data[*pos] == '\r' || data[*pos] == '\n') (*pos)++;
    
    return val;
}

// Parse RESP message
RespValue* parse_resp(const char *data, size_t *pos) {
    if (data[*pos] == '\0' || data[*pos] == '\r' || data[*pos] == '\n') {
        (*pos)++;
        return NULL;
    }
    
    // Check if it's an inline command (not starting with RESP type)
    char first = data[*pos];
    if (first != '+' && first != '-' && first != ':' && first != '$' && first != '*') {
        return parse_inline(data, pos);
    }
    
    RespValue *val = malloc(sizeof(RespValue));
    if (!val) return NULL;
    
    char type = data[(*pos)++];
    
    switch (type) {
        case '+': {
            val->type = RESP_SIMPLE_STRING;
            const char *end = strchr(&data[*pos], '\r');
            if (!end) { free(val); return NULL; }
            val->len = end - &data[*pos];
            val->str = malloc(val->len + 1);
            strncpy(val->str, &data[*pos], val->len);
            val->str[val->len] = '\0';
            *pos += val->len + 2;
            val->array = NULL;
            val->count = 0;
            break;
        }
        case '-': {
            val->type = RESP_ERROR;
            const char *end = strchr(&data[*pos], '\r');
            if (!end) { free(val); return NULL; }
            val->len = end - &data[*pos];
            val->str = malloc(val->len + 1);
            strncpy(val->str, &data[*pos], val->len);
            val->str[val->len] = '\0';
            *pos += val->len + 2;
            val->array = NULL;
            val->count = 0;
            break;
        }
        case ':': {
            val->type = RESP_INTEGER;
            char *end;
            val->len = strtol(&data[*pos], &end, 10);
            *pos = end - data + 2;
            val->str = NULL;
            val->array = NULL;
            val->count = 0;
            break;
        }
        case '$': {
            char *end;
            int len = strtol(&data[*pos], &end, 10);
            *pos = end - data + 2;
            if (len == -1) {
                val->type = RESP_NULL;
                val->len = -1;
                val->str = NULL;
                val->array = NULL;
                val->count = 0;
            } else {
                val->type = RESP_BULK_STRING;
                val->len = len;
                val->str = malloc(len + 1);
                strncpy(val->str, &data[*pos], len);
                val->str[len] = '\0';
                *pos += len + 2;
                val->array = NULL;
                val->count = 0;
            }
            break;
        }
        case '*': {
            val->type = RESP_ARRAY;
            char *end;
            int count = strtol(&data[*pos], &end, 10);
            *pos = end - data + 2;
            val->count = count;
            val->str = NULL;
            if (count == -1) {
                val->array = NULL;
            } else {
                val->array = malloc(count * sizeof(RespValue*));
                for (int i = 0; i < count; i++) {
                    val->array[i] = parse_resp(data, pos);
                }
            }
            break;
        }
        default:
            free(val);
            return NULL;
    }
    return val;
}

// Print parsed value (for debugging)
void print_resp(RespValue *val, int indent) {
    if (!val) return;
    char spaces[64];
    memset(spaces, ' ', indent * 2);
    spaces[indent * 2] = '\0';
    
    switch (val->type) {
        case RESP_SIMPLE_STRING:
            printf("%s+%s\n", spaces, val->str ? val->str : "");
            break;
        case RESP_ERROR:
            printf("%s-%s\n", spaces, val->str ? val->str : "");
            break;
        case RESP_INTEGER:
            printf("%s:%d\n", spaces, val->len);
            break;
        case RESP_BULK_STRING:
            printf("%s$%d: %s\n", spaces, val->len, val->str ? val->str : "");
            break;
        case RESP_ARRAY:
            printf("%s*%d\n", spaces, val->count);
            for (int i = 0; i < val->count; i++) {
                print_resp(val->array[i], indent + 1);
            }
            break;
        case RESP_NULL:
            printf("%s(null)\n", spaces);
            break;
        default:
            printf("%s(unknown type: %d)\n", spaces, val->type);
            break;
    }
}

// Free RESP value
void free_resp(RespValue *val) {
    if (!val) return;
    if (val->type == RESP_ARRAY) {
        for (int i = 0; i < val->count; i++) {
            free_resp(val->array[i]);
        }
        free(val->array);
    }
    if (val->str) free(val->str);
    free(val);
}

// Serialize a simple response
char* serialize_response(RespValue *val) {
    static char buf[1024];
    switch (val->type) {
        case RESP_SIMPLE_STRING:
            sprintf(buf, "+%s\r\n", val->str ? val->str : "");
            break;
        case RESP_ERROR:
            sprintf(buf, "-%s\r\n", val->str ? val->str : "");
            break;
        case RESP_INTEGER:
            sprintf(buf, ":%d\r\n", val->len);
            break;
        case RESP_BULK_STRING:
            sprintf(buf, "$%d\r\n%s\r\n", val->len, val->str ? val->str : "");
            break;
        case RESP_NULL:
            sprintf(buf, "$-1\r\n");
            break;
        case RESP_ARRAY:
            if (val->count == 0) {
                sprintf(buf, "*0\r\n");
            } else {
                sprintf(buf, "*%d\r\n", val->count);
            }
            break;
        default:
            sprintf(buf, "-ERR unknown response type\r\n");
            break;
    }
    return buf;
}

// Process command (handles both inline and RESP)
char* process_command(const char *buffer) {
    static char response[1024];
    size_t pos = 0;
    RespValue *parsed = parse_resp(buffer, &pos);
    
    if (!parsed) {
        sprintf(response, "-ERR invalid command\r\n");
        return response;
    }
    
    printf("✅ Parsed command:\n");
    print_resp(parsed, 0);
    
    // Handle commands
    if (parsed->type == RESP_ARRAY && parsed->count > 0) {
        RespValue *cmd = parsed->array[0];
        if (cmd && cmd->type == RESP_BULK_STRING && cmd->str) {
            // PING
            if (strcasecmp(cmd->str, "PING") == 0) {
                RespValue pong = {
                    .type = RESP_SIMPLE_STRING,
                    .str = "PONG",
                    .len = 4,
                    .array = NULL,
                    .count = 0
                };
                strcpy(response, serialize_response(&pong));
                free_resp(parsed);
                return response;
            }
            
            // SET key value
            if (strcasecmp(cmd->str, "SET") == 0 && parsed->count >= 3) {
                RespValue *key = parsed->array[1];
                RespValue *value = parsed->array[2];
                if (key && value) {
                    RespValue ok = {
                        .type = RESP_SIMPLE_STRING,
                        .str = "OK",
                        .len = 2,
                        .array = NULL,
                        .count = 0
                    };
                    strcpy(response, serialize_response(&ok));
                    printf("📝 SET %s = %s\n", key->str, value->str);
                    free_resp(parsed);
                    return response;
                }
            }
            
            // GET key
            if (strcasecmp(cmd->str, "GET") == 0 && parsed->count >= 2) {
                RespValue *key = parsed->array[1];
                if (key) {
                    RespValue result = {
                        .type = RESP_BULK_STRING,
                        .str = "value",
                        .len = 5,
                        .array = NULL,
                        .count = 0
                    };
                    strcpy(response, serialize_response(&result));
                    printf("📝 GET %s = value\n", key->str);
                    free_resp(parsed);
                    return response;
                }
            }
        }
    }
    
    sprintf(response, "+OK\r\n");
    free_resp(parsed);
    return response;
}

// Server main
int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6379);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    
    if (listen(fd, SOMAXCONN) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    printf("📡 RESP parser server on port 6379\n");
    printf("📝 Supports both inline commands (telnet) and RESP (redis-cli)\n");
    printf("📝 Commands: PING, SET key value, GET key\n\n");
    
    while (1) {
        int connfd = accept(fd, NULL, NULL);
        if (connfd < 0) {
            perror("accept");
            continue;
        }
        
        char buf[MAX_MSG] = {};
        ssize_t n = read(connfd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            printf("\n📨 Raw received:\n%s", buf);
            
            char *response = process_command(buf);
            write(connfd, response, strlen(response));
            printf("✅ Sent: %s", response);
        }
        close(connfd);
    }
    
    close(fd);
    return 0;
}