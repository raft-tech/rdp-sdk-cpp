cmake_minimum_required(VERSION 3.24)

project(
    rdp_sdk_cpp
    VERSION 0.0.0
    DESCRIPTION "C and C++ SDK for client applications interacting with RDP."
    LANGUAGES C CXX
)

include(CMakePackageConfigHelpers)
include(FetchContent)
include(GNUInstallDirs)
include(CTest)

option(RDP_SDK_BUILD_TESTS "Build rdp-sdk-cpp tests" ON)
option(RDP_SDK_BUILD_EXAMPLES "Build rdp-sdk-cpp examples" ON)
option(RDP_SDK_ENABLE_WDM "Enable generated WDM gRPC C++ clients" OFF)

set(RDP_SDK_VERSION "${PROJECT_VERSION}" CACHE STRING "SDK version embedded in user-agent")
set(RDP_SDK_WDM_PROTOBUF_CPP_VERSION "v35.1-489cef7b7304.1" CACHE STRING "Buf protocolbuffers/cpp SDK version")
set(RDP_SDK_WDM_GRPC_CPP_VERSION "v1.83.0-489cef7b7304.1" CACHE STRING "Buf grpc/cpp SDK version")

find_package(CURL REQUIRED)

add_library(
    rdp_sdk_cpp
    src/auth.cpp
    src/c_api.cpp
    src/client.cpp
    src/config.cpp
    src/curl_http.cpp
    src/error.cpp
    src/rest.cpp
    src/util.cpp
    src/version.cpp
)
add_library(Rdp::Sdk ALIAS rdp_sdk_cpp)
set_target_properties(rdp_sdk_cpp PROPERTIES EXPORT_NAME Sdk)

target_compile_features(rdp_sdk_cpp PUBLIC cxx_std_17)
target_include_directories(
    rdp_sdk_cpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(rdp_sdk_cpp PRIVATE CURL::libcurl)
target_compile_definitions(
    rdp_sdk_cpp
    PUBLIC
        RDP_SDK_ENABLE_WDM=$<IF:$<BOOL:${RDP_SDK_ENABLE_WDM}>,1,0>
    PRIVATE
        RDP_SDK_VERSION="${RDP_SDK_VERSION}"
)

if(RDP_SDK_ENABLE_WDM)
    find_package(Protobuf CONFIG REQUIRED)
    find_package(gRPC CONFIG REQUIRED)

    FetchContent_Declare(
        raft_wdm_grpc_cpp
        URL "https://buf.build/gen/cmake/raft/wdm/grpc/cpp/${RDP_SDK_WDM_GRPC_CPP_VERSION}"
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(raft_wdm_grpc_cpp)

    get_target_property(RDP_SDK_WDM_SOURCES raft_wdm_grpc_cpp SOURCES)
    list(FILTER RDP_SDK_WDM_SOURCES EXCLUDE REGEX "(^|/)src/google/protobuf/.*\\.cc$")
    set_target_properties(raft_wdm_grpc_cpp PROPERTIES SOURCES "${RDP_SDK_WDM_SOURCES}")

    target_link_libraries(raft_wdm_grpc_cpp PUBLIC gRPC::grpc++ protobuf::libprotobuf)
    target_link_libraries(rdp_sdk_cpp PUBLIC raft_wdm_grpc_cpp gRPC::grpc++ protobuf::libprotobuf)

    install(
        TARGETS raft_wdm_grpc_cpp
        EXPORT RdpSdkCppTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    install(
        DIRECTORY "${raft_wdm_grpc_cpp_SOURCE_DIR}/src/raft/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/raft"
        FILES_MATCHING
        PATTERN "*.h"
    )
endif()

if(RDP_SDK_BUILD_TESTS AND BUILD_TESTING)
    add_executable(rdp_sdk_cpp_tests tests/sdk_tests.cpp)
    target_link_libraries(rdp_sdk_cpp_tests PRIVATE Rdp::Sdk)
    add_test(NAME rdp_sdk_cpp_tests COMMAND rdp_sdk_cpp_tests)

    add_executable(rdp_sdk_c_smoke tests/c_smoke.c)
    target_link_libraries(rdp_sdk_c_smoke PRIVATE Rdp::Sdk)
    add_test(NAME rdp_sdk_c_smoke COMMAND rdp_sdk_c_smoke)
endif()

if(RDP_SDK_BUILD_EXAMPLES)
    add_executable(objects-search-cpp examples/objects-search-cpp/main.cpp)
    target_link_libraries(objects-search-cpp PRIVATE Rdp::Sdk)

    add_executable(objects-search-c examples/objects-search-c/main.c)
    target_link_libraries(objects-search-c PRIVATE Rdp::Sdk)
endif()

install(
    TARGETS rdp_sdk_cpp
    EXPORT RdpSdkCppTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(
    FILES LICENSE TERMS_OF_USE.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/rdp-sdk-cpp"
)

install(
    EXPORT RdpSdkCppTargets
    NAMESPACE Rdp::
    FILE RdpSdkCppTargets.cmake
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/RdpSdkCpp"
)

configure_package_config_file(
    cmake/RdpSdkCppConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/RdpSdkCppConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/RdpSdkCpp"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/RdpSdkCppConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/RdpSdkCppConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/RdpSdkCppConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/RdpSdkCpp"
)

set(CPACK_PACKAGE_NAME "rdp-sdk-cpp")
set(CPACK_PACKAGE_VENDOR "Raft")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES
    "/build[^/]*/"
    "/test_package/build/"
    "/internal/"
    "/CMakeUserPresets.json"
    "/test_package/CMakeUserPresets.json"
    "/.git/"
    "/.github/"
    "/.publicignore"
    "/.DS_Store"
    "__pycache__/"
)
include(CPack)
