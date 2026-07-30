#!/bin/bash

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
REMOTE_ROOT="${REMOTE_ROOT:-$ROOT_DIR}"
HOSTS_FILE="${HOSTS_FILE:-hosts}"
USER="root"

REMOTE_COMMAND="
cd ${REMOTE_ROOT} && \
git clone https://github.com/magnific0/wondershaper.git && \
cd wondershaper && \
sudo make install
"
PARALLEL=5

echo "Running command on all nodes..."
pdsh -R ssh -w ^$HOSTS_FILE -l $USER -f $PARALLEL "$REMOTE_COMMAND"

if [ $? -eq 0 ]; then
	echo "Command executed successfully on all nodes."
else
	echo "Failed to execute command on some nodes."
fi
