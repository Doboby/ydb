
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(cpp-monlib-encode)
target_link_libraries(cpp-monlib-encode PUBLIC
  contrib-libs-cxxsupp
  yutil
  cpp-monlib-metrics
  tools-enum_parser-enum_serialization_runtime
)
target_sources(cpp-monlib-encode PRIVATE
  ${CMAKE_SOURCE_DIR}/library/cpp/monlib/encode/encoder.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/monlib/encode/encoder_state.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/monlib/encode/format.cpp
)
generate_enum_serilization(cpp-monlib-encode
  ${CMAKE_SOURCE_DIR}/library/cpp/monlib/encode/encoder_state_enum.h
  GEN_HEADER
  ${CMAKE_BINARY_DIR}/library/cpp/monlib/encode/encoder_state_enum.h_serialized.h
  INCLUDE_HEADERS
  library/cpp/monlib/encode/encoder_state_enum.h
)
