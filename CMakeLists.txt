cmake_minimum_required(VERSION 3.20)
project(sim LANGUAGES CXX)

# C++20, standard library only.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Keep assertions on in every build type.
foreach(cfg RELEASE RELWITHDEBINFO MINSIZEREL)
  string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_${cfg} "${CMAKE_CXX_FLAGS_${cfg}}")
  string(REPLACE "/DNDEBUG" "" CMAKE_CXX_FLAGS_${cfg} "${CMAKE_CXX_FLAGS_${cfg}}")
endforeach()

add_executable(sim
  src/main.cpp
  src/core/json.cpp
  src/core/config.cpp
  src/core/rng.cpp
  src/io/metrics.cpp
  src/io/topology.cpp
  src/io/trace.cpp
  src/io/nodes.cpp
  src/io/rotation.cpp
  src/io/recovery_log.cpp
  src/io/dcfr_log.cpp
  src/model/graph.cpp
  src/model/deployment.cpp
  src/model/calibrate.cpp
  src/model/energy.cpp
  src/sim/variants.cpp
  src/sim/precedence.cpp
  src/sim/setup.cpp
  src/sim/network.cpp
  src/arms/registry.cpp
  src/arms/algo_pathmetric.cpp
  src/arms/algo_static.cpp
  src/arms/algo_radpr.cpp
  src/arms/algo_nemcr.cpp
  src/arms/algo_dcfr.cpp
)
target_include_directories(sim PRIVATE src)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sim PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(MINGW)
  # Static link so sim.exe runs without the MinGW DLLs.
  target_link_options(sim PRIVATE -static)
endif()

set_target_properties(sim PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}")
