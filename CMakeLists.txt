cmake_minimum_required(VERSION 3.31 FATAL_ERROR)

project(jittest C CXX)

set(JIT_TEST_BINS_DIR "${CMAKE_SOURCE_DIR}/build")
set(JIT_TEST_DEPS_DIR "${CMAKE_SOURCE_DIR}/deps")
set(JIT_TEST_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src")
set(JIT_TEST_INCS_DIR "${CMAKE_SOURCE_DIR}/include")

set(ASMJIT_DIR "${JIT_TEST_DEPS_DIR}/asmjit")
set(ASMJIT_STATIC TRUE)
add_subdirectory("${ASMJIT_DIR}")

add_library(toyjit_impl)
target_include_directories(toyjit_impl PRIVATE "${JIT_TEST_INCS_DIR}")
target_sources(toyjit_impl PRIVATE
    "${JIT_TEST_SOURCE_DIR}/vm.cpp"
    "${JIT_TEST_SOURCE_DIR}/jit.cpp"
)

add_executable(toyjit "${JIT_TEST_SOURCE_DIR}/main.cpp")
target_include_directories(toyjit PRIVATE "${JIT_TEST_INCS_DIR}")
target_link_directories(toyjit PRIVATE "${JIT_TEST_BINS_DIR}")
target_link_libraries(toyjit PRIVATE asmjit::asmjit PRIVATE toyjit_impl)
# target_compile_features(app PUBLIC cxx_std_17)
