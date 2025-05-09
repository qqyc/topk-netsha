#!/bin/bash
# 查看日志，实时输出结果
puts "查看switch日志，实时输出结果"
docker logs -f switch
puts "查看receiver日志，实时输出结果"
docker logs -f receiver