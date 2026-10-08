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
    // Tasks finished since this one was last finished today, or -1 if it
    // hasn't been. Only matters for a task listed more than once.
    int finished_since = -1;
};

// Stride scheduling: each project turns up at a rate proportional to its
// priority. Deterministic. Every candidate comes back exactly once, and
// input order is preserved within a project. A project with no priority
// still appears, at the back of the rotation.
//
// A task listed more than once (a repeat's remaining instances) keeps one
// rotation between appearances: as many rows as there are projects in the
// list, never fewer than two. Its first appearance keeps the same gap from
// its last completion, counting each task finished since as a row. A task
// held back this way doesn't hold up the rest of its project.
std::vector<int> interleave(const std::vector<Candidate>& candidates,
                            const std::unordered_map<int, double>& project_priorities);

}  // namespace scheduler

#endif
