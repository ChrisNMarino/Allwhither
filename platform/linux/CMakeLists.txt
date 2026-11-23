cmake_minimum_required(VERSION 3.16)
project(omni_linux C)

set(OMNI_PROJECT "example" CACHE STRING "Omni project to build")
# Older builds stored an example/helloworld allowlist in the CMake cache.
get_property(_omni_project_strings CACHE OMNI_PROJECT PROPERTY STRINGS)
if(_omni_project_strings)
  set_property(CACHE OMNI_PROJECT PROPERTY STRINGS)
endif()
unset(_omni_project_strings)
if(NOT OMNI_PROJECT MATCHES "^[A-Za-z0-9][A-Za-z0-9._-]*$")
  message(FATAL_ERROR "OMNI_PROJECT must be a project directory name.")
endif()

set(OMNI_PROJECT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../projects/${OMNI_PROJECT}")
if(NOT EXISTS "${OMNI_PROJECT_DIRECTORY}/${OMNI_PROJECT}.omni")
  message(FATAL_ERROR "Missing ${OMNI_PROJECT}.omni in ${OMNI_PROJECT_DIRECTORY}.")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(JSON_GLIB REQUIRED IMPORTED_TARGET json-glib-1.0)
pkg_check_modules(JSC REQUIRED IMPORTED_TARGET javascriptcoregtk-4.1)
pkg_check_modules(SOUP3 REQUIRED IMPORTED_TARGET libsoup-3.0)
pkg_check_modules(CRYPTO REQUIRED IMPORTED_TARGET libcrypto)

set(OMNI_EMBEDDED_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/embedded_resources_data.c")
file(GLOB_RECURSE OMNI_PROJECT_FILES CONFIGURE_DEPENDS
  "${OMNI_PROJECT_DIRECTORY}/*.omni"
  "${OMNI_PROJECT_DIRECTORY}/*.json"
  "${OMNI_PROJECT_DIRECTORY}/*.js")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
  OUTPUT "${OMNI_EMBEDDED_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/embed_resources.py"
          --platform c
          --project-dir "${OMNI_PROJECT_DIRECTORY}"
          --output "${OMNI_EMBEDDED_SOURCE}"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/embed_resources.py"
    ${OMNI_PROJECT_FILES}
  VERBATIM
)
file(REMOVE_RECURSE "${CMAKE_CURRENT_BINARY_DIR}/scripts")

add_executable(omni-linux
  main.c
  control_renderer.c
  menu_renderer.c
  ui_config.c
  action_runtime.c
  http_client.c
  file_client.c
  crypto_client.c
  embedded_resources.c
  "${OMNI_EMBEDDED_SOURCE}"
)
set_target_properties(omni-linux PROPERTIES OUTPUT_NAME "${OMNI_PROJECT}")
target_compile_features(omni-linux PRIVATE c_std_11)
target_include_directories(omni-linux PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(omni-linux PRIVATE PkgConfig::GTK4 PkgConfig::JSON_GLIB PkgConfig::JSC PkgConfig::SOUP3 PkgConfig::CRYPTO)
target_compile_definitions(omni-linux PRIVATE OMNI_PROJECT_NAME="${OMNI_PROJECT}")
