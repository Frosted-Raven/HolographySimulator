
# Setup

## Compiler


## CPM

The setup uses CPM (Cmake Package Manager) to fetch and control dependencies. Setting up a "cache" will make your life
easier and avoids extra downloads. Simply set the `CPM_SOURCE_CACHE` environment variable to a location that makes you
happy.


# Building


Use `cmake` to setup the build. Preset is available for clang-18

```bash
cmake --preset=clang-18
```

