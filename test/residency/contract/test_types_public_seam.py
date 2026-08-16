import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
SOURCE = REPO_ROOT / "test/residency/contract/types_public_seam.cpp"
INCLUDE_ROOT = REPO_ROOT / "src/cpp/include"
CONTRACT_HEADER = INCLUDE_ROOT / "lemon/residency/types.h"


def compiler_command(output: Path) -> list[str]:
    configured = shlex.split(os.environ.get("CXX", "c++"))
    if not configured or shutil.which(configured[0]) is None:
        raise RuntimeError(f"C++ compiler is unavailable: {configured!r}")

    executable = Path(configured[0]).name.lower()
    if executable in {"cl", "cl.exe"}:
        return [
            *configured,
            "/nologo",
            "/std:c++17",
            "/W4",
            "/WX",
            "/EHsc",
            f"/I{INCLUDE_ROOT}",
            f"/Fo{output.with_suffix('.obj')}",
            f"/Fd{output.with_suffix('.pdb')}",
            str(SOURCE),
            f"/Fe:{output}",
        ]

    return [
        *configured,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic",
        "-I",
        str(INCLUDE_ROOT),
        str(SOURCE),
        "-o",
        str(output),
    ]


def main() -> int:
    if not CONTRACT_HEADER.is_file():
        sys.stderr.write("TASK-008 public residency type contract is unavailable\n")
        return 1

    with tempfile.TemporaryDirectory(prefix="residency-types-") as directory:
        suffix = ".exe" if os.name == "nt" else ""
        output = Path(directory) / f"types_public_seam{suffix}"
        compiled = subprocess.run(
            compiler_command(output),
            cwd=REPO_ROOT,
            check=False,
            text=True,
            capture_output=True,
        )
        if compiled.returncode != 0:
            sys.stderr.write(compiled.stdout)
            sys.stderr.write(compiled.stderr)
            return compiled.returncode

        executed = subprocess.run(
            [str(output)],
            cwd=REPO_ROOT,
            check=False,
            text=True,
            capture_output=True,
        )
        sys.stdout.write(executed.stdout)
        sys.stderr.write(executed.stderr)
        return executed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
