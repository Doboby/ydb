
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(cpp-actors-memory_log)
target_link_libraries(cpp-actors-memory_log PUBLIC
  contrib-libs-cxxsupp
  yutil
  cpp-threading-queue
  contrib-libs-linuxvdso
  cpp-deprecated-atomic
)
target_sources(cpp-actors-memory_log PRIVATE
  ${CMAKE_SOURCE_DIR}/library/cpp/actors/memory_log/memlog.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/actors/memory_log/mmap.cpp
)
