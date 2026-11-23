cmake_minimum_required(VERSION 3.16)
project(allwhither_linux C)

set(ALLWHITHER_PROJECT "example" CACHE STRING "Allwhither project to build")
# Older builds stored an example/helloworld allowlist in the CMake cache.
get_property(_allwhither_project_strings CACHE ALLWHITHER_PROJECT PROPERTY STRINGS)
if(_allwhither_project_strings)
  set_property(CACHE ALLWHITHER_PROJECT PROPERTY STRINGS)
endif()
unset(_allwhither_project_strings)
if(NOT ALLWHITHER_PROJECT MATCHES "^[A-Za-z0-9][A-Za-z0-9._-]*$")
  message(FATAL_ERROR "ALLWHITHER_PROJECT must be a project directory name.")
endif()

set(ALLWHITHER_PROJECT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../projects/${ALLWHITHER_PROJECT}")
if(NOT EXISTS "${ALLWHITHER_PROJECT_DIRECTORY}/${ALLWHITHER_PROJECT}.json")
  message(FATAL_ERROR "Missing ${ALLWHITHER_PROJECT}.json in ${ALLWHITHER_PROJECT_DIRECTORY}.")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(JSON_GLIB REQUIRED IMPORTED_TARGET json-glib-1.0)
pkg_check_modules(JSC REQUIRED IMPORTED_TARGET javascriptcoregtk-4.1)
pkg_check_modules(SOUP3 REQUIRED IMPORTED_TARGET libsoup-3.0)
pkg_check_modules(CRYPTO REQUIRED IMPORTED_TARGET libcrypto)

set(ALLWHITHER_EMBEDDED_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/embedded_resources_data.c")
file(GLOB_RECURSE ALLWHITHER_PROJECT_FILES CONFIGURE_DEPENDS
  "${ALLWHITHER_PROJECT_DIRECTORY}/*.json"
  "${ALLWHITHER_PROJECT_DIRECTORY}/*.js")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
  OUTPUT "${ALLWHITHER_EMBEDDED_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/embed_resources.py"
          --platform c
          --project-dir "${ALLWHITHER_PROJECT_DIRECTORY}"
          --output "${ALLWHITHER_EMBEDDED_SOURCE}"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/embed_resources.py"
    ${ALLWHITHER_PROJECT_FILES}
  VERBATIM
)
file(REMOVE_RECURSE "${CMAKE_CURRENT_BINARY_DIR}/scripts")

add_executable(allwhither-linux
  main.c
  control_renderer.c
  menu_renderer.c
  ui_config.c
  action_runtime.c
  http_client.c
  file_client.c
  crypto_client.c
  embedded_resources.c
  "${ALLWHITHER_EMBEDDED_SOURCE}"
)
set_target_properties(allwhither-linux PROPERTIES OUTPUT_NAME "${ALLWHITHER_PROJECT}")
target_compile_features(allwhither-linux PRIVATE c_std_11)
target_include_directories(allwhither-linux PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(allwhither-linux PRIVATE PkgConfig::GTK4 PkgConfig::JSON_GLIB PkgConfig::JSC PkgConfig::SOUP3 PkgConfig::CRYPTO)
target_compile_definitions(allwhither-linux PRIVATE ALLWHITHER_PROJECT_NAME="${ALLWHITHER_PROJECT}")
