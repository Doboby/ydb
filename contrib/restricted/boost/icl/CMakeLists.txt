
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(restricted-boost-icl INTERFACE)
target_include_directories(restricted-boost-icl INTERFACE
  ${CMAKE_SOURCE_DIR}/contrib/restricted/boost/icl/include
)
target_link_libraries(restricted-boost-icl INTERFACE
  contrib-libs-cxxsupp
  yutil
  restricted-boost-assert
  restricted-boost-concept_check
  restricted-boost-config
  restricted-boost-container
  restricted-boost-core
  restricted-boost-date_time
  restricted-boost-detail
  restricted-boost-iterator
  restricted-boost-move
  restricted-boost-mpl
  restricted-boost-rational
  restricted-boost-static_assert
  restricted-boost-type_traits
  restricted-boost-utility
)
