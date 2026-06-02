// SPDX-License-Identifier: Apache-2.0
//
// Physical hardware topology (coupling map) for the routing heuristic.
//
// The coupling map is an undirected sparse graph. We store it as a flat
// adjacency list of `int` indices — no hash maps, no sets, no shared_ptr
// — so that adjacency queries and neighbor iteration are branch-light
// cache walks. Typical superconducting hardware has degree <= 6, so the
// O(deg) connectivity check is effectively O(1).

#ifndef MQSS_ROUTER_ARCHITECTURE_HPP
#define MQSS_ROUTER_ARCHITECTURE_HPP

#include <cstddef>
#include <utility>
#include <vector>

namespace mqss {

class CouplingMap {
public:
    CouplingMap() = default;

    /// Construct an empty topology with `num_physical_qubits` nodes and
    /// zero edges.
    explicit CouplingMap(int num_physical_qubits);

    /// Construct and populate in one shot from an edge list. Edges are
    /// undirected; duplicates and self-loops are ignored.
    CouplingMap(int num_physical_qubits,
                const std::vector<std::pair<int, int>>& edges);

    /// Add an undirected edge `(p1, p2)`. Throws `std::out_of_range` if
    /// either endpoint is outside `[0, num_physical_qubits)`. Self-loops
    /// and duplicate edges are silently dropped.
    void add_edge(int p1, int p2);

    /// O(deg(p1)) adjacency check. `noexcept` and bounds-safe: out-of-range
    /// indices return `false` rather than throwing, so this can be called
    /// freely from hot inner loops.
    [[nodiscard]] bool is_physically_connected(int p1, int p2) const noexcept;

    [[nodiscard]] int num_physical_qubits() const noexcept { return num_qubits_; }

    [[nodiscard]] const std::vector<int>& neighbors(int p) const noexcept {
        return adj_list_[static_cast<std::size_t>(p)];
    }

    [[nodiscard]] const std::vector<std::vector<int>>& adj_list() const noexcept {
        return adj_list_;
    }

private:
    int                            num_qubits_ = 0;
    std::vector<std::vector<int>>  adj_list_;
};

} // namespace mqss

#endif // MQSS_ROUTER_ARCHITECTURE_HPP
