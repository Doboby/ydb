
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(restricted-boost-unordered INTERFACE)
target_include_directories(restricted-boost-unordered INTERFACE
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/unordered/include
)
target_link_libraries(restricted-boost-unordered INTERFACE
  contrib-libs-cxxsupp
  yutil
  restricted-boost-assert
  restricted-boost-config
  restricted-boost-container
  restricted-boost-container_hash
  restricted-boost-core
  restricted-boost-detail
  restricted-boost-move
  restricted-boost-predef
  restricted-boost-preprocessor
  restricted-boost-smart_ptr
  restricted-boost-throw_exception
  restricted-boost-tuple
  restricted-boost-type_traits
)
