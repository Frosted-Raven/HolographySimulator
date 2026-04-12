cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(
  em
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
#add_versioned_package("gh:intel/cpp-std-extensions#603ba35")

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


add_executable(holo
  src/main.cpp
  src/space/medium.cpp
  src/space/utility/spacial_nav.cpp
  src/space/object/object_descriptor.cpp
  src/space/object/object.cpp
  src/space/grid.cpp
  src/transducer/transducer.cpp
  src/transducer/transducer_array.cpp
)

target_include_directories(holo
  PUBLIC
    src
)

target_link_libraries_system(holo
  PRIVATE
  Boost::mp11
  Boost::hana
  Boost::intrusive
  fmt
  lager
  immer
  zug
)
