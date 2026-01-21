cmake_minimum_required(VERSION 3.16)

project(nautylus C)

add_library(nautylus STATIC
  src/graph.c
  src/slab.c
  src/vector.c
)

target_compile_features(nautylus PUBLIC c_std_99)
target_include_directories(nautylus PUBLIC include)
target_include_directories(nautylus PRIVATE src)

option(NAUTYLUS_BUILD_TESTS "Build Nautylus tests" ON)
option(NAUTYLUS_BUILD_CLI "Build Nautylus CLI demo" ON)
if(NAUTYLUS_BUILD_CLI)
  add_executable(nautylus_cli src/nautylus_cli.c src/jsmn.c)
  target_link_libraries(nautylus_cli PRIVATE nautylus)
endif()
if(NAUTYLUS_BUILD_TESTS)
  enable_testing()
  add_executable(nautylus_tests_graph
    tests/c/test_graph.c
  )
  target_link_libraries(nautylus_tests_graph PRIVATE nautylus)
  add_test(NAME nautylus_tests_graph COMMAND nautylus_tests_graph)

  add_executable(nautylus_tests_vector
    tests/c/test_vector.c
  )
  target_link_libraries(nautylus_tests_vector PRIVATE nautylus)
  add_test(NAME nautylus_tests_vector COMMAND nautylus_tests_vector)
endif()
