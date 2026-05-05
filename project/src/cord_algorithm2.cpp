#include "cord_algorithm2.h"
#include "meta_definition.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace ECProject
{
  namespace cord_alg2
  {
    namespace
    {
      int64_t delta_bytes_for_block(
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

      int block_cluster(const Stripe &stripe, int bid)
      {
        if (bid < 0 || bid >= static_cast<int>(stripe.blocks.size()))
          return -1;
        return stripe.blocks[bid]->map2cluster;
      }

      double transfer_sec(int src_c, int dst_c, int64_t bytes, const TransferParams &tp)
      {
        if (bytes <= 0)
          return 0.0;
        double lat = (src_c == dst_c) ? tp.same_cluster_latency_sec : tp.cross_cluster_latency_sec;
        return lat + static_cast<double>(bytes) * tp.inv_bw_sec_per_byte;
      }

      struct DSU
      {
        std::vector<int> p;
        explicit DSU(int n) : p(n)
        {
          for (int i = 0; i < n; ++i)
            p[i] = i;
        }
        int find(int x)
        {
          return p[x] == x ? x : (p[x] = find(p[x]));
        }
        bool unite(int a, int b)
        {
          a = find(a);
          b = find(b);
          if (a == b)
            return false;
          p[a] = b;
          return true;
        }
      };

      struct KruskalEdge
      {
        int u, v;
        double w;
        bool operator<(KruskalEdge const &o) const { return w < o.w; }
      };

      // ---------- Dinic（S→L_i cap1，L_i→R_j 来自链路，R_j→T cap1）----------
      struct FlowEdge
      {
        int to, rev, cap;
        int lid;
      };

      struct Dinic
      {
        int n, s, t;
        std::vector<std::vector<FlowEdge>> g;
        std::vector<int> level, it;
        Dinic(int n_, int s_, int t_) : n(n_), s(s_), t(t_), g(n_) {}

        void add_edge(int fr, int to, int cap, int lid)
        {
          int ga = static_cast<int>(g[fr].size());
          int gb = static_cast<int>(g[to].size());
          g[fr].push_back({to, gb, cap, lid});
          g[to].push_back({fr, ga, 0, -1});
        }

        bool bfs()
        {
          level.assign(n, -1);
          std::queue<int> q;
          level[s] = 0;
          q.push(s);
          while (!q.empty())
          {
            int v = q.front();
            q.pop();
            for (const FlowEdge &e : g[v])
            {
              if (e.cap > 0 && level[e.to] < 0)
              {
                level[e.to] = level[v] + 1;
                q.push(e.to);
              }
            }
          }
          return level[t] >= 0;
        }

        int dfs(int v, int f, std::vector<int> &itv)
        {
          if (v == t)
            return f;
          for (int &i = itv[v]; i < static_cast<int>(g[v].size()); ++i)
          {
            FlowEdge &e = g[v][i];
            if (e.cap > 0 && level[v] < level[e.to])
            {
              int d = dfs(e.to, std::min(f, e.cap), itv);
              if (d > 0)
              {
                e.cap -= d;
                g[e.to][e.rev].cap += d;
                return d;
              }
            }
          }
          return 0;
        }

        int maxflow()
        {
          int flow = 0, inf = 1e9;
          while (bfs())
          {
            it.assign(n, 0);
            int f;
            while ((f = dfs(s, inf, it)) > 0)
              flow += f;
          }
          return flow;
        }

        // 读出 L→R 上前向边（原 cap=1）上是否流过 1 单位：看反向边 cap
        void collect_used_links(int C, std::vector<int> &out_link_ids)
        {
          for (int c = 0; c < C; ++c)
          {
            int L = 1 + c;
            for (const FlowEdge &e : g[L])
            {
              if (e.lid < 0)
                continue;
              // 初始前向 cap=1，流 1 则前向 cap=0，反向 cap=1
              FlowEdge const &rev = g[e.to][e.rev];
              if (rev.cap > 0)
                out_link_ids.push_back(e.lid);
            }
          }
        }
      };

      void orient_mst_from_root(
          const std::vector<std::pair<int, int>> &mst_undirected,
          int root,
          int n_nodes,
          std::vector<std::pair<int, int>> *out_directed)
      {
        std::vector<std::vector<int>> adj(n_nodes);
        for (auto e : mst_undirected)
        {
          adj[e.first].push_back(e.second);
          adj[e.second].push_back(e.first);
        }
        std::vector<int> parent(n_nodes, -2);
        std::queue<int> q;
        parent[root] = -1;
        q.push(root);
        while (!q.empty())
        {
          int u = q.front();
          q.pop();
          for (int v : adj[u])
          {
            if (parent[v] == -2)
            {
              parent[v] = u;
              q.push(v);
            }
          }
        }
        for (int v = 0; v < n_nodes; ++v)
        {
          if (v == root)
            continue;
          int p = parent[v];
          if (p < 0)
            continue;
          // 由靠近根一侧指向外侧：p 更近根 => p -> v
          out_directed->push_back({p, v});
        }
      }

    } // namespace

    std::string train_link_kind_name(TrainLinkKind k)
    {
      switch (k)
      {
      case TrainLinkKind::STAR_DATA_TO_CENTER:
        return "STAR_DATA_TO_CENTER";
      case TrainLinkKind::STAR_CENTER_TO_GLOBAL:
        return "STAR_CENTER_TO_GLOBAL";
      case TrainLinkKind::STAR_CENTER_TO_LOCAL:
        return "STAR_CENTER_TO_LOCAL";
      default:
        return "MST_FORWARD";
      }
    }

    Algorithm2Result build_algorithm2(
        const Stripe &stripe,
        const std::map<int, std::vector<std::pair<int, int>>> &block_intervals,
        const std::vector<std::vector<int>> &U,
        int cluster_num,
        const TransferParams &tp,
        int slot_unit_bytes)
    {
      Algorithm2Result out;
      out.slot_unit_bytes = std::max(1, slot_unit_bytes);
      const int k = stripe.k;
      const int r = stripe.r;
      if (cluster_num <= 0 || k <= 0)
        return out;

      int gi = 0;
      for (const std::vector<int> &N : U)
      {
        if (N.empty())
        {
          ++gi;
          continue;
        }
        if (r <= 0)
        {
          ++gi;
          continue;
        }

        if (static_cast<int>(N.size()) > 1)
        {
          // ---------- 相交集：星型 + 传输时间最优全局校验中心 ----------
          int best_c = k;
          double best_cost = std::numeric_limits<double>::infinity();
          for (int cand = k; cand < k + r; ++cand)
          {
            int cc = block_cluster(stripe, cand);
            if (cc < 0)
              continue;
            double sum = 0.0;
            for (int d : N)
            {
              int64_t b = delta_bytes_for_block(block_intervals, d);
              if (b <= 0)
                continue;
              int dc = block_cluster(stripe, d);
              if (dc < 0)
                continue;
              sum += transfer_sec(dc, cc, b, tp);
            }
            if (sum < best_cost)
            {
              best_cost = sum;
              best_c = cand;
            }
          }
          out.center_global_block_id = best_c;

          int64_t max_b = 0;
          for (int d : N)
            max_b = std::max(max_b, delta_bytes_for_block(block_intervals, d));
          if (max_b <= 0)
          {
            ++gi;
            continue;
          }

          int cc = block_cluster(stripe, best_c);
          for (int d : N)
          {
            int64_t b = delta_bytes_for_block(block_intervals, d);
            if (b <= 0)
              continue;
            int dc = block_cluster(stripe, d);
            TrainLink L;
            L.src_block_id = d;
            L.dst_block_id = best_c;
            L.src_cluster = dc;
            L.dst_cluster = cc;
            L.payload_bytes = b;
            L.est_transfer_sec = transfer_sec(dc, cc, b, tp);
            L.group_index = gi;
            L.kind = TrainLinkKind::STAR_DATA_TO_CENTER;
            out.train_route.push_back(std::move(L));
          }
          for (int g = k; g < k + r; ++g)
          {
            if (g == best_c)
              continue;
            int gc = block_cluster(stripe, g);
            TrainLink L;
            L.src_block_id = best_c;
            L.dst_block_id = g;
            L.src_cluster = cc;
            L.dst_cluster = gc;
            L.payload_bytes = max_b;
            L.est_transfer_sec = transfer_sec(cc, gc, max_b, tp);
            L.group_index = gi;
            L.kind = TrainLinkKind::STAR_CENTER_TO_GLOBAL;
            out.train_route.push_back(std::move(L));
          }
          // 中心向各受影响数据组对应的局校验块转发（与论文「再写入局校验」一致）
          std::vector<int> groups_touched;
          for (int d : N)
          {
            int gnum = stripe.blocks[d]->map2group;
            if (std::find(groups_touched.begin(), groups_touched.end(), gnum) == groups_touched.end())
              groups_touched.push_back(gnum);
          }
          for (int gnum : groups_touched)
          {
            int Lb = -1;
            for (int i = k + r; i < stripe.n; ++i)
            {
              if (stripe.blocks[i]->map2group == gnum)
              {
                Lb = i;
                break;
              }
            }
            if (Lb < 0)
              continue;
            int lc = block_cluster(stripe, Lb);
            TrainLink L;
            L.src_block_id = best_c;
            L.dst_block_id = Lb;
            L.src_cluster = cc;
            L.dst_cluster = lc;
            L.payload_bytes = max_b;
            L.est_transfer_sec = transfer_sec(cc, lc, max_b, tp);
            L.group_index = gi;
            L.kind = TrainLinkKind::STAR_CENTER_TO_LOCAL;
            out.train_route.push_back(std::move(L));
          }
        }
        else
        {
          // ---------- 不相交集：MST ----------
          int d = N[0];
          int64_t bd = delta_bytes_for_block(block_intervals, d);
          if (bd <= 0)
          {
            ++gi;
            continue;
          }
          int numV = 1 + r;
          std::vector<int> vid(numV);
          vid[0] = d;
          for (int j = 0; j < r; ++j)
            vid[1 + j] = k + j;

          std::vector<KruskalEdge> edges;
          edges.reserve(static_cast<size_t>(numV * (numV - 1) / 2));
          for (int i = 0; i < numV; ++i)
          {
            for (int j = i + 1; j < numV; ++j)
            {
              int bi = vid[i], bj = vid[j];
              int ci = block_cluster(stripe, bi);
              int cj = block_cluster(stripe, bj);
              double w;
              if (i == 0 || j == 0)
              {
                int dcl = (i == 0) ? ci : cj;
                int gcl = (i == 0) ? cj : ci;
                w = transfer_sec(dcl, gcl, bd, tp);
              }
              else
              {
                w = transfer_sec(ci, cj, bd, tp);
              }
              edges.push_back({i, j, w});
            }
          }
          std::sort(edges.begin(), edges.end());
          DSU dsu(numV);
          std::vector<std::pair<int, int>> mst;
          for (const auto &e : edges)
          {
            if (dsu.unite(e.u, e.v))
              mst.push_back({e.u, e.v});
          }
          std::vector<std::pair<int, int>> directed;
          orient_mst_from_root(mst, 0, numV, &directed);
          for (auto pr : directed)
          {
            int sb = vid[pr.first];
            int db = vid[pr.second];
            int sc = block_cluster(stripe, sb);
            int dc = block_cluster(stripe, db);
            TrainLink L;
            L.src_block_id = sb;
            L.dst_block_id = db;
            L.src_cluster = sc;
            L.dst_cluster = dc;
            L.payload_bytes = bd;
            L.est_transfer_sec = transfer_sec(sc, dc, bd, tp);
            L.group_index = gi;
            L.kind = TrainLinkKind::MST_FORWARD;
            out.train_route.push_back(std::move(L));
          }
        }
        ++gi;
      }

      // ---------- 最大流时隙调度 ----------
      const int C = cluster_num;
      const int S = 0;
      const int T = 2 + 2 * C;
      const int nV = T + 1;

      std::vector<int> remaining;
      remaining.reserve(out.train_route.size());
      for (const auto &L : out.train_route)
      {
        int slots = static_cast<int>(
            std::ceil(static_cast<double>(L.payload_bytes) / static_cast<double>(out.slot_unit_bytes)));
        remaining.push_back(std::max(1, slots));
      }

      int ts = 0;
      while (true)
      {
        bool any = false;
        for (int x : remaining)
        {
          if (x > 0)
          {
            any = true;
            break;
          }
        }
        if (!any)
          break;

        Dinic din(nV, S, T);
        for (int c = 0; c < C; ++c)
          din.add_edge(S, 1 + c, 1, -1);
        for (int c = 0; c < C; ++c)
          din.add_edge(1 + C + c, T, 1, -1);

        for (size_t i = 0; i < out.train_route.size(); ++i)
        {
          if (remaining[i] <= 0)
            continue;
          const TrainLink &L = out.train_route[i];
          if (L.src_cluster < 0 || L.dst_cluster < 0 ||
              L.src_cluster >= C || L.dst_cluster >= C)
            continue;
          int Lu = 1 + L.src_cluster;
          int Rv = 1 + C + L.dst_cluster;
          din.add_edge(Lu, Rv, 1, static_cast<int>(i));
        }

        din.maxflow();
        std::vector<int> used;
        din.collect_used_links(C, used);
        if (used.empty())
        {
          // 无匹配（不应发生）；强制推进一条剩余链路避免死循环
          for (size_t i = 0; i < remaining.size(); ++i)
          {
            if (remaining[i] > 0)
            {
              used.push_back(static_cast<int>(i));
              break;
            }
          }
        }
        TimeslotEntry te;
        te.timeslot = ts++;
        te.link_indices = std::move(used);
        for (int id : te.link_indices)
        {
          if (id >= 0 && id < static_cast<int>(remaining.size()) && remaining[id] > 0)
            remaining[id]--;
        }
        out.timeslot_schedule.push_back(std::move(te));
      }

      return out;
    }
  } // namespace cord_alg2
} // namespace ECProject
