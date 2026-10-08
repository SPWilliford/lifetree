#include <memory>

#include "core/Database.hpp"
#include "core/TaskAttributes.hpp"
#include "core/TreeController.hpp"
#include "tests/test.hpp"

namespace {

constexpr int EVERY_DAY = 0x7F;

struct Model {
    std::shared_ptr<Database> db = std::make_shared<Database>(":memory:");
    TreeController projects{db, TreeType::PROJECTS};
    TaskAttributes attributes{db, projects};

    Model() {
        db->insert_root(TreeType::PROJECTS, "Root");
        projects.load();
        attributes.load();
    }
};

}  // namespace

TEST(a_repeat_is_due_once_per_remaining_instance) {
    Model m;
    const int project = m.projects.add(0, "Hydration");
    const int water = m.projects.add(project, "Drink a glass of water");
    m.attributes.apply_repeat(water, EVERY_DAY, 4);

    CHECK_EQ(m.attributes.remaining_today(water), 4);
    m.attributes.record_completion(water);
    m.attributes.record_completion(water);
    CHECK_EQ(m.attributes.remaining_today(water), 2);
}

TEST(a_task_that_does_not_recur_is_due_once) {
    Model m;
    const int project = m.projects.add(0, "Job");
    const int task = m.projects.add(project, "Update resume");
    CHECK_EQ(m.attributes.remaining_today(task), 1);
    CHECK_EQ(m.attributes.finished_since(task), -1);
}

TEST(finished_since_counts_completions_after_the_last_one) {
    Model m;
    const int project = m.projects.add(0, "Hydration");
    const int water = m.projects.add(project, "Drink a glass of water");
    const int stretch = m.projects.add(project, "Stretch");
    m.attributes.apply_repeat(water, EVERY_DAY, 4);
    m.attributes.apply_repeat(stretch, EVERY_DAY, 3);

    CHECK_EQ(m.attributes.finished_since(water), -1);
    m.attributes.record_completion(water);
    CHECK_EQ(m.attributes.finished_since(water), 0);
    m.attributes.record_completion(stretch);
    m.attributes.record_completion(stretch);
    CHECK_EQ(m.attributes.finished_since(water), 2);
    m.attributes.record_completion(water);
    CHECK_EQ(m.attributes.finished_since(water), 0);
    CHECK_EQ(m.attributes.finished_since(stretch), 1);
}
