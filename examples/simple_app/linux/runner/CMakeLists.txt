cmake_minimum_required(VERSION 3.13)
project(runner LANGUAGES C CXX)

set(PIGEON_RUNNER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../platform/pigeon/runner/gobject")
set(PLATFORM_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../platform/include")
set(SPARROW_PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../proto")
set(SPARROW_RUNNER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/runner")
set(RAPIDJSON_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../platform/rapidjson/include")

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(GEN_CLIENT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/sparrow-ipc-v1-client-protocol.h")
set(GEN_CLIENT_CODE "${CMAKE_CURRENT_BINARY_DIR}/sparrow-ipc-v1-protocol.c")

add_custom_command(
  OUTPUT "${GEN_CLIENT_HEADER}"
  COMMAND ${WAYLAND_SCANNER} client-header "${SPARROW_PROTO_DIR}/sparrow-ipc-v1.xml" "${GEN_CLIENT_HEADER}"
  DEPENDS "${SPARROW_PROTO_DIR}/sparrow-ipc-v1.xml"
  COMMENT "Generating sparrow-ipc-v1 client header"
)

add_custom_command(
  OUTPUT "${GEN_CLIENT_CODE}"
  COMMAND ${WAYLAND_SCANNER} private-code "${SPARROW_PROTO_DIR}/sparrow-ipc-v1.xml" "${GEN_CLIENT_CODE}"
  DEPENDS "${SPARROW_PROTO_DIR}/sparrow-ipc-v1.xml"
  COMMENT "Generating sparrow-ipc-v1 protocol code"
)

# Define the application target. To change its name, change BINARY_NAME in the
# top-level CMakeLists.txt, not the value here, or `flutter run` will no longer
# work.
#
# Any new source files that you add to the application should be added here.
add_executable(${BINARY_NAME}
  "main.cc"
  "${PIGEON_RUNNER_DIR}/messages.g.cc"
  "my_application.cc"
  "${SPARROW_RUNNER_DIR}/ipc_client.cpp"
  "${GEN_CLIENT_CODE}"
  "${GEN_CLIENT_HEADER}"
  "${FLUTTER_MANAGED_DIR}/generated_plugin_registrant.cc"
)

# Apply the standard set of build settings. This can be removed for applications
# that need different build settings.
apply_standard_settings(${BINARY_NAME})

# Add preprocessor definitions for the application ID.
add_definitions(-DAPPLICATION_ID="${APPLICATION_ID}")

# Add dependency libraries. Add any application-specific dependencies here.
target_link_libraries(${BINARY_NAME} PRIVATE flutter)
target_link_libraries(${BINARY_NAME} PRIVATE PkgConfig::GTK)
target_link_libraries(${BINARY_NAME} PRIVATE PkgConfig::WAYLAND_CLIENT)

target_include_directories(${BINARY_NAME} PRIVATE "${CMAKE_SOURCE_DIR}")
target_include_directories(${BINARY_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(${BINARY_NAME} PRIVATE "${PIGEON_RUNNER_DIR}")
target_include_directories(${BINARY_NAME} PRIVATE "${PLATFORM_INCLUDE_DIR}")
target_include_directories(${BINARY_NAME} PRIVATE "${SPARROW_RUNNER_DIR}")
target_include_directories(${BINARY_NAME} PRIVATE "${RAPIDJSON_INCLUDE_DIR}")
