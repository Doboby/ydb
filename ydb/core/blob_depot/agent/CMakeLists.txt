
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(core-blob_depot-agent)
target_link_libraries(core-blob_depot-agent PUBLIC
  contrib-libs-cxxsupp
  yutil
  blobstorage-vdisk-common
  ydb-core-blob_depot
  ydb-core-protos
)
target_sources(core-blob_depot-agent PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/agent.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/blob_mapping_cache.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/blocks.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/channel_kind.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/comm.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/garbage.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/proxy.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/query.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/read.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/request.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/status.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/storage_put.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/storage_get.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/storage_block.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/storage_discover.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/storage_range.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/storage_collect_garbage.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/storage_status.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/storage_patch.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/blob_depot/agent/storage_assimilate.cpp
)
