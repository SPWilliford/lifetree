#include <cstdio>
#include <memory>
#include <string>

#include "core/Database.hpp"
#include "core/Day.hpp"
#include "tests/test.hpp"

TEST(notes_start_empty) {
    auto db = std::make_shared<Database>(":memory:");
    Day day(db);
    day.load();
    CHECK_EQ(day.notes(), std::string());
}

TEST(notes_survive_reopening) {
    const std::string path = "/tmp/lifetree_day_test.db";
    std::remove(path.c_str());

    {
        Day day(std::make_shared<Database>(path));
        day.load();
        day.set_notes("Call the dentist\nBuy coffee");
    }
    {
        Day day(std::make_shared<Database>(path));
        day.load();
        CHECK_EQ(day.notes(), std::string("Call the dentist\nBuy coffee"));
        day.set_notes("");
    }
    {
        Day day(std::make_shared<Database>(path));
        day.load();
        CHECK_EQ(day.notes(), std::string());
    }
    std::remove(path.c_str());
}

TEST(notes_are_not_tied_to_a_date) {
    auto db = std::make_shared<Database>(":memory:");
    db->set_notes("Kept across days");
    db->set_day_hours("2026-10-01", 8 * 60, 22 * 60);
    CHECK_EQ(db->load_notes(), std::string("Kept across days"));
}
