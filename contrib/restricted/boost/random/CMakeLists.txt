
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(restricted-boost-random)
target_include_directories(restricted-boost-random PUBLIC
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/random/include
)
target_link_libraries(restricted-boost-random PUBLIC
  contrib-libs-cxxsupp
  yutil
  restricted-boost-assert
  restricted-boost-config
  restricted-boost-core
  restricted-boost-integer
  restricted-boost-io
  restricted-boost-math
  restricted-boost-mpl
  restricted-boost-range
  restricted-boost-static_assert
  restricted-boost-system
  restricted-boost-throw_exception
  restricted-boost-type_traits
  restricted-boost-utility
)
target_sources(restricted-boost-random PRIVATE
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/random/src/random_device.cpp
)
