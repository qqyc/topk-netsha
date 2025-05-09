#!/usr/bin/env bash
set -e

# 1) 先删旧网和容器（若有）
docker rm -f sender switch receiver 2>/dev/null || true
docker network rm topk-net 2>/dev/null || true

# 2) 创建一个固定子网，和你代码里用的 10.10.0.0/16 对应
docker network create \
  --driver bridge \
  --subnet 10.10.0.0/16 \
  --gateway 10.10.0.1 \
  topk-net

# 3) 构建三个镜像
docker build -t topk-sender   -f Dockerfile.sender   .
docker build -t topk-switch   -f Dockerfile.switch   .
docker build -t topk-receiver -f Dockerfile.receiver .

# 4) 运行容器，并显式指定 IP + 网络别名
docker run -d --name switch \
  --network topk-net \
  --ip 10.10.0.3 \
  --network-alias switch \
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
