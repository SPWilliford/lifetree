#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

#include <unordered_map>
#include <vector>

// Decides what order to work in.
//
// Free functions, no state: everything arrives as an argument, so two
// orderings can be run over identical inputs and compared. Keep it that
// way — a scheduler that fetches what it needs can't be tested against
// itself, and every tweak becomes a matter of opinion. When aging arrives
// it comes in as another parameter, not a member.
namespace scheduler {

// One task available to be worked, and the project it belongs to.
struct Candidate {
    int task_id;
    int project_id;
};

// Orders tasks so each project turns up at a rate proportional to its
// priority, rather than every project's tasks arriving in one block.
//
// Stride scheduling: a project's stride is the inverse of its share, so
// repeatedly taking the smallest accumulated pass interleaves them at
// exactly that ratio. Deterministic on purpose — a to-do list that
// reshuffled on every refresh would be unusable, and a bad run of luck
// would be indistinguishable from a bug.
//
// Every candidate comes back exactly once, and input order is preserved
// within each project — so give it a deterministic order to get one back.
//
// The tail is unavoidably one project alone: once every other queue is
// exhausted there's nothing left to alternate with.
std::vector<int> interleave(const std::vector<Candidate>& candidates,
                            const std::unordered_map<int, double>& project_priorities);

}  // namespace scheduler

#endif
