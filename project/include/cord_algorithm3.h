#ifndef ECPROJECT_CORD_ALGORITHM3_H
#define ECPROJECT_CORD_ALGORITHM3_H

#include "cord_algorithm2.h"
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace ECProject
{
  struct Stripe;
}

namespace ECProject
{
  namespace cord_alg3
  {
    /** PDP 输出的一组：条带内逻辑地址尽量连续 */
    struct PdpGroup
    {
      std::vector<int> block_ids;
      int64_t span_bytes = 0;
      int64_t sum_delta_bytes = 0;
    };

    /** DCP：每组映射到的全局校验块（收集器）下标 0..r-1 */
    struct DcpAssignment
    {
      double T_limit_sec = 0.0;
      std::vector<int> group_to_collector;
    };

    struct Algorithm3Result
    {
      bool applied = false;
      std::string note;
      std::vector<int> sorted_block_ids;
      int g = 0;
      std::vector<int> collector_block_ids;
      std::vector<PdpGroup> G;
      DcpAssignment dcp;
    };

    /**
     * 算法 3（不改变算法 1 的输入/分组定义；本函数仅消费 |N|≥3 的交集组）。
     * PDP：按条带逻辑起始地址排序；cost[i][j]=覆盖块 i..j 的最小连续字节跨度；DP 最小化「各组 span 的最大值」。
     * g = min(r, ceil(|N|/2))，C 为全部全局校验块。
     * DCP：二分完成时间 T，检验是否存在分配使每组选一个收集器且各收集器上总传输时间 ≤ T（回溯）。
     */
    Algorithm3Result build_algorithm3(
        const Stripe &stripe,
        const std::map<int, std::vector<std::pair<int, int>>> &block_intervals,
        const std::vector<int> &N_intersection,
        int cluster_num,
        const cord_alg2::TransferParams &tp);
  } // namespace cord_alg3
} // namespace ECProject

#endif
