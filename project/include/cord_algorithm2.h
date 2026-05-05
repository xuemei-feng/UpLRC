#ifndef ECPROJECT_CORD_ALGORITHM2_H
#define ECPROJECT_CORD_ALGORITHM2_H

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ECProject
{
  struct Stripe;
}

namespace ECProject
{
  namespace cord_alg2
  {
    /** 传输时间模型：t = latency + bytes * inv_bw（同/跨 cluster） */
    struct TransferParams
    {
      double same_cluster_latency_sec = 1e-4;
      double cross_cluster_latency_sec = 2e-3;
      double inv_bw_sec_per_byte = 1.0 / (100.0 * 1024.0 * 1024.0); // ~100 MiB/s
    };

    enum class TrainLinkKind
    {
      STAR_DATA_TO_CENTER,
      STAR_CENTER_TO_GLOBAL,
      STAR_CENTER_TO_LOCAL,
      MST_FORWARD
    };

    struct TrainLink
    {
      int src_block_id = -1;
      int dst_block_id = -1;
      int src_cluster = -1;
      int dst_cluster = -1;
      int64_t payload_bytes = 0;
      double est_transfer_sec = 0.0;
      int group_index = -1;
      TrainLinkKind kind = TrainLinkKind::MST_FORWARD;
    };

    struct TimeslotEntry
    {
      int timeslot = 0;
      std::vector<int> link_indices;
    };

    struct Algorithm2Result
    {
      std::vector<TrainLink> train_route;
      std::vector<TimeslotEntry> timeslot_schedule;
      int slot_unit_bytes = 1;
      int center_global_block_id = -1; // 最后一组相交集中心（调试）
    };

    /**
     * 算法二：输入算法一的分组 U、条带与块内更新区间。
     * |N|>1：三层星型，全局校验中心 pop_c = argmin_c Σ_i t_{i,c}·b_i（实现为按字节量加权传输时间之和最小）。
     * |N|=1：MST(Kruskal) 于 V={d}∪{全局校验}，边权为估计传输时间，边定向为自数据块 BFS 外向。
     * 调度：将每条链路按 slot_unit_bytes 切整数时隙；每时隙在 cluster 约束下做 Dinic 最大流（每 cluster 每时隙最多 1 发、1 收，可同时收发）。
     */
    Algorithm2Result build_algorithm2(
        const Stripe &stripe,
        const std::map<int, std::vector<std::pair<int, int>>> &block_intervals,
        const std::vector<std::vector<int>> &U,
        int cluster_num,
        const TransferParams &tp,
        int slot_unit_bytes);

    std::string train_link_kind_name(TrainLinkKind k);
  } // namespace cord_alg2
} // namespace ECProject

#endif
