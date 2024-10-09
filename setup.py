import os
import re
import subprocess
import sys
import shutil
import platform
from pathlib import Path

from distutils.command.install_data import install_data

from setuptools import find_packages, setup, Extension
from setuptools.command.build_ext import build_ext
from setuptools.command.install_lib import install_lib
from setuptools.command.install_scripts import install_scripts

class CMakeExtension(Extension):
    """An extension to run the cmake build"""

    def __init__(self, name, sourcedir=''):
        super().__init__(name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)

class BuildCMakeExt(build_ext):
    """Builds using cmake instead of the python setuptools implicit build"""

    def run(self):
        """Perform build_cmake before doing the 'normal' stuff"""
        for ext in self.extensions:
            if isinstance(ext, CMakeExtension):
                self.build_cmake(ext)
        super().run()

    def build_cmake(self, ext: Extension):
        """The steps required to build the extension"""
        self.announce("Preparing the build environment", level=3)
        extpath = os.path.abspath(self.get_ext_fullpath(ext.name))
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))

        # required for auto-detection of auxiliary "native" libs
        if not extdir.endswith(os.path.sep):
            extdir += os.path.sep

        bin_dir = os.path.abspath(os.path.join(self.build_temp, 'install'))
        try:
            if os.path.exists(bin_dir):
                shutil.rmtree(bin_dir)
        except OSError as e:
            print("Error: %s - %s." % (e.filename, e.strerror))

        cmake_args = [f"-DVERSION_INFO={self.distribution.get_version()}"]
        cmake_args += ['-DPython3_ROOT_DIR=' + os.path.dirname(sys.executable)]
        cfg = 'Debug' if self.debug else 'Release'
        build_args = ['--config', cfg]
        cmake_args += ['-DCMAKE_BUILD_TYPE=' + cfg]
        install_args = ['--prefix', bin_dir]

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)

        self.announce("Configuring cmake project", level=3)
        self.spawn(['cmake', '-S' + ext.sourcedir, '-B' + self.build_temp] + cmake_args)
        self.announce("Building binaries", level=3)
        self.spawn(["cmake", "--build", self.build_temp] + build_args)
        self.spawn(["cmake", "--install", self.build_temp] + install_args)

        self.announce("Moving built python module", level=3)
        self.distribution.bin_dir = bin_dir

        pyd_path = [os.path.join(root, _pyd) for root, _, files in
                    os.walk(bin_dir) for _pyd in files if
                    os.path.isfile(os.path.join(root, _pyd)) and
                    os.path.splitext(_pyd)[0].startswith('_kburn') and
                    os.path.splitext(_pyd)[-1] in [".pyd", ".so"]][0]

        shutil.move(pyd_path, extpath)

class InstallCMakeLibs(install_lib):
    """Get the libraries from the parent distribution, use those as the outfiles"""

    def run(self):
        """Copy libraries from the bin directory and place them as appropriate"""
        self.announce("Moving library files", level=3)
        self.skip_build = True
        bin_dir = self.distribution.bin_dir

        libs = [os.path.join(root, _lib) for root, _, files in
                os.walk(bin_dir) for _lib in files if
                os.path.isfile(os.path.join(root, _lib)) and
                (os.path.splitext(_lib)[-1] in [".dll", ".so", ".dylib"] or
                 _lib.startswith("lib"))
                and not (_lib.startswith("python") or _lib.startswith("_kburn"))]

        for lib in libs:
            shutil.copy(lib, os.path.join(self.build_dir, os.path.basename(lib)))

        data_files = [os.path.join(self.install_dir, os.path.basename(lib)) for lib in libs]

        self.distribution.data_files = data_files
        self.distribution.run_command("install_data")
        super().run()

class InstallCMakeLibsData(install_data):
    """Just a wrapper to get the install data into the egg-info"""

    def run(self):
        """Outfiles are the libraries that were built using cmake"""
        self.outfiles = self.distribution.data_files

setup(
    name="k230_flash",
    version="0.0.2",
    author="kendryte747",
    author_email="kendryte747@gmail.com",
    description="K230 Burning Tool",
    long_description="",
    packages=['kburn'],
    package_dir={'': 'src/python'},
    ext_modules=[CMakeExtension("_kburn")],
    cmdclass={
        'build_ext': BuildCMakeExt,
        'install_data': InstallCMakeLibsData,
        'install_lib': InstallCMakeLibs
    },
    entry_points={
        'console_scripts': [
            'k230_flash = kburn.k230_flash:main',
        ],
    },
    zip_safe=False,
    extras_require={},
    python_requires=">=3.7",
)
