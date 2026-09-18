# LifeTree

Break down life goals as a tree, assign priority weights to the branches, and link projects (also trees)
to the goals they serve.

A work in progress.

Written in C++ with gtkmm. Data is local, in SQLite.

### Build

Linux, with a C++20 compiler.

**Debian/Ubuntu**

```sh
sudo apt install g++ pkg-config libgtkmm-4.0-dev libsqlite3-dev
```

**Fedora**

```sh
sudo dnf install gcc-c++ gtkmm4.0-devel sqlite-devel
```

Then:

```sh
make
./lifetree_app
```
