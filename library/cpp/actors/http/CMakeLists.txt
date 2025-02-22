
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.


find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

add_library(cpp-actors-http)
target_link_libraries(cpp-actors-http PUBLIC
  contrib-libs-cxxsupp
  yutil
  OpenSSL::OpenSSL
  ZLIB::ZLIB
  cpp-actors-core
  cpp-actors-interconnect
  library-cpp-dns
  cpp-monlib-metrics
  cpp-string_utils-quote
)
target_sources(cpp-actors-http PRIVATE
  ${CMAKE_SOURCE_DIR}/library/cpp/actors/http/http_cache.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/actors/http/http_compress.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/actors/http/http_proxy_acceptor.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/actors/http/http_proxy_incoming.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/actors/http/http_proxy_outgoing.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/actors/http/http_proxy.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/actors/http/http_static.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/actors/http/http.cpp
)
