#ifndef ENUMERABLE_COMMUTABLE_H
#define ENUMERABLE_COMMUTABLE_H
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "absl/strings/str_join.h"
#include "enumerable/enumerable.hpp"
#include "util/graph.hpp"

template <class T, class S, class C>
void clearpq(std::priority_queue<T, S, C>& q) {
  struct HackedQueue : private std::priority_queue<T, S, C> {
    static S& Container(std::priority_queue<T, S, C>& q) {
      return q.*&HackedQueue::c;
    }
  };
  HackedQueue::Container(q).clear();
}

template <typename node_t>
using CommutableItem = std::vector<node_t>;

template <typename node_t>
using CommutableNode = std::pair<CommutableItem<node_t>, std::vector<int32_t>>;

template <typename Graph, typename Aux = int32_t>
class CommutableSystem
    : public Enumerable<CommutableNode<typename Graph::node_t>,
                        CommutableItem<typename Graph::node_t>> {
 public:
  using node_t = typename Graph::node_t;
  using NodeCallback =
      typename Enumerable<CommutableNode<node_t>,
                          CommutableItem<node_t>>::NodeCallback;

  size_t MaxRoots() override { return graph_size_; }

  void GetRoot(size_t i, const NodeCallback& cb) {
    if (!IsSeed(i, nullptr)) return;
    CommutableNode<node_t> root;
    root.first.push_back(i);
    root.second.push_back(0);
    if (!Complete(root.first, root.second, nullptr, nullptr, true)) {
      cb(root);
    }
  }

  void ListChildren(const CommutableNode<node_t>& node,
                    const NodeCallback& cb) {
    Children(node.first, node.second,
             [&cb](const std::vector<node_t>& sol,
                   const std::vector<int32_t>& levels) {
               CommutableNode<node_t> node{sol, levels};
               return cb(node);
             });
  }

  CommutableItem<node_t> NodeToItem(const CommutableNode<node_t>& node) {
    return node.first;
  }

 protected:
  CommutableSystem(size_t graph_size) : graph_size_(graph_size) {}
  /**
   * Checks if a given subset is a solution.
   */
  virtual bool IsGood(const std::vector<node_t>& s) = 0;

  /**
   * Solves the restricted problem
   */
  virtual void RestrictedProblem(
      const std::vector<node_t>& s, node_t v,
      const std::function<bool(std::vector<node_t>)>& cb) = 0;

  /**
   * Checks if we can add a given element to a solution
   */
  virtual bool CanAdd(const std::vector<node_t>& s, Aux& aux, node_t v) {
    auto cnd = s;
    cnd.push_back(v);
    return IsGood(cnd);
  }

  /**
   * Returns true if the resticted problem may have at least two solutions.
   */
  virtual bool RestrMultiple() { return true; }

  /**
   * Checks if the given element can be a valid seed of a solution,
   * or a root if NULL is specified.
   */
  virtual bool IsSeed(node_t v, const std::vector<node_t>* s) {
    return IsGood({v});
  }

  /**
   * Iterates over all the possible new elements that could be added
   * because of a single new element in a solution.
   */
  virtual void CompleteCands(const std::vector<node_t>* ground_set,
                             node_t new_elem, size_t sol_size,
                             const std::function<bool(node_t)>& cb) {
    if (!ground_set) {
      for (node_t i = 0; i < graph_size_; i++) {
        if (!cb(i)) break;
      }
    } else {
      for (auto i : *ground_set) {
        if (!cb(i)) break;
      }
    }
  }
  // New version
  virtual bool CompleteCandNum(const std::vector<node_t>* ground_set,
                               node_t new_elem, size_t iterator_num, size_t idx,
                               node_t* out) {
    if (!ground_set) {
      if (idx < graph_size_) {
        *out = idx;
        return true;
      }
      return false;
    } else {
      if (idx < ground_set->size()) {
        *out = (*ground_set)[idx];
        return true;
      }
      return false;
    }
  }

  /**
   * Iterates over all the possible new elements that could be used
   * for the restricted problem
   */
  virtual void RestrictedCands(const std::vector<node_t>& s,
                               const std::vector<int32_t>& level,
                               const std::function<bool(node_t)>& cb) {
    auto ss = s;
    std::sort(ss.begin(), ss.end());
    for (node_t i = 0; i < graph_size_; i++) {
      if (std::binary_search(ss.begin(), ss.end(), i)) continue;
      if (!cb(i)) break;
    }
  }

  using CandEl = std::pair<int32_t, node_t>;
  using CandSet =
      std::priority_queue<CandEl, std::vector<CandEl>, std::greater<CandEl>>;

  virtual Aux InitAux(const std::vector<node_t>& s) { return {}; }

  virtual void UpdateAux(Aux& aux, const std::vector<node_t>& s, size_t pos) {}

  /**
   * Update candidate list when a new element is added to the solution.
   */
  virtual void UpdateStep(std::vector<node_t>& s, node_t v, int32_t level,
                          size_t sol_size, CandSet& candidates,
                          std::unordered_map<node_t, int32_t>& cand_level,
                          const std::vector<node_t>* ground_set, Aux& aux) {
    CompleteCands(ground_set, v, sol_size, [&](node_t cnd) {
      for (node_t n : s) {
        if (cnd == n) return true;
      }
      if (!CanAdd(s, aux, cnd)) return true;
      cand_level[cnd] = level + 1;
      candidates.emplace(level + 1, cnd);
      return true;
    });
  }

  /**
   * Extracts the next valid cand from candidates
   */
  virtual std::pair<node_t, int32_t> NextCand(const std::vector<node_t>& s,
                                              CandSet& candidates, Aux& aux) {
    while (!candidates.empty()) {
      auto p = candidates.top();
      candidates.pop();
      bool present = false;
      for (node_t n : s) {
        if (p.second == n) {
          present = true;
          break;
        }
      }
      if (present) continue;
      if (!CanAdd(s, aux, p.second)) continue;
      return {p.second, p.first};
    }
    return {graph_size_, -1};
  }

  /**
   * Recomputes the order and the level of the elements in s with another seed.
   */
  virtual void Resort(std::vector<node_t>& s, std::vector<int32_t>& level,
                      node_t seed) {
    // TODO: implement this to be faster.
    throw std::runtime_error("Never call this for now");
  }

  /**
   * Complete function. Returns true if there was a seed change, false otherwise
   */
  virtual bool OldComplete(std::vector<node_t>& s, std::vector<int32_t>& level,
                           bool stop_on_seed_change = false) {
    if (s.empty()) throw std::runtime_error("??");
    CandSet candidates;
    // Only needed for CanAdd
    Aux aux = InitAux(s);
    std::unordered_map<node_t, int32_t> cand_level;
    for (uint32_t i = 0; i < s.size(); i++) {
      UpdateStep(s, s[i], level[i], i, candidates, cand_level, nullptr, aux);
    }
    bool seed_change = false;
    while (true) {
      node_t n;
      int32_t l;
      std::tie(n, l) = NextCand(s, candidates, aux);
      if (n == graph_size_) break;
      unsigned pos = s.size();
      while (pos > 0 &&
             (l < level[pos - 1] || (l == level[pos - 1] && n < s[pos - 1])))
        pos--;
      s.insert(s.begin() + pos, n);
      level.insert(level.begin() + pos, l);
      if (n < s[0]) {  // Seed change
        if (stop_on_seed_change) {
          return true;
        }
        seed_change = true;
        Resort(s, level, n);
        cand_level.clear();
        clearpq(candidates);
        aux = InitAux(s);
        for (uint32_t i = 0; i < s.size(); i++) {
          UpdateStep(s, s[i], level[i], i, candidates, cand_level, nullptr,
                     aux);
        }
      } else {
        UpdateAux(aux, s, pos);
        UpdateStep(s, n, l, s.size() - 1, candidates, cand_level, nullptr, aux);
      }
    }
    return seed_change;
  }

  /**
   * Runs complete inside a given set.
   */
  virtual void CompleteInside(std::vector<node_t>& s,
                              std::vector<int32_t>& level,
                              const std::vector<node_t>& inside,
                              bool change_seed = true) {
    if (s.empty()) throw std::runtime_error("??");
    CandSet candidates;
    Aux aux = InitAux(s);
    std::unordered_map<node_t, int32_t> cand_level;
    for (uint32_t i = 0; i < s.size(); i++) {
      UpdateStep(s, s[i], level[i], i, candidates, cand_level, &inside, aux);
    }
    while (true) {
      node_t n;
      int32_t l;
      std::tie(n, l) = NextCand(s, candidates, aux);
      if (n == graph_size_) break;
      unsigned pos = s.size();
      while (pos > 0 &&
             (l < level[pos - 1] || (l == level[pos - 1] && n < s[pos - 1])))
        pos--;
      s.insert(s.begin() + pos, n);
      level.insert(level.begin() + pos, l);
      if (n < s[0] && change_seed) {  // Seed change
        Resort(s, level, n);
        cand_level.clear();
        clearpq(candidates);
        aux = InitAux(s);
        for (uint32_t i = 0; i < s.size(); i++) {
          UpdateStep(s, s[i], level[i], i, candidates, cand_level, &inside,
                     aux);
        }
      } else {
        UpdateAux(aux, s, pos);
        UpdateStep(s, n, l, s.size() - 1, candidates, cand_level, &inside, aux);
      }
    }
  }

  class Candidates {
   public:
    Candidates(CommutableSystem* commutable_system, std::vector<node_t>& s,
               Aux& aux, const std::vector<node_t>* ground_set)
        : commutable_system_(commutable_system),
          s_(s),
          aux_(aux),
          ground_set_(ground_set) {}
    bool Next(node_t* n, int32_t* lv) {
      while (!pq_.empty()) {
        auto p = pq_.top();
        pq_.pop();
        *n = std::get<1>(p);
        *lv = std::get<0>(p);
        InsertInPQ(std::get<2>(p));
        if (CanReallyAdd(*n)) return true;
      }
      return false;
    }
    void Add(node_t v, int32_t lv) {
      info_.emplace_back(0, v, lv);
      InsertInPQ(info_.size() - 1);
    }

   private:
    bool CanReallyAdd(node_t v) {
      bool present = false;
      for (node_t n : s_) {
        if (v == n) {
          present = true;
          break;
        }
      }
      if (present) return false;
      return commutable_system_->CanAdd(s_, aux_, v);
    }
    void InsertInPQ(size_t iterator_num) {
      node_t cand;
      auto& inf = info_[iterator_num];
      if (commutable_system_->CompleteCandNum(ground_set_, std::get<1>(inf),
                                              iterator_num, std::get<0>(inf)++,
                                              &cand)) {
        pq_.emplace(std::get<2>(inf) + 1, cand, iterator_num);
      }
    }
    CommutableSystem* commutable_system_;
    std::vector<node_t>& s_;
    Aux& aux_;
    const std::vector<node_t>* ground_set_;
    // level, node, iterator number
    using CandIter = std::tuple<int32_t, node_t, size_t>;
    std::priority_queue<CandIter, std::vector<CandIter>, std::greater<CandIter>>
        pq_;
    // iteration idx, owner node, owner lvl
    std::vector<std::tuple<size_t, node_t, int32_t>> info_;
  };

  /**
   * Returns false if it failed for some reason.
   * We must have s \subseteq target \subseteq ground_set.
   */
  virtual bool Complete(std::vector<node_t>& s, std::vector<int32_t>& level,
                        const std::vector<node_t>* ground_set = nullptr,
                        const std::vector<node_t>* target = nullptr,
                        bool fail_on_seed_change = false,
                        std::pair<int32_t, node_t> fail_if_smaller_than = {-1,
                                                                           0}) {
    std::function<bool(node_t)> is_in_target;
    if (target) {
      cuckoo_hash_set<node_t> target_set;
      for (node_t v : *target) {
        target_set.insert(v);
      }
      is_in_target = [target_set](node_t v) { return target_set.count(v); };
    } else {
      is_in_target = [](node_t) { return true; };
    }
    while (true) {
      Aux aux = InitAux(s);
      Candidates candidates(this, s, aux, ground_set);
      bool finished = false;
      candidates.Add(s[0], 0);
      size_t next_in_s = 1;
      while (true) {
        // Add cands_to_add
        node_t next = 0;
        int32_t next_lvl = 0;
        if (!candidates.Next(&next, &next_lvl)) {
          finished = true;
          break;
        }
        if (next_in_s >= s.size() || next != s[next_in_s]) {
          if (!is_in_target(next)) {
            return false;
          }
          if (std::pair<int32_t, node_t>{next_lvl, next} <
              fail_if_smaller_than) {
            return false;
          }
          s.push_back(next);
          level.push_back(next_lvl);
          UpdateAux(aux, s, s.size() - 1);
          // seed change
          if (next < s[0]) {
            std::swap(s.front(), s.back());
            if (fail_on_seed_change || fail_if_smaller_than.first != -1)
              return false;
            break;
          }
        } else {
          next_in_s++;
        }
        candidates.Add(next, next_lvl);
      }
      Resort(s, level, s.front());
      if (finished) return true;
    }
  }

  /**
   * Computes the prefix of the solution with a given seed and ending with v
   */
  virtual void GetPrefix(std::vector<node_t>& s, std::vector<int32_t>& level,
                         node_t seed, node_t v) {
    Resort(s, level, seed);
    std::size_t i;
    for (i = 0; i < s.size(); i++)
      if (s[i] == v) break;
    s.resize(i + 1);
    level.resize(i + 1);
  }

  virtual void ValidSeeds(const std::vector<node_t>& sol, node_t cand,
                          const std::function<bool(node_t)>& cb) {
    for (auto seed : sol) {
      if (!IsSeed(seed, &sol)) continue;
      if (cand <= seed) continue;
      if (!cb(seed)) break;
    }
  }

  /**
   * Computes the children of a given solution. Returns true if we stopped
   * generating them because the callback returned false.
   */
  virtual bool Children(
      const std::vector<node_t>& s, const std::vector<int32_t>& level,
      const std::function<bool(const std::vector<node_t>&,
                               const std::vector<int32_t>&)>& cb) {
    bool not_done = true;
    RestrictedCands(s, level, [&](node_t cand) {
      RestrictedProblem(s, cand, [&](const std::vector<node_t>& sol) {
        ValidSeeds(sol, cand, [&](node_t seed) {
          std::vector<node_t> core = sol;
          std::vector<int32_t> clvl = level;
          GetPrefix(core, clvl, seed, cand);
          std::vector<node_t> child = core;
          std::vector<int32_t> lvl = clvl;
          // Finding the solution from a wrong seed.
          node_t correct_seed = child[0];
          for (node_t n : child) {
            if (n < correct_seed) {
              correct_seed = n;
            }
          }
          if (seed != correct_seed) return true;
          // There was a seed change
          if (!Complete(child, lvl, nullptr, nullptr, true,
                        {lvl.back(), child.back()}))
            return true;
          // Parent check. NOTE: assumes things to be
          // in the correct order.
          bool starts_with_core = true;
          for (size_t i = 0; i < core.size(); i++) {
            if (core[i] != child[i]) {
              starts_with_core = false;
              break;
            }
          }
          if (!starts_with_core) return true;
          std::vector<node_t> p(core.begin(), core.end());
          std::vector<int32_t> plvl = clvl;
          p.pop_back();
          plvl.pop_back();
          if (!Complete(p, plvl, nullptr, &s)) return true;
          if (RestrMultiple()) {
            p.push_back(cand);
            if (!Complete(core, clvl, &p, &sol)) return true;
          }
          if (!cb(child, lvl)) {
            not_done = false;
          }
          return not_done;
        });
        return not_done;
      });
      return not_done;
    });
    return not_done;
  }
  size_t graph_size_;
};

#endif  // ENUMERABLE_COMMUTABLE_H
