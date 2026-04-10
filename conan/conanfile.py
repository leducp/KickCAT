from conan import ConanFile


class KickCATDev(ConanFile):
    """Local development conanfile — installs dependencies based on build options.

    Options mirror those in scripts/configure.sh and CMakeLists.txt.
    setup_build.sh passes them via -o when calling conan install.
    """
    settings = "os", "compiler", "build_type", "arch"

    options = {
        "unit_tests":       [True, False],
        "benchmarks":       [True, False],
        "esi_parser":       [True, False],
        "simulation":       [True, False],
        "tools":            [True, False],
        "master_examples":  [True, False],
    }
    default_options = {
        "unit_tests":       False,
        "benchmarks":       False,
        "esi_parser":       True,
        "simulation":       True,
        "tools":            True,
        "master_examples":  True,
    }

    generators = "CMakeDeps"

    def requirements(self):
        if self.options.unit_tests:
            self.requires("gtest/1.15.0")

        if self.options.esi_parser:
            self.requires("tinyxml2/10.0.0")

        if self.options.simulation:
            self.requires("nlohmann_json/3.12.0")

        if self.options.tools or self.options.master_examples or self.options.simulation:
            self.requires("argparse/3.2")

        if self.options.benchmarks:
            self.requires("benchmark/1.9.1")

        if self.settings.os == "Windows":
            self.requires("npcap/1.70")

    def configure(self):
        if self.options.unit_tests:
            self.options["gtest"].build_gmock = True
