#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main() {
    // 1. Create a TCP socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    printf("✅ Socket created: fd=%d\n", fd);
    
    // 2. Set socket options (reuse address)
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    
    // 3. Bind to address and port
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1
    
    int rv = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rv < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    printf("✅ Bound to 127.0.0.1:1234\n");
    
    // 4. Listen for connections
    rv = listen(fd, SOMAXCONN);
    if (rv < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("✅ Listening for connections...\n");
    
    // 5. Accept a connection
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }
    printf("✅ Client connected from %s:%d\n", 
           inet_ntoa(client_addr.sin_addr), 
           ntohs(client_addr.sin_port));
    
    // 6. Read and write
    char buf[64] = {};
    ssize_t n = read(connfd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("📨 Received: %s", buf);
        write(connfd, buf, n);  // Echo back
    }
    
    // 7. Close connections
    close(connfd);
    close(fd);
    printf("🔌 Disconnected\n");
    
    return 0;
}