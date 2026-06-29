cmake_minimum_required(VERSION 3.20)

project(openGil VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(opengil_core
  src/core/gil.cpp
  src/core/sha256.cpp
  src/core/wire.cpp
  src/ops/attachment_from_decoration_ops.cpp
  src/ops/attachment_ops.cpp
  src/ops/custom_vars_ops.cpp
  src/ops/decoration_ops.cpp
  src/ops/model_ops.cpp
  src/ops/nodegraph_ops.cpp
  src/ops/object_ops.cpp
  src/ops/pixel_decoration_import_ops.cpp
  src/ops/png_loader.cpp
  src/ops/prefab_ops.cpp
  src/ops/projectile_ops.cpp
  src/ops/ui_generated_ops.cpp
  src/ops/ui_ops.cpp
  src/ops/ui_patch_ops.cpp
  src/ops/ui_pixel_import_ops.cpp
  src/ops/ui_structure_ops.cpp
  src/semantic/semantic.cpp
)

target_include_directories(opengil_core PUBLIC include PRIVATE third_party/stb)

if(MSVC)
  target_compile_options(opengil_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(opengil_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(opengil
  src/cli/json_formatters.cpp
  src/cli/main.cpp
)
target_link_libraries(opengil PRIVATE opengil_core)

if(MSVC)
  target_compile_options(opengil PRIVATE /W4 /permissive-)
else()
  target_compile_options(opengil PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
add_subdirectory(third_party/pybind11)

pybind11_add_module(opengil_python
  src/python/bindings.cpp
)
set_target_properties(opengil_python PROPERTIES OUTPUT_NAME opengil)
target_link_libraries(opengil_python PRIVATE opengil_core)

if(MSVC)
  target_compile_options(opengil_python PRIVATE /W4 /permissive-)
else()
  target_compile_options(opengil_python PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
  add_executable(opengil_gui WIN32
    src/gui/i18n.cpp
    src/gui/main.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
  )
  target_include_directories(opengil_gui PRIVATE
    third_party/imgui
    third_party/imgui/backends
  )
  target_link_libraries(opengil_gui PRIVATE
    opengil_core
    d3d11
    dxgi
    comdlg32
    imm32
  )
  if(MSVC)
    target_compile_options(opengil_gui PRIVATE /permissive- /utf-8)
  endif()
  add_custom_command(TARGET opengil_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/assets
      $<TARGET_FILE_DIR:opengil_gui>/assets
  )
endif()

enable_testing()

add_executable(opengil_core_tests tests/unit/test_core.cpp)
target_link_libraries(opengil_core_tests PRIVATE opengil_core)
add_test(NAME opengil_core_tests COMMAND opengil_core_tests)

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

add_executable(opengil_ui_tests tests/unit/test_ui_ops.cpp)
target_link_libraries(opengil_ui_tests PRIVATE opengil_core)
add_test(NAME opengil_ui_tests COMMAND opengil_ui_tests)

add_executable(opengil_ui_patch_tests tests/unit/test_ui_patch_ops.cpp)
target_link_libraries(opengil_ui_patch_tests PRIVATE opengil_core)
add_test(NAME opengil_ui_patch_tests COMMAND opengil_ui_patch_tests)

add_executable(opengil_ui_structure_tests tests/unit/test_ui_structure_ops.cpp)
target_link_libraries(opengil_ui_structure_tests PRIVATE opengil_core)
add_test(NAME opengil_ui_structure_tests COMMAND opengil_ui_structure_tests)

add_executable(opengil_ui_generated_tests tests/unit/test_ui_generated_ops.cpp)
target_link_libraries(opengil_ui_generated_tests PRIVATE opengil_core)
add_test(NAME opengil_ui_generated_tests COMMAND opengil_ui_generated_tests)

add_executable(opengil_ui_pixel_import_tests tests/unit/test_ui_pixel_import_ops.cpp)
target_link_libraries(opengil_ui_pixel_import_tests PRIVATE opengil_core)
add_test(NAME opengil_ui_pixel_import_tests COMMAND opengil_ui_pixel_import_tests)

add_executable(opengil_pixel_decoration_import_tests tests/unit/test_pixel_decoration_import_ops.cpp)
target_link_libraries(opengil_pixel_decoration_import_tests PRIVATE opengil_core)
target_compile_definitions(opengil_pixel_decoration_import_tests PRIVATE
  OPENGIL_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)
add_test(NAME opengil_pixel_decoration_import_tests COMMAND opengil_pixel_decoration_import_tests)

add_executable(opengil_object_tests tests/unit/test_object_ops.cpp)
target_link_libraries(opengil_object_tests PRIVATE opengil_core)
add_test(NAME opengil_object_tests COMMAND opengil_object_tests)

add_executable(opengil_decoration_tests tests/unit/test_decoration_ops.cpp)
target_link_libraries(opengil_decoration_tests PRIVATE opengil_core)
add_test(NAME opengil_decoration_tests COMMAND opengil_decoration_tests)

add_executable(opengil_attachment_tests tests/unit/test_attachment_ops.cpp)
target_link_libraries(opengil_attachment_tests PRIVATE opengil_core)
add_test(NAME opengil_attachment_tests COMMAND opengil_attachment_tests)

add_executable(opengil_attachment_from_decoration_tests tests/unit/test_attachment_from_decoration_ops.cpp)
target_link_libraries(opengil_attachment_from_decoration_tests PRIVATE opengil_core)
add_test(NAME opengil_attachment_from_decoration_tests COMMAND opengil_attachment_from_decoration_tests)

add_executable(opengil_custom_vars_tests tests/unit/test_custom_vars_ops.cpp)
target_link_libraries(opengil_custom_vars_tests PRIVATE opengil_core)
add_test(NAME opengil_custom_vars_tests COMMAND opengil_custom_vars_tests)

add_test(NAME opengil_cli_validate_fixture
  COMMAND opengil validate --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test1.gil
)

add_test(NAME opengil_cli_ui_list_fixture
  COMMAND opengil ui list --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test1.gil
)

add_test(NAME opengil_cli_list_scene_objects_fixture
  COMMAND opengil list-scene-objects --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test1.gil
)

add_test(NAME opengil_cli_list_preview_objects_fixture
  COMMAND opengil list-preview-objects --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test1.gil
)

add_test(NAME opengil_cli_ui_delete_dry_run
  COMMAND opengil ui delete
    --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test1.gil
    --controller-entry-id 1073741855
    --primitive-indexes 0
    --dry-run
)

add_test(NAME opengil_cli_ui_import_pixel_dry_run
  COMMAND opengil ui import-pixel
    --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test1.gil
    --png ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pixel-2x2.png
    --pixel-size 8
    --dry-run
)

add_test(NAME opengil_cli_pixel_art_import_decoration_dry_run
  COMMAND opengil pixel-art import-decoration
    --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test1.gil
    --png ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pixel-2x2.png
    --prefab-id 1077939001
    --asset-id 20001220
    --pixel-size 0.25
    --dry-run
)

add_test(NAME opengil_cli_unknown_command_fails
  COMMAND opengil frobnicate --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test1.gil
)
set_tests_properties(opengil_cli_unknown_command_fails PROPERTIES WILL_FAIL TRUE)

add_test(NAME opengil_cli_create_prefab_duplicate_id_fails
  COMMAND opengil create-prefab
    --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test1.gil
    --asset-id 20001220
    --prefab-id 1086324737
    --dry-run
)
set_tests_properties(opengil_cli_create_prefab_duplicate_id_fails PROPERTIES WILL_FAIL TRUE)

add_test(NAME opengil_python_bindings
  COMMAND ${Python_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_python_bindings.py
    $<TARGET_FILE_DIR:opengil_python>
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
)
