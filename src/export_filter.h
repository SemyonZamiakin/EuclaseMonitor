// What the HTTP export may reveal. Loopback: everything. LAN mode (opt-in, no authentication!): the list of running
// programs is withheld, because it reveals what a person is doing; plain hardware numbers are fine.
#pragma once
#include <string>

namespace qm {
inline bool exportAllowed(const std::string& name, bool lan) {
    if (!lan) return true;
    return name.compare(0, 5, "proc.") != 0;
}
}  // namespace qm
