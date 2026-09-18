# LifeTree

Break down life goals as a tree, assign priority weights to the branches, and link project trees
to the goals they serve. Task ordering comes out of that structure rather than being set by hand.
A work in progress.

Written in C++ with gtkmm. Data is local, in SQLite.

### Build

Linux, with a C++20 compiler.

```
# Debian/Ubuntu
sudo apt install g++ pkg-config libgtkmm-4.0-dev libsqlite3-dev

# Fedora
sudo dnf install gcc-c++ gtkmm4.0-devel sqlite-devel
```
