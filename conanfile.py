"""Conan recipe that installs the dependencies and generates the CMake presets.

Started from ``conan new cmake_lib`` and cut to what a consumer needs. Each
option passes one CMake cache variable through when it is given, so the
CMakeLists.txt defaults stay the only defaults, and the options given name the
build folder and the generated presets. A value that names no configuration,
such as a path, goes through ``tools.cmake.cmaketoolchain:extra_variables``.
"""

from typing import ClassVar

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout

_CMAKE_VARIABLES = {
    "sanitizer": "LOB_SANITIZER",
    "fuzz": "LOB_BUILD_FUZZ",
    "tests": "LOB_BUILD_TESTS",
    "bench": "LOB_BUILD_BENCH",
    "coverage": "LOB_COVERAGE",
    "lto": "LOB_ENABLE_LTO",
    "pgo": "LOB_PGO",
}


class OrderbooksRecipe(ConanFile):
    """Dependencies, toolchain and presets for the engine, its tests and apps."""

    settings = "os", "compiler", "build_type", "arch"
    options: ClassVar = {name: [None, "ANY"] for name in _CMAKE_VARIABLES}
    # The engine uses Boost's header-only libraries, Intrusive and PFR.
    default_options = dict.fromkeys(_CMAKE_VARIABLES) | {"boost/*:header_only": True}

    def requirements(self) -> None:
        """Declare the libraries, each resolved into conan.lock."""
        self.requires("boost/[*]")
        self.requires("hdrhistogram-c/[*]")
        self.test_requires("catch2/[*]")
        self.test_requires("rapidcheck/[*]")
        self.test_requires("magic_enum/[*]")
        self.test_requires("benchmark/[*]")

    def build_requirements(self) -> None:
        """Bring the generator the toolchain names."""
        self.tool_requires("ninja/[*]")

    def validate(self) -> None:
        """Require the language standard the sources are written in."""
        check_min_cppstd(self, 20)

    def layout(self) -> None:
        """Name each build folder and preset after the options given."""
        self.folders.build_folder_vars = [
            f"options.{name}"
            for name in _CMAKE_VARIABLES
            if getattr(self.options, name).value is not None
        ]
        cmake_layout(self)

    def generate(self) -> None:
        """Write the dependency config files, the toolchain and the presets."""
        CMakeDeps(self).generate()
        toolchain = CMakeToolchain(self, generator="Ninja")
        # clang-tidy, include-what-you-use and clangd read the database.
        toolchain.cache_variables["CMAKE_EXPORT_COMPILE_COMMANDS"] = True
        for name, variable in _CMAKE_VARIABLES.items():
            value = getattr(self.options, name).value
            if value is not None:
                toolchain.cache_variables[variable] = value
        toolchain.generate()

    def build(self) -> None:
        """Configure, build and run the tests, as a workflow preset did."""
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        # ctest() runs in parallel over tools.build:jobs; these are the other
        # two settings the test presets carried.
        cmake.ctest(cli_args=["--output-on-failure", "--no-tests=error"])
