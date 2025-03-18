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
)

cc_library(
    name = "nfuture_utils",
    hdrs = [
        "do_with.h",
        "do_until.h",
        "latch.h",
        "semaphore.h",
        "when_all.h",
    ],
    deps = [
        ":nfuture"
    ],
)

cc_test(
    name = "nfuture_test",
    srcs = [
        "nfuture_test.cc",
    ],
    deps = [
        ":nfuture",
        ":nfuture_utils",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)