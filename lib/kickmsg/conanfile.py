from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout


class KickMsgConan(ConanFile):
    name = "kickmsg"
    version = "0.1.0"
    license = "CeCILL-C"
    description = "Lock-free shared-memory messaging library"
    url = "https://github.com/leducphil/kickmsg"

    settings = "os", "compiler", "build_type", "arch"
    options = {
        "unit_tests": [True, False],
    }
    default_options = {
        "unit_tests": False,
    }

    exports_sources = (
        "CMakeLists.txt",
        "include/*",
        "src/*",
        "tests/*",
        "examples/*",
        "LICENSE",
    )

    def requirements(self):
        if self.options.unit_tests:
            self.requires("gtest/1.15.0")

    def configure(self):
        if self.options.unit_tests:
            self.options["gtest"].build_gmock = True

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        if self.options.unit_tests:
            tc.variables["KICKMSG_BUILD_TESTS"] = "ON"
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["kickmsg"]
        if self.settings.os in ("Linux", "FreeBSD"):
            self.cpp_info.system_libs = ["rt", "pthread"]
        elif self.settings.os == "Macos":
            self.cpp_info.system_libs = ["pthread"]
