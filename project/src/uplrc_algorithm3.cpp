#include "uplrc_algorithm3.h"
#include "meta_definition.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>

namespace ECProject
{
  namespace uplrc_alg3
  {
    namespace
    {
      int64_t delta_bytes_block(
          const std::map<int, std::vector<std::pair<int, int>>> &block_intervals,
          int block_id)
      {
        auto it = block_intervals.find(block_id);
        if (it == block_intervals.end())
          return 0;
        int64_t s = 0;
        for (const auto &seg : it->second)
          s += static_cast<int64_t>(seg.second - seg.first);
        return s;
      }

      /** 排序用：块在条带全局逻辑地址上的最小起点 bid*block_size + min(seg.lo)。merged_delta_hull_span_bytes 只给出跨度，不给出位置。 */
      static int64_t block_global_sort_key(
          const std::map<int, std::vector<std::pair<int, int>>> &block_intervals, int block_id, int block_size)
      {
        const int64_t base = static_cast<int64_t>(block_id) * static_cast<int64_t>(block_size);
        auto it = block_intervals.find(block_id);
        if (it == block_intervals.end() || it->second.empty())
          return base;
        int lo = std::numeric_limits<int>::max();
        for (const auto &seg : it->second)
        {
          if (seg.second <= seg.first)
            continue;
          lo = std::min(lo, seg.first);
        }
        if (lo == std::numeric_limits<int>::max())
          return base;
        return base + static_cast<int64_t>(lo);
      }

      int block_cluster(const Stripe &stripe, int bid)
      {
        if (bid < 0 || bid >= static_cast<int>(stripe.blocks.size()))
          return -1;
        return stripe.blocks[bid]->map2cluster;
      }

      double transfer_sec_clu(int src_c, int dst_c, int64_t bytes,
                              const uplrc_alg2::TransferParams &tp)
      {
        if (bytes <= 0)
          return 0.0;
        double lat =
            (src_c == dst_c) ? tp.same_cluster_latency_sec : tp.cross_cluster_latency_sec;
        return lat + static_cast<double>(bytes) * tp.inv_bw_sec_per_byte;
      }

      /** DCP 最终评分：先压低最大传出时间负载，再压低块数/组数极差（均衡）。 */
      struct DcpAssignScore
      {
        double mx_time;
        int spread_blocks;
        int spread_groups;
      };

      static bool dcp_score_better(const DcpAssignScore &a, const DcpAssignScore &b)
      {
        constexpr double eps = 1e-9;
        if (std::fabs(a.mx_time - b.mx_time) > eps)
          return a.mx_time < b.mx_time;
        if (a.spread_blocks != b.spread_blocks)
          return a.spread_blocks < b.spread_blocks;
        return a.spread_groups < b.spread_groups;
      }

      static DcpAssignScore dcp_leaf_score(int R, const std::vector<double> &load,
                                           const std::vector<int> &blk_sum,
                                           const std::vector<int> &grp_cnt)
      {
        DcpAssignScore s{0.0, 0, 0};
        for (int c = 0; c < R; ++c)
          s.mx_time = std::max(s.mx_time, load[static_cast<size_t>(c)]);
        int mn_b = std::numeric_limits<int>::max();
        int mx_b = 0;
        int mn_g = std::numeric_limits<int>::max();
        int mx_g = 0;
        bool any = false;
        for (int c = 0; c < R; ++c)
        {
          if (grp_cnt[static_cast<size_t>(c)] <= 0)
            continue;
          any = true;
          mn_b = std::min(mn_b, blk_sum[static_cast<size_t>(c)]);
          mx_b = std::max(mx_b, blk_sum[static_cast<size_t>(c)]);
          mn_g = std::min(mn_g, grp_cnt[static_cast<size_t>(c)]);
          mx_g = std::max(mx_g, grp_cnt[static_cast<size_t>(c)]);
        }
        if (!any)
        {
          s.mx_time = 1e300;
          s.spread_blocks = std::numeric_limits<int>::max();
          s.spread_groups = std::numeric_limits<int>::max();
          return s;
        }
        s.spread_blocks = mx_b - mn_b;
        s.spread_groups = mx_g - mn_g;
        return s;
      }
    } // namespace

