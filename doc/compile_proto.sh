#!/bin/bash
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
PROTOC="${ROOT_DIR}/project/third_party/grpc/bin/protoc"
PLUGIN="${ROOT_DIR}/project/third_party/grpc/bin/grpc_cpp_plugin"
PROTO_DIR="${ROOT_DIR}/project/src/proto"
cd "$PROTO_DIR"
"$PROTOC" --proto_path=. --grpc_out=. --plugin=protoc-gen-grpc="$PLUGIN" coordinator.proto
"$PROTOC" --proto_path=. --cpp_out=. coordinator.proto
"$PROTOC" --proto_path=. --grpc_out=. --plugin=protoc-gen-grpc="$PLUGIN" proxy.proto
"$PROTOC" --proto_path=. --cpp_out=. proxy.proto
"$PROTOC" --proto_path=. --grpc_out=. --plugin=protoc-gen-grpc="$PLUGIN" datanode.proto
"$PROTOC" --proto_path=. --cpp_out=. datanode.proto
