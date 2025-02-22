
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(api-grpc)
set_property(TARGET api-grpc PROPERTY
  PROTOC_EXTRA_OUTS .grpc.pb.cc .grpc.pb.h
)
target_link_libraries(api-grpc PUBLIC
  contrib-libs-cxxsupp
  yutil
  contrib-libs-grpc
  api-protos
  contrib-libs-protobuf
)
target_proto_messages(api-grpc PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/fq_v1.proto
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/ydb_auth_v1.proto
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/ydb_cms_v1.proto
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/ydb_coordination_v1.proto
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/ydb_discovery_v1.proto
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/ydb_export_v1.proto
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/ydb_import_v1.proto
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/ydb_monitoring_v1.proto
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/ydb_operation_v1.proto
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/ydb_rate_limiter_v1.proto
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/ydb_scheme_v1.proto
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/ydb_scripting_v1.proto
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/ydb_table_v1.proto
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/ydb_topic_v1.proto
  ${CMAKE_SOURCE_DIR}/ydb/public/api/grpc/yq_v1.proto
)
target_proto_addincls(api-grpc
  ./
  ${CMAKE_SOURCE_DIR}/
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/contrib/libs/protobuf/src
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/contrib/libs/protobuf/src
)
target_proto_outs(api-grpc
  --cpp_out=${CMAKE_BINARY_DIR}/
  --cpp_styleguide_out=${CMAKE_BINARY_DIR}/
)
target_proto_plugin(api-grpc
  grpc_cpp
  grpc_cpp
)
