from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class RdpSdkCppConan(ConanFile):
    name = "rdp-sdk-cpp"
    license = "Apache-2.0"
    author = "Raft"
    url = "https://github.com/raft-tech/rdp-sdk-cpp"
    description = "C and C++ SDK for client applications interacting with RDP."
    topics = ("rdp", "raft", "sdk", "cpp", "c")
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_wdm": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_wdm": False,
    }
    exports_sources = (
        "CMakeLists.txt",
        "cmake/*",
        "include/*",
        "src/*",
        "tests/*",
        "examples/*",
        "LICENSE",
        "README.md",
    )

    def requirements(self):
        self.requires("libcurl/8.11.1")
        if self.options.with_wdm:
            self.requires("grpc/1.67.1")
            self.requires("protobuf/5.27.0")

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        toolchain = CMakeToolchain(self)
        toolchain.variables["RDP_SDK_BUILD_TESTS"] = False
        toolchain.variables["RDP_SDK_BUILD_EXAMPLES"] = False
        toolchain.variables["RDP_SDK_ENABLE_WDM"] = bool(self.options.with_wdm)
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "RdpSdkCpp")
        self.cpp_info.set_property("cmake_target_name", "Rdp::Sdk")
        self.cpp_info.libs = ["rdp_sdk_cpp"]
