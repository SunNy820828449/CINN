core_gather_headers()

gather_srcs(cinnrt_src SRCS
    cinnrt_api.cc
    )

cc_test(test_cinnrt_api SRCS cinnrt_api_test.cc DEPS cinnrt ${MLIR_IR_LIBS})
