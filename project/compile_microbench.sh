#!/bin/bash
# Compile microbench.cpp using the project's standalone asio headers.
# Usage: ./compile_microbench.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ASIO_INCLUDE="${SCRIPT_DIR}/third_party/asio/include"

g++ -std=c++17 \
    -DASIO_STANDALONE \
    -O3 \
    -Wall \
    -funroll-loops \
    -fdata-sections -ffunction-sections \
    -march=native \
    -I"${ASIO_INCLUDE}" \
    -o microbench \
    microbench.cpp \
    -lpthread

echo "[OK] microbench compiled successfully."
echo "Usage examples:"
echo "  Server: ./microbench -s 0.0.0.0 8888 1048576"
echo "  Client: ./microbench -c <server_ip> 8888 1048576 10000"
