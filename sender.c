#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <inttypes.h>

static uint64_t current_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + ts.tv_nsec;
}

int main() {
    // 使 stdout 行缓冲
    setvbuf(stdout, NULL, _IOLBF, 0);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sw_addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(SWITCH_PORT),
        .sin_addr.s_addr = inet_addr("10.10.0.3")
    };

    srand(time(NULL));
    for (uint32_t i = 0; i < 1000000; i++) {
        topk_pkt_t pkt = {
            .query_id = QUERY_ID,
            .seq_num  = i,
            .value    = rand(),
            .ts       = current_time_ns()
        };
        sendto(sock, &pkt, sizeof(pkt), 0,
               (struct sockaddr*)&sw_addr, sizeof(sw_addr));

        if (i % 1000 == 0) {
            // sleep(1);
            printf("[SENDER] Sent %u packets\n", i);
        }
    }
    // 发送 END 包两次
    topk_pkt_t endpkt = {
        .query_id = QUERY_ID,
        .seq_num  = 1000000,
        .value    = END_VALUE,
        .ts       = current_time_ns()
    };
    sendto(sock, &endpkt, sizeof(endpkt), 0,
           (struct sockaddr*)&sw_addr, sizeof(sw_addr));
    sendto(sock, &endpkt, sizeof(endpkt), 0,
           (struct sockaddr*)&sw_addr, sizeof(sw_addr));

    close(sock);
    return 0;
}
