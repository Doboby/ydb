
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(library-pretty_types_print-wilson)
target_link_libraries(library-pretty_types_print-wilson PUBLIC
  contrib-libs-cxxsupp
  yutil
  cpp-actors-wilson
)
target_sources(library-pretty_types_print-wilson PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/library/pretty_types_print/wilson/out.cpp
)
