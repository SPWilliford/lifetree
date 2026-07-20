#ifndef ENTITIES_HPP
#define ENTITIES_HPP

#include <string>

enum class TreeType {
    LIFE,
    PROJECTS
};

// Represents an entry in your Life Tree (your aspirations/intentions)
struct LifeNode {
    std::string title; 
};

// Represents an entry in your Projects Tree (your actionable tasks/milestones)
struct TaskNode {
    std::string title; 
};

#endif
