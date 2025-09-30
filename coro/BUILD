cc_library(
    name = "coro",
    hdrs = ["coro.h"],
    deps = [
        "//:nfuture",
    ],
)

cc_test(
    name = "coro_test",
    srcs = ["coro_test.cc"],
    deps = [
        ":coro",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
