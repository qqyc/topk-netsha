#!/usr/bin/env bash
set -e
if [ $# -lt 2 ]; then
  echo "Usage: $0 P Q [<K>]"
  exit 1
fi

P=$1
Q=$2
K=${3:-$(grep '#define K_TARGET' protocol.h | awk '{print $3}')}  # 若没给，就保留旧的

./gen_slots.sh $P $Q $K

# 1) 清理旧资源
docker rm -f sender switch receiver 2>/dev/null || true
docker network rm topk-net 2>/dev/null || true

# 2) 创建 Docker 网络
docker network create \
  --driver bridge \
  --subnet 10.10.0.0/16 \
  --gateway 10.10.0.1 \
  topk-net

# 3) 构建镜像
docker build -t topk-sender   -f Dockerfile.sender   .
docker build -t topk-switch   -f Dockerfile.switch   .
docker build -t topk-receiver -f Dockerfile.receiver .

# 4) 运行容器（默认聚合模式 aggr；如需直通 baseline，可改为 -e MODE=bypass）
docker run -d --name switch \
  --network topk-net \
  --ip 10.10.0.3 \
  --network-alias switch \
  -e MODE=aggr \
  topk-switch

docker run -d --name receiver \
  --network topk-net \
  --ip 10.10.0.4 \
  --network-alias receiver \
  topk-receiver

docker run -d --name sender \
  --network topk-net \
  --ip 10.10.0.2 \
  --network-alias sender \
  topk-sender

echo "部署完成！容器状态："
docker ps --filter "name=sender|switch|receiver" --format "table {{.Names}}\t{{.Networks}}"
