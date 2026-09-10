"""Check LLVM-MinGW target selection and transitive DLL packaging without Wine."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]


class WindowsRuntimeTests(unittest.TestCase):
    def test_toolchain_targets(self):
        for arch in ("x86_64", "aarch64"):
            with self.subTest(arch=arch), tempfile.TemporaryDirectory() as directory:
                script = Path(directory) / "check.cmake"
                script.write_text(
                    f'include("{REPO}/src/cli/cmake/mingw-toolchain.cmake")\n'
                    f'if(NOT CMAKE_C_COMPILER STREQUAL "{arch}-w64-mingw32-gcc")\n'
                    '  message(FATAL_ERROR "Wrong compiler")\nendif()\n'
                    f'if(NOT TOOLCHAIN_ROOT STREQUAL "/opt/llvm-mingw/{arch}-w64-mingw32/bin")\n'
                    '  message(FATAL_ERROR "Wrong runtime directory")\nendif()\n'
                )
                result = subprocess.run(
                    ["cmake", "-P", str(script)], env=dict(os.environ, LLVM_MINGW_TARGET=arch),
                    capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_invalid_target(self):
        result = subprocess.run(
            ["cmake", "-P", str(REPO / "src/cli/cmake/mingw-toolchain.cmake")],
            env=dict(os.environ, LLVM_MINGW_TARGET="unsupported"), capture_output=True,
        )
        self.assertNotEqual(result.returncode, 0)

    def test_transitive_runtime_dependencies(self):
        for missing in (False, True):
            with self.subTest(missing=missing), tempfile.TemporaryDirectory(prefix="runtime test ") as directory:
                root = Path(directory)
                output = root / "install/bin"
                output.mkdir(parents=True)
                for name in ("k230_flash_cli.exe", "libkburn.dll", "libusb-1.0.dll"):
                    (output / name).touch()
                runtime = root / "toolchain/bin"
                runtime.mkdir(parents=True)
                (runtime / "libc++.dll").write_bytes(b"target c++ runtime")
                if not missing:
                    (runtime / "libunwind.dll").write_bytes(b"target unwind runtime")
                objdump = root / "objdump"
                objdump.write_text(
                    '#!/bin/sh\ncase "$2" in\n'
                    '  */libkburn.dll) echo "DLL Name: libc++.dll" ;;\n'
                    '  */libc++.dll) echo "DLL Name: libunwind.dll" ;;\nesac\n'
                )
                objdump.chmod(0o755)
                result = subprocess.run([
                    "cmake", "-DTARGET_PLATFORM=windows", "-DBUILD_WITH_MINGW=ON",
                    f"-DINSTALL_PREFIX={root / 'install'}", "-DEXECUTABLE_NAME=k230_flash_cli",
                    f"-DTOOLCHAIN_ROOT={runtime}", f"-DOBJDUMP_COMMAND={objdump}",
                    "-P", str(REPO / "src/cli/cmake/win-install.cmake"),
                ], capture_output=True, text=True)
                if missing:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("libunwind.dll", result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual((output / "libunwind.dll").read_bytes(), b"target unwind runtime")
                    self.assertEqual((output / "libc++.dll").read_bytes(), b"target c++ runtime")


if __name__ == "__main__":
    unittest.main()
