
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(yq-libs-db_schema)
target_compile_options(yq-libs-db_schema PRIVATE
  -DUSE_CURRENT_UDF_ABI_VERSION
)
target_link_libraries(yq-libs-db_schema PUBLIC
  contrib-libs-cxxsupp
  yutil
  cpp-client-ydb_params
  cpp-client-ydb_table
)
target_sources(yq-libs-db_schema PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/core/yq/libs/db_schema/db_schema.cpp
)
