
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(cpp-testing-common)
target_link_libraries(cpp-testing-common PUBLIC
  contrib-libs-cxxsupp
  yutil
  library-cpp-json
)
target_sources(cpp-testing-common PRIVATE
  ${CMAKE_SOURCE_DIR}/library/cpp/testing/common/env.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/testing/common/network.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/testing/common/probe.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/testing/common/scope.cpp
)
