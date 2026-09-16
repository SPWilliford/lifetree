#include "core/Scheduler.hpp"

#include <algorithm>
#include <cstddef>
#include <map>

namespace {
// Floor for unlinked projects, so they rotate rather than vanish.
constexpr double MIN_SHARE = 0.5;
}  // namespace

namespace scheduler {

std::vector<int> interleave(const std::vector<Candidate>& candidates,
                            const std::unordered_map<int, double>& project_priorities) {
    // Ordered maps: ties break by project id, which needs repeatable iteration.
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
        int chosen = -1;
        double lowest_pass = 0.0;
        for (const auto& [project_id, tasks] : queues) {
            if (cursor[project_id] >= tasks.size()) continue;
            const double project_pass = pass.at(project_id);
            // Strictly less: an exact tie keeps the lower id.
            if (chosen == -1 || project_pass < lowest_pass) {
                chosen = project_id;
                lowest_pass = project_pass;
            }
        }
        if (chosen == -1) break;

        ordered.push_back(queues[chosen][cursor[chosen]]);
        ++cursor[chosen];
        pass[chosen] += stride[chosen];
    }

    return ordered;
}

}  // namespace scheduler
