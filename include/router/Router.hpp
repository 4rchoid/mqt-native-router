// SPDX-License-Identifier: Apache-2.0
//
// MQT-inspired greedy SWAP-insertion router.
//
// The router maintains a dynamic permutation between logical and physical
// qubits as a pair of flat `std::vector<int>` index tables (no hash maps,
// no smart pointers in the hot path). All-pairs shortest-path distances
// across the coupling graph are pre-computed once via Floyd-Warshall, so
// each candidate-SWAP evaluation is an O(1) array lookup against a
// `std::vector<std::vector<int>>` distance matrix.
//
// For every CNOT whose endpoints are not physically adjacent, a greedy
// pass enumerates the SWAPs incident to the two endpoints, scores each
// by (post-SWAP distance + lookahead penalty over upcoming CNOTs), and
// commits the best strictly-descending candidate. The pass iterates
// until the endpoints become adjacent, at which point the native CNOT
// is emitted against the *current* physical layout.

#ifndef MQSS_ROUTER_ROUTER_HPP
#define MQSS_ROUTER_ROUTER_HPP

#include "router/Architecture.hpp"
#include "router/Instruction.hpp"

#include <cstddef>
#include <vector>

namespace mqss {

class MQTInspiredRouter {
public:
    /// Construct a router for the given hardware `topology`. Pre-computes
    /// the all-pairs distance matrix and installs the trivial identity
    /// layout (logical i -> physical i).
    ///
    /// `lookahead_depth` controls how many subsequent CNOT gates the
    /// heuristic looks at when tie-breaking candidate SWAPs. Set to 0 to
    /// disable lookahead and fall back to pure greedy descent.
    explicit MQTInspiredRouter(const CouplingMap& topology,
                               int lookahead_depth = 8);

    /// Reset to the identity mapping: logical_to_physical_[i] = i.
    void reset_trivial_layout() noexcept;

    /// Install an explicit initial mapping. Throws on size mismatch,
    /// out-of-range physical indices, or duplicate assignments.
    void set_initial_layout(std::vector<int> logical_to_physical);

    /// Run the routing pass and return the physical instruction stream.
    /// The input list is not mutated. The router's layout *is* mutated,
    /// reflecting the final permutation after all SWAPs.
    [[nodiscard]] std::vector<QuantumInstruction>
    route_circuit(const std::vector<QuantumInstruction>& logical_circuit);

    [[nodiscard]] const std::vector<int>& logical_to_physical() const noexcept {
        return logical_to_physical_;
    }

    [[nodiscard]] const std::vector<int>& physical_to_logical() const noexcept {
        return physical_to_logical_;
    }

    /// O(1) shortest-path distance lookup against the pre-computed matrix.
    [[nodiscard]] int distance(int p1, int p2) const noexcept {
        return distance_matrix_[static_cast<std::size_t>(p1)]
                               [static_cast<std::size_t>(p2)];
    }

    [[nodiscard]] const std::vector<std::vector<int>>&
    distance_matrix() const noexcept {
        return distance_matrix_;
    }

    [[nodiscard]] int num_physical_qubits() const noexcept { return num_qubits_; }

private:
    /// Floyd-Warshall: O(n^3) one-shot during construction. The matrix
    /// uses a large-but-non-overflowing INF sentinel so that additions
    /// stay within `int` range.
    void build_distance_matrix_();

    /// Apply an in-place physical SWAP and update both index tables.
    /// `noexcept` and branchless on the hot path.
    void apply_physical_swap_(int p1, int p2) noexcept;

    /// Greedy + lookahead SWAP selection for the CNOT currently at
    /// `circuit[cursor]` whose endpoints map to physical qubits
    /// (phys_ctrl, phys_tgt). Returns the chosen swap as an encoded
    /// `lo * num_qubits_ + hi` value (lo < hi), or -1 if no candidate
    /// strictly decreases the current distance.
    int select_best_swap_(int phys_ctrl, int phys_tgt,
                          const std::vector<QuantumInstruction>& circuit,
                          std::size_t cursor);

    /// Sum of decayed distances of the next few CNOTs under the *current*
    /// layout. Pure read-only; used as a tie-breaker between candidate
    /// SWAPs that produce the same primary cost.
    double lookahead_cost_(const std::vector<QuantumInstruction>& circuit,
                           std::size_t cursor) const noexcept;

    const CouplingMap&            topology_;
    int                           num_qubits_;
    int                           lookahead_depth_;
    std::vector<std::vector<int>> distance_matrix_;
    std::vector<int>              logical_to_physical_;
    std::vector<int>              physical_to_logical_;
};

} // namespace mqss

#endif // MQSS_ROUTER_ROUTER_HPP
