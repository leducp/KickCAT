from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps
from conan.tools.files import get, copy
import os

required_conan_version = ">=1.55"

class KickCATRecipe(ConanFile):
    name = "kickcat"
    url = "https://github.com/conan-io/conan-center-index"
    homepage = "https://github.com/Siviuze/KickCAT"
    description = "Thin EtherCAT stack designed to be embedded in a more complex software and with efficiency in mind"
    license = "CeCILL-C"
    topics = ("ethercat")
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False], "with_esi_parser": [True, False]}
    default_options = {"shared": False, "fPIC": True, "with_esi_parser": False}

    # Sources are located in the same place as this recipe, copy them to the recipe
    exports_sources = "CMakeLists.txt", "lib/*", "examples/*", "cmake/*"

    # def source(self):
    #     get(self, **self.conan_data["sources"][self.version])

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["ENABLE_ESI_PARSER"] = bool(self.options.with_esi_parser)
        tc.generate()

        deps = CMakeDeps(self)
        deps.generate()

    def requirements(self):
        if self.options.with_esi_parser:
            self.requires("tinyxml2/10.0.0")

    def build(self):
        cmake = CMake(self)
        cmake.configure(variables={"BUILD_UNIT_TESTS": "OFF",
                                   "BUILD_EXAMPLES": "OFF",
                                   "BUILD_SIMULATION": "OFF",
                                   "BUILD_TOOLS": "OFF"})
        cmake.build()

    def package(self):
        src_folders = ["lib/include", "lib/slave/include", "lib/slave/driver/include", "lib/master/include"]
        for folder in src_folders:
            copy(self, "*.h", os.path.join(self.source_folder, folder),
                os.path.join(self.package_folder, "include"))

        copy(self, "*.a", self.build_folder,
             os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.so", self.build_folder,
             os.path.join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["kickcat"]


