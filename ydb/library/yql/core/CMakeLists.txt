
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(library-yql-core)
target_compile_options(library-yql-core PRIVATE
  -DUSE_CURRENT_UDF_ABI_VERSION
)
target_link_libraries(library-yql-core PUBLIC
  contrib-libs-cxxsupp
  yutil
  library-cpp-enumbitset
  library-cpp-random_provider
  cpp-threading-future
  library-cpp-time_provider
  library-cpp-yson
  cpp-yson-node
  library-yql-ast
  yql-core-file_storage
  yql-core-sql_types
  library-yql-minikql
  library-yql-protos
  yql-public-udf
  public-udf-tz
  yql-sql-settings
  library-yql-utils
  yql-utils-log
  yql-core-expr_nodes
  providers-common-proto
  tools-enum_parser-enum_serialization_runtime
)
target_sources(library-yql-core PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_callable_transform.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_csv.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_execution.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_expr_constraint.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_expr_csee.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_expr_optimize.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_expr_type_annotation.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_gc_transformer.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_graph_transformer.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_holding_file_storage.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_join.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_library_compiler.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_opt_aggregate.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_opt_proposed_by_data.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_opt_range.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_opt_rewrite_io.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_opt_utils.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_opt_window.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_type_annotation.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_type_helpers.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_udf_index.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_udf_index_package_set.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_udf_resolver.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_user_data.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_user_data_storage.cpp
)
generate_enum_serilization(library-yql-core
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_data_provider.h
  INCLUDE_HEADERS
  ydb/library/yql/core/yql_data_provider.h
)
generate_enum_serilization(library-yql-core
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_user_data.h
  INCLUDE_HEADERS
  ydb/library/yql/core/yql_user_data.h
)
generate_enum_serilization(library-yql-core
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_atom_enums.h
  INCLUDE_HEADERS
  ydb/library/yql/core/yql_atom_enums.h
)
generate_enum_serilization(library-yql-core
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/core/yql_type_annotation.h
  INCLUDE_HEADERS
  ydb/library/yql/core/yql_type_annotation.h
)
