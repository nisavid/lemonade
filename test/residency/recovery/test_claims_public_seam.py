import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
PUBLIC_SEAM = REPO_ROOT / "test/residency/recovery/claims_public_seam.cpp"
INCLUDE_ROOT = REPO_ROOT / "src/cpp/include"
CLAIMS_HEADER = INCLUDE_ROOT / "lemon/residency/claims.h"
CLAIMS_SOURCE = REPO_ROOT / "src/cpp/server/residency/claims.cpp"
UNAVAILABLE = "TASK-021 checked residency claims contract is unavailable"
PROCESS_TIMEOUT_SECONDS = 120


def split_compiler_command(value: str, *, windows: bool) -> list[str]:
    configured = shlex.split(value, posix=not windows)
    if windows:
        configured = [
            (
                token[1:-1]
                if len(token) >= 2 and token[0] == token[-1] and token[0] in {'"', "'"}
                else token
            )
            for token in configured
        ]
    return configured


def configured_compiler() -> list[str]:
    windows = os.name == "nt"
    configured = split_compiler_command(
        os.environ.get("CXX", "cl.exe" if windows else "c++"), windows=windows
    )
    if not configured or shutil.which(configured[0]) is None:
        raise RuntimeError("C++ compiler is unavailable")
    return configured


def compiler_command(output: Path, configured: list[str]) -> list[str]:
    executable = configured[0].replace("\\", "/").rsplit("/", 1)[-1].lower()
    sources = [CLAIMS_SOURCE, PUBLIC_SEAM]
    if executable in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}:
        return [
            *configured,
            "/nologo",
            "/std:c++17",
            "/W4",
            "/WX",
            "/EHsc",
            f"/I{INCLUDE_ROOT}",
            f"/Fo{output.parent}{os.sep}",
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


def failure_line(message: str) -> str:
    rendered = f"{Path(__file__).name}: {message}\n"
    if rendered.count("\n") != 1 or len(rendered.encode("utf-8")) > 256:
        raise AssertionError("public-seam failure line is not bounded")
    return rendered


def main() -> int:
    if not CLAIMS_HEADER.is_file() or not CLAIMS_SOURCE.is_file():
        sys.stderr.write(f"{UNAVAILABLE}\n")
        return 1

    try:
        configured = configured_compiler()
        with tempfile.TemporaryDirectory(prefix="residency-claims-") as directory:
            suffix = ".exe" if os.name == "nt" else ""
            output = Path(directory) / f"claims_public_seam{suffix}"
            completed = subprocess.run(
                compiler_command(output, configured),
                cwd=REPO_ROOT,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=PROCESS_TIMEOUT_SECONDS,
            )
            if completed.returncode != 0:
                failure = "claims public-seam compilation failed"
            else:
                completed = subprocess.run(
                    [str(output)],
                    cwd=REPO_ROOT,
                    check=False,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=PROCESS_TIMEOUT_SECONDS,
                )
                failure = (
                    None
                    if completed.returncode == 0
                    else "claims public-seam contract assertion failed"
                )
    except subprocess.TimeoutExpired:
        failure = "claims public-seam timed out"
    except (OSError, RuntimeError, UnicodeError, ValueError):
        failure = "claims public-seam compiler is unavailable"

    if failure is not None:
        sys.stderr.write(failure_line(failure))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
