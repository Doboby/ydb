
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(common-ip_base-lib)
target_compile_options(common-ip_base-lib PRIVATE
  -DUDF_ABI_VERSION_MAJOR=2
  -DUDF_ABI_VERSION_MINOR=9
  -DUDF_ABI_VERSION_PATCH=0
)
target_link_libraries(common-ip_base-lib PUBLIC
  contrib-libs-cxxsupp
  yutil
  yql-public-udf
)
target_sources(common-ip_base-lib PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/udfs/common/ip_base/lib/ip_base_udf.cpp
)
