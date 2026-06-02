// SPDX-License-Identifier: Apache-2.0
//
// Integration harness for the MQSS routing heuristic.
//
// Builds a 4-qubit linear topology (Q0 - Q1 - Q2 - Q3), feeds the router
// a logical circuit containing a maximally non-adjacent CNOT(c=0, t=3),
// and prints the resulting physical instruction stream so the reader can
// see exactly where SWAPs were inserted and where the native CNOT was
// finally emitted.

#include "router/Architecture.hpp"
#include "router/Instruction.hpp"
#include "router/Router.hpp"

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {

const char* op_to_string(mqss::OpType op) noexcept {
    switch (op) {
        case mqss::OpType::SINGLE: return "U";
        case mqss::OpType::CNOT:   return "CNOT";
        case mqss::OpType::SWAP:   return "SWAP";
    }
    return "?";
}

void print_instruction(std::size_t idx,
                       const mqss::QuantumInstruction& ins,
                       const char* qubit_prefix) {
    std::cout << "  [" << std::setw(3) << idx << "]  "
              << std::left  << std::setw(5) << op_to_string(ins.type)
              << std::right;
    switch (ins.type) {
        case mqss::OpType::SINGLE:
            std::cout << "   " << qubit_prefix << "[" << ins.target << "]";
            break;
        case mqss::OpType::CNOT:
            std::cout << "   ctrl=" << qubit_prefix << "[" << ins.control
                      << "]   tgt=" << qubit_prefix << "[" << ins.target << "]";
            break;
        case mqss::OpType::SWAP:
            std::cout << "   " << qubit_prefix << "[" << ins.control
                      << "] <-> " << qubit_prefix << "[" << ins.target << "]";
            break;
    }
    std::cout << '\n';
}

void print_layout(const char* label, const std::vector<int>& l2p) {
    std::cout << "  " << label;
    for (std::size_t l = 0; l < l2p.size(); ++l) {
        std::cout << "   q" << l << " -> p" << l2p[l];
    }
    std::cout << '\n';
}

void print_topology(const mqss::CouplingMap& topo) {
    std::cout << "  physical qubits = " << topo.num_physical_qubits() << '\n';
    std::cout << "  edges          =";
    const auto& adj = topo.adj_list();
    for (std::size_t u = 0; u < adj.size(); ++u) {
        for (const int v : adj[u]) {
            if (static_cast<int>(u) < v) {
                std::cout << "  (p" << u << "-p" << v << ")";
            }
        }
    }
    std::cout << '\n';
}

} // namespace

int main() {
    using namespace mqss;

    const CouplingMap topology(4, {{0, 1}, {1, 2}, {2, 3}});

    // Logical circuit that *cannot* execute natively on the topology:
    // CNOT(c=q0, t=q3) crosses three hops on a linear chain.
    const std::vector<QuantumInstruction> logical_circuit = {
        QuantumInstruction(OpType::SINGLE, /*target=*/0),
        QuantumInstruction(OpType::SINGLE, /*target=*/3),
        QuantumInstruction(OpType::CNOT,   /*target=*/3, /*control=*/0),
        QuantumInstruction(OpType::SINGLE, /*target=*/3),
    };

    std::cout
        << "============================================================\n"
        << "  MQSS Routing Heuristic  --  integration trace\n"
        << "============================================================\n\n"
        << "Hardware topology (linear chain):\n";
    print_topology(topology);

    std::cout << "\nLogical circuit (" << logical_circuit.size() << " ops):\n";
    for (std::size_t i = 0; i < logical_circuit.size(); ++i) {
        print_instruction(i, logical_circuit[i], "q");
    }

    MQTInspiredRouter router(topology, /*lookahead_depth=*/8);

    std::cout << "\nInitial layout:\n";
    print_layout("L2P:", router.logical_to_physical());

    const auto physical = router.route_circuit(logical_circuit);

    std::cout << "\nRouted physical instruction stream ("
              << physical.size() << " ops):\n";

    int swap_count = 0;
    int cnot_count = 0;
    for (std::size_t i = 0; i < physical.size(); ++i) {
        print_instruction(i, physical[i], "p");
        if (physical[i].type == OpType::SWAP) ++swap_count;
        if (physical[i].type == OpType::CNOT) ++cnot_count;
    }

    std::cout << "\nFinal layout:\n";
    print_layout("L2P:", router.logical_to_physical());

    std::cout << "\nSummary:\n"
              << "  SWAPs inserted = " << swap_count << '\n'
              << "  Native CNOTs   = " << cnot_count << '\n'
              << "  Total physical ops = " << physical.size() << '\n';

    std::cout
        << "\nThe SWAPs above migrate logical q0 along the chain until it\n"
        << "becomes physically adjacent to logical q3, at which point the\n"
        << "native CNOT executes against the current hardware layout.\n";

    return 0;
}
