# Build Your Own Redis with C/C++

A complete Redis clone built from scratch following the book "Build Your Own Redis with C/C++".

## 📚 Chapters Completed

### Part 1: Redis from 0 to 1
- [x] Chapter 1: Introduction
- [x] Chapter 2: Socket Programming (`ch2-socket.c`)
- [x] Chapter 3: TCP Server and Client (`ch3-server.c`, `ch3-client.c`)
- [x] Chapter 4: Request-Response Protocol (`ch4-resp.c`)
- [x] Chapter 5: Concurrent IO Models (`ch5-select.c`)
- [x] Chapter 6-7: Event Loop (`ch6-epoll.c`)
- [x] Chapter 8: Key-Value Server (`ch8-kvserver.c`)

### Part 2: Advanced Topics
- [x] Chapter 9-10: Hashtables (`ch9-hashtable.c`)
- [x] Chapter 11: Data Serialization (`ch11-serialization.c`)
- [x] Chapter 12-13: Sorted Sets (`ch12-skiplist.c`, `ch13-sorted-set.c`)
- [x] Chapter 14-15: TTL & Expiration (`ch14-ttl.c`)
- [x] Chapter 16: Thread Pool (`ch16-threadpool.c`)

## 🚀 Commands Supported
PING, SET, GET, DEL, EXISTS, KEYS, FLUSHALL
SAVE, INFO, ZADD, ZRANGE, ZSCORE, ZREM, ZCARD
EXPIRE, TTL, PERSIST


## 🔧 Build & Run

```bash
# Compile a specific chapter
gcc src/part2/ch16-threadpool.c -o bin/part2/redis-server -pthread

# Run the server
./bin/part2/redis-server

# Connect with telnet
telnet localhost 6379

├── src/
│   ├── part1/     # Chapters 1-8
│   └── part2/     # Chapters 9-16
├── bin/
│   ├── part1/     # Compiled binaries
│   └── part2/     # Compiled binaries
├── docs/
│   ├── README
│   └── LICENSE
└── test/

📖 Reference

    Book: Build Your Own Redis with C/C++

    Author: James Smith
