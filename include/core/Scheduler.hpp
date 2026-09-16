#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

#include <unordered_map>
#include <vector>

// Decides what order to work in. Free functions with no state, so two
// orderings can be run over identical input and compared.
namespace scheduler {

struct Candidate {
    int task_id;
    int project_id;
};

// Stride scheduling: each project turns up at a rate proportional to its
// priority. Deterministic. Every candidate comes back exactly once, and
// input order is preserved within a project. A project with no priority
// still appears, at the back of the rotation.
std::vector<int> interleave(const std::vector<Candidate>& candidates,
                            const std::unordered_map<int, double>& project_priorities);

}  // namespace scheduler

#endif