    Algorithm3Result build_algorithm3(
        const Stripe &stripe,
        const std::map<int, std::vector<std::pair<int, int>>> &block_intervals,
        const std::vector<int> &N_intersection,
        int cluster_num,
        const uplrc_alg2::TransferParams &tp)
    {
      Algorithm3Result out;
      (void)cluster_num;

      const int n = static_cast<int>(N_intersection.size());
      const int r = stripe.r;
      const int k = stripe.k;
      const int bs = static_cast<int>(stripe.blocks.empty() ? 0 : stripe.blocks[0]->block_size);
      if (n < 3 || r <= 0 || k <= 0 || bs <= 0)
      {
        out.note = "skip: need |N|>=3 and r>0";
        return out;
      }

      const int P = static_cast<int>(std::ceil(static_cast<double>(n) / 2.0));
      const int g_col = std::min(r, P);
      if (P < 1 || g_col < 1)
      {
        out.note = "skip: P<1 or g_col<1";
        return out;
      }

      out.g = g_col;
      for (int bid = k; bid < k + r; ++bid)
        out.collector_block_ids.push_back(bid);

      std::vector<int> sorted = N_intersection;
      std::sort(sorted.begin(), sorted.end(), [&](int a, int b)
                {
                  const int64_t ka = block_global_sort_key(block_intervals, a, bs);
                  const int64_t kb = block_global_sort_key(block_intervals, b, bs);
                  if (ka != kb)
                    return ka < kb;
                  return a < b;
                });
      out.sorted_block_ids = sorted;

      const int N = static_cast<int>(sorted.size());
      std::vector<std::vector<int64_t>> cost(N, std::vector<int64_t>(N, 0));
      for (int i = 0; i < N; ++i)
      {
        for (int j = i; j < N; ++j)
        {
          std::vector<int> subset(sorted.begin() + i, sorted.begin() + j + 1);
          cost[i][j] = uplrc_alg2::merged_delta_hull_span_bytes(block_intervals, subset);
        }
      }

      const int64_t INF = std::numeric_limits<int64_t>::max() / 4;
      std::vector<std::vector<int64_t>> dp(
          P + 1, std::vector<int64_t>(N, INF));
      std::vector<std::vector<int>> prev(P + 1, std::vector<int>(N, -1));

      for (int j = 0; j < N; ++j)
        dp[1][j] = cost[0][j];

      for (int gi = 2; gi <= P; ++gi)
      {
        for (int j = gi - 1; j < N; ++j)
        {
          int64_t best = INF;
          int best_p = -1;
          for (int p = gi - 2; p <= j - 1; ++p)
          {
            if (dp[gi - 1][p] >= INF)
              continue;
            int64_t v = std::max(dp[gi - 1][p], cost[p + 1][j]);
            if (v < best)
            {
              best = v;
              best_p = p;
            }
          }
          dp[gi][j] = best;
          prev[gi][j] = best_p;
        }
      }

      if (dp[P][N - 1] >= INF)
      {
        out.note = "dp infeasible";
        return out;
      }

      out.G.resize(static_cast<size_t>(P));
      int cur = N - 1;
      for (int gi = P; gi >= 2; --gi)
      {
        int p = prev[gi][cur];
        if (p < 0)
        {
          out.note = "prev invalid";
          out.G.clear();
          return out;
        }
        for (int t = p + 1; t <= cur; ++t)
          out.G[static_cast<size_t>(gi - 1)].block_ids.push_back(sorted[t]);
        cur = p;
      }
      for (int t = 0; t <= cur; ++t)
        out.G[0].block_ids.push_back(sorted[t]);

      for (auto &grp : out.G)
      {
        if (grp.block_ids.empty())
          continue;
        int lo_idx = N;
        int hi_idx = -1;
        for (int bid : grp.block_ids)
        {
          auto it = std::find(sorted.begin(), sorted.end(), bid);
          if (it == sorted.end())
            continue;
          int idx = static_cast<int>(it - sorted.begin());
          lo_idx = std::min(lo_idx, idx);
          hi_idx = std::max(hi_idx, idx);
        }
        if (lo_idx <= hi_idx)
          grp.span_bytes = cost[lo_idx][hi_idx];
        grp.sum_delta_bytes = 0;
        for (int bid : grp.block_ids)
          grp.sum_delta_bytes += delta_bytes_block(block_intervals, bid);
      }

      const int R = r;
      std::vector<std::vector<double>> t(static_cast<size_t>(P),
                                         std::vector<double>(R, 0.0));
      for (int j = 0; j < P; ++j)
      {
        int src_b = out.G[static_cast<size_t>(j)].block_ids.empty()
                        ? -1
                        : out.G[static_cast<size_t>(j)].block_ids.front();
        int sc = (src_b >= 0) ? block_cluster(stripe, src_b) : -1;
        int64_t payload = out.G[static_cast<size_t>(j)].sum_delta_bytes;
        if (payload <= 0)
          payload = out.G[static_cast<size_t>(j)].span_bytes;
        for (int c = 0; c < R; ++c)
        {
          int cb = k + c;
          int dc = block_cluster(stripe, cb);
          t[static_cast<size_t>(j)][static_cast<size_t>(c)] =
              transfer_sec_clu(sc, dc, payload, tp);
        }
      }

      std::vector<int> blkcnt_per_group(static_cast<size_t>(P));
      for (int j = 0; j < P; ++j)
        blkcnt_per_group[static_cast<size_t>(j)] =
            static_cast<int>(out.G[static_cast<size_t>(j)].block_ids.size());

      double mx_all = 0.0;
      for (int j = 0; j < P; ++j)
        for (int c = 0; c < R; ++c)
          mx_all = std::max(mx_all, t[static_cast<size_t>(j)][static_cast<size_t>(c)]);
      double hi = mx_all * static_cast<double>(P) + 1.0;
      double lo = 0.0;

      std::vector<int> assign(static_cast<size_t>(P), -1);
      std::vector<double> load(static_cast<size_t>(R), 0.0);

      auto feasible = [&](double Tlim) -> bool
      {
        std::fill(assign.begin(), assign.end(), -1);
        std::fill(load.begin(), load.end(), 0.0);
        std::vector<int> ord(static_cast<size_t>(P));
        std::iota(ord.begin(), ord.end(), 0);
        std::sort(ord.begin(), ord.end(), [&](int a, int b)
                  {
                    double ma = 0, mb = 0;
                    for (int c = 0; c < R; ++c)
                    {
                      ma = std::max(ma, t[static_cast<size_t>(a)][static_cast<size_t>(c)]);
                      mb = std::max(mb, t[static_cast<size_t>(b)][static_cast<size_t>(c)]);
                    }
                    return ma > mb;
                  });

        std::function<bool(int)> dfs = [&](int idx) -> bool
        {
          if (idx == P)
            return true;
          int j = ord[static_cast<size_t>(idx)];
          for (int c = 0; c < R; ++c)
          {
            double add = t[static_cast<size_t>(j)][static_cast<size_t>(c)];
            if (load[static_cast<size_t>(c)] + add <= Tlim + 1e-9)
            {
              load[static_cast<size_t>(c)] += add;
              assign[static_cast<size_t>(j)] = c;
              if (dfs(idx + 1))
                return true;
              load[static_cast<size_t>(c)] -= add;
              assign[static_cast<size_t>(j)] = -1;
            }
          }
          return false;
        };
        return dfs(0);
      };

      for (int bit = 0; bit < 56; ++bit)
      {
        double mid = (lo + hi) * 0.5;
        if (feasible(mid))
        {
          hi = mid;
          out.dcp.T_limit_sec = hi;
          out.dcp.group_to_collector = assign;
        }
        else
          lo = mid;
        if (hi - lo < 1e-7 * std::max(1.0, hi))
          break;
      }
      feasible(hi);
      out.dcp.T_limit_sec = hi;

      /** 在 makespan 上界 hi 下枚举可行分配，字典序最优：(max 传出时间, 块数极差, PDP 组数极差)。 */
      constexpr int k_max_exhaust_P = 12;
      constexpr int k_max_exhaust_R = 8;
      const bool exhaustive_ok = (P <= k_max_exhaust_P && R <= k_max_exhaust_R);

      if (exhaustive_ok)
      {
        std::vector<int> ord_best(static_cast<size_t>(P));
        std::iota(ord_best.begin(), ord_best.end(), 0);
        std::sort(ord_best.begin(), ord_best.end(), [&](int a, int b)
                  {
                    double ma = 0, mb = 0;
                    for (int c = 0; c < R; ++c)
                    {
                      ma = std::max(ma, t[static_cast<size_t>(a)][static_cast<size_t>(c)]);
                      mb = std::max(mb, t[static_cast<size_t>(b)][static_cast<size_t>(c)]);
                    }
                    return ma > mb;
                  });

        std::vector<int> best_assign(static_cast<size_t>(P), -1);
        DcpAssignScore best_score;
        best_score.mx_time = 1e300;
        best_score.spread_blocks = std::numeric_limits<int>::max();
        best_score.spread_groups = std::numeric_limits<int>::max();
        bool have_best = false;

        std::vector<double> cur_load(static_cast<size_t>(R), 0.0);
        std::vector<int> cur_blk(static_cast<size_t>(R), 0);
        std::vector<int> cur_grp(static_cast<size_t>(R), 0);
        std::vector<int> cur_assign(static_cast<size_t>(P), -1);

        std::function<void(int)> dfs_best = [&](int idx)
        {
          if (idx == P)
          {
            DcpAssignScore sc = dcp_leaf_score(R, cur_load, cur_blk, cur_grp);
            if (!have_best || dcp_score_better(sc, best_score))
            {
              best_score = sc;
              best_assign = cur_assign;
              have_best = true;
            }
            return;
          }
          const int j = ord_best[static_cast<size_t>(idx)];
          std::vector<int> try_c(static_cast<size_t>(R));
          std::iota(try_c.begin(), try_c.end(), 0);
          std::sort(try_c.begin(), try_c.end(), [&](int ca, int cb)
                    {
                      const double na =
                          cur_load[static_cast<size_t>(ca)] +
                          t[static_cast<size_t>(j)][static_cast<size_t>(ca)];
                      const double nb =
                          cur_load[static_cast<size_t>(cb)] +
                          t[static_cast<size_t>(j)][static_cast<size_t>(cb)];
                      if (std::fabs(na - nb) > 1e-12)
                        return na < nb;
                      if (cur_load[static_cast<size_t>(ca)] != cur_load[static_cast<size_t>(cb)])
                        return cur_load[static_cast<size_t>(ca)] < cur_load[static_cast<size_t>(cb)];
                      return ca < cb;
                    });
          for (int c : try_c)
          {
            const double add = t[static_cast<size_t>(j)][static_cast<size_t>(c)];
            if (cur_load[static_cast<size_t>(c)] + add > hi + 1e-9)
              continue;
            cur_load[static_cast<size_t>(c)] += add;
            cur_blk[static_cast<size_t>(c)] += blkcnt_per_group[static_cast<size_t>(j)];
            cur_grp[static_cast<size_t>(c)] += 1;
            cur_assign[static_cast<size_t>(j)] = c;
            dfs_best(idx + 1);
            cur_assign[static_cast<size_t>(j)] = -1;
            cur_grp[static_cast<size_t>(c)] -= 1;
            cur_blk[static_cast<size_t>(c)] -= blkcnt_per_group[static_cast<size_t>(j)];
            cur_load[static_cast<size_t>(c)] -= add;
          }
        };

        dfs_best(0);
        if (have_best)
          out.dcp.group_to_collector = std::move(best_assign);
        else
          out.dcp.group_to_collector = assign;
      }
      else
      {
        out.dcp.group_to_collector = assign;
      }

      out.applied = true;
      out.note = "ok";
      return out;
    }
  } // namespace uplrc_alg3
} // namespace ECProject
