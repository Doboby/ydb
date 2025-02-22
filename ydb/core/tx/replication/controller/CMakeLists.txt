
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(tx-replication-controller)
target_compile_options(tx-replication-controller PRIVATE
  -DUSE_CURRENT_UDF_ABI_VERSION
)
target_link_libraries(tx-replication-controller PUBLIC
  contrib-libs-cxxsupp
  yutil
  ydb-core-base
  core-engine-minikql
  ydb-core-protos
  ydb-core-tablet_flat
  tx-replication-ydb_proxy
  ydb-core-util
  ydb-core-ydb_convert
  tools-enum_parser-enum_serialization_runtime
)
target_sources(tx-replication-controller PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/controller.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/discoverer.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/dst_creator.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/logging.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/private_events.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/replication.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/stream_creator.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/sys_params.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/target_base.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/target_table.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/target_with_stream.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/tx_assign_stream_name.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/tx_create_dst_result.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/tx_create_replication.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/tx_create_stream_result.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/tx_discovery_result.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/tx_drop_replication.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/tx_init.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/tx_init_schema.cpp
)
generate_enum_serilization(tx-replication-controller
  ${CMAKE_SOURCE_DIR}/ydb/core/tx/replication/controller/replication.h
  INCLUDE_HEADERS
  ydb/core/tx/replication/controller/replication.h
)
