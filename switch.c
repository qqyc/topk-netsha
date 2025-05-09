#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static topk_pkt_t slots[P+Q];
static topk_pkt_t outbuf[P];
static uint32_t sent = 0, temp_T = END_VALUE, T_final = END_VALUE;
static int stage = 1, cnt_stage2 = 0;

// 手动展开的冒泡式插入到 slots[0..n-1] 保持升序
// 这里 n = 6 (P+Q) 或 4 (P)
static void insert_slots_pq(topk_pkt_t pkt) {
    // 把 pkt 放到最末
    slots[5] = pkt;
    // 从 i=5 向前冒泡到 i=1
    if (slots[5].value < slots[4].value) { topk_pkt_t t=slots[5]; slots[5]=slots[4]; slots[4]=t; }
    if (slots[4].value < slots[3].value) { topk_pkt_t t=slots[4]; slots[4]=slots[3]; slots[3]=t; }
    if (slots[3].value < slots[2].value) { topk_pkt_t t=slots[3]; slots[3]=slots[2]; slots[2]=t; }
    if (slots[2].value < slots[1].value) { topk_pkt_t t=slots[2]; slots[2]=slots[1]; slots[1]=t; }
    if (slots[1].value < slots[0].value) { topk_pkt_t t=slots[1]; slots[1]=slots[0]; slots[0]=t; }
}

static void insert_slots_p(topk_pkt_t pkt) {
    slots[3] = pkt;
    if (slots[3].value < slots[2].value) { topk_pkt_t t=slots[3]; slots[3]=slots[2]; slots[2]=t; }
    if (slots[2].value < slots[1].value) { topk_pkt_t t=slots[2]; slots[2]=slots[1]; slots[1]=t; }
    if (slots[1].value < slots[0].value) { topk_pkt_t t=slots[1]; slots[1]=slots[0]; slots[0]=t; }
}

// 手动展开的批量转发 & 清空前 P 槽
static void forward_and_clear(int sock, struct sockaddr_in *rcv) {
    // 拷贝
    outbuf[0] = slots[0];
    outbuf[1] = slots[1];
    outbuf[2] = slots[2];
    outbuf[3] = slots[3];
    // 更新临时阈值
    temp_T = outbuf[3].value;
    // 发送
    // 顺便打印
    printf("[SWITCH] 发出第 %u 批, 阈值=%u, 累计发包=%u\n",
           sent / P + 1, temp_T, sent + P);
    
    sendto(sock, outbuf, sizeof(outbuf), 0, (struct sockaddr*)rcv, sizeof(*rcv));
    sent += P;
    // 清空 slots[0..3]
    slots[0].value = END_VALUE;
    slots[1].value = END_VALUE;
    slots[2].value = END_VALUE;
    slots[3].value = END_VALUE;

}

int main() {
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rsock = socket(AF_INET, SOCK_DGRAM, 0),
        ssock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sw_in = {
        .sin_family = AF_INET,
        .sin_port   = htons(SWITCH_PORT),
        .sin_addr.s_addr = INADDR_ANY
    }, rcv = {
        .sin_family = AF_INET,
        .sin_port   = htons(RECEIVER_PORT),
        .sin_addr.s_addr = inet_addr("10.10.0.4")  // recv 容器 IP
    };
    bind(rsock, (void*)&sw_in, sizeof(sw_in));

    // 初始化所有 slots
    slots[0].value = slots[1].value = slots[2].value =
    slots[3].value = slots[4].value = slots[5].value = END_VALUE;

    while (1) {
        topk_pkt_t pkt;
        recv(rsock, &pkt, sizeof(pkt), 0);

        if (stage == 1) {
            if (pkt.value == END_VALUE) {
                // 收到 END，结束阶段1
                T_final = temp_T;
                stage = 2;
                // 清空所有槽，进入阈值过滤
                slots[0].value = slots[1].value = slots[2].value =
                slots[3].value = slots[4].value = slots[5].value = END_VALUE;
                continue;
            }
            // 插入到 P+Q 槽
            insert_slots_pq(pkt);
            // 若前 P 槽满（slots[3] != END_VALUE），批量转发
            if (slots[3].value != END_VALUE) {
                forward_and_clear(ssock, &rcv);
            }

        } else { // stage == 2
            if (pkt.value == END_VALUE) {
                // 最后一批：检查 slots[0..5] 中 < T_final 的，发送并退出
                int outcnt = 0;
                // 手动展开检查
                if (slots[0].value < T_final) outbuf[outcnt++] = slots[0];
                if (outcnt == P) { sendto(ssock, outbuf, sizeof(outbuf),0,(struct sockaddr*)&rcv,sizeof(rcv)); outcnt=0; }
                if (slots[1].value < T_final) outbuf[outcnt++] = slots[1];
                if (outcnt == P) { sendto(ssock, outbuf, sizeof(outbuf),0,(struct sockaddr*)&rcv,sizeof(rcv)); outcnt=0; }
                if (slots[2].value < T_final) outbuf[outcnt++] = slots[2];
                if (outcnt == P) { sendto(ssock, outbuf, sizeof(outbuf),0,(struct sockaddr*)&rcv,sizeof(rcv)); outcnt=0; }
                if (slots[3].value < T_final) outbuf[outcnt++] = slots[3];
                if (outcnt == P) { sendto(ssock, outbuf, sizeof(outbuf),0,(struct sockaddr*)&rcv,sizeof(rcv)); outcnt=0; }
                if (slots[4].value < T_final) outbuf[outcnt++] = slots[4];
                if (outcnt == P) { sendto(ssock, outbuf, sizeof(outbuf),0,(struct sockaddr*)&rcv,sizeof(rcv)); outcnt=0; }
                if (slots[5].value < T_final) outbuf[outcnt++] = slots[5];
                if (outcnt > 0) sendto(ssock, outbuf, sizeof(outbuf),0,(struct sockaddr*)&rcv,sizeof(rcv));
                break;
            }
            // 普通包：小于 T_final 才插入 P 槽
            if (pkt.value < T_final) {
                insert_slots_p(pkt);
                cnt_stage2++;
                if (cnt_stage2 == P) {
                    sendto(ssock, outbuf, sizeof(outbuf),0,(struct sockaddr*)&rcv,sizeof(rcv));
                    // 清空 P 槽
                    slots[0].value = slots[1].value =
                    slots[2].value = slots[3].value = END_VALUE;
                    cnt_stage2 = 0;
                }
            }
        }
    }

    close(rsock);
    close(ssock);
    return 0;
}
