#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

// Simple key-value store
#define MAX_KEYS 100
char keys[MAX_KEYS][64];
char values[MAX_KEYS][1024];
int key_count = 0;

// Find key in our store
int find_key(char *key) {
    for (int i = 0; i < key_count; i++) {
        if (strcmp(keys[i], key) == 0) {
            return i;
        }
    }
    return -1;
}

// Handle SET command
void handle_set(char *key, char *value, char *response) {
    int index = find_key(key);
    if (index == -1) {
        // New key
        strcpy(keys[key_count], key);
        strcpy(values[key_count], value);
        key_count++;
    } else {
        // Update existing key
        strcpy(values[index], value);
    }
    strcpy(response, "+OK\r\n");
    printf("✅ SET %s = %s\n", key, value);
}

// Handle GET command
void handle_get(char *key, char *response) {
    int index = find_key(key);
    if (index == -1) {
        strcpy(response, "$-1\r\n");  // Redis null response
        printf("❌ GET %s -> not found\n", key);
    } else {
        sprintf(response, "$%zu\r\n%s\r\n", strlen(values[index]), values[index]);
        printf("✅ GET %s -> %s\n", key, values[index]);
    }
}

// Parse and execute command
void execute_command(char *buffer, char *response) {
    char cmd[64], key[64], value[1024];
    
    if (sscanf(buffer, "SET %s %s", key, value) == 2) {
        handle_set(key, value, response);
    } else if (sscanf(buffer, "GET %s", key) == 1) {
        handle_get(key, response);
    } else if (strncmp(buffer, "PING", 4) == 0) {
        strcpy(response, "+PONG\r\n");
        printf("🏓 PING\n");
    } else {
        strcpy(response, "-ERR unknown command\r\n");
        printf("❌ Unknown command\n");
    }
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
    char response[2048];
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(6379);
    
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 3);
    
    printf("🚀 Redis server with SET/GET running on port 6379\n");
    printf("📝 Commands: PING, SET key value, GET key\n\n");
    
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (client_fd < 0) continue;
        
        int bytes_read = read(client_fd, buffer, 1024);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            printf("📨 Received: %s", buffer);
            
            execute_command(buffer, response);
            write(client_fd, response, strlen(response));
        }
        
        close(client_fd);
    }
    
    close(server_fd);
    return 0;
}