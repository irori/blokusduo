cmake_minimum_required(VERSION 3.18...3.27)
project(blokusduo)

set(CMAKE_CXX_STANDARD 20)

option(BLOKUSDUO_ENABLE_NATIVE
  "Optimize blokusduo for the CPU used to build it" ON)
option(BLOKUSDUO_ENABLE_WASM_SIMD
  "Use WebAssembly SIMD instructions when building with Emscripten" OFF)

find_package(Python 3.9 REQUIRED Interpreter OPTIONAL_COMPONENTS Development.Module)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/piece.cpp
  COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/src/piece.py > ${CMAKE_CURRENT_BINARY_DIR}/piece.cpp
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/piece.py
)

add_library(blokusduo STATIC
  src/search.cpp
  src/board.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/piece.cpp
)
target_include_directories(blokusduo PUBLIC include PRIVATE src)
set_target_properties(blokusduo PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BLOKUSDUO_ENABLE_WASM_SIMD)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR
      "BLOKUSDUO_ENABLE_WASM_SIMD requires the Emscripten toolchain")
  endif()
  target_compile_options(blokusduo PUBLIC "-msimd128" "-mfpu=neon")
  target_link_options(blokusduo INTERFACE "-msimd128")
endif()

if(BLOKUSDUO_ENABLE_NATIVE AND NOT EMSCRIPTEN)
  include(CheckCXXCompilerFlag)
  if(MSVC)
    check_cxx_compiler_flag("/arch:AVX2" BLOKUSDUO_HAS_NATIVE_FLAG)
    set(BLOKUSDUO_NATIVE_FLAG "/arch:AVX2")
  else()
    check_cxx_compiler_flag("-march=native" BLOKUSDUO_HAS_NATIVE_FLAG)
    set(BLOKUSDUO_NATIVE_FLAG "-march=native")
  endif()

  if(BLOKUSDUO_HAS_NATIVE_FLAG)
    target_compile_options(blokusduo PRIVATE ${BLOKUSDUO_NATIVE_FLAG})
  else()
    message(WARNING
      "BLOKUSDUO_ENABLE_NATIVE is ON, but the compiler does not support "
      "a native architecture flag")
  endif()
endif()

find_package(GTest)
if (GTest_FOUND)
  enable_testing()
  add_executable(board_test src/board_test.cpp)
  target_link_libraries(board_test blokusduo GTest::gtest GTest::gtest_main)
  add_test(NAME board_test COMMAND board_test)
  add_executable(search_test src/search_test.cpp)
  target_link_libraries(search_test blokusduo GTest::gtest GTest::gtest_main)
  add_test(NAME search_test COMMAND search_test)
endif()

add_executable(search_benchmark src/search_benchmark.cpp)
target_link_libraries(search_benchmark blokusduo)

option(BUILD_PYTHON "Build Python binding" OFF)

if (BUILD_PYTHON)
  if (NOT Python_Development.Module_FOUND)
    message(FATAL_ERROR "Python development headers not found")
  endif()

  if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
  endif()

  # Detect the installed nanobind package and import it into CMake
  execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE NB_DIR)
  list(APPEND CMAKE_PREFIX_PATH "${NB_DIR}")
  find_package(nanobind CONFIG REQUIRED)

  nanobind_add_module(blokusduo-python python/binding.cpp)
  set_target_properties(blokusduo-python PROPERTIES OUTPUT_NAME blokusduo)
  target_link_libraries(blokusduo-python PRIVATE blokusduo)

  enable_testing()
  add_test(
    NAME python_binding_test
    COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python/binding_test.py
  )
  set_tests_properties(
    python_binding_test PROPERTIES
    ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:blokusduo-python>"
  )

  install(TARGETS blokusduo-python DESTINATION ${Python_SITEARCH})
endif()
