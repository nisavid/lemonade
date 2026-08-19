import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Mapping
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
PUBLIC_SEAM = REPO_ROOT / "test/residency/recovery/journal_public_seam.cpp"
INCLUDE_ROOT = REPO_ROOT / "src/cpp/include"
JOURNAL_HEADER = INCLUDE_ROOT / "lemon/residency/journal.h"
JOURNAL_SOURCE = REPO_ROOT / "src/cpp/server/residency/journal.cpp"
PROCESS_TIMEOUT_SECONDS = 120
COMPILER_UNAVAILABLE = "journal public-seam compiler is unavailable"
COMPILATION_FAILED = "journal public-seam compilation failed"
COMPILATION_TIMED_OUT = "journal public-seam compilation timed out"
EXECUTABLE_UNAVAILABLE = "journal public-seam executable is unavailable"
EXECUTION_TIMED_OUT = "journal public-seam execution timed out"
CONTRACT_ASSERTION_FAILED = "journal public-seam contract assertion failed"


def split_compiler_command(value: str, *, windows: bool) -> list[str]:
    try:
        configured = shlex.split(value, posix=not windows)
    except ValueError as error:
        raise RuntimeError("C++ compiler command is invalid") from error
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
    default = "cl.exe" if windows else "c++"
    configured = split_compiler_command(os.environ.get("CXX", default), windows=windows)
    if not configured or shutil.which(configured[0]) is None:
        raise RuntimeError(f"C++ compiler is unavailable: {configured!r}")
    return configured


def compiler_command(
    output: Path,
    *,
    configured: list[str] | None = None,
    environment: Mapping[str, str] | None = None,
) -> list[str]:
    if configured is None:
        configured = configured_compiler()
    if environment is None:
        environment = os.environ

    executable = configured[0].replace("\\", "/").rsplit("/", 1)[-1].lower()
    nlohmann_include = environment.get("NLOHMANN_JSON_INCLUDE_DIR")
    mbedtls_include = environment.get("MBEDTLS_INCLUDE_DIR")
    mbedcrypto_library = environment.get("MBEDCRYPTO_LIBRARY")
    sources = [JOURNAL_SOURCE, PUBLIC_SEAM]

    if executable in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}:
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
    if mbedtls_include:
        command.extend(["-I", mbedtls_include])
    command.extend(
        [
            *(str(source) for source in sources),
            mbedcrypto_library or "-lmbedcrypto",
            "-o",
            str(output),
        ]
    )
    return command


def compile_only_command(
    source: Path,
    output: Path,
    *,
    configured: list[str],
) -> list[str]:
    executable = configured[0].replace("\\", "/").rsplit("/", 1)[-1].lower()
    if executable in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}:
        return [
            *configured,
            "/nologo",
            "/std:c++17",
            "/EHsc",
            f"/I{INCLUDE_ROOT}",
            "/c",
            str(source),
            f"/Fo{output}",
        ]
    return [
        *configured,
        "-std=c++17",
        "-I",
        str(INCLUDE_ROOT),
        "-c",
        str(source),
        "-o",
        str(output),
    ]


def require_private_document_construction(
    directory: Path,
    *,
    configured: list[str],
    runner=subprocess.run,
) -> str | None:
    adversaries = {
        "record": """
#include "lemon/residency/journal.h"

#include <optional>
#include <string>
#include <utility>

namespace lemon::residency {
class JournalCodecAccess {
  static auto mint_record()
      -> decltype(ParsedJournalRecord(
          std::declval<JournalRecordDraft>(), 1,
          std::declval<std::optional<std::string>>(),
          std::declval<std::string>(), std::declval<std::string>()));
};
}
""",
        "history": """
#include "lemon/residency/journal.h"

#include <memory>
#include <utility>

namespace lemon::residency {
class JournalCodecAccess {
  static auto mint_history(std::unique_ptr<JournalHistory::State> state)
      -> decltype(JournalHistory(std::move(state)));
};
}
""",
        "root": """
#include "lemon/residency/journal.h"

#include <cstdint>
#include <string>
#include <utility>

namespace lemon::residency {
class JournalCodecAccess {
  static auto mint_root()
      -> decltype(AuthorityRootCandidate(
          std::declval<SchemaVersion>(), std::declval<std::string>(),
          std::declval<std::string>(), std::uint64_t{1}, std::uint64_t{1},
          std::declval<std::string>(), std::declval<std::string>(),
          std::declval<std::string>()));
};
}
""",
    }
    for name, source_text in adversaries.items():
        source = directory / f"forged_{name}.cpp"
        output = directory / f"forged_{name}.obj"
        source.write_text(source_text, encoding="utf-8")
        try:
            completed = runner(
                compile_only_command(source, output, configured=configured),
                cwd=REPO_ROOT,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=PROCESS_TIMEOUT_SECONDS,
            )
        except subprocess.TimeoutExpired:
            return COMPILATION_TIMED_OUT
        except (OSError, UnicodeError):
            return COMPILER_UNAVAILABLE
        if completed.returncode == 0:
            return CONTRACT_ASSERTION_FAILED
    return None


