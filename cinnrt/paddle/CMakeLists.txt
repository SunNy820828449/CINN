proto_library(paddle_framework_proto SRCS framework.proto)

add_subdirectory(cpp)
add_subdirectory(pb)

core_gather_headers()

gather_srcs(cinnrt_src SRCS
    model_parser.cc
    scope.cc
    tensor.cc
    )

foreach(cpp ${SRCS})
  set(cinnrt_src
    "${cinnrt_src};cinnrt/paddle/${cpp}"
    CACHE INTERNAL "")
endforeach()

file(GLOB includes LIST_DIRECTORIES false RELATIVE ${CMAKE_SOURCE_DIR} *.h)

foreach(header ${includes})
  set(core_includes "${core_includes};${header}" CACHE INTERNAL "")
endforeach()
