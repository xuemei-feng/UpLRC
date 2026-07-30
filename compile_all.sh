#!/bin/bash

HOSTS_FILE="hosts"

USER="root"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
REMOTE_ROOT="${REMOTE_ROOT:-$ROOT_DIR}"
REMOTE_COMMAND="cd ${REMOTE_ROOT}/project && sh compile.sh"

PARALLEL=5

echo "Running command on all nodes..."
pdsh -R ssh -w ^$HOSTS_FILE -l $USER -f $PARALLEL "$REMOTE_COMMAND"

if [ $? -eq 0 ]; then
	echo "Command executed successfully on all nodes."
else
	echo "Failed to execute command on some nodes."
fi
