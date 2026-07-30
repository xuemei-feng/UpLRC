#ifndef DATANODE_H
#define DATANODE_H

#include "datanode.grpc.pb.h"
#include <grpc++/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <asio.hpp>
#include <array>
#include <atomic>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "meta_definition.h"
#include "config.h"
// #define IF_DEBUG true
#define IF_DEBUG false
namespace ECProject
{
    class DatanodeImpl final
        : public datanode_proto::datanodeService::Service
    {
    public:
        explicit DatanodeImpl(std::string datanode_ip_port);
        ~DatanodeImpl();
        grpc::Status checkalive(
            grpc::ServerContext *context,
            const datanode_proto::CheckaliveCMD *request,
            datanode_proto::RequestResult *response) override;
        // set
        grpc::Status handleSet(
            grpc::ServerContext *context,
            const datanode_proto::SetInfo *set_info,
            datanode_proto::RequestResult *response) override;
        // append
        grpc::Status handleAppend(
            grpc::ServerContext *context,
            const datanode_proto::AppendInfo *append_info,
            datanode_proto::RequestResult *response) override;
        // merge parity
        grpc::Status handleMergeParity(
            grpc::ServerContext *context,
            const datanode_proto::MergeParityInfo *merge_parity_info,
            datanode_proto::RequestResult *response) override;
        // merge parity with rep
        /*grpc::Status handleMergeParityWithRep(
            grpc::ServerContext *context,
            const datanode_proto::MergeParityInfo *merge_parity_info,
            datanode_proto::RequestResult *response) override;*/
        // recovery
        grpc::Status handleRecovery(
            grpc::ServerContext *context,
            const datanode_proto::MergeParityInfo *recovery_info,
            datanode_proto::RequestResult *response) override;
        grpc::Status handleRecoveryBreakdown(
            grpc::ServerContext *context,
            const datanode_proto::MergeParityInfo *recovery_info,
            datanode_proto::RequestResult *response) override;
        // get
        grpc::Status handleGet(
            grpc::ServerContext *context,
            const datanode_proto::GetInfo *get_info,
            datanode_proto::RequestResult *response) override;
        grpc::Status handleGetBreakdown(
            grpc::ServerContext *context,
            const datanode_proto::GetInfo *get_info,
            datanode_proto::RequestResult *response) override;
        grpc::Status handleUpLRCRangeRead(
            grpc::ServerContext *context,
            const datanode_proto::UpLRCRangeRWInfo *info,
            datanode_proto::RequestResult *response) override;
        grpc::Status handleUpLRCRangeWrite(
            grpc::ServerContext *context,
            const datanode_proto::UpLRCRangeRWInfo *info,
            datanode_proto::RequestResult *response) override;
        grpc::Status handleUpLRCRangeXorWrite(
            grpc::ServerContext *context,
            const datanode_proto::UpLRCRangeRWInfo *info,
            datanode_proto::RequestResult *response) override;
        grpc::Status handleUpLRCDeltaBlob(
            grpc::ServerContext *context,
            const datanode_proto::UpLRCDeltaBlobInfo *info,
            datanode_proto::RequestResult *response) override;
        // delete
        grpc::Status handleDelete(
            grpc::ServerContext *context,
            const datanode_proto::DelInfo *del_info,
            datanode_proto::RequestResult *response) override;

        void serialize(const std::string &filename, const ParitySlice &slice);
        std::vector<ParitySlice> deserialize(const std::string &filename);
        void deserialize(const std::string &filename, char *buf);
        bool createDirectories(const std::string &path);
        ECProject::Config *m_sys_config;

    private:
        enum class DnConnWaitKind
        {
            PlainRead,
            PlainWrite
        };

        struct UpLRCDnPendingRead
        {
            std::vector<char> data;
            int range_length = 0;
        };
        struct UpLRCDnPendingWrite
        {
            std::string writepath;
            int range_offset = 0;
            int range_length = 0;
        };
        struct UpLRCDnPendingBlob
        {
            std::string writepath;
            int byte_length = 0;
        };

        enum class UpLRCDnDispatchOp
        {
            Read,
            Write,
            XorWrite,
            Blob
        };

        struct UpLRCDnDispatchJob
        {
            UpLRCDnDispatchOp op = UpLRCDnDispatchOp::Read;
            UpLRCDnPendingRead read;
            UpLRCDnPendingWrite write;
            UpLRCDnPendingBlob blob;
        };

        struct DnDeliveredSocket
        {
            asio::ip::tcp::socket socket;
            std::array<char, 8> prefix{};
            size_t prefix_len = 0;

            explicit DnDeliveredSocket(asio::io_context &io_context)
                : socket(io_context)
            {
            }
        };

        void dn_start_accept_loop();
        void dn_accept_dispatch_loop();
        DnDeliveredSocket dn_wait_for_connection(DnConnWaitKind kind);
        bool dn_take_uplrc_pending(uint64_t wire_tag, UpLRCDnDispatchJob &job);
        void dn_dispatch_uplrc_job(asio::ip::tcp::socket socket, uint64_t wire_tag, UpLRCDnDispatchJob job);
        void dn_run_uplrc_read_worker(asio::ip::tcp::socket socket, uint64_t wire_tag, UpLRCDnPendingRead pending);
        void dn_run_uplrc_write_worker(asio::ip::tcp::socket socket, uint64_t wire_tag, UpLRCDnPendingWrite pending);
        void dn_run_uplrc_xor_write_worker(asio::ip::tcp::socket socket, uint64_t wire_tag, UpLRCDnPendingWrite pending);
        void dn_run_uplrc_blob_worker(asio::ip::tcp::socket socket, uint64_t wire_tag, UpLRCDnPendingBlob pending);
        static asio::error_code dn_tcp_read_with_prefix(DnDeliveredSocket &delivered, char *out, size_t total);

        std::string datanode_ip_port;
        std::string m_ip;
        int m_port;
        int m_block_size;
        int m_download_port;
        asio::io_context io_context;
        asio::ip::tcp::acceptor acceptor;

        std::thread m_dn_accept_thread;
        std::atomic<bool> m_dn_accept_running{false};
        std::mutex m_dn_conn_wait_mu;
        std::deque<std::pair<DnConnWaitKind, std::shared_ptr<std::promise<DnDeliveredSocket>>>> m_dn_conn_waiters;

        std::mutex m_uplrc_pending_mu;
        std::map<uint64_t, UpLRCDnPendingRead> m_uplrc_pending_reads;
        std::map<uint64_t, UpLRCDnPendingWrite> m_uplrc_pending_writes;
        std::map<uint64_t, UpLRCDnPendingWrite> m_uplrc_pending_xor_writes;
        std::map<uint64_t, UpLRCDnPendingBlob> m_uplrc_pending_blobs;
    };

    class DataNode
    {
    public:
        DataNode(std::string datanode_ip_port) : datanode_ip_port(datanode_ip_port), m_datanodeImpl_ptr(datanode_ip_port) {}
        DataNode(std::string datanode_ip_port, std::string sys_config_path) : datanode_ip_port(datanode_ip_port), m_datanodeImpl_ptr(datanode_ip_port)
        {
            m_datanodeImpl_ptr.m_sys_config = ECProject::Config::getInstance(sys_config_path);
        }

        void Run()
        {
            grpc::EnableDefaultHealthCheckService(true);
            grpc::reflection::InitProtoReflectionServerBuilderPlugin();
            grpc::ServerBuilder builder;
            std::cout << "datanode_ip_port:" << datanode_ip_port << std::endl;
            builder.AddListeningPort(datanode_ip_port, grpc::InsecureServerCredentials());
            builder.RegisterService(&m_datanodeImpl_ptr);
            std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
            server->Wait();
        }

    private:
        std::string datanode_ip_port;
        ECProject::DatanodeImpl m_datanodeImpl_ptr;
    };
}

#endif