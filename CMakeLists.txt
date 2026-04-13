cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(
  HolographySim
  VERSION 0.1.0
  LANGUAGES CXX
)

set(CPM_DOWNLOAD_VERSION 0.40.2)
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")

if(NOT EXISTS ${CPM_DOWNLOAD_LOCATION})
  message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
  file(DOWNLOAD
    "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
    ${CPM_DOWNLOAD_LOCATION}
  )
endif()

include(${CPM_DOWNLOAD_LOCATION})

CPMAddPackage("gh:intel/cicd-repo-infrastructure#3e2bef0")

add_versioned_package(
  NAME Boost
  VERSION 1.88.0
  GITHUB_REPOSITORY "boostorg/boost"
  GIT_TAG "boost-1.88.0"
)

add_versioned_package("gh:fmtlib/fmt#9.1.0")

CPMAddPackage("gh:catchorg/Catch2@3.7.1")

add_versioned_package(
  NAME lager
  GITHUB_REPOSITORY "arximboldi/lager"
  GIT_TAG "8fd338d"
  OPTIONS
  "lager_BUILD_TESTS OFF"
  "lager_BUILD_FAILURE_TESTS OFF"
  "lager_BUILD_EXAMPLES OFF"
  "lager_BUILD_DEBUGGER_EXAMPLES OFF"
  "lager_BUILD_DOCS OFF"
  "lager_EMBED_RESOURCES_PATH OFF"
)

add_versioned_package(
  NAME zug
  GITHUB_REPOSITORY "arximboldi/zug"
  GIT_TAG "d7e814b"
  OPTIONS
  "zug_BUILD_TESTS OFF"
  "zug_BUILD_EXAMPLES OFF"
  "zug_BUILD_DOCS OFF"
)

add_versioned_package(
  NAME immer
  GITHUB_REPOSITORY "arximboldi/immer"
  GIT_TAG "8872e23"
  OPTIONS
  "immer_BUILD_TESTS OFF"
  "immer_BUILD_EXAMPLES OFF"
  "immer_BUILD_DOCS OFF"
)

# ── OpenGL + windowing (shared by ImGui frontend) ────────────────────────────
find_package(OpenGL REQUIRED)

CPMAddPackage(
  NAME glfw
  GITHUB_REPOSITORY "glfw/glfw"
  GIT_TAG "3.4"
  OPTIONS
    "GLFW_BUILD_TESTS OFF"
    "GLFW_BUILD_EXAMPLES OFF"
    "GLFW_BUILD_DOCS OFF"
    "GLFW_INSTALL OFF"
    "GLFW_BUILD_WAYLAND OFF"
    "GLFW_BUILD_X11 ON"
)

CPMAddPackage(
  NAME glad
  GITHUB_REPOSITORY "Dav1dde/glad"
  GIT_TAG "v0.1.36"
  OPTIONS
    "GLAD_PROFILE \"core\""
    "GLAD_API \"gl=3.3\""
    "GLAD_GENERATOR \"c\""
    "GLAD_INSTALL OFF"
)

# ── ImGui — docking branch (required for DockSpace) ───────────────────────────
CPMAddPackage(
  NAME imgui
  GITHUB_REPOSITORY "ocornut/imgui"
  GIT_TAG "docking"
  DOWNLOAD_ONLY YES
)

if(imgui_ADDED)
  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  )
  target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
  )
  target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)
  target_link_libraries(imgui PUBLIC glfw OpenGL::GL glad)
endif()

# ── JetBrains Mono font ───────────────────────────────────────────────────────
CPMAddPackage(
  NAME JetBrainsMono
  GITHUB_REPOSITORY "JetBrains/JetBrainsMono"
  GIT_TAG "v2.304"
  DOWNLOAD_ONLY YES
)

# ── FTXUI ─────────────────────────────────────────────────────────────────────
CPMAddPackage(
  NAME ftxui
  GITHUB_REPOSITORY "ArthurSonzogni/FTXUI"
  GIT_TAG "v5.0.0"
  OPTIONS
    "FTXUI_BUILD_EXAMPLES OFF"
    "FTXUI_BUILD_TESTS OFF"
    "FTXUI_BUILD_DOCS OFF"
    "FTXUI_ENABLE_INSTALL OFF"
)

# ── Engine static library (shared by all three frontends + tests) ─────────────
set(ENGINE_SOURCES
  src/space/medium.cpp
  src/space/utility/spacial_nav.cpp
  src/space/object/object_descriptor.cpp
  src/space/object/object.cpp
  src/space/grid.cpp
  src/transducer/transducer.cpp
  src/transducer/transducer_array.cpp
  src/world.cpp
  src/simulation_model.cpp
  src/simulation/inverse_solver.cpp
  src/simulation/ray_traversal.cpp
  src/simulation/simulation_state.cpp
  src/simulation/field_query.cpp
)

add_library(acoustic_engine STATIC ${ENGINE_SOURCES})

target_include_directories(acoustic_engine PUBLIC src)

target_link_libraries_system(acoustic_engine
  PUBLIC
  Boost::mp11
  Boost::hana
  Boost::intrusive
  fmt
  lager
  immer
  zug
)

# ── acoustic_imgui ────────────────────────────────────────────────────────────
add_executable(acoustic_imgui
  src/main_imgui.cpp
  src/gui/imgui/app.cpp
)

target_include_directories(acoustic_imgui PRIVATE src)

target_link_libraries(acoustic_imgui
  PRIVATE
  acoustic_engine
  imgui
  glfw
  OpenGL::GL
  glad
)

if(JetBrainsMono_ADDED)
  target_compile_definitions(acoustic_imgui PRIVATE
    JETBRAINS_MONO_REGULAR="${JetBrainsMono_SOURCE_DIR}/fonts/ttf/JetBrainsMono-Regular.ttf"
    JETBRAINS_MONO_BOLD="${JetBrainsMono_SOURCE_DIR}/fonts/ttf/JetBrainsMono-Bold.ttf"
  )
endif()

# ── acoustic_ftxui ────────────────────────────────────────────────────────────
add_executable(acoustic_ftxui
  src/main_ftxui.cpp
  src/gui/ftxui/app.cpp
)

target_include_directories(acoustic_ftxui PRIVATE src)

target_link_libraries(acoustic_ftxui
  PRIVATE
  acoustic_engine
  ftxui::screen
  ftxui::dom
  ftxui::component
)

# ── Tests ─────────────────────────────────────────────────────────────────────
add_executable(holo_tests
  tests/test_space.cpp
  tests/test_transducer.cpp
  tests/test_simulation.cpp
)

target_include_directories(holo_tests PUBLIC src)

target_link_libraries(holo_tests
  PRIVATE
  acoustic_engine
  Catch2::Catch2WithMain
)

include(CTest)
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(holo_tests)
