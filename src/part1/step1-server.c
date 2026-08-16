#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
    
    // 1. Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    printf("✅ Socket created successfully\n");
    
    // 2. Configure address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(6379);  // Redis default port
    
    // 3. Bind socket to port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    printf("✅ Bound to port 6379\n");
    
    // 4. Start listening
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    printf("✅ Redis server is listening on port 6379\n");
    printf("📡 Waiting for connections...\n\n");
    
    // 5. Accept connections in a loop
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }
        printf("🔗 New client connected!\n");
        
        // Read data from client
        int bytes_read = read(client_fd, buffer, 1024);
        if (bytes_read > 0) {
            printf("📨 Received: %s\n", buffer);
            
            // Send a simple response
            char *response = "+OK\r\n";
            write(client_fd, response, strlen(response));
            printf("✅ Sent: OK\n");
        }
        
        close(client_fd);
        printf("🔌 Client disconnected\n\n");
    }
    
    close(server_fd);
    return 0;
}