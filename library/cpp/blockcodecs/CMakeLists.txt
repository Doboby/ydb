
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(library-cpp-blockcodecs)
target_link_libraries(library-cpp-blockcodecs PUBLIC
  contrib-libs-cxxsupp
  yutil
  cpp-blockcodecs-core
  blockcodecs-codecs-brotli
  blockcodecs-codecs-bzip
  blockcodecs-codecs-fastlz
  blockcodecs-codecs-legacy_zstd06
  blockcodecs-codecs-lz4
  blockcodecs-codecs-lzma
  blockcodecs-codecs-snappy
  blockcodecs-codecs-zlib
  blockcodecs-codecs-zstd
)
target_sources(library-cpp-blockcodecs PRIVATE
  ${CMAKE_SOURCE_DIR}/library/cpp/blockcodecs/codecs.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/blockcodecs/stream.cpp
)
