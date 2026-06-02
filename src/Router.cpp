// SPDX-License-Identifier: Apache-2.0

#include "router/Router.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mqss {

namespace {

/// Sentinel "infinity" for the distance matrix. Chosen so that
/// `kInf + kInf` still fits comfortably in a signed `int`, removing
/// overflow risk from the Floyd-Warshall relaxation step.
constexpr int    kInf              = std::numeric_limits<int>::max() / 4;

/// Geometric decay applied to successive future-gate distances when
/// computing the lookahead score. Mirrors the SABRE / MQT extended-set
/// heuristic.
constexpr double kLookaheadDecay   = 0.5;

/// Relative weight of the lookahead penalty against the primary
/// (current-CNOT) distance. Stay < 1 to keep greedy descent dominant.
constexpr double kLookaheadWeight  = 0.5;

} // namespace

MQTInspiredRouter::MQTInspiredRouter(const CouplingMap& topology,
                                     int lookahead_depth)
    : topology_(topology),
      num_qubits_(topology.num_physical_qubits()),
      lookahead_depth_(lookahead_depth),
      distance_matrix_(),
      logical_to_physical_(),
      physical_to_logical_() {
    if (num_qubits_ <= 0) {
        throw std::invalid_argument(
            "MQTInspiredRouter: topology has no physical qubits");
    }
    if (lookahead_depth_ < 0) {
        throw std::invalid_argument(
            "MQTInspiredRouter: lookahead_depth must be non-negative");
    }
    build_distance_matrix_();
    reset_trivial_layout();
}

void MQTInspiredRouter::reset_trivial_layout() noexcept {
    const std::size_t n = static_cast<std::size_t>(num_qubits_);
    logical_to_physical_.assign(n, 0);
    physical_to_logical_.assign(n, 0);
    for (int i = 0; i < num_qubits_; ++i) {
        const std::size_t ui = static_cast<std::size_t>(i);
        logical_to_physical_[ui] = i;
        physical_to_logical_[ui] = i;
    }
}

void MQTInspiredRouter::set_initial_layout(std::vector<int> l2p) {
    if (static_cast<int>(l2p.size()) != num_qubits_) {
        throw std::invalid_argument(
            "set_initial_layout: size mismatch with physical qubit count");
    }
    logical_to_physical_ = std::move(l2p);
    physical_to_logical_.assign(static_cast<std::size_t>(num_qubits_), -1);
    for (int l = 0; l < num_qubits_; ++l) {
        const int p = logical_to_physical_[static_cast<std::size_t>(l)];
        if (p < 0 || p >= num_qubits_) {
            throw std::invalid_argument(
                "set_initial_layout: physical index out of range");
        }
        if (physical_to_logical_[static_cast<std::size_t>(p)] != -1) {
            throw std::invalid_argument(
                "set_initial_layout: duplicate physical qubit assignment");
        }
        physical_to_logical_[static_cast<std::size_t>(p)] = l;
    }
}

void MQTInspiredRouter::build_distance_matrix_() {
    const std::size_t n = static_cast<std::size_t>(num_qubits_);
    distance_matrix_.assign(n, std::vector<int>(n, kInf));

    for (std::size_t i = 0; i < n; ++i) {
        distance_matrix_[i][i] = 0;
    }

    const auto& adj = topology_.adj_list();
    for (std::size_t u = 0; u < n; ++u) {
        for (const int v : adj[u]) {
            distance_matrix_[u][static_cast<std::size_t>(v)] = 1;
        }
    }

    // Classical Floyd-Warshall triple loop. The `dik >= kInf` short-circuit
    // skips dense regions of unreachable intermediate vertices and keeps
    // the inner loop straight-line.
    for (std::size_t k = 0; k < n; ++k) {
        const auto& row_k = distance_matrix_[k];
        for (std::size_t i = 0; i < n; ++i) {
            const int dik = distance_matrix_[i][k];
            if (dik >= kInf) {
                continue;
            }
            auto& row_i = distance_matrix_[i];
            for (std::size_t j = 0; j < n; ++j) {
                const int alt = dik + row_k[j];
                if (alt < row_i[j]) {
                    row_i[j] = alt;
                }
            }
        }
    }
}

void MQTInspiredRouter::apply_physical_swap_(int p1, int p2) noexcept {
    const std::size_t up1 = static_cast<std::size_t>(p1);
    const std::size_t up2 = static_cast<std::size_t>(p2);

    const int l1 = physical_to_logical_[up1];
    const int l2 = physical_to_logical_[up2];

    if (l1 != -1) {
        logical_to_physical_[static_cast<std::size_t>(l1)] = p2;
    }
    if (l2 != -1) {
        logical_to_physical_[static_cast<std::size_t>(l2)] = p1;
    }
    physical_to_logical_[up1] = l2;
    physical_to_logical_[up2] = l1;
}

