#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>

// Function to parse Redis RESP protocol
void parse_redis_command(char *buffer) {
    printf("📨 Raw: %s", buffer);
    
    // Check if it's an array (RESP format)
    if (buffer[0] == '*') {
        // Array: *<number of elements>\r\n
        int num_args = atoi(buffer + 1);
        printf("📊 Number of arguments: %d\n", num_args);
        
        // Skip past the first \r\n
        char *ptr = buffer;
        while (*ptr != '\n') ptr++;
        ptr++; // Move past \n
        
        // Parse each argument
        for (int i = 0; i < num_args; i++) {
            if (*ptr == '$') {
                // Bulk string: $<length>\r\n<data>\r\n
                int len = atoi(ptr + 1);
                ptr = strchr(ptr, '\n') + 1; // Move to data
                
                char arg[256];
                strncpy(arg, ptr, len);
                arg[len] = '\0';
                printf("📝 Arg %d: '%s' (length: %d)\n", i+1, arg, len);
                
                ptr += len + 2; // Skip data and \r\n
            }
        }
    } 
    // Check for simple inline commands (like "PING\r\n")
    else if (strncmp(buffer, "PING", 4) == 0) {
        printf("🏓 PING command detected (inline)\n");
    } else {
        printf("❓ Unknown command format\n");
    }
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
    
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    printf("✅ Socket created\n");
    
    // Configure address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(6379);
    
    // Bind
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    printf("✅ Bound to port 6379\n");
    
    // Listen
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("✅ Redis parser server running on port 6379\n");
    printf("📡 Waiting for connections...\n\n");
    
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }
        printf("🔗 Client connected\n");
        
        int bytes_read = read(client_fd, buffer, 1024);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            parse_redis_command(buffer);
            
            // Send PONG response
            char *response = "+PONG\r\n";
            write(client_fd, response, strlen(response));
            printf("✅ Sent: +PONG\n\n");
        }
        
        close(client_fd);
        printf("🔌 Client disconnected\n\n");
    }
    
    close(server_fd);
    return 0;
}
