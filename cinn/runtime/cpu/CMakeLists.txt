core_gather_headers()


gather_srcs(cinnapi_src SRCS
    host_intrinsics.cc
    mkl_math.cc
    cblas.cc
    mkldnn_math.cc
    thread_backend.cc)

if (NOT WITH_CUDA)
cc_test(test_mkl_math SRCS mkl_math_test.cc mkl_math.cc DEPS cinncore)
endif()
cc_test(test_host_intrinsics SRCS host_intrinsics_test.cc DEPS cinncore)
cc_test(test_mkldnn_math SRCS mkldnn_math_test.cc mkldnn_math.cc DEPS cinncore)
