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

    const long gap = std::max<long>(2, static_cast<long>(queues.size()));

    // Row of each task's last appearance. A completion today counts as an
    // appearance above the top, one row further up per task finished since.
    std::unordered_map<int, long> last_row;
    for (const auto& candidate : candidates) {
        if (candidate.finished_since >= 0) {
            last_row[candidate.task_id] = -1 - static_cast<long>(candidate.finished_since);
        }
    }
    auto ready_at = [&](int task_id) {
        auto it = last_row.find(task_id);
        return it == last_row.end() ? 0L : it->second + gap;
    };

    std::vector<int> ordered;
    ordered.reserve(candidates.size());

    while (ordered.size() < candidates.size()) {
        const long row = static_cast<long>(ordered.size());

        // The lowest-pass project with a task ready for this row.
        int chosen = -1;
        std::size_t chosen_index = 0;
        for (const auto& [project_id, tasks] : queues) {
            for (std::size_t i = 0; i < tasks.size(); ++i) {
                if (ready_at(tasks[i]) > row) continue;
                // Strictly less: an exact tie keeps the lower id.
                if (chosen == -1 || pass.at(project_id) < pass.at(chosen)) {
                    chosen = project_id;
                    chosen_index = i;
                }
                break;
            }
        }

        // Everything left is held back: take whatever is ready soonest, so
        // every candidate still appears.
        if (chosen == -1) {
            long soonest = 0;
            for (const auto& [project_id, tasks] : queues) {
                for (std::size_t i = 0; i < tasks.size(); ++i) {
                    const long at = ready_at(tasks[i]);
                    if (chosen == -1 || at < soonest) {
                        chosen = project_id;
                        chosen_index = i;
                        soonest = at;
                    }
                }
            }
        }

        auto& tasks = queues[chosen];
        const int task_id = tasks[chosen_index];
        tasks.erase(tasks.begin() + static_cast<std::ptrdiff_t>(chosen_index));
        ordered.push_back(task_id);
        last_row[task_id] = row;
        pass[chosen] += stride[chosen];
    }

    return ordered;
}

}  // namespace scheduler
