from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout
from conan.tools.files import get, copy
import os


class KickCATRecipe(ConanFile):
    name = "kickcat"
    url = "https://github.com/conan-io/conan-center-index"
    homepage = "https://github.com/Siviuze/KickCAT"
    description = "Thin EtherCAT stack designed to be embedded in a more complex software and with efficiency in mind"
    license = "CeCILL-C"
    topics = ("ethercat")
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    def source(self):
        get(self, **self.conan_data["sources"][self.version])

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def layout(self):
        cmake_layout(self, src_folder="src")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure(variables={"BUILD_UNIT_TESTS": "OFF",
                                   "BUILD_EXAMPLES": "OFF",
                                   "BUILD_SIMULATION": "OFF",
                                   "BUILD_TOOLS": "OFF"})
        cmake.build()

    def package(self):
        copy(self, "*.h", os.path.join(self.source_folder, "include"),
             os.path.join(self.package_folder, "include"))
        copy(self, "*.a", self.build_folder,
             os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.so", self.build_folder,
             os.path.join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["kickcat"]
