# mqss-routing-heuristic

A compact, dependency-free C++17 implementation of a quantum-circuit
**mapping & gate-routing heuristic**, modelled after the design ideas in
the [Munich Quantum Toolkit (MQT)][mqt] and the low-latency systems
philosophy of the [Munich Quantum Software Stack (MQSS)][mqss].

Given a logical circuit and a physical hardware topology (coupling map),
the router inserts the minimum number of `SWAP` gates needed to make every
two-qubit gate satisfy the hardware's adjacency constraints, and emits a
stream of physical instructions ready for execution.

[mqt]:  https://mqt.readthedocs.io/
[mqss]: https://www.munichquantumvalley.de/research/consortia/mqss

---

## What this is (and isn't)

- **Is**: a self-contained reference implementation of the core
  *SWAP-insertion* loop — flat index tables, a pre-computed all-pairs
  shortest-path matrix, and a SABRE/MQT-style greedy + lookahead heuristic.
- **Is not**: a full compiler. There is no parser, no scheduler, no
  noise model, no optimisation passes beyond routing. The IR is a plain
  `std::vector<QuantumInstruction>`.

The whole thing is ~300 lines of code split across three translation
units, builds in well under a second, and has zero external dependencies.

---

## Project layout

```
mqss-routing-heuristic/
├── CMakeLists.txt
├── include/router/
│   ├── Instruction.hpp     # QuantumInstruction POD + OpType enum
│   ├── Architecture.hpp    # CouplingMap: hardware topology
│   └── Router.hpp          # MQTInspiredRouter public API
└── src/
    ├── Architecture.cpp    # adjacency-list impl
    ├── Router.cpp          # Floyd-Warshall + greedy SWAP heuristic
    └── main.cpp            # integration trace (4-qubit linear chain)
```

---

## Core design choices

| Concern | Decision | Why |
|---|---|---|
| Layout tracking | Two flat `std::vector<int>` index tables (`logical_to_physical`, `physical_to_logical`) | O(1) lookups & swaps; no hash maps, no allocations on the hot path |
| Distance queries | `std::vector<std::vector<int>>` populated once via Floyd-Warshall | O(1) per query during the heuristic pass; one-shot O(n³) at startup |
| SWAP candidates | Only edges incident to the two CNOT endpoints | Degree-bounded work per gate (typical hardware: ≤ 6 candidates) |
| Heuristic | `score = new_distance + λ · lookahead`, with a geometric-decay sum (λ = 0.5) over the next *k* CNOTs | Matches the SABRE / MQT "extended set" idea; avoids local minima without a global solver |
| Termination | Strict descent (`new_d < curr_d`) | Eliminates oscillation; proves the inner `while` loop ends |
| Memory discipline | No `shared_ptr` / `unique_ptr` / `std::map` anywhere in the routing loop; mutate-then-restore for candidate evaluation | Mirrors MQSS's zero-overhead, latency-sensitive philosophy |

`OpType` is a `std::uint8_t`-backed enum class and `QuantumInstruction`
fits in 16 bytes (`static_assert`-enforced) so the IR stays cache-friendly.

---

## API at a glance

```cpp
#include "router/Architecture.hpp"
#include "router/Router.hpp"

using namespace mqss;

// 1) Describe the hardware.
CouplingMap topology(/*n=*/4, {{0,1}, {1,2}, {2,3}});

// 2) Build a logical circuit.
std::vector<QuantumInstruction> circuit = {
    {OpType::CNOT, /*tgt=*/3, /*ctrl=*/0},
};

// 3) Route.
MQTInspiredRouter router(topology, /*lookahead_depth=*/8);
auto physical = router.route_circuit(circuit);

// `physical` is a stream of SINGLE / CNOT / SWAP instructions
// against the physical qubit indices, ready for the backend.
```

The router also exposes `set_initial_layout(...)`, `reset_trivial_layout()`,
and an O(1) `distance(p1, p2)` accessor for downstream passes.

---

## Building

### With CMake (recommended)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/mqss_router
```

The build produces a static library `mqss_router_core` plus the
`mqss_router` integration-trace executable.

### Without CMake (single command)

```bash
clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
        -Iinclude src/Architecture.cpp src/Router.cpp src/main.cpp \
        -o mqss_router
./mqss_router
```

Requires only a C++17 compiler. Verified clean on Apple Clang 17 and
GCC 11+.

---

## Example run

The bundled `main.cpp` builds a linear 4-qubit topology and routes the
maximally non-adjacent gate `CNOT(c=q0, t=q3)`:

```
Hardware topology (linear chain):
  physical qubits = 4
  edges          =  (p0-p1)  (p1-p2)  (p2-p3)

Logical circuit:
  [  0]  CNOT    ctrl=q[0]   tgt=q[3]

Routed physical instruction stream:
  [  0]  SWAP    p[0] <-> p[1]
  [  1]  SWAP    p[1] <-> p[2]
  [  2]  CNOT    ctrl=p[2]   tgt=p[3]

Summary:
  SWAPs inserted = 2
  Native CNOTs   = 1
```

Two SWAPs is provably minimal for distance-3 endpoints on a linear chain
(⌈d − 1⌉). Logical `q0` migrates rightward along the chain to physical
`p2`, at which point the native CNOT executes against the adjacent
`(p2, p3)` pair.

---

## Extending

- **New hardware topologies** — just pass a different edge list to
  `CouplingMap` (heavy-hex, grid, T-shape, etc.).
- **Custom initial mapping** — call `router.set_initial_layout(...)`
  before `route_circuit` to start from a non-trivial permutation.
- **Tuning the heuristic** — adjust `lookahead_depth` at construction or
  `kLookaheadDecay` / `kLookaheadWeight` in `Router.cpp`.

---

## License

SPDX-License-Identifier: Apache-2.0
