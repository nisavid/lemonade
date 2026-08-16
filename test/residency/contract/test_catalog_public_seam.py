import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
PUBLIC_SEAM = REPO_ROOT / "test/residency/contract/catalog_public_seam.cpp"
INCLUDE_ROOT = REPO_ROOT / "src/cpp/include"
CATALOG_HEADER = INCLUDE_ROOT / "lemon/residency/catalog.h"
CATALOG_SOURCE = REPO_ROOT / "src/cpp/server/residency/catalog.cpp"
GENERATED_SOURCE = REPO_ROOT / "src/cpp/server/residency/generated_contract.cpp"
PACKAGED_CATALOG = REPO_ROOT / "src/cpp/resources/residency_profiles.json"


def configured_compiler() -> list[str]:
    configured = shlex.split(os.environ.get("CXX", "c++"))
    if not configured or shutil.which(configured[0]) is None:
        raise RuntimeError(f"C++ compiler is unavailable: {configured!r}")
    return configured


def compiler_command(output: Path) -> list[str]:
    configured = configured_compiler()
    executable = Path(configured[0]).name.lower()
    sources = [CATALOG_SOURCE, GENERATED_SOURCE, PUBLIC_SEAM]
    nlohmann_include = os.environ.get("NLOHMANN_JSON_INCLUDE_DIR")
    mbedtls_include = os.environ.get("MBEDTLS_INCLUDE_DIR")
    mbedcrypto_library = os.environ.get("MBEDCRYPTO_LIBRARY")

    if executable in {"cl", "cl.exe"}:
        command = [
            *configured,
            "/nologo",
            "/std:c++17",
            "/W4",
            "/WX",
            "/EHsc",
            f"/I{INCLUDE_ROOT}",
        ]
        if nlohmann_include:
            command.append(f"/I{nlohmann_include}")
        if mbedtls_include:
            command.append(f"/I{mbedtls_include}")
        if not mbedcrypto_library:
            raise RuntimeError(
                "MBEDCRYPTO_LIBRARY is required for the MSVC public seam"
            )
        command.extend(
            [
                f"/Fo{output.parent}{os.sep}",
                f"/Fd{output.with_suffix('.pdb')}",
                *(str(source) for source in sources),
                mbedcrypto_library,
                f"/Fe:{output}",
            ]
        )
        return command

    command = [
        *configured,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic",
        "-I",
        str(INCLUDE_ROOT),
    ]
    if nlohmann_include:
        command.extend(["-I", nlohmann_include])
    command.extend(
        [
            *(str(source) for source in sources),
            "-lmbedcrypto",
            "-o",
            str(output),
        ]
    )
    return command


def main() -> int:
    if not CATALOG_HEADER.is_file() or not CATALOG_SOURCE.is_file():
        sys.stderr.write("TASK-010 residency catalog contract is unavailable\n")
        return 1

    with tempfile.TemporaryDirectory(prefix="residency-catalog-") as directory:
        suffix = ".exe" if os.name == "nt" else ""
        output = Path(directory) / f"catalog_public_seam{suffix}"
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
            [str(output), str(PACKAGED_CATALOG)],
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
