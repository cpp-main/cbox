# cbox
CBOX is a library of components based on the Reactor model

## Build
---
We use cmake for building projects:

```
# cmake -B build
# cd build
# make -j4
# sudo make install
```

You can build with debug or tests, or samples with additional params:
```
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
                -DCMAKE_BUILD_TYPE=Debug \
                -DCMAKE_INSTALL_PREFIX=./install \
                -DCMAKE_ENABLE_SAMPLES=OFF \
                -DCMAKE_ENABLE_TESTS=ON
```

Field description:

| Field | params | Description |
|-|-|-|
| CMAKE_BUILD_TYPE | Release/Debug | set build type, default : Release |
| CMAKE_EXPORT_COMPILE_COMMANDS | ON/OFF | used for language server, default: OFF |
| CMAKE_INSTALL_PREFIX | <path> | set install target dir, default : /usr/local |
| CMAKE_ENABLE_SAMPLES | ON/OFF | build samples , default: ON |
| CMAKE_ENABLE_TESTS | ON/OFF | build tests, default: OFF |

If you enabled tests, first you need install [googletest](https://github.com/google/googletest), and then you can run test with command:

```
# cd build
# ctest
```
