// SPDX-License-Identifier: Apache-2.0
//
// Lightweight value-type representation of a quantum instruction.
//
// Designed for the MQSS-style hot path: trivially copyable, no virtual
// dispatch, no allocations, no string fields. A flat `std::vector` of
// these is the canonical IR between the compiler front-end and the
// routing/scheduling stages.

#ifndef MQSS_ROUTER_INSTRUCTION_HPP
#define MQSS_ROUTER_INSTRUCTION_HPP

#include <cstdint>

namespace mqss {

/// Coarse-grained gate type tag used by the routing pass.
///
/// The router only needs to distinguish three classes of operations:
///   - SINGLE: any single-qubit gate (U, H, X, RZ, ...). Treated as opaque
///             and forwarded with a remapped target.
///   - CNOT:   any two-qubit entangling gate constrained by the coupling
///             map. Subject to SWAP insertion.
///   - SWAP:   physical 3-CNOT SWAP emitted by the router (or supplied by
///             the user as a logical permutation).
enum class OpType : std::uint8_t {
    SINGLE = 0,
    CNOT   = 1,
    SWAP   = 2,
};

/// POD-like instruction record.
///
/// `control == -1` is the canonical sentinel for single-qubit ops. For
/// CNOT/SWAP both indices are valid hardware (or logical) qubit indices
/// depending on which side of the routing pass you are on.
struct QuantumInstruction {
    OpType type;
    int    target;
    int    control;

    constexpr QuantumInstruction() noexcept
        : type(OpType::SINGLE), target(-1), control(-1) {}

    constexpr QuantumInstruction(OpType t, int tgt, int ctrl = -1) noexcept
        : type(t), target(tgt), control(ctrl) {}
};

static_assert(sizeof(QuantumInstruction) <= 16,
              "QuantumInstruction must stay cache-friendly");

} // namespace mqss

#endif // MQSS_ROUTER_INSTRUCTION_HPP