double MQTInspiredRouter::lookahead_cost_(
    const std::vector<QuantumInstruction>& circuit,
    std::size_t cursor) const noexcept {
    if (lookahead_depth_ == 0) {
        return 0.0;
    }
    double total   = 0.0;
    double weight  = 1.0;
    int    counted = 0;
    for (std::size_t i = cursor; i < circuit.size() && counted < lookahead_depth_;
         ++i) {
        const auto& g = circuit[i];
        if (g.type != OpType::CNOT) {
            continue;
        }
        const int pc = logical_to_physical_[static_cast<std::size_t>(g.control)];
        const int pt = logical_to_physical_[static_cast<std::size_t>(g.target)];
        const int d  = distance_matrix_[static_cast<std::size_t>(pc)]
                                       [static_cast<std::size_t>(pt)];
        total  += weight * static_cast<double>(d);
        weight *= kLookaheadDecay;
        ++counted;
    }
    return total;
}

int MQTInspiredRouter::select_best_swap_(
    int phys_ctrl, int phys_tgt,
    const std::vector<QuantumInstruction>& circuit,
    std::size_t cursor) {
    const int curr_d = distance_matrix_[static_cast<std::size_t>(phys_ctrl)]
                                       [static_cast<std::size_t>(phys_tgt)];

    int    best_encoded = -1;
    double best_score   = std::numeric_limits<double>::infinity();

    // Evaluate one candidate SWAP(a, b). Mutates layout, scores, restores.
    // The mutate-restore pattern avoids allocating a shadow layout copy
    // per candidate — the layout vectors are touched O(1) times per
    // evaluation regardless of qubit count.
    const auto evaluate = [&](int a, int b) {
        // Post-swap physical positions of the *current* CNOT endpoints.
        const int new_pc = (phys_ctrl == a) ? b
                         : (phys_ctrl == b) ? a
                         : phys_ctrl;
        const int new_pt = (phys_tgt  == a) ? b
                         : (phys_tgt  == b) ? a
                         : phys_tgt;
        const int new_d  = distance_matrix_[static_cast<std::size_t>(new_pc)]
                                           [static_cast<std::size_t>(new_pt)];
        if (new_d >= curr_d) {
            // Strict descent only — guarantees termination of the outer
            // while-loop in route_circuit().
            return;
        }

        apply_physical_swap_(a, b);
        const double look = lookahead_cost_(circuit, cursor + 1);
        apply_physical_swap_(a, b); // restore

        const double score = static_cast<double>(new_d) +
                             kLookaheadWeight * look;
        if (score < best_score) {
            best_score = score;
            const int lo = (a < b) ? a : b;
            const int hi = (a < b) ? b : a;
            best_encoded = lo * num_qubits_ + hi;
        }
    };

    for (const int n : topology_.neighbors(phys_ctrl)) {
        evaluate(phys_ctrl, n);
    }
    for (const int n : topology_.neighbors(phys_tgt)) {
        evaluate(phys_tgt, n);
    }
    return best_encoded;
}

std::vector<QuantumInstruction> MQTInspiredRouter::route_circuit(
    const std::vector<QuantumInstruction>& logical_circuit) {
    std::vector<QuantumInstruction> physical;
    // Heuristic over-reserve: most CNOTs need ~1 SWAP on sparse hardware.
    physical.reserve(logical_circuit.size() * 2);

    for (std::size_t idx = 0; idx < logical_circuit.size(); ++idx) {
        const auto& gate = logical_circuit[idx];

        switch (gate.type) {
            case OpType::SINGLE: {
                const int pt =
                    logical_to_physical_[static_cast<std::size_t>(gate.target)];
                physical.emplace_back(OpType::SINGLE, pt);
                break;
            }

            case OpType::SWAP: {
                // User-supplied logical SWAP: realize it as a physical SWAP
                // on whatever physical pair currently holds the two
                // logical operands, then commute the layout.
                const int pa =
                    logical_to_physical_[static_cast<std::size_t>(gate.control)];
                const int pb =
                    logical_to_physical_[static_cast<std::size_t>(gate.target)];
                if (!topology_.is_physically_connected(pa, pb)) {
                    throw std::runtime_error(
                        "Router: input contains a SWAP across non-adjacent "
                        "physical qubits — decompose first");
                }
                physical.emplace_back(OpType::SWAP, pb, pa);
                apply_physical_swap_(pa, pb);
                break;
            }

            case OpType::CNOT: {
                int pc =
                    logical_to_physical_[static_cast<std::size_t>(gate.control)];
                int pt =
                    logical_to_physical_[static_cast<std::size_t>(gate.target)];

                while (!topology_.is_physically_connected(pc, pt)) {
                    const int enc = select_best_swap_(pc, pt,
                                                      logical_circuit, idx);
                    if (enc < 0) {
                        throw std::runtime_error(
                            "Router: no descending SWAP candidate found — "
                            "topology may be disconnected");
                    }
                    const int a = enc / num_qubits_;
                    const int b = enc % num_qubits_;

                    apply_physical_swap_(a, b);
                    physical.emplace_back(OpType::SWAP, b, a);

                    pc = logical_to_physical_
                             [static_cast<std::size_t>(gate.control)];
                    pt = logical_to_physical_
                             [static_cast<std::size_t>(gate.target)];
                }

                physical.emplace_back(OpType::CNOT, pt, pc);
                break;
            }
        }
    }

    return physical;
}

} // namespace mqss
