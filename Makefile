CXX = g++
CC = gcc

# make DEBUG=1 for an unoptimized build with symbols. Run `make clean` when
# switching, since both flavours share build/.
ifeq ($(DEBUG),1)
OPT = -O0 -g
else
OPT = -O2
endif

# -MMD -MP emit a .d file per object listing the headers it included, so
# editing a header rebuilds its users. Pulled in at the bottom.
WARNINGS = -Wall -Wextra -Wshadow -Wpedantic
CXXFLAGS = -std=c++20 $(WARNINGS) $(OPT) -Iinclude -I. -MMD -MP

# core/ depends on sigc++ and sqlite only. Compiling it against exactly
# those is what keeps it that way — a GTK include in core/ fails to build.
CORE_CFLAGS = $(shell pkg-config --cflags sigc++-3.0 sqlite3)
CORE_LIBS   = $(shell pkg-config --libs sigc++-3.0 sqlite3)
GTK_CFLAGS  = $(shell pkg-config --cflags gtkmm-4.0)
GTK_LIBS    = $(shell pkg-config --libs gtkmm-4.0) -lsqlite3

TARGET = lifetree_app
TEST_TARGET = build/lifetree_tests
BUILD_DIR = build

# Icons compiled into the binary rather than looked up in the icon theme,
# so the same icon ships everywhere. The prefix in the .xml must match the
# application id passed to Gtk::Application::create.
RESOURCE_XML = lifetree.gresource.xml
RESOURCE_SRC = $(BUILD_DIR)/resources.c
RESOURCE_OBJ = $(BUILD_DIR)/resources.o
RESOURCE_FILES = $(shell glib-compile-resources --generate-dependencies $(RESOURCE_XML) 2>/dev/null)

CORE_SRCS = $(wildcard src/core/*.cpp)
VIEW_SRCS = $(wildcard src/view/*.cpp)
TEST_SRCS = $(wildcard tests/*.cpp)
HEADERS   = $(wildcard include/core/*.hpp include/view/*.hpp tests/*.hpp)

CORE_OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(CORE_SRCS))
VIEW_OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(VIEW_SRCS) src/main.cpp)
TEST_OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(TEST_SRCS))
DEPS = $(CORE_OBJS:.o=.d) $(VIEW_OBJS:.o=.d) $(TEST_OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(CORE_OBJS) $(VIEW_OBJS) $(RESOURCE_OBJ)
	@echo "Linking $(TARGET)..."
	@$(CXX) $(CXXFLAGS) -o $@ $^ $(GTK_LIBS)

$(RESOURCE_SRC): $(RESOURCE_XML) $(RESOURCE_FILES)
	@mkdir -p $(dir $@)
	@echo "Compiling resources..."
	@glib-compile-resources --generate-source --sourcedir=. --target=$@ $(RESOURCE_XML)

# Generated C, so it takes CFLAGS rather than CXXFLAGS. Registers itself
# through a constructor; it only has to be linked in.
$(RESOURCE_OBJ): $(RESOURCE_SRC)
	@$(CC) $(OPT) $(shell pkg-config --cflags gio-2.0) -c $< -o $@

$(BUILD_DIR)/src/core/%.o: src/core/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) $(CORE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) $(CORE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) $(GTK_CFLAGS) -c $< -o $@

# Links core/ and tests/ only: no GTK, no display needed.
$(TEST_TARGET): $(CORE_OBJS) $(TEST_OBJS)
	@echo "Linking $(TEST_TARGET)..."
	@$(CXX) $(CXXFLAGS) -o $@ $^ $(CORE_LIBS)

test: $(TEST_TARGET)
	@./$(TEST_TARGET)

format:
	@clang-format -i $(CORE_SRCS) $(VIEW_SRCS) src/main.cpp $(TEST_SRCS) $(HEADERS)

clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR) $(TARGET)

# Leading '-' so the first build, with no .d files yet, isn't an error.
-include $(DEPS)

.PHONY: all test format clean
