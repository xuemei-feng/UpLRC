#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_FILE="${SCRIPT_DIR}/small_tools/generator_sh.py"
HOSTS_FILE="${SCRIPT_DIR}/hosts"

if [[ ! -f "$HOSTS_FILE" ]]; then
  echo "Error: hosts file not found: $HOSTS_FILE" >&2
  exit 1
fi

# 读取 hosts 文件中的主机列表
HOSTS=$(cat "$HOSTS_FILE")

# 使用 scp 复制文件到所有主机
echo "Copying $SOURCE_FILE to all hosts..."
for HOST in $HOSTS; do
  echo "Copying to $HOST..."
  scp "$SOURCE_FILE" "${HOST}:/users/xue/xue/small_tools/"
  if [ $? -eq 0 ]; then
    echo "Successfully copied to $HOST!"
  else
    echo "Failed to copy to $HOST!"
    exit 1
  fi
done

# pdsh -R ssh 不会展开远程命令里的 %h/%n，需显式传入 LOCAL_IP
PARALLEL=10
USER="root"
REMOTE_DIR="/users/xue/xue/small_tools"

echo "Running generator_sh.py on all hosts (parallel=${PARALLEL})..."
if xargs -P "$PARALLEL" -I{} ssh -o ConnectTimeout=15 "${USER}@{}" \
  "LOCAL_IP={} cd ${REMOTE_DIR} && python generator_sh.py" \
  < "$HOSTS_FILE"; then
  echo "Successfully ran generator_sh.py on all hosts!"
else
  echo "Failed to run generator_sh.py on some hosts!" >&2
  exit 1
fi

echo "All done!"