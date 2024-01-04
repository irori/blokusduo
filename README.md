# libblokusduo

This is a library for the board game Blokus Duo. This library is written in C++
and also provides Python bindings.

## Building

CMake 3.18 or later is required.

### For C++ users

If you are using C++, you can easily integrate this library into your project
using CMake. Here are the steps to do so:

1. **Add the library as a subdirectory:** In your project's `CMakeLists.txt`
   file, import the root directory of this library as a subdirectory:
   ```cmake
   add_subdirectory(path/to/library)
   ```
2. **Link the library to your target:** In the same `CMakeLists.txt` file, link
   the `blokusduo` library to your target:
   ```cmake
   target_link_libraries(YourTarget PRIVATE blokusduo)
   ```

3. **Include the library's header:** In your C++ code, include the library's
   header:
    ```cpp
    #include <blokusduo.h>
    ```

That's it! See [blokusduo.h](include/blokusduo.h) for the API documentation.

### For Python users

Install [nanobind](https://github.com/wjakob/nanobind) first, then build and
install the library:

```bash
pip install nanobind
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DBUILD_PYTHON=ON
cmake --build build
cmake --install build
```

Then you can import the library in Python:

```python
import blokusduo
```
