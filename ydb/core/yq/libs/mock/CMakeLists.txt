
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(yq-libs-mock)
target_compile_options(yq-libs-mock PRIVATE
  -DUSE_CURRENT_UDF_ABI_VERSION
)
target_link_libraries(yq-libs-mock PUBLIC
  contrib-libs-cxxsupp
  yutil
  cpp-actors-core
  cpp-json-yson
  cpp-monlib-dynamic_counters
  library-cpp-random_provider
  library-cpp-time_provider
  library-cpp-yson
  cpp-yson-node
  ydb-core-base
  ydb-core-protos
  yq-libs-actors
  yq-libs-common
  yq-libs-db_schema
  libs-shared_resources-interface
  ydb-library-mkql_proto
  library-yql-ast
  yql-core-facade
  core-services-mounts
  library-yql-minikql
  yql-minikql-comp_nodes
  yql-public-issue
  public-issue-protos
  yql-sql-settings
  api-protos
  cpp-client-ydb_table
  providers-clickhouse-provider
  providers-common-codec
  providers-common-comp_nodes
  providers-common-provider
  common-schema-mkql
  providers-common-udf_resolve
  providers-dq-interface
  providers-dq-provider
  dq-worker_manager-interface
  providers-ydb-provider
)
target_sources(yq-libs-mock PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/core/yq/libs/mock/yql_mock.cpp
)
