#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define MAX_BATCH 1024

#define HASH_SIZE  100003

static uint8_t seen[HASH_SIZE] = {0};

int hash_key(uint32_t q, uint32_t s) {
    return (q * 1315423911u + s) % HASH_SIZE;
}

int main() {
    setvbuf(stdout, NULL, _IOLBF, 0);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in r_in = {
        .sin_family = AF_INET,
        .sin_port = htons(RECEIVER_PORT),
        .sin_addr.s_addr = INADDR_ANY
    };
    bind(sock, (void*)&r_in, sizeof(r_in));

    topk_pkt_t buf[MAX_BATCH];
    while (1) {
        int n = recv(sock, buf, sizeof(buf), 0) / sizeof(topk_pkt_t);
        for (int i = 0; i < n; i++) {
            topk_pkt_t *p = &buf[i];
            int h = hash_key(p->query_id, p->seq_num);
            if (!seen[h]) {
                seen[h] = 1;
                printf("Q%u Seq%u Val%u\n", p->query_id, p->seq_num, p->value);
            }
        }
    }
    close(sock);
    return 0;
}
