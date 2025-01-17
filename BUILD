config_setting(
    name = "coroutines",
    values = {"define": "coroutines=true"},
)

cc_library(
    name = "traits",
    hdrs = ["traits.h"],
)

cc_library(
    name = "mfuture",
    hdrs = ["mfuture.h"],
    deps = [":traits"],
)

cc_test(
    name = "mfuture_test",
    srcs = [
        "mfuture_test.cc",
    ],
    deps = [
        ":mfuture",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "nfuture",
    hdrs = ["nfuture.h"],
    deps = [":traits"],
    defines = select({
        ":coroutines": ["COROUTINES_ENABLED"],
        "//conditions:default": [],
    }),
    copts = select({
        ":coroutines": ["-fcoroutines"],
        "//conditions:default": [],
    }),
)

cc_test(
    name = "nfuture_test",
    srcs = [
        "nfuture_test.cc",
    ],
    deps = [
        ":nfuture",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "nfuture_perf",
    srcs = [
        "nfuture_perf.cc",
    ],
    deps = [
        ":nfuture",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "coro_test",
    srcs = [
        "coro_test.cc",
    ],
)