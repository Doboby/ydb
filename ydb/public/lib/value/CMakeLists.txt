
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(public-lib-value)
target_link_libraries(public-lib-value PUBLIC
  contrib-libs-cxxsupp
  yutil
  contrib-libs-protobuf
  cpp-string_utils-base64
  ydb-core-protos
  library-mkql_proto-protos
  public-lib-scheme_types
)
target_sources(public-lib-value PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/public/lib/value/value.cpp
)
