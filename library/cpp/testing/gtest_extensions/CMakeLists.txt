
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(cpp-testing-gtest_extensions)
target_link_libraries(cpp-testing-gtest_extensions PUBLIC
  contrib-libs-cxxsupp
  yutil
  restricted-googletest-googlemock
  restricted-googletest-googletest
)
target_sources(cpp-testing-gtest_extensions PRIVATE
  ${CMAKE_SOURCE_DIR}/library/cpp/testing/gtest_extensions/assertions.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/testing/gtest_extensions/gtest_extensions.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/testing/gtest_extensions/matchers.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/testing/gtest_extensions/pretty_printers.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/testing/gtest_extensions/probe.cpp
)
