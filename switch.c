#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <inttypes.h>

static topk_pkt_t slots[P+Q];
static topk_pkt_t outbuf[P];

static uint64_t total_recv_pkts = 0;   // 用于流量统计
static uint64_t total_fwd_pkts  = 0;
static uint32_t sent            = 0;
static uint32_t temp_T          = END_VALUE;
static uint32_t T_final         = END_VALUE;
static int stage                = 1;
static int cnt_stage2           = 0;

// 手动展开冒泡插入到 slots[0..5] 保持升序
static void insert_slots_pq(topk_pkt_t pkt) {
    slots[5] = pkt;
    if (slots[5].value < slots[4].value) { topk_pkt_t t=slots[5]; slots[5]=slots[4]; slots[4]=t; }
    if (slots[4].value < slots[3].value) { topk_pkt_t t=slots[4]; slots[4]=slots[3]; slots[3]=t; }
    if (slots[3].value < slots[2].value) { topk_pkt_t t=slots[3]; slots[3]=slots[2]; slots[2]=t; }
    if (slots[2].value < slots[1].value) { topk_pkt_t t=slots[2]; slots[2]=slots[1]; slots[1]=t; }
    if (slots[1].value < slots[0].value) { topk_pkt_t t=slots[1]; slots[1]=slots[0]; slots[0]=t; }
}

// 手动展开冒泡插入到 slots[0..3] 保持升序
static void insert_slots_p(topk_pkt_t pkt) {
    slots[3] = pkt;
    if (slots[3].value < slots[2].value) { topk_pkt_t t=slots[3]; slots[3]=slots[2]; slots[2]=t; }
    if (slots[2].value < slots[1].value) { topk_pkt_t t=slots[2]; slots[2]=slots[1]; slots[1]=t; }
    if (slots[1].value < slots[0].value) { topk_pkt_t t=slots[1]; slots[1]=slots[0]; slots[0]=t; }
}

// 批量转发 & 清空前 P 槽
static void forward_and_clear(int sock, struct sockaddr_in *rcv) {
    outbuf[0] = slots[0];
    outbuf[1] = slots[1];
    outbuf[2] = slots[2];
    outbuf[3] = slots[3];
    temp_T = outbuf[3].value;

    printf("[SWITCH] 发出第 %u 批, 阈值=%u, 累计发包=%u\n",
           sent / P + 1, temp_T, sent + P);

    sendto(sock, outbuf, sizeof(outbuf), 0,
           (struct sockaddr*)rcv, sizeof(*rcv));
    sent += P;
    total_fwd_pkts += P;

    // 清空前 P 个槽
    slots[0].value = slots[1].value =
    slots[2].value = slots[3].value = END_VALUE;
}

int main() {
    setvbuf(stdout, NULL, _IOLBF, 0);

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
    for (int i = 0; i < P+Q; i++) slots[i].value = END_VALUE;

    while (1) {
        topk_pkt_t pkt;
        recv(rsock, &pkt, sizeof(pkt), 0);
        if (pkt.value != END_VALUE) {
            total_recv_pkts++;
        }
        if (stage == 1) {
            // 结束条件：END 包 或 达到 K_TARGET
            if (pkt.value == END_VALUE || sent >= K_TARGET) {
                T_final = temp_T;
                stage = 2;
                // 清空所有槽
                for (int i = 0; i < P+Q; i++) slots[i].value = END_VALUE;
                if (pkt.value == END_VALUE) continue;
            }

            if (stage == 1) {
                // 插入 P+Q 槽
                insert_slots_pq(pkt);
                // 若前 P 槽满，批量转发
                if (slots[3].value != END_VALUE) {
                    forward_and_clear(ssock, &rcv);
                }
            }

        } else { // stage == 2
            if (pkt.value == END_VALUE) {
                // 最后一批：检查所有槽中 < T_final 的，并发出
                int outcnt = 0;
                #define TRY_SEND(i) \
                  if (slots[i].value < T_final) outbuf[outcnt++] = slots[i]; \
                  if (outcnt == P) { \
                    sendto(ssock, outbuf, sizeof(outbuf),0,(struct sockaddr*)&rcv,sizeof(rcv)); \
                    total_fwd_pkts += P; \
                    outcnt = 0; \
                  }
                TRY_SEND(0); TRY_SEND(1); TRY_SEND(2);
                TRY_SEND(3); TRY_SEND(4); TRY_SEND(5);
                if (outcnt > 0) {
                    sendto(ssock, outbuf, sizeof(outbuf),0,(struct sockaddr*)&rcv,sizeof(rcv));
                    total_fwd_pkts += outcnt;
                }
                break;
            }
            // 普通包：小于 T_final 才插入 P 槽
            if (pkt.value < T_final) {
                insert_slots_p(pkt);
                cnt_stage2++;
                if (cnt_stage2 == P) {
                    sendto(ssock, outbuf, sizeof(outbuf),0,(struct sockaddr*)&rcv,sizeof(rcv));
                    total_fwd_pkts += P;
                    slots[0].value = slots[1].value =
                    slots[2].value = slots[3].value = END_VALUE;
                    cnt_stage2 = 0;
                }
            }
        }
    }

    // 打印流量削减统计
    fprintf(stderr,
        "[SWITCH-STAT] 接收包=%" PRIu64
        ", 转发包=%" PRIu64
        ", 削减比例=%.2f%%\n",
        total_recv_pkts,
        total_fwd_pkts,
        100.0 * total_fwd_pkts / total_recv_pkts);

    close(rsock);
    close(ssock);
    return 0;
}
