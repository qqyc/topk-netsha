#!/usr/bin/env bash
# gen_slots.sh — 生成无循环展开所需的 X-Macro 比较对
# 用法：./gen_slots.sh P Q
# 例如：./gen_slots.sh 4 2

if [ $# -lt 2 ]; then
  echo "Usage: $0 P Q [<K>]"
  exit 1
fi


P=$1
Q=$2
K=${3:-$(
  sed -n 's/^\s*#define\s\+K_TARGET\s\+\([0-9]\+\)/\1/p' protocol.h
)} # 若没给，就保留旧的

if [[ -z "$K" ]]; then
  echo "❌ 无法从 protocol.h 提取 K_TARGET，请检查格式"
  exit 1
fi

# slots_pq.inc: 生成 P+Q 槽的比较对 (i vs i-1)
file_pq=slots_pq.inc
cat > $file_pq <<EOF
/* 自动生成：insert_slots_pq 的比较对 */
EOF
for ((i=P+Q-1; i>0; i--)); do
  j=$((i-1))
  echo "X($i,$j)" >> $file_pq
done

# slots_p.inc: 生成 P 槽的比较对 (i vs i-1)
file_p=slots_p.inc
cat > $file_p <<EOF
/* 自动生成：insert_slots_p 的比较对 */
EOF
for ((i=P-1; i>0; i--)); do
  j=$((i-1))
  echo "X($i,$j)" >> $file_p
done

# copy_p.inc: 生成拷贝前 P 个槽的代码
cat > copy_p.inc <<EOF
/* 自动生成：拷贝前 P 个槽到 outbuf */
EOF
for ((i=0;i<P;i++)); do
  echo "X($i)" >> copy_p.inc
done

# clear_p.inc: 生成清空前 P 个槽的代码
cat > clear_p.inc <<EOF
/* 自动生成：清空前 P 个槽 */
EOF
for ((i=0;i<P;i++)); do
  echo "X($i)" >> clear_p.inc
done

# clear_pq.inc: 生成清空前 P+Q 个槽的代码
cat > clear_pq.inc <<EOF
/* 自动生成：清空前 P+Q 个槽 */
EOF
for ((i=0;i<P+Q;i++)); do
  echo "X($i)" >> clear_pq.inc
done

# final_flush.inc: 生成 Stage 2 最终 flush 逻辑
cat > final_flush.inc <<EOF
/* 自动生成：Stage 2 最终 flush 逻辑，只含各槽处理 X(i) */
EOF
for ((i=0;i<P+Q;i++)); do
  echo "X($i)" >> final_flush.inc
done

# 用 sed 就地更新 protocol.h
# 注意用 ^#define P, Q, K_TARGET 这样能精确匹配行首
sed -i "s@^#define P .*@#define P             $P@" protocol.h
sed -i "s@^#define Q .*@#define Q             $Q@" protocol.h
sed -i "s@^#define K_TARGET .*@#define K_TARGET      $K@" protocol.h

echo "✅ Generated $file_pq and $file_p for P=$P, Q=$Q"