def require_posix_command_contract() -> None:
    output = Path("journal-public-seam")
    fallback = compiler_command(output, configured=["c++"], environment={})
    if "-lmbedcrypto" not in fallback:
        raise AssertionError("POSIX journal seam omitted the mbedcrypto fallback")

    environment: Mapping[str, str] = {
        "NLOHMANN_JSON_INCLUDE_DIR": "/fixture/nlohmann",
        "MBEDTLS_INCLUDE_DIR": "/fixture/mbedtls",
        "MBEDCRYPTO_LIBRARY": "/fixture/libmbedcrypto.a",
    }
    configured = compiler_command(output, configured=["c++"], environment=environment)
    require_posix_include_contract(
        configured, ("/fixture/nlohmann", "/fixture/mbedtls")
    )
    if "/fixture/libmbedcrypto.a" not in configured:
        raise AssertionError("POSIX journal seam ignored MBEDCRYPTO_LIBRARY")
    if "-lmbedcrypto" in configured:
        raise AssertionError("POSIX journal seam kept fallback with an override")

    try:
        require_posix_include_contract(["c++"], ("/fixture/missing",))
    except AssertionError:
        pass
    else:
        raise AssertionError("POSIX journal seam accepted a missing configured include")


def require_posix_include_contract(
    configured: list[str], includes: tuple[str, ...]
) -> None:
    for include in includes:
        if include not in configured:
            raise AssertionError(f"POSIX journal seam omitted include {include}")
        include_index = configured.index(include)
        if configured[include_index - 1] != "-I":
            raise AssertionError(f"POSIX journal seam miswired include {include}")


def require_msvc_command_contract() -> None:
    output = Path("journal-public-seam.exe")
    try:
        compiler_command(output, configured=["cl.exe"], environment={})
    except RuntimeError:
        pass
    else:
        raise AssertionError("MSVC journal seam accepted a missing mbedcrypto library")

    environment: Mapping[str, str] = {
        "NLOHMANN_JSON_INCLUDE_DIR": "C:/fixture/nlohmann",
        "MBEDTLS_INCLUDE_DIR": "C:/fixture/mbedtls",
        "MBEDCRYPTO_LIBRARY": "C:/fixture/mbedcrypto.lib",
    }
    command = compiler_command(output, configured=["cl.exe"], environment=environment)
    for include in ("C:/fixture/nlohmann", "C:/fixture/mbedtls"):
        if f"/I{include}" not in command:
            raise AssertionError(f"MSVC journal seam miswired include {include}")
    if "C:/fixture/mbedcrypto.lib" not in command:
        raise AssertionError("MSVC journal seam ignored MBEDCRYPTO_LIBRARY")

    tokenized = split_compiler_command(
        '"C:\\Program Files\\Microsoft Visual Studio\\VC\\cl.exe" /O2',
        windows=True,
    )
    if tokenized != [
        "C:\\Program Files\\Microsoft Visual Studio\\VC\\cl.exe",
        "/O2",
    ]:
        raise AssertionError("Windows CXX tokenization is not path-safe")
    if split_compiler_command("c++ -O2", windows=False) != ["c++", "-O2"]:
        raise AssertionError("POSIX CXX tokenization drifted")


