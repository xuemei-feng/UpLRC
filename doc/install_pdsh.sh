#!/bin/bash

HOSTS_FILE="hosts"

USER="root"

REMOTE_COMMAND="sudo apt install -y pdsh"

PARALLEL=50

echo "Running command on all nodes..."
pdsh -R ssh -w ^$HOSTS_FILE -l $USER -f $PARALLEL "$REMOTE_COMMAND"

if [ $? -eq 0 ]; then
	echo "Command executed successfully on all nodes."
else
	echo "Failed to execute command on some nodes."
fi
