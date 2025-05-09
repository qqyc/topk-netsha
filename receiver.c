#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <inttypes.h>
#include <string.h>

#define MAX_BATCH   1024
#define HASH_SIZE   100003

static uint8_t seen[HASH_SIZE] = {0};

static uint64_t current_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + ts.tv_nsec;
}

static int hash_key(uint32_t q, uint32_t s) {
    return (q * 1315423911u + s) % HASH_SIZE;
}

int main() {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    char *mode = getenv("MODE");
    if (!mode) mode = "aggr";

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in r_in = {
        .sin_family      = AF_INET,
        .sin_port        = htons(RECEIVER_PORT),
        .sin_addr.s_addr = INADDR_ANY
    };
    bind(sock, (void*)&r_in, sizeof(r_in));

    topk_pkt_t buf[MAX_BATCH];
    uint64_t lat_sum = 0;
    uint32_t lat_cnt = 0;

    while (1) {
        int bytes = recv(sock, buf, sizeof(buf), 0);
        if (bytes <= 0) continue;
        int n = bytes / sizeof(topk_pkt_t);

        for (int i = 0; i < n; i++) {
            topk_pkt_t *p = &buf[i];
            // 跳过 END_VALUE
            if (p->value == END_VALUE) continue;

            int h = hash_key(p->query_id, p->seq_num);
            if (!seen[h]) {
                seen[h] = 1;
                uint64_t now = current_time_ns();
                lat_sum += now - p->ts;
                lat_cnt++;
                printf("Q%u Seq%u Val%u\n",
                       p->query_id, p->seq_num, p->value);
                if (lat_cnt == K_TARGET) {
                    fprintf(stderr,
                        "[%s-LATENCY] 平均一跳时延=%.2f ms\n",
                        mode,
                        lat_sum * 1e-6 / lat_cnt);
                    return 0;
                }
            }
        }
    }

    close(sock);
    return 0;
}
