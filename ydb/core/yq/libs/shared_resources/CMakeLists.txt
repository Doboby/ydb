
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(yq-libs-shared_resources)
target_compile_options(yq-libs-shared_resources PRIVATE
  -DUSE_CURRENT_UDF_ABI_VERSION
)
target_link_libraries(yq-libs-shared_resources PUBLIC
  contrib-libs-cxxsupp
  yutil
  cpp-actors-core
  cpp-monlib-dynamic_counters
  ydb-core-protos
  libs-control_plane_storage-proto
  yq-libs-events
  libs-quota_manager-events
  libs-shared_resources-interface
  ydb-library-logger
  ydb-library-security
  cpp-client-ydb_driver
  cpp-client-ydb_table
)
target_sources(yq-libs-shared_resources PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/core/yq/libs/shared_resources/db_exec.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/yq/libs/shared_resources/db_pool.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/yq/libs/shared_resources/shared_resources.cpp
)