def run_stage(
    command: list[str],
    *,
    failed: str,
    timed_out: str,
    unavailable: str,
    runner=subprocess.run,
) -> str | None:
    try:
        completed = runner(
            command,
            cwd=REPO_ROOT,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=PROCESS_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired:
        return timed_out
    except (OSError, UnicodeError):
        return unavailable
    if completed.returncode != 0:
        return failed
    return None


def execute_public_seam(
    output: Path,
    *,
    configured: list[str],
    environment: Mapping[str, str],
    runner=subprocess.run,
) -> str | None:
    try:
        command = compiler_command(
            output,
            configured=configured,
            environment=environment,
        )
    except (RuntimeError, OSError, UnicodeError, ValueError):
        return COMPILER_UNAVAILABLE
    failure = run_stage(
        command,
        failed=COMPILATION_FAILED,
        timed_out=COMPILATION_TIMED_OUT,
        unavailable=COMPILER_UNAVAILABLE,
        runner=runner,
    )
    if failure is not None:
        return failure
    return run_stage(
        [str(output)],
        failed=CONTRACT_ASSERTION_FAILED,
        timed_out=EXECUTION_TIMED_OUT,
        unavailable=EXECUTABLE_UNAVAILABLE,
        runner=runner,
    )


def failure_line(message: str) -> str:
    rendered = f"{Path(__file__).name}: {message}\n"
    if rendered.count("\n") != 1 or len(rendered.encode("utf-8")) > 256:
        raise AssertionError("public-seam failure line is not bounded")
    return rendered


def require_runner_failure_contract() -> None:
    secret = "private-compiler-path/💥"
    raw_output = f"raw tool output {secret}".encode()

    class FakeRunner:
        def __init__(self, outcomes):
            self.outcomes = iter(outcomes)

        def __call__(self, command, **_kwargs):
            outcome = next(self.outcomes)
            if isinstance(outcome, BaseException):
                raise outcome
            return outcome

    successful = subprocess.CompletedProcess([], 0, b"", b"")
    failed = subprocess.CompletedProcess([], 17, raw_output, raw_output)
    timeout = subprocess.TimeoutExpired([secret], 1, output=raw_output)
    invalid_utf8 = UnicodeDecodeError("utf-8", b"\xff", 0, 1, "invalid")
    fixtures = [
        ([f"/{secret}/c++"], {}),
        (
            [f"C:\\{secret}\\cl.exe"],
            {"MBEDCRYPTO_LIBRARY": f"C:\\{secret}\\mbedcrypto.lib"},
        ),
    ]
    cases = [
        ([OSError(secret)], COMPILER_UNAVAILABLE),
        ([invalid_utf8], COMPILER_UNAVAILABLE),
        ([failed], COMPILATION_FAILED),
        ([timeout], COMPILATION_TIMED_OUT),
        ([successful, failed], CONTRACT_ASSERTION_FAILED),
        ([successful, timeout], EXECUTION_TIMED_OUT),
        ([successful, OSError(secret)], EXECUTABLE_UNAVAILABLE),
    ]
    for configured, environment in fixtures:
        for outcomes, expected in cases:
            observed = execute_public_seam(
                Path("journal-public-seam"),
                configured=configured,
                environment=environment,
                runner=FakeRunner(outcomes),
            )
            if observed != expected:
                raise AssertionError("public-seam failure classification drifted")
            rendered = failure_line(observed)
            if secret in rendered or "Traceback" in rendered:
                raise AssertionError("public-seam failure leaked process details")


def main() -> int:
    if not JOURNAL_HEADER.is_file() or not JOURNAL_SOURCE.is_file():
        sys.stderr.write("TASK-019 durable residency journal contract is unavailable\n")
        return 1

    try:
        require_posix_command_contract()
        require_msvc_command_contract()
        require_runner_failure_contract()
        configured = configured_compiler()
        with tempfile.TemporaryDirectory(prefix="residency-journal-") as directory:
            suffix = ".exe" if os.name == "nt" else ""
            output = Path(directory) / f"journal_public_seam{suffix}"
            failure = execute_public_seam(
                output,
                configured=configured,
                environment=os.environ,
            )
            if failure is None:
                failure = require_private_document_construction(
                    Path(directory), configured=configured
                )
    except AssertionError:
        failure = CONTRACT_ASSERTION_FAILED
    except (
        RuntimeError,
        OSError,
        subprocess.TimeoutExpired,
        UnicodeError,
        ValueError,
    ):
        failure = COMPILER_UNAVAILABLE

    if failure is not None:
        sys.stderr.write(failure_line(failure))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
