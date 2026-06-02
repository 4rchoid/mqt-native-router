// SPDX-License-Identifier: Apache-2.0

#include "router/Architecture.hpp"

#include <algorithm>
#include <stdexcept>

namespace mqss {

CouplingMap::CouplingMap(int num_physical_qubits)
    : num_qubits_(num_physical_qubits),
      adj_list_(static_cast<std::size_t>(num_physical_qubits)) {
    if (num_physical_qubits < 0) {
        throw std::invalid_argument(
            "CouplingMap: num_physical_qubits must be non-negative");
    }
}

CouplingMap::CouplingMap(int num_physical_qubits,
                         const std::vector<std::pair<int, int>>& edges)
    : CouplingMap(num_physical_qubits) {
    for (const auto& e : edges) {
        add_edge(e.first, e.second);
    }
}

void CouplingMap::add_edge(int p1, int p2) {
    if (p1 < 0 || p2 < 0 || p1 >= num_qubits_ || p2 >= num_qubits_) {
        throw std::out_of_range(
            "CouplingMap::add_edge: qubit index out of range");
    }
    if (p1 == p2) {
        return;
    }

    auto insert_unique = [](std::vector<int>& bucket, int v) {
        if (std::find(bucket.begin(), bucket.end(), v) == bucket.end()) {
            bucket.push_back(v);
        }
    };

    insert_unique(adj_list_[static_cast<std::size_t>(p1)], p2);
    insert_unique(adj_list_[static_cast<std::size_t>(p2)], p1);
}

bool CouplingMap::is_physically_connected(int p1, int p2) const noexcept {
    if (p1 < 0 || p2 < 0 || p1 >= num_qubits_ || p2 >= num_qubits_) {
        return false;
    }
    const auto& bucket = adj_list_[static_cast<std::size_t>(p1)];
    for (const int n : bucket) {
        if (n == p2) {
            return true;
        }
    }
    return false;
}

} // namespace mqss
