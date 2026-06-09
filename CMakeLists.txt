cmake_minimum_required(VERSION 3.20)

project(openGil VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(opengil_core
  src/core/gil.cpp
  src/core/json_value.cpp
  src/core/sha256.cpp
  src/core/wire.cpp
  src/ops/custom_vars_ops.cpp
  src/ops/model_ops.cpp
  src/ops/nodegraph_ops.cpp
  src/ops/prefab_ops.cpp
  src/ops/projectile_ops.cpp
  src/semantic/semantic.cpp
)

target_include_directories(opengil_core PUBLIC include)

if(MSVC)
  target_compile_options(opengil_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(opengil_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(opengil src/cli/main.cpp)
target_link_libraries(opengil PRIVATE opengil_core)

enable_testing()

add_executable(opengil_core_tests tests/unit/test_core.cpp)
target_link_libraries(opengil_core_tests PRIVATE opengil_core)
add_test(NAME opengil_core_tests COMMAND opengil_core_tests)

add_executable(opengil_json_tests tests/unit/test_json_value.cpp)
target_link_libraries(opengil_json_tests PRIVATE opengil_core)
add_test(NAME opengil_json_tests COMMAND opengil_json_tests)

add_executable(opengil_model_tests tests/unit/test_model_ops.cpp)
target_link_libraries(opengil_model_tests PRIVATE opengil_core)
target_compile_definitions(opengil_model_tests PRIVATE
  OPENGIL_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)
add_test(NAME opengil_model_tests COMMAND opengil_model_tests)

add_executable(opengil_prefab_tests tests/unit/test_prefab_ops.cpp)
target_link_libraries(opengil_prefab_tests PRIVATE opengil_core)
target_compile_definitions(opengil_prefab_tests PRIVATE
  OPENGIL_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)
add_test(NAME opengil_prefab_tests COMMAND opengil_prefab_tests)

add_executable(opengil_nodegraph_tests tests/unit/test_nodegraph_ops.cpp)
target_link_libraries(opengil_nodegraph_tests PRIVATE opengil_core)
add_test(NAME opengil_nodegraph_tests COMMAND opengil_nodegraph_tests)

add_executable(opengil_projectile_tests tests/unit/test_projectile_ops.cpp)
target_link_libraries(opengil_projectile_tests PRIVATE opengil_core)
add_test(NAME opengil_projectile_tests COMMAND opengil_projectile_tests)

add_executable(opengil_custom_vars_tests tests/unit/test_custom_vars_ops.cpp)
target_link_libraries(opengil_custom_vars_tests PRIVATE opengil_core)
add_test(NAME opengil_custom_vars_tests COMMAND opengil_custom_vars_tests)

add_test(NAME opengil_cli_validate_fixture
  COMMAND opengil validate --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test1.gil
)

add_test(NAME opengil_cli_batch_dry_run
  COMMAND opengil batch
    --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test1.gil
    --ops ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/batch-model-rename.json
    --dry-run
)

add_test(NAME opengil_cli_batch_custom_vars_dry_run
  COMMAND opengil batch
    --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test1.gil
    --ops ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/batch-custom-vars.json
    --dry-run
)
