#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
    addr.sin_port = htons(6379);  // Redis default port
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // All interfaces
    
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    
    if (listen(fd, SOMAXCONN) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("📡 Redis server listening on port 6379\n");
    
    while (1) {
        struct sockaddr_in client_addr = {};
        socklen_t socklen = sizeof(client_addr);
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
        if (connfd < 0) {
            perror("accept");
            continue;
        }
        
        printf("🔗 Client connected\n");
        
        char buf[1024] = {};
        ssize_t n = read(connfd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            printf("📨 %s", buf);
            write(connfd, "+OK\r\n", 5);
        }
        
        close(connfd);
        printf("🔌 Client disconnected\n");
    }
    
    close(fd);
    return 0;
}