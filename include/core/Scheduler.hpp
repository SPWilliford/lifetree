#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

#include <vector>
#include <unordered_map>

// Decides what order to work in.
//
// Free functions rather than a class, because there is no state to own:
// everything the ordering depends on arrives as an argument and nothing is
// remembered between calls. That's deliberate and worth keeping. It means
// two orderings can be run over identical inputs and compared, which is the
// only way to tell whether a change to the policy actually improved
// anything. A scheduler that reaches out to fetch what it needs can't be
// A/B tested, and every future tweak becomes a matter of opinion.
//
// When aging arrives — nudging a project up the longer it goes untouched —
// it comes in as another parameter (last-worked times), not as a member.
namespace scheduler {

// One task available to be worked, and the project it belongs to.
struct Candidate {
    int task_id;
    int project_id;
};

// Orders tasks so each project turns up at a rate proportional to its
// priority, instead of every project's tasks arriving in one block.
//
// Stride scheduling: a project's stride is the inverse of its share, so one
// worth twice as much gets a turn twice as often. Repeatedly taking the
// project with the smallest accumulated pass interleaves them at exactly
// that ratio. This is the deterministic sibling of lottery scheduling —
// same proportional guarantee, no randomness, which matters for a to-do
// list: one that reshuffled itself on every refresh would be unusable, and
// a bad run of luck would be indistinguishable from a bug.
//
// Every candidate comes back exactly once. Ordering by priority alone can
// leave a fragmented goal starved — four projects sharing one goal each
// rank below two projects sharing an equal goal, so a top-down reader never
// reaches them. Proportional interleaving removes that: the four
// collectively draw the same share as the two.
//
// Input order is preserved within each project, so give it a deterministic
// order if you want a deterministic result.
//
// The tail of the result is unavoidably one project on its own: once every
// other queue is exhausted there is nothing left to alternate with. That's
// arithmetic rather than a shortcoming, but it does mean the bottom of a
// long backlog looks like the old sorted behaviour.
std::vector<int> interleave(const std::vector<Candidate>& candidates,
                            const std::unordered_map<int, double>& project_priorities);

}

#endif
