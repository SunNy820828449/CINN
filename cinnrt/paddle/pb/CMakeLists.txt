core_gather_headers()

gather_srcs(cinnrt_src SRCS
    var_desc.cc
    op_desc.cc
    block_desc.cc
    program_desc.cc
    )

foreach(cpp ${SRCS})
  set(cinnrt_src
    "${cinnrt_src};cinnrt/paddle/pb/${cpp}"
    CACHE INTERNAL "")
endforeach()

file(GLOB includes LIST_DIRECTORIES false RELATIVE ${CMAKE_SOURCE_DIR} *.h)

foreach(header ${includes})
  set(core_includes "${core_includes};${header}" CACHE INTERNAL "")
endforeach()
