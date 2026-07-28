CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2 -Iinclude $(shell pkg-config --cflags gtkmm-4.0)
LIBS = $(shell pkg-config --libs gtkmm-4.0) -lsqlite3
TARGET = lifetree_app

BUILD_DIR = build

# Scans your core, view, and main entries cleanly
SRCS = $(strip $(shell find src/core src/view -name "*.cpp" 2>/dev/null) src/main.cpp)
OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking target: $(TARGET)..."
	@$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LIBS)
	@echo "Build successful!"

# Automatically compiles .cpp source files and mirrors directory structures inside build/
$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR) $(TARGET)
	@find src -name "*.o" -type f -delete

.PHONY: all clean
