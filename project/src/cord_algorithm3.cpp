#include "cord_algorithm3.h"
#include "meta_definition.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace ECProject
{
  namespace cord_alg3
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

      void block_logical_extents(
          const std::map<int, std::vector<std::pair<int, int>>> &block_intervals,
          int block_id,
          int block_size,
          int64_t *out_min_L,
          int64_t *out_max_R_excl)
      {
        auto it = block_intervals.find(block_id);
        if (it == block_intervals.end() || it->second.empty())
        {
          *out_min_L = 0;
          *out_max_R_excl = 0;
          return;
        }
        int64_t mn = std::numeric_limits<int64_t>::max();
        int64_t mx = 0;
        const int64_t base = static_cast<int64_t>(block_id) * static_cast<int64_t>(block_size);
        for (const auto &seg : it->second)
        {
          mn = std::min(mn, base + static_cast<int64_t>(seg.first));
          mx = std::max(mx, base + static_cast<int64_t>(seg.second));
        }
        *out_min_L = mn;
        *out_max_R_excl = mx;
      }

      int block_cluster(const Stripe &stripe, int bid)
      {
        if (bid < 0 || bid >= static_cast<int>(stripe.blocks.size()))
          return -1;
        return stripe.blocks[bid]->map2cluster;
      }

      double transfer_sec_clu(int src_c, int dst_c, int64_t bytes,
                              const cord_alg2::TransferParams &tp)
      {
        if (bytes <= 0)
          return 0.0;
        double lat =
            (src_c == dst_c) ? tp.same_cluster_latency_sec : tp.cross_cluster_latency_sec;
        return lat + static_cast<double>(bytes) * tp.inv_bw_sec_per_byte;
      }
    } // namespace

    Algorithm3Result build_algorithm3(
        const Stripe &stripe,
        const std::map<int, std::vector<std::pair<int, int>>> &block_intervals,
        const std::vector<int> &N_intersection,
        int cluster_num,
        const cord_alg2::TransferParams &tp)
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

      const int g = std::min(r, static_cast<int>(std::ceil(static_cast<double>(n) / 2.0)));
      if (g < 1)
      {
        out.note = "skip: g<1";
        return out;
      }

      out.g = g;
      for (int bid = k; bid < k + r; ++bid)
        out.collector_block_ids.push_back(bid);

      std::vector<int> sorted = N_intersection;
      std::sort(sorted.begin(), sorted.end(), [&](int a, int b)
                {
                  int64_t la, ra, lb, rb;
                  block_logical_extents(block_intervals, a, bs, &la, &ra);
                  block_logical_extents(block_intervals, b, bs, &lb, &rb);
                  if (la != lb)
                    return la < lb;
                  return a < b;
                });
      out.sorted_block_ids = sorted;

      const int N = static_cast<int>(sorted.size());
      std::vector<int64_t> minL(N), maxR(N);
      for (int i = 0; i < N; ++i)
        block_logical_extents(block_intervals, sorted[i], bs, &minL[i], &maxR[i]);

      std::vector<std::vector<int64_t>> cost(
          N, std::vector<int64_t>(N, 0));
      for (int i = 0; i < N; ++i)
      {
        int64_t cur_min = minL[i];
        int64_t cur_max = maxR[i];
        for (int j = i; j < N; ++j)
        {
          cur_min = std::min(cur_min, minL[j]);
          cur_max = std::max(cur_max, maxR[j]);
          cost[i][j] = cur_max - cur_min;
        }
      }

      const int64_t INF = std::numeric_limits<int64_t>::max() / 4;
      std::vector<std::vector<int64_t>> dp(
          g + 1, std::vector<int64_t>(N, INF));
      std::vector<std::vector<int>> prev(g + 1, std::vector<int>(N, -1));

      for (int j = 0; j < N; ++j)
        dp[1][j] = cost[0][j];

      for (int gi = 2; gi <= g; ++gi)
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

      if (dp[g][N - 1] >= INF)
      {
        out.note = "dp infeasible";
        return out;
      }

      out.G.resize(static_cast<size_t>(g));
      int cur = N - 1;
      for (int gi = g; gi >= 2; --gi)
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
      std::vector<std::vector<double>> t(static_cast<size_t>(g),
                                         std::vector<double>(R, 0.0));
      for (int j = 0; j < g; ++j)
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

      double mx_all = 0.0;
      for (int j = 0; j < g; ++j)
        for (int c = 0; c < R; ++c)
          mx_all = std::max(mx_all, t[static_cast<size_t>(j)][static_cast<size_t>(c)]);
      double hi = mx_all * static_cast<double>(g) + 1.0;
      double lo = 0.0;

      std::vector<int> assign(static_cast<size_t>(g), -1);
      std::vector<double> load(static_cast<size_t>(R), 0.0);

      auto feasible = [&](double Tlim) -> bool
      {
        std::fill(assign.begin(), assign.end(), -1);
        std::fill(load.begin(), load.end(), 0.0);
        std::vector<int> ord(static_cast<size_t>(g));
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
          if (idx == g)
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
      out.dcp.group_to_collector = assign;

      out.applied = true;
      out.note = "ok";
      return out;
    }
  } // namespace cord_alg3
} // namespace ECProject
