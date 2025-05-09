#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>
#include <stdlib.h>
#include <inttypes.h>

static topk_pkt_t slots[P+Q];
static topk_pkt_t outbuf[P];

static uint64_t total_recv_pkts = 0;
static uint64_t total_fwd_pkts  = 0;
static uint32_t sent            = 0;
static uint32_t temp_T          = END_VALUE;
static uint32_t T_final         = END_VALUE;
static int stage                = 1;
static int cnt_stage2           = 0;

static char *mode;

// 插入到 slots[0..P+Q-1]，保持升序
static void insert_slots_pq(topk_pkt_t pkt) {
    slots[P+Q-1] = pkt;
  #define X(i,j) \
    if (slots[i].value < slots[j].value) { \
      topk_pkt_t t = slots[i]; slots[i] = slots[j]; slots[j] = t; \
    }
  #include "slots_pq.inc"
  #undef X
}

// 插入到 slots[0..P-1]，保持升序
static void insert_slots_p(topk_pkt_t pkt) {
    slots[P-1] = pkt;
  #define X(i,j) \
    if (slots[i].value < slots[j].value) { \
      topk_pkt_t t = slots[i]; slots[i] = slots[j]; slots[j] = t; \
    }
  #include "slots_p.inc"
  #undef X
}

// 批量转发 & 清空前 P 槽
static void forward_and_clear(int sock, struct sockaddr_in *rcv) {
    // 1) 拷贝 slots -> outbuf
    #define X(i) outbuf[i] = slots[i];
    #include "copy_p.inc"
    #undef X
    temp_T = outbuf[P-1].value;
    // 2) 发送
    sendto(sock, outbuf, sizeof(outbuf), 0,
           (struct sockaddr*)rcv, sizeof(*rcv));
    sent += P;
    total_fwd_pkts += P;
    // 2) 清空 slots
    #define X(i) slots[i].value = END_VALUE;
    #include "clear_p.inc"
    #undef X
}

int main() {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    mode = getenv("MODE");
    if (!mode) mode = "aggr";

    int rsock = socket(AF_INET, SOCK_DGRAM, 0),
        ssock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sw_in = {
        .sin_family      = AF_INET,
        .sin_port        = htons(SWITCH_PORT),
        .sin_addr.s_addr = INADDR_ANY
    }, rcv = {
        .sin_family      = AF_INET,
        .sin_port        = htons(RECEIVER_PORT),
        .sin_addr.s_addr = inet_addr("10.10.0.4")
    };
    bind(rsock, (void*)&sw_in, sizeof(sw_in));

    // 初始化所有 slots
    #define X(i) slots[i].value = END_VALUE;
    #include "clear_pq.inc"
    #undef X

    while (1) {
        // Bypass 模式：直通前 K_TARGET 个非 END 包后退出
        if (strcmp(mode, "bypass") == 0) {
            topk_pkt_t pkt;
            if (recv(rsock, &pkt, sizeof(pkt), 0) <= 0) {
                continue;
            }
            if (pkt.value != END_VALUE) {
                total_recv_pkts++;
                sendto(ssock, &pkt, sizeof(pkt), 0,
                       (struct sockaddr*)&rcv, sizeof(rcv));
                total_fwd_pkts++;
                if (total_fwd_pkts >= K_TARGET) {
                    break;
                }
            }
            continue;
        }

        // 聚合模式：Stage 1 的超时切换
        if (stage == 1) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(rsock, &rfds);
            struct timeval tv = {1, 0};  // 1s 超时
            int ret = select(rsock+1, &rfds, NULL, NULL, &tv);
            if (ret == 0) {
                // 超时，无 END 包，强制进入 Stage 2
                T_final = temp_T;
                fprintf(stderr,"[SWITCH] 超时切换到 Stage 2\n");
                fprintf(stderr,"[SWITCH] 当前阈值 T_final=%u\n", T_final);
                stage = 2;
                // 清空 slots[0..P+Q-1]
                #define X(i) slots[i].value = END_VALUE;
                #include "clear_pq.inc"
                #undef X
                continue;
            }
            if (ret < 0 && errno != EINTR) {
                perror("select");
                break;
            }
        }

        topk_pkt_t pkt;
        if (recv(rsock, &pkt, sizeof(pkt), 0) <= 0) {
            continue;
        }
        if (pkt.value != END_VALUE) {
            total_recv_pkts++;
        }

        if (stage == 1) {
            // 收到 END 或已满 K_TARGET 时切换
            if (pkt.value == END_VALUE || sent >= K_TARGET) {
                T_final = temp_T;
                stage = 2;
                #define X(i) slots[i].value = END_VALUE;
                #include "clear_pq.inc"
                #undef X
                if (pkt.value == END_VALUE) {
                    continue;
                }
            }
            // Sort-Reduce 阶段
            insert_slots_pq(pkt);
            if (slots[P-1].value != END_VALUE) {
                forward_and_clear(ssock, &rcv);
            }

        } else {
            // 阈值过滤阶段
            if (pkt.value == END_VALUE) {
                // 最后一批
                int outcnt = 0;
                #define X(i) \
                  if (slots[i].value < T_final) { outbuf[outcnt++] = slots[i]; } \
                  if (outcnt == P) { \
                    sendto(ssock, outbuf, sizeof(outbuf), 0, (struct sockaddr*)&rcv, sizeof(rcv)); \
                    total_fwd_pkts += P; \
                    outcnt = 0; \
                  }
                #include "final_flush.inc"
                #undef X
                if (outcnt > 0) {
                    sendto(ssock, outbuf, outcnt*sizeof(topk_pkt_t), 0, (struct sockaddr*)&rcv, sizeof(rcv));
                    total_fwd_pkts += outcnt;
                }
                break;
            }
            if (pkt.value < T_final) {
                insert_slots_p(pkt);
                cnt_stage2++;
                if (cnt_stage2 == P) {
                    // 1) 拷贝 slots -> outbuf
                    #define X(i) outbuf[i] = slots[i];
                    #include "copy_p.inc"
                    #undef X
                  
                    // 2) 发送
                    sendto(ssock, outbuf, sizeof(outbuf), 0,
                           (struct sockaddr*)&rcv, sizeof(rcv));
                    total_fwd_pkts += P;
                  
                    // 3) 清空 slots
                    #define X(i) slots[i].value = END_VALUE;
                    #include "clear_p.inc"
                    #undef X
                  
                    cnt_stage2 = 0;
                  }                  
            }
        }
    }

    // 最终统计
    fprintf(stderr,
        "[SWITCH-STAT] 接收包=%" PRIu64
        ", 转发包=%" PRIu64
        ", 削减比例=%.2f%%\n",
        total_recv_pkts,
        total_fwd_pkts,
        100.0 * (1.0 - (double)total_fwd_pkts / total_recv_pkts));
    
    fprintf(stderr,"[SWITCH] 最终阈值 T_final=%u\n", T_final);

    close(rsock);
    close(ssock);
    return 0;
}
