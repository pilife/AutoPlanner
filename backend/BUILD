cc_binary(
    name = "autoplanner",
    srcs = [
        "src/main.cpp",
        "src/auth.cpp",
        "src/auth.h",
        "src/database.cpp",
        "src/database.h",
        "src/models.h",
        "src/routes.cpp",
        "src/routes.h",
        "src/seed.cpp",
        "src/seed.h",
    ],
    copts = select({
        "@bazel_tools//src/conditions:windows": ["/std:c++17"],
        "//conditions:default": ["-std=c++17", "-DCPPHTTPLIB_OPENSSL_SUPPORT"],
    }),
    linkopts = select({
        "@bazel_tools//src/conditions:windows": ["-DEFAULTLIB:ws2_32"],
        "//conditions:default": ["-lpthread", "-lssl", "-lcrypto"],
    }),
    deps = [
        "@cpp_httplib//:httplib",
        "@nlohmann_json//:json",
        "@sqlite3//:sqlite3",
    ],
)
