#include "core/Scheduler.hpp"
#include <algorithm>
#include <map>
#include <cstddef>

namespace {
    // A project with no associations still contains real tasks, so it has
    // to appear somewhere rather than disappear from the list entirely.
    // This floor puts it at the back of the rotation without removing it —
    // "why is this on your list at all?" is a question for the review, not
    // something to enforce by hiding the work.
    constexpr double MIN_SHARE = 0.5;
}

namespace scheduler {

std::vector<int> interleave(const std::vector<Candidate>& candidates,
                            const std::unordered_map<int, double>& project_priorities) {
    // One queue per project, keeping the order candidates arrived in.
    // Ordered map rather than unordered: ties below are broken by project
    // id, and that only produces a repeatable result if iteration is
    // repeatable too.
    std::map<int, std::vector<int>> queues;
    for (const auto& candidate : candidates) {
        queues[candidate.project_id].push_back(candidate.task_id);
    }

    std::map<int, double> stride;
    std::map<int, double> pass;
    for (const auto& [project_id, tasks] : queues) {
        auto it = project_priorities.find(project_id);
        const double share = std::max(it != project_priorities.end() ? it->second : 0.0, MIN_SHARE);
        stride[project_id] = 1.0 / share;
        pass[project_id] = stride[project_id];
    }

    std::vector<int> ordered;
    ordered.reserve(candidates.size());

    std::map<int, std::size_t> cursor;
    while (true) {
        // The project owed a turn soonest, among those with work left.
        int chosen = -1;
        double lowest_pass = 0.0;
        for (const auto& [project_id, tasks] : queues) {
            if (cursor[project_id] >= tasks.size()) continue;

            const double project_pass = pass.at(project_id);
            // Strictly-less, so an exact tie keeps whichever project came
            // first in id order. That's what makes the whole ordering
            // reproducible rather than dependent on traversal accidents.
            if (chosen == -1 || project_pass < lowest_pass) {
                chosen = project_id;
                lowest_pass = project_pass;
            }
        }
        if (chosen == -1) break; // every queue exhausted

        ordered.push_back(queues[chosen][cursor[chosen]]);
        ++cursor[chosen];
        pass[chosen] += stride[chosen];
    }

    return ordered;
}

}
