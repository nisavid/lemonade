import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
PUBLIC_SEAM = REPO_ROOT / "test/residency/contract/explanations_public_seam.cpp"
INCLUDE_ROOT = REPO_ROOT / "src/cpp/include"
EXPLANATIONS_HEADER = INCLUDE_ROOT / "lemon/residency/explanations.h"
EXPLANATIONS_SOURCE = REPO_ROOT / "src/cpp/server/residency/explanations.cpp"
GENERATED_SOURCE = REPO_ROOT / "src/cpp/server/residency/generated_contract.cpp"


def configured_compiler() -> list[str]:
    configured = shlex.split(os.environ.get("CXX", "c++"))
    if not configured or shutil.which(configured[0]) is None:
        raise RuntimeError(f"C++ compiler is unavailable: {configured!r}")
    return configured


def compiler_command(output: Path) -> list[str]:
    configured = configured_compiler()
    executable = Path(configured[0]).name.lower()
    sources = [EXPLANATIONS_SOURCE, GENERATED_SOURCE, PUBLIC_SEAM]

    if executable in {"cl", "cl.exe"}:
        return [
            *configured,
            "/nologo",
            "/std:c++17",
            "/W4",
            "/WX",
            "/EHsc",
            f"/I{INCLUDE_ROOT}",
            f"/Fo{output.parent}{os.sep}",
            f"/Fd{output.with_suffix('.pdb')}",
            *(str(source) for source in sources),
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
        *(str(source) for source in sources),
        "-o",
        str(output),
    ]


def main() -> int:
    if not EXPLANATIONS_HEADER.is_file() or not EXPLANATIONS_SOURCE.is_file():
        sys.stderr.write("TASK-011 residency explanation contract is unavailable\n")
        return 1

    with tempfile.TemporaryDirectory(prefix="residency-explanations-") as directory:
        suffix = ".exe" if os.name == "nt" else ""
        output = Path(directory) / f"explanations_public_seam{suffix}"
        compiled = subprocess.run(
            compiler_command(output),
            cwd=REPO_ROOT,
            check=False,
            text=True,
            capture_output=True,
        )
        if compiled.returncode != 0:
            sys.stdout.write(compiled.stdout)
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
