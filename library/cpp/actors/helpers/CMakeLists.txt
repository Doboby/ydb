
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(cpp-actors-helpers)
target_link_libraries(cpp-actors-helpers PUBLIC
  contrib-libs-cxxsupp
  yutil
  cpp-actors-core
  cpp-monlib-dynamic_counters
)
target_sources(cpp-actors-helpers PRIVATE
  ${CMAKE_SOURCE_DIR}/library/cpp/actors/helpers/activeactors.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/actors/helpers/flow_controlled_queue.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/actors/helpers/selfping_actor.cpp
)
