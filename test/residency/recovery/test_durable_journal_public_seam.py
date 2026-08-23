import hashlib
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from collections.abc import Mapping
from pathlib import Path, PurePath, PureWindowsPath

RUNNER = Path(__file__).resolve()
REPO_ROOT = RUNNER.parents[3]
PUBLIC_SEAM = REPO_ROOT / "test/residency/recovery/durable_journal_public_seam.cpp"
PRIVATE_AUTHORITY_GATE = (
    REPO_ROOT / "test/residency/recovery/durable_journal_private_authority_gate.cpp"
)
TEST_SUPPORT = (
    REPO_ROOT / "test/residency/recovery/journal_persistence_test_support.cpp"
)
TEST_SUPPORT_HEADER = (
    REPO_ROOT / "test/residency/recovery/journal_persistence_test_support.h"
)
INCLUDE_ROOT = REPO_ROOT / "src/cpp/include"
INTERNAL_INCLUDE_ROOT = REPO_ROOT / "src/cpp/server/residency"
TEST_INCLUDE_ROOT = REPO_ROOT / "test/residency/recovery"
DURABLE_HEADER = INCLUDE_ROOT / "lemon/residency/durable_journal.h"
DURABLE_SOURCE = REPO_ROOT / "src/cpp/server/residency/durable_journal.cpp"
AUTHORITY_FENCE = INTERNAL_INCLUDE_ROOT / "authority_fence.h"
CLAIMS_SOURCE = REPO_ROOT / "src/cpp/server/residency/claims.cpp"
DURABLE_LOCAL_OVERLAY_SOURCE = (
    REPO_ROOT / "src/cpp/server/residency/durable_local_overlay.cpp"
)
LOCAL_OVERLAY_SOURCE = REPO_ROOT / "src/cpp/server/residency/local_overlay.cpp"
JOURNAL_SOURCE = REPO_ROOT / "src/cpp/server/residency/journal.cpp"
JOURNAL_HEADER = INCLUDE_ROOT / "lemon/residency/journal.h"
ADAPTER_HEADER = INTERNAL_INCLUDE_ROOT / "platform/durable_file_adapter.h"
ADAPTER_COMMON = INTERNAL_INCLUDE_ROOT / "platform/durable_file_adapter.cpp"
ADAPTER_POSIX = INTERNAL_INCLUDE_ROOT / "platform/durable_file_adapter_posix.cpp"
ADAPTER_WINDOWS = INTERNAL_INCLUDE_ROOT / "platform/durable_file_adapter_windows.cpp"
ADAPTER_MACOS = INTERNAL_INCLUDE_ROOT / "platform/durable_file_adapter_macos.cpp"
CMAKE = REPO_ROOT / "CMakeLists.txt"
WORKFLOW = REPO_ROOT / ".github/workflows/cpp_server_build_test_release.yml"
PROCESS_TIMEOUT_SECONDS = 240
TESTING_DEFINITION = "LEMONADE_RESIDENCY_DURABLE_TESTING"
UNAVAILABLE = "TASK-020 durable residency persistence contract is unavailable"
COMPILER_UNAVAILABLE = "durable journal public-seam compiler is unavailable"
COMPILATION_FAILED = "durable journal public-seam compilation failed"
COMPILATION_TIMED_OUT = "durable journal public-seam compilation timed out"
EXECUTABLE_UNAVAILABLE = "durable journal public-seam executable is unavailable"
EXECUTION_TIMED_OUT = "durable journal public-seam execution timed out"
CONTRACT_ASSERTION_FAILED = "durable journal public-seam contract assertion failed"
TASK019_HASHES = {
    JOURNAL_HEADER: "afc94955a9fa57dc0154d4fbb2e19ca416d2d33c770616c75c45f031c7b167f7",
    JOURNAL_SOURCE: "de9ae2b8943e8a539f01809537590cacf8676b6b54256d2f6497094792fa6b73",
}


def cmake_relative_path(path: PurePath, root: PurePath) -> str:
    return path.relative_to(root).as_posix()


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


def configured_compiler(
    *,
    environment: Mapping[str, str] | None = None,
    which=shutil.which,
) -> list[str]:
    if environment is None:
        environment = os.environ
    windows = os.name == "nt"
    default = "cl.exe" if windows else "c++"
    configured = split_compiler_command(
        environment.get("CXX", default), windows=windows
    )
    if not configured or which(configured[0]) is None:
        raise RuntimeError(f"C++ compiler is unavailable: {configured!r}")
    return configured


def explicit_compiler(value: str) -> list[str]:
    compiler = Path(value)
    if not compiler.is_absolute() or not compiler.is_file():
        raise RuntimeError("hosted C++ compiler is unavailable")
    if os.name != "nt" and not os.access(compiler, os.X_OK):
        raise RuntimeError("hosted C++ compiler is unavailable")
    return [str(compiler)]


def is_msvc_frontend(configured: list[str]) -> bool:
    executable = configured[0].replace("\\", "/").rsplit("/", 1)[-1].lower()
    return executable in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}


def native_adapter_source() -> Path:
    if os.name == "nt":
        return ADAPTER_WINDOWS
    if sys.platform == "darwin":
        return ADAPTER_MACOS
    return ADAPTER_POSIX


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
    sources = [
        JOURNAL_SOURCE,
        CLAIMS_SOURCE,
        DURABLE_SOURCE,
        DURABLE_LOCAL_OVERLAY_SOURCE,
        LOCAL_OVERLAY_SOURCE,
        ADAPTER_COMMON,
        native_adapter_source(),
        TEST_SUPPORT,
        PUBLIC_SEAM,
    ]

    if executable in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}:
        command = [
            *configured,
            "/nologo",
            "/std:c++17",
            "/W4",
            "/WX",
            "/EHsc",
            f"/D{TESTING_DEFINITION}",
            f"/I{INCLUDE_ROOT}",
            f"/I{INTERNAL_INCLUDE_ROOT}",
            f"/I{TEST_INCLUDE_ROOT}",
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
        "-pthread",
        f"-D{TESTING_DEFINITION}",
        "-I",
        str(INCLUDE_ROOT),
        "-I",
        str(INTERNAL_INCLUDE_ROOT),
        "-I",
        str(TEST_INCLUDE_ROOT),
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
    system_includes: tuple[str, ...] = (),
) -> list[str]:
    executable = configured[0].replace("\\", "/").rsplit("/", 1)[-1].lower()
    if executable in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}:
        command = [
            *configured,
            "/nologo",
            "/std:c++17",
            "/EHsc",
            f"/U{TESTING_DEFINITION}",
            f"/I{INCLUDE_ROOT}",
            "/c",
            str(source),
            f"/Fo{output}",
        ]
        command.extend(f"/I{include}" for include in system_includes)
        return command
    command = [
        *configured,
        "-std=c++17",
        f"-U{TESTING_DEFINITION}",
        "-I",
        str(INCLUDE_ROOT),
        "-c",
        str(source),
        "-o",
        str(output),
    ]
    system_include_flags = [
        token for include in system_includes for token in ("-isystem", include)
    ]
    command[2:2] = system_include_flags
    return command


def require_private_authority_construction(
    directory: Path,
    *,
    configured: list[str],
    system_includes: tuple[str, ...] = (),
    runner=subprocess.run,
) -> str | None:
    positive_source = directory / "published_journal_positive_control.cpp"
    positive_output = directory / "published_journal_positive_control.obj"
    positive_source.write_text(
        """
#include "lemon/residency/durable_journal.h"
#include "platform/durable_file_adapter.h"
#include <type_traits>
using lemon::residency::PublishedJournal;
static_assert(std::is_move_constructible_v<PublishedJournal>);
static_assert(!std::is_copy_constructible_v<PublishedJournal>);
namespace lemon::residency::detail {
enum class DurablePreflightTestFault { MacroOffNameCollision };
class DurableJournalTestFactory {};
inline constexpr int make_platform_durable_file_adapter_for_test = 0;
inline constexpr int make_durable_journal_for_test = 0;
}
""",
        encoding="utf-8",
    )
    try:
        positive = runner(
            compile_only_command(
                positive_source,
                positive_output,
                configured=configured,
                system_includes=(str(INTERNAL_INCLUDE_ROOT), *system_includes),
            ),
            cwd=directory,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=PROCESS_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired:
        return COMPILATION_TIMED_OUT
    except (OSError, UnicodeError):
        return COMPILER_UNAVAILABLE
    if positive.returncode != 0:
        return COMPILATION_FAILED

    adversaries = {
        "default": """
#include "lemon/residency/durable_journal.h"
using lemon::residency::PublishedJournal;
PublishedJournal forged;
""",
        "root": """
#include "lemon/residency/durable_journal.h"
#include <string>
using lemon::residency::PublishedJournal;
PublishedJournal forged(std::string("root"));
""",
        "copy": """
#include "lemon/residency/durable_journal.h"
using lemon::residency::PublishedJournal;
PublishedJournal copy(const PublishedJournal &value) { return value; }
""",
        "export": """
#include "lemon/residency/durable_journal.h"
#include <utility>
using namespace lemon::residency;
PublishedJournal forge(ExactSchemaExportCandidate value) {
    return PublishedJournal(std::move(value));
}
""",
        "record": """
#include "lemon/residency/durable_journal.h"
using namespace lemon::residency;
PublishedJournal forge(const ParsedJournalRecord &value) {
    return PublishedJournal(value);
}
""",
        "history": """
#include "lemon/residency/durable_journal.h"
using namespace lemon::residency;
PublishedJournal forge(JournalHistory &&value) {
    return PublishedJournal(static_cast<JournalHistory &&>(value));
}
""",
        "root_candidate": """
#include "lemon/residency/durable_journal.h"
using namespace lemon::residency;
PublishedJournal forge(const AuthorityRootCandidate &value) {
    return PublishedJournal(value);
}
""",
        "adapter": """
#include "lemon/residency/durable_journal.h"
#include <memory>
struct ForgedAdapter {};
using lemon::residency::PublishedJournal;
PublishedJournal forge(std::shared_ptr<ForgedAdapter> value) {
    return PublishedJournal(value);
}
""",
        "public_access": """
#include "lemon/residency/durable_journal.h"
namespace lemon::residency {
class PublishedJournalAccess {
public:
    static PublishedJournal forge() { return PublishedJournal(); }
};
}
""",
        "public_test_access": """
#include "lemon/residency/durable_journal.h"
#include <memory>
#include <utility>
struct ForgedAdapter {};
namespace lemon::residency {
class DurableJournalTestAccess {
public:
    static DurableJournal forge(std::unique_ptr<::ForgedAdapter> adapter) {
        return DurableJournal(std::move(adapter), JournalLimits{});
    }
};
}
""",
        "detail_access": """
#include "lemon/residency/durable_journal.h"
#include <memory>
#include <utility>
struct ForgedAdapter {};
namespace lemon::residency::detail {
class DurableJournalAccess {
public:
    static DurableJournal forge(std::unique_ptr<::ForgedAdapter> adapter) {
        return DurableJournal(std::move(adapter), JournalLimits{});
    }
};
}
""",
        "detail_test_access": """
#include "lemon/residency/durable_journal.h"
#include <memory>
#include <utility>
struct ForgedAdapter {};
namespace lemon::residency::detail {
class DurableJournalTestAccess {
public:
    static DurableJournal forge(std::unique_ptr<::ForgedAdapter> adapter) {
        return DurableJournal(std::move(adapter), JournalLimits{});
    }
};
}
""",
        "journal_default": """
#include "lemon/residency/durable_journal.h"
using lemon::residency::DurableJournal;
DurableJournal forged;
""",
        "journal_copy": """
#include "lemon/residency/durable_journal.h"
using lemon::residency::DurableJournal;
DurableJournal copy(const DurableJournal &value) { return value; }
""",
        "exact_test_factory": """
#include "lemon/residency/durable_journal.h"
#include <memory>
#include <utility>
struct ForgedAdapter {};
namespace lemon::residency::detail {
class DurableJournalTestFactory {
public:
    static DurableJournal forge(std::unique_ptr<::ForgedAdapter> adapter) {
        return DurableJournal(std::move(adapter), JournalLimits{});
    }
};
}
""",
    }
    for name, source_text in adversaries.items():
        source = directory / f"forged_published_journal_{name}.cpp"
        output = directory / f"forged_published_journal_{name}.obj"
        source.write_text(source_text, encoding="utf-8")
        try:
            completed = runner(
                compile_only_command(
                    source,
                    output,
                    configured=configured,
                    system_includes=system_includes,
                ),
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


def require_platform_source_contract() -> None:
    adapter_header = strip_cpp_comments_and_literals(
        ADAPTER_HEADER.read_text(encoding="utf-8")
    )
    common = strip_cpp_comments_and_literals(ADAPTER_COMMON.read_text(encoding="utf-8"))
    posix = strip_cpp_comments_and_literals(ADAPTER_POSIX.read_text(encoding="utf-8"))
    windows = strip_cpp_comments_and_literals(
        ADAPTER_WINDOWS.read_text(encoding="utf-8")
    )
    macos = strip_cpp_comments_and_literals(ADAPTER_MACOS.read_text(encoding="utf-8"))
    test_support = strip_cpp_comments_and_literals(
        TEST_SUPPORT.read_text(encoding="utf-8")
    )
    durable = DURABLE_SOURCE.read_text(encoding="utf-8")
    authority_fence = AUTHORITY_FENCE.read_text(encoding="utf-8")
    durable_header_raw = DURABLE_HEADER.read_text(encoding="utf-8")
    durable_header = strip_cpp_comments_preserve_literals(durable_header_raw)
    durable_header_code = strip_cpp_comments_and_literals(durable_header_raw)

    require_exact_struct_shape(
        adapter_header,
        "DurableFixedNamespaceResult",
        (
            "DurableFileResult result;",
            "bool journal_present;",
            "bool root_present;",
            "bool journal_stage_present;",
            "bool root_stage_present;",
            "bool lock_present;",
        ),
    )
    require_exact_struct_shape(
        adapter_header,
        "DurableReadChunkResult",
        (
            "DurableFileResult result;",
            "std::string bytes;",
            "bool end_of_file = false;",
        ),
    )
    if (
        re.search(
            r"\bclass\s+DurableReadChannel\s*\{(?P<body>.*?)\n\};",
            adapter_header,
            re.DOTALL,
        )
        is None
        or re.search(
            r"\bvirtual\s+DurableReadChunkResult\s+read_some\s*"
            r"\(\s*std::size_t\s+max_bytes\s*\)\s*=\s*0\s*;",
            adapter_header,
        )
        is None
        or re.search(
            r"\bvirtual\s+DurableFileResult\s+close\s*\(\s*\)\s*=\s*0\s*;",
            adapter_header,
        )
        is None
    ):
        raise AssertionError("durable bounded-read channel shape drifted")
    if (
        re.search(
            r"\bvirtual\s+DurableFixedNamespaceResult\s+"
            r"inspect_fixed_namespace\s*\(\s*\)\s*=\s*0\s*;",
            adapter_header,
        )
        is None
    ):
        raise AssertionError("durable adapter omits fixed-namespace inspection")

    fixed_constants = {
        "durable_journal_data_filename": "journal.jsonl",
        "durable_journal_root_filename": "authority-root.json",
        "durable_journal_lock_filename": "authority.lock",
    }
    for constant, value in fixed_constants.items():
        declaration = re.compile(
            rf"inline\s+constexpr\s+std::string_view\s+{constant}\s*=\s*\"{re.escape(value)}\"\s*;"
        )
        if declaration.search(durable_header) is None:
            raise AssertionError(
                f"missing fixed authority filename contract {constant}"
            )
        narrated = f'// inline constexpr std::string_view {constant} = "{value}";\n'
        if declaration.search(strip_cpp_comments_preserve_literals(narrated)):
            raise AssertionError("fixed-name scanner accepted a commented declaration")
    published_match = re.search(
        r"class\s+PublishedJournal\s*\{(?P<body>.*?)\n\};",
        durable_header_code,
        re.DOTALL,
    )
    if published_match is None:
        raise AssertionError("PublishedJournal public declaration is unavailable")
    require_published_journal_public_shape(durable_header_code)
    require_durable_journal_public_shape(durable_header_code)
    require_trusted_replay_floor_public_shape(durable_header_code)
    if re.search(
        r"\b(?:static\s+)?PublishedJournal\s+[A-Za-z_]\w*\s*\(",
        durable_header_code,
    ) or re.search(
        r"\bstd\s*::\s*optional\s*<\s*PublishedJournal\s*>\s+" r"[A-Za-z_]\w*\s*\(",
        durable_header_code,
    ):
        raise AssertionError("public header exposes an authority-minting factory")
    friend_declarations = re.findall(r"\bfriend\b[^;]*;", published_match.group("body"))
    if friend_declarations != ["friend class DurableJournal;"]:
        raise AssertionError("PublishedJournal has a forgeable friend graph")
    testing_friend = "friend class detail::DurableJournalTestFactory;"
    if durable_header_code.count(testing_friend) != 1 or not exactly_macro_guarded(
        durable_header_code,
        testing_friend,
        TESTING_DEFINITION,
    ):
        raise AssertionError("DurableJournal test factory is not macro-confined")
    unguarded_factory_mutant = f"""
#ifdef {TESTING_DEFINITION}
#endif
{testing_friend}
#ifdef OTHER_GUARD
#endif
"""
    if exactly_macro_guarded(
        unguarded_factory_mutant,
        testing_friend,
        TESTING_DEFINITION,
    ):
        raise AssertionError("test-factory scanner crossed an early endif")
    unguarded_declaration_mutant = f"""
#ifdef {TESTING_DEFINITION}
#endif
namespace detail {{ class DurableJournalTestFactory; }}
#ifdef OTHER_GUARD
#endif
"""
    if exactly_macro_guarded_region(
        unguarded_declaration_mutant,
        TESTING_DEFINITION,
        ("namespace detail", "class DurableJournalTestFactory;"),
    ):
        raise AssertionError("test-factory declaration scanner crossed endif")
    for forbidden in (
        "DurableFileAdapter",
        "make_durable_journal_for_test",
        "from_adapter",
        "testing_adapter",
    ):
        if forbidden in durable_header_code:
            raise AssertionError("public durable journal exposes adapter injection")
    if not exactly_macro_guarded_region(
        durable_header_code,
        TESTING_DEFINITION,
        ("class DurableJournalTestFactory;",),
    ):
        raise AssertionError("test-factory declaration is not macro-confined")
    require_exact_enum_members(
        durable_header_code,
        "DurableJournalStatus",
        (
            "Published",
            "ConflictBeforeWrite",
            "UnsupportedStorage",
            "CorruptOrRollback",
            "LimitExceeded",
            "RecoveryRequired",
        ),
    )
    require_exact_enum_members(
        durable_header_code,
        "ExactSchemaExportStatus",
        (
            "ExportedCandidate",
            "QuiescenceRequired",
            "ConflictBeforeRead",
            "RecoveryRequired",
            "UnsupportedStorage",
            "CorruptOrRollback",
            "SchemaMismatch",
            "LimitExceeded",
        ),
    )
    require_exact_enum_members(
        durable_header_code,
        "JournalQuiescence",
        ("Unconfirmed", "Confirmed"),
    )
    enum_mutant = """
enum class DurableJournalStatus {
    Published, ConflictBeforeWrite, UnsupportedStorage, CorruptOrRollback,
    LimitExceeded, RecoveryRequired, ForgedSuccess
};
"""
    try:
        require_exact_enum_members(
            enum_mutant,
            "DurableJournalStatus",
            (
                "Published",
                "ConflictBeforeWrite",
                "UnsupportedStorage",
                "CorruptOrRollback",
                "LimitExceeded",
                "RecoveryRequired",
            ),
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("status scanner accepted an extra public outcome")
    quiescence_mutant = """
enum class JournalQuiescence { Unconfirmed, Confirmed, Assumed };
"""
    try:
        require_exact_enum_members(
            quiescence_mutant,
            "JournalQuiescence",
            ("Unconfirmed", "Confirmed"),
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("quiescence scanner accepted an extra public state")
    require_exact_struct_shape(
        durable_header_code,
        "DurableJournalResult",
        (
            "DurableJournalStatus status;",
            "std::optional<PublishedJournal> journal;",
            "bool published() const noexcept;",
        ),
    )
    require_exact_struct_shape(
        durable_header_code,
        "ExactSchemaExportCandidate",
        (
            "SchemaVersion schema;",
            "std::string journal_bytes;",
            "std::string authority_root_bytes;",
        ),
    )
    require_exact_struct_shape(
        durable_header_code,
        "ExactSchemaExportResult",
        (
            "ExactSchemaExportStatus status;",
            "std::optional<ExactSchemaExportCandidate> candidate;",
            "bool exported() const noexcept;",
        ),
    )
    diagnostic_mutant = durable_header_code.replace(
        "DurableJournalStatus status;",
        "DurableJournalStatus status; std::string diagnostic;",
        1,
    )
    if diagnostic_mutant == durable_header_code:
        raise AssertionError("public diagnostic mutant was unavailable")
    try:
        require_exact_struct_shape(
            diagnostic_mutant,
            "DurableJournalResult",
            (
                "DurableJournalStatus status;",
                "std::optional<PublishedJournal> journal;",
                "bool published() const noexcept;",
            ),
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("public result scanner accepted diagnostics")
    combined_code = f"{common}\n{posix}\n{windows}\n{macos}\n{strip_cpp_comments_and_literals(durable)}"
    if "atomic_replace_file" in combined_code or "copy_file" in combined_code:
        raise AssertionError("durable adapter uses a forbidden copy fallback")
    durable_code = strip_cpp_comments_and_literals(durable)
    if "parse_authority_root_candidate" not in durable_code:
        raise AssertionError("persistence bypassed the TASK-019 root parser")
    if "inspect_fixed_namespace" not in durable_code:
        raise AssertionError("persistence bypassed fixed-namespace inspection")
    if "nlohmann" in durable_code or "parse_json" in durable_code:
        raise AssertionError("persistence duplicated TASK-019 JSON parsing")
    require_task019_live_contract(durable_code)
    require_compaction_prefix_binding(durable_code)
    require_directory_lineage_fence_registry(
        strip_cpp_comments_and_literals(authority_fence)
    )
    require_final_authority_revalidation(durable_code)
    for path, expected in TASK019_HASHES.items():
        observed = hashlib.sha256(path.read_bytes()).hexdigest()
        if observed != expected:
            raise AssertionError("TASK-019 pure codec bytes changed during TASK-020")

    require_tokens(
        posix,
        (
            "openat",
            "renameat",
            "fsync",
            "flock",
            "O_APPEND",
            "O_CLOEXEC",
            "O_NOFOLLOW",
            "AT_SYMLINK_NOFOLLOW",
            "LOCK_EX",
            "EINTR",
            "S_ISREG",
            "st_ino",
            "st_dev",
            "st_nlink",
            "ftruncate",
            "O_DIRECTORY",
            "close",
            "read",
        ),
        "POSIX durable adapter",
    )
    if "atomic_replace_file" in posix:
        raise AssertionError("POSIX adapter reused generic replacement")

    require_tokens(
        windows,
        (
            "CreateFileW",
            "WriteFile",
            "ReadFile",
            "FlushFileBuffers",
            "CloseHandle",
            "LockFileEx",
            "UnlockFileEx",
            "MoveFileExW",
            "MOVEFILE_REPLACE_EXISTING",
            "MOVEFILE_WRITE_THROUGH",
            "SetFilePointerEx",
            "SetEndOfFile",
            "MAXDWORD",
            "OPEN_ALWAYS",
            "ERROR_ALREADY_EXISTS",
            "OVERLAPPED",
            "FILE_SHARE_READ",
            "FILE_SHARE_WRITE",
            "FILE_FLAG_OPEN_REPARSE_POINT",
            "FILE_ATTRIBUTE_REPARSE_POINT",
            "FILE_ATTRIBUTE_DIRECTORY",
            "GetFileInformationByHandleEx",
            "FileIdInfo",
            "FileAttributeTagInfo",
            "FILE_ID_INFO",
            "FILE_ATTRIBUTE_TAG_INFO",
        ),
        "Windows durable adapter",
    )
    require_absent_tokens(
        windows,
        (
            "MOVEFILE_COPY_ALLOWED",
            "FILE_ATTRIBUTE_TEMPORARY",
            "FILE_FLAG_DELETE_ON_CLOSE",
            "FILE_FLAG_OVERLAPPED",
            "FILE_SHARE_DELETE",
        ),
        "Windows durable adapter",
    )

    require_tokens(
        macos,
        (
            "F_FULLFSYNC",
            "fcntl",
            "fsync",
            "renameat",
            "flock",
            "openat",
            "fstatat",
            "O_NOFOLLOW",
            "O_DIRECTORY",
            "AT_SYMLINK_NOFOLLOW",
            "LOCK_EX",
            "EINTR",
            "S_ISREG",
            "st_ino",
            "st_dev",
            "st_nlink",
            "close",
            "O_APPEND",
            "O_CLOEXEC",
            "ftruncate",
        ),
        "macOS durable adapter",
    )
    if "F_BARRIERFSYNC" in macos:
        raise AssertionError("macOS adapter substituted a barrier for persistence")
    require_call_argument(macos, "fcntl", "F_FULLFSYNC", "macOS regular-file policy")
    require_call_arguments(
        windows,
        "MoveFileExW",
        ("MOVEFILE_REPLACE_EXISTING", "MOVEFILE_WRITE_THROUGH"),
        "Windows root replacement",
    )
    require_call_arguments(
        windows,
        "LockFileEx",
        ("LOCKFILE_EXCLUSIVE_LOCK", "MAXDWORD", "MAXDWORD"),
        "Windows whole-file lock",
    )
    require_call_argument(posix, "flock", "LOCK_EX", "POSIX authority lock")
    require_call_argument(macos, "flock", "LOCK_EX", "macOS authority lock")
    for platform_source, label in (
        (posix, "POSIX durable adapter"),
        (macos, "macOS durable adapter"),
    ):
        require_call_arguments_unordered(
            platform_source,
            "openat",
            ("O_APPEND", "O_CLOEXEC", "O_NOFOLLOW"),
            f"{label} journal append",
        )
        require_call_argument(
            platform_source,
            "open",
            "O_DIRECTORY",
            f"{label} authority directory",
        )
        require_call_argument(
            platform_source,
            "fstatat",
            "AT_SYMLINK_NOFOLLOW",
            f"{label} stable lock identity",
        )
        require_call_expression(platform_source, "renameat", label)
    require_call_arguments_unordered(
        windows,
        "CreateFileW",
        ("OPEN_ALWAYS", "FILE_SHARE_READ", "FILE_SHARE_WRITE"),
        "Windows stable authority lock",
    )
    if re.search(r"\bOVERLAPPED\s+\w+\s*(?:\{\s*\}|=\s*\{\s*\})", windows) is None:
        raise AssertionError("Windows authority lock OVERLAPPED is not zeroed")
    require_call_argument(
        windows,
        "GetFileInformationByHandleEx",
        "FileIdInfo",
        "Windows composite authority identity",
    )
    all_adapter_sources = f"{common}\n{posix}\n{windows}\n{macos}"
    helper_declarations = {
        "retry_interrupted": (
            "DurableFileResult retry_interrupted(DurableInterruptibleCall &call);"
        ),
        "write_all": (
            "DurableFileResult write_all(DurableFileChannel &channel, "
            "std::string_view bytes);"
        ),
        "flush_retrying_interrupts": (
            "DurableFileResult flush_retrying_interrupts("
            "DurableFileChannel &channel);"
        ),
        "truncate_retrying_interrupts": (
            "DurableFileResult truncate_retrying_interrupts("
            "DurableFileChannel &channel, std::size_t bytes);"
        ),
        "close_once": ("DurableFileResult close_once(DurableFileChannel &channel);"),
        "write_flush_close": (
            "DurableFileResult write_flush_close(DurableFileChannel &channel, "
            "std::string_view bytes);"
        ),
        "truncate_flush_close": (
            "DurableFileResult truncate_flush_close("
            "DurableFileChannel &channel, std::size_t bytes);"
        ),
        "close_read_once": (
            "DurableFileResult close_read_once(DurableReadChannel &channel);"
        ),
    }
    for helper, declaration in helper_declarations.items():
        require_unique_helper_definition(
            adapter_header,
            all_adapter_sources,
            helper,
            declaration,
            "durable adapter",
        )
    require_unique_helper_definition(
        adapter_header,
        all_adapter_sources,
        "read_bounded_close",
        "DurableReadResult read_bounded_close(DurableReadChannel &channel, "
        "std::size_t max_bytes);",
        "durable adapter",
        return_type="DurableReadResult",
    )
    for platform_source, label in (
        (posix, "POSIX durable adapter"),
        (windows, "Windows durable adapter"),
        (macos, "macOS durable adapter"),
    ):
        combined_platform = f"{common}\n{platform_source}"
        for required_call in (
            "write_flush_close",
            "truncate_flush_close",
        ):
            require_each_override_call(
                combined_platform,
                "DurableFileAdapter",
                "preflight_capabilities",
                required_call,
                f"{label} capability probe",
            )
        for entry, required_calls in (
            ("append_journal", ("write_flush_close",)),
            ("truncate_journal", ("truncate_flush_close",)),
            ("read_root", ("read_bounded_close",)),
            ("read_journal", ("read_bounded_close",)),
            ("create_journal", ("write_flush_close",)),
            ("replace_journal", ("write_flush_close",)),
            ("replace_root", ("write_flush_close",)),
        ):
            for required_call in required_calls:
                require_each_override_call_exactly_once(
                    combined_platform,
                    "DurableFileAdapter",
                    entry,
                    required_call,
                    label,
                )
        for entry in ("create_journal", "append_journal"):
            require_each_override_call_result_propagated(
                combined_platform,
                "DurableFileAdapter",
                entry,
                "write_flush_close",
                label,
            )
        require_each_override_call_result_propagated(
            combined_platform,
            "DurableFileAdapter",
            "truncate_journal",
            "truncate_flush_close",
            label,
        )
        for entry in ("replace_journal", "replace_root"):
            require_each_override_call_result_propagated(
                combined_platform,
                "DurableFileAdapter",
                entry,
                "write_flush_close",
                label,
            )
        for entry in ("read_root", "read_journal"):
            require_each_override_call_result_propagated(
                combined_platform,
                "DurableFileAdapter",
                entry,
                "read_bounded_close",
                label,
            )
        require_each_override_call_result_propagated(
            combined_platform,
            "DurableFileAdapter",
            "preflight_capabilities",
            "write_flush_close",
            f"{label} capability probe",
            minimum_calls=2,
        )
        require_each_override_call_result_propagated(
            combined_platform,
            "DurableFileAdapter",
            "preflight_capabilities",
            "truncate_flush_close",
            f"{label} capability probe",
        )
    for platform_source, platform, label in (
        (posix, "posix", "POSIX durable adapter"),
        (macos, "posix", "macOS durable adapter"),
        (windows, "windows", "Windows durable adapter"),
    ):
        require_post_replace_verification(platform_source, platform, label)
        require_post_replace_verification_mutants(platform_source, platform, label)
    require_fixed_namespace_factory_contract(
        adapter_header, common, posix, macos, windows
    )
    require_windows_fixed_namespace_convergence_mutants(windows)
    require_windows_bound_directory_factory(windows)
    require_windows_bound_directory_factory_mutants(windows)
    require_test_storage_identity_capture_contract(test_support)
    require_test_storage_identity_capture_mutant(test_support)
    require_transaction_helper_contract(all_adapter_sources)
    require_bounded_read_helper_contract(f"{adapter_header}\n{all_adapter_sources}")
    require_close_read_once_contract(all_adapter_sources)
    for platform_source, label in (
        (posix, "POSIX durable adapter"),
        (macos, "macOS durable adapter"),
    ):
        combined_platform = f"{common}\n{platform_source}"
        require_retrying_posix_flock(
            platform_source,
            "lock_file_retrying_interrupts",
            "LOCK_EX",
            f"{label} acquisition",
        )
        require_retrying_posix_flock(
            platform_source,
            "unlock_file_retrying_interrupts",
            "LOCK_UN",
            f"{label} release",
        )
        require_retrying_posix_flock(
            platform_source,
            "lock_directory_retrying_interrupts",
            "LOCK_EX",
            f"{label} directory acquisition",
        )
        require_retrying_posix_flock(
            platform_source,
            "unlock_directory_retrying_interrupts",
            "LOCK_UN",
            f"{label} directory release",
        )
        require_retrying_posix_flock_mutant(
            platform_source,
            "lock_directory_retrying_interrupts",
            "LOCK_EX",
            f"{label} directory acquisition",
        )
        require_retrying_posix_flock_mutant(
            platform_source,
            "unlock_directory_retrying_interrupts",
            "LOCK_UN",
            f"{label} directory release",
        )
        require_unique_function_call_arguments(
            platform_source,
            "lock_fixed_namespace_parent_retrying_interrupts",
            "lock_directory_retrying_interrupts",
            ("parent_fd",),
            label,
        )
        require_unique_function_call_arguments(
            platform_source,
            "unlock_fixed_namespace_parent_retrying_interrupts",
            "unlock_directory_retrying_interrupts",
            ("parent_fd",),
            label,
        )
        for entry in (
            "lock_authority",
            "authority_identity",
            "preflight_capabilities",
            "inspect_fixed_namespace",
            "read_root",
            "read_journal",
            "read_immutable_object",
            "create_immutable_object",
            "create_journal",
            "append_journal",
            "truncate_journal",
            "replace_journal",
            "replace_root",
            "unlock_authority",
        ):
            require_each_override_call(
                combined_platform,
                "DurableFileAdapter",
                entry,
                "validate_fixed_namespace_binding",
                f"{label} fixed namespace lifetime binding",
            )
        require_each_override_call_exactly_once(
            combined_platform,
            "DurableFileAdapter",
            "lock_authority",
            "lock_fixed_namespace_parent_retrying_interrupts",
            label,
        )
        require_each_override_call_exactly_once(
            combined_platform,
            "DurableFileAdapter",
            "unlock_authority",
            "unlock_fixed_namespace_parent_retrying_interrupts",
            label,
        )
        require_tokens(
            platform_source,
            (
                "fixed_parent_fd_",
                "fixed_child_name_",
                "fixed_parent_identity_",
                "fixed_child_identity_",
                "AT_SYMLINK_NOFOLLOW",
            ),
            f"{label} fixed namespace lifetime binding",
        )
        require_each_override_call_exactly_once(
            combined_platform,
            "DurableFileAdapter",
            "lock_authority",
            "lock_file_retrying_interrupts",
            label,
        )
        require_each_override_call_exactly_once(
            combined_platform,
            "DurableFileAdapter",
            "unlock_authority",
            "unlock_file_retrying_interrupts",
            label,
        )
        require_each_override_call_exactly_once(
            combined_platform,
            "DurableFileAdapter",
            "lock_authority",
            "lock_directory_retrying_interrupts",
            label,
        )
        require_each_override_call_exactly_once(
            combined_platform,
            "DurableFileAdapter",
            "unlock_authority",
            "unlock_directory_retrying_interrupts",
            label,
        )
        require_each_override_result_call_checked(
            combined_platform,
            "DurableFileAdapter",
            "lock_authority",
            "lock_file_retrying_interrupts",
            label,
        )
        require_each_override_result_call_checked(
            combined_platform,
            "DurableFileAdapter",
            "lock_authority",
            "lock_directory_retrying_interrupts",
            label,
        )
        require_each_override_result_call_checked(
            combined_platform,
            "DurableFileAdapter",
            "unlock_authority",
            "unlock_directory_retrying_interrupts",
            label,
        )
        require_posix_unlock_result_mapping(combined_platform, label)
        require_each_override_ordered_calls(
            combined_platform,
            "DurableFileAdapter",
            "lock_authority",
            (
                "lock_directory_retrying_interrupts",
                "openat",
                "lock_file_retrying_interrupts",
            ),
            label,
        )
        require_each_override_ordered_calls(
            combined_platform,
            "DurableFileAdapter",
            "unlock_authority",
            (
                "unlock_file_retrying_interrupts",
                "close",
                "unlock_directory_retrying_interrupts",
            ),
            label,
        )
        require_posix_live_lock_rebinding(combined_platform, label)
        require_posix_lock_path_binding_mutants(combined_platform, label)
        require_retrying_directory_sync(platform_source, label)
        require_each_override_call_arguments(
            combined_platform,
            "DurableFileAdapter",
            "append_journal",
            "openat",
            ("O_APPEND", "O_CLOEXEC", "O_NOFOLLOW"),
            label,
        )
        require_each_override_call(
            combined_platform,
            "DurableFileAdapter",
            "preflight_capabilities",
            "renameat",
            f"{label} capability probe replacement",
        )
        require_each_override_call_exactly_once(
            combined_platform,
            "DurableFileAdapter",
            "preflight_capabilities",
            "renameat",
            f"{label} capability probe replacement",
        )
        require_each_override_call_exactly_once(
            combined_platform,
            "DurableFileAdapter",
            "preflight_capabilities",
            "sync_directory_retrying_interrupts",
            f"{label} capability probe namespace durability",
        )
        require_each_override_directory_sync_binding(
            combined_platform,
            "preflight_capabilities",
            f"{label} capability probe namespace durability",
        )
        require_each_override_result_call_checked(
            combined_platform,
            "DurableFileAdapter",
            "preflight_capabilities",
            "sync_directory_retrying_interrupts",
            f"{label} capability probe namespace durability",
        )
        require_each_override_call_arguments(
            combined_platform,
            "DurableFileAdapter",
            "lock_authority",
            "fstatat",
            ("AT_SYMLINK_NOFOLLOW",),
            label,
        )
        require_unique_function_call_arguments(
            combined_platform,
            "make_platform_durable_file_adapter",
            "open",
            ("O_DIRECTORY",),
            label,
        )
        require_each_override_fail_closed_checks(
            combined_platform,
            "DurableFileAdapter",
            "lock_authority",
            ("fstat", "fstatat", "S_ISREG", "st_nlink", "st_dev", "st_ino"),
            f"{label} stable lock",
        )
        require_each_override_ordered_calls(
            combined_platform,
            "DurableFileAdapter",
            "lock_authority",
            ("lock_file_retrying_interrupts", "fstat", "fstatat"),
            f"{label} stable lock",
        )
        for entry in (
            "read_root",
            "read_journal",
            "create_journal",
            "append_journal",
            "truncate_journal",
            "replace_journal",
            "replace_root",
        ):
            require_each_override_call_arguments(
                combined_platform,
                "DurableFileAdapter",
                entry,
                "openat",
                ("O_CLOEXEC", "O_NOFOLLOW"),
                f"{label} fixed-child open",
            )
            require_each_override_call(
                combined_platform,
                "DurableFileAdapter",
                entry,
                "fstat",
                f"{label} fixed-child open",
            )
            require_each_override_pattern(
                combined_platform,
                "DurableFileAdapter",
                entry,
                r"\bS_ISREG\s*\(",
                f"{label} fixed-child open",
            )
            require_each_override_fail_closed_checks(
                combined_platform,
                "DurableFileAdapter",
                entry,
                ("fstat", "S_ISREG"),
                f"{label} fixed-child open",
            )
        for entry in ("preflight_capabilities", "inspect_fixed_namespace"):
            require_each_override_call_arguments(
                combined_platform,
                "DurableFileAdapter",
                entry,
                "fstatat",
                ("AT_SYMLINK_NOFOLLOW",),
                f"{label} fixed-child preflight",
            )
            require_each_override_pattern(
                combined_platform,
                "DurableFileAdapter",
                entry,
                r"\bS_ISREG\s*\(",
                f"{label} fixed-child preflight",
            )
            require_each_override_fail_closed_checks(
                combined_platform,
                "DurableFileAdapter",
                entry,
                ("fstatat", "S_ISREG"),
                f"{label} fixed-child preflight",
            )
    require_each_override_call_arguments(
        f"{common}\n{macos}",
        "DurableFileChannel",
        "flush",
        "fcntl",
        ("F_FULLFSYNC",),
        "macOS durable adapter",
    )
    combined_windows = f"{common}\n{windows}"
    require_windows_composite_identity(combined_windows)
    require_each_override_call_arguments(
        combined_windows,
        "DurableFileAdapter",
        "lock_authority",
        "CreateFileW",
        (
            "FILE_SHARE_READ",
            "FILE_SHARE_WRITE",
            "OPEN_ALWAYS",
            "FILE_FLAG_OPEN_REPARSE_POINT",
        ),
        "Windows durable adapter",
    )
    require_each_override_call_arguments(
        combined_windows,
        "DurableFileAdapter",
        "preflight_capabilities",
        "MoveFileExW",
        ("MOVEFILE_REPLACE_EXISTING", "MOVEFILE_WRITE_THROUGH"),
        "Windows durable adapter capability probe",
    )
    require_each_override_call_exactly_once(
        combined_windows,
        "DurableFileAdapter",
        "preflight_capabilities",
        "MoveFileExW",
        "Windows durable adapter capability probe",
    )
    require_each_override_call_arguments(
        combined_windows,
        "DurableFileAdapter",
        "lock_authority",
        "LockFileEx",
        ("LOCKFILE_EXCLUSIVE_LOCK", "MAXDWORD", "MAXDWORD"),
        "Windows durable adapter",
    )
    require_each_override_pattern(
        combined_windows,
        "DurableFileAdapter",
        "lock_authority",
        r"\bOVERLAPPED\s+\w+\s*(?:\{\s*\}|=\s*\{\s*\})",
        "Windows zeroed lock OVERLAPPED",
    )
    require_windows_lock_overlapped_binding(combined_windows)
    require_each_override_minimum_call_argument(
        combined_windows,
        "DurableFileAdapter",
        "lock_authority",
        "GetFileInformationByHandleEx",
        "FileIdInfo",
        2,
        "Windows stable lock identity",
    )
    require_each_override_fail_closed_checks(
        combined_windows,
        "DurableFileAdapter",
        "lock_authority",
        (
            "LockFileEx",
            "VolumeSerialNumber",
            "FileId",
            "NumberOfLinks",
        ),
        "Windows durable adapter lock",
    )
    require_each_override_call_exactly_once(
        combined_windows,
        "DurableFileAdapter",
        "lock_authority",
        "LockFileEx",
        "Windows durable adapter lock",
    )
    require_windows_unlock_result_mapping(combined_windows)
    require_each_override_ordered_calls(
        combined_windows,
        "DurableFileAdapter",
        "unlock_authority",
        ("UnlockFileEx", "CloseHandle"),
        "Windows durable adapter unlock",
    )
    require_each_override_call_exactly_once(
        combined_windows,
        "DurableFileAdapter",
        "unlock_authority",
        "UnlockFileEx",
        "Windows durable adapter unlock",
    )
    for entry in (
        "lock_authority",
        "preflight_capabilities",
        "inspect_fixed_namespace",
        "read_root",
        "read_journal",
        "create_journal",
        "append_journal",
        "truncate_journal",
        "replace_journal",
        "replace_root",
    ):
        require_each_override_call_arguments(
            combined_windows,
            "DurableFileAdapter",
            entry,
            "CreateFileW",
            ("FILE_FLAG_OPEN_REPARSE_POINT",),
            "Windows fixed-child open",
        )
        require_each_override_call_arguments(
            combined_windows,
            "DurableFileAdapter",
            entry,
            "GetFileInformationByHandleEx",
            ("FileAttributeTagInfo",),
            "Windows fixed-child open",
        )
        require_each_override_pattern(
            combined_windows,
            "DurableFileAdapter",
            entry,
            r"\bFILE_ATTRIBUTE_REPARSE_POINT\b",
            "Windows fixed-child open",
        )
        require_each_override_fail_closed_checks(
            combined_windows,
            "DurableFileAdapter",
            entry,
            (
                "GetFileInformationByHandleEx",
                "FILE_ATTRIBUTE_REPARSE_POINT",
                "FILE_ATTRIBUTE_DIRECTORY",
            ),
            "Windows fixed-child open",
        )
    for entry in ("replace_journal", "replace_root"):
        require_each_override_call_arguments(
            combined_windows,
            "DurableFileAdapter",
            entry,
            "MoveFileExW",
            ("MOVEFILE_REPLACE_EXISTING", "MOVEFILE_WRITE_THROUGH"),
            "Windows durable adapter",
        )
        require_each_override_ordered_calls(
            combined_windows,
            "DurableFileAdapter",
            entry,
            (
                "write_flush_close",
                "MoveFileExW",
            ),
            "Windows durable adapter",
        )
        require_each_override_call_exactly_once(
            combined_windows,
            "DurableFileAdapter",
            entry,
            "MoveFileExW",
            "Windows durable adapter",
        )
    for entry, required_calls in (
        ("lock_authority", ("openat", "fstat", "fstatat")),
        ("authority_identity", ("fstat",)),
        ("append_journal", ("openat",)),
        ("replace_journal", ("renameat",)),
        ("replace_root", ("renameat",)),
    ):
        for required_call in required_calls:
            require_each_override_call(
                f"{common}\n{posix}",
                "DurableFileAdapter",
                entry,
                required_call,
                "POSIX durable adapter",
            )
            require_each_override_call(
                f"{common}\n{macos}",
                "DurableFileAdapter",
                entry,
                required_call,
                "macOS durable adapter",
            )
    for platform_source, label, flush_call in (
        (posix, "POSIX durable adapter", "fsync"),
        (macos, "macOS durable adapter", "fcntl"),
    ):
        combined_platform = f"{common}\n{platform_source}"
        require_each_override_write_count_mapping(combined_platform, "posix", label)
        require_each_override_read_count_mapping(combined_platform, "posix", label)
        require_each_override_retryable_result_mapping(
            combined_platform,
            "DurableFileChannel",
            "flush",
            flush_call,
            "EINTR",
            label,
        )
        require_each_override_retryable_result_mapping(
            combined_platform,
            "DurableFileChannel",
            "truncate",
            "ftruncate",
            "EINTR",
            label,
        )
        require_each_override_close_result_mapping(combined_platform, "close", label)
        require_each_override_close_result_mapping(
            combined_platform, "close", label, base="DurableReadChannel"
        )
        for entry, required_call in (
            ("write_some", "write"),
            ("flush", flush_call),
            ("truncate", "ftruncate"),
        ):
            require_each_override_call(
                combined_platform,
                "DurableFileChannel",
                entry,
                required_call,
                label,
            )
            require_each_override_call_exactly_once(
                combined_platform,
                "DurableFileChannel",
                entry,
                required_call,
                label,
            )
        require_each_override_call_exactly_once(
            combined_platform,
            "DurableFileChannel",
            "close",
            "close",
            label,
        )
        require_each_override_call_exactly_once(
            combined_platform,
            "DurableReadChannel",
            "read_some",
            "read",
            label,
        )
        require_each_override_call_exactly_once(
            combined_platform,
            "DurableReadChannel",
            "close",
            "close",
            label,
        )
        require_each_override_call_exactly_once(
            combined_platform,
            "DurableFileAdapter",
            "unlock_authority",
            "close",
            label,
        )
        require_each_override_omits_calls(
            combined_platform,
            "DurableFileAdapter",
            "replace_journal",
            ("ftruncate", "truncate_retrying_interrupts"),
            label,
        )
        require_each_override_omits_calls(
            combined_platform,
            "DurableFileAdapter",
            "truncate_journal",
            ("renameat",),
            label,
        )
        for entry in ("replace_journal", "replace_root"):
            require_each_override_directory_sync_binding(
                combined_platform,
                entry,
                f"{label} parent namespace durability",
            )
            require_each_override_result_call_checked(
                combined_platform,
                "DurableFileAdapter",
                entry,
                "sync_directory_retrying_interrupts",
                f"{label} parent namespace durability",
            )
            require_each_override_fail_closed_checks(
                combined_platform,
                "DurableFileAdapter",
                entry,
                ("renameat",),
                f"{label} replacement",
            )
            require_each_override_ordered_calls(
                combined_platform,
                "DurableFileAdapter",
                entry,
                (
                    "write_flush_close",
                    "renameat",
                    "sync_directory_retrying_interrupts",
                ),
                f"{label} parent namespace durability",
            )
            require_each_override_call_exactly_once(
                combined_platform,
                "DurableFileAdapter",
                entry,
                "renameat",
                f"{label} parent namespace durability",
            )
            require_each_override_call_exactly_once(
                combined_platform,
                "DurableFileAdapter",
                entry,
                "sync_directory_retrying_interrupts",
                f"{label} parent namespace durability",
            )
            require_each_override_omits_calls(
                combined_platform,
                "DurableFileAdapter",
                entry,
                ("fsync",),
                f"{label} parent namespace durability",
            )
    for entry, required_calls in (
        ("lock_authority", ("CreateFileW", "LockFileEx")),
        ("authority_identity", ("GetFileInformationByHandleEx",)),
        ("append_journal", ("CreateFileW",)),
        ("replace_journal", ("MoveFileExW",)),
        ("replace_root", ("MoveFileExW",)),
        ("unlock_authority", ("UnlockFileEx",)),
    ):
        for required_call in required_calls:
            require_each_override_call(
                f"{common}\n{windows}",
                "DurableFileAdapter",
                entry,
                required_call,
                "Windows durable adapter",
            )
    for entry, required_call in (
        ("write_some", "WriteFile"),
        ("flush", "FlushFileBuffers"),
        ("truncate", "SetEndOfFile"),
    ):
        require_each_override_call(
            combined_windows,
            "DurableFileChannel",
            entry,
            required_call,
            "Windows durable adapter",
        )
        require_each_override_call_exactly_once(
            combined_windows,
            "DurableFileChannel",
            entry,
            required_call,
            "Windows durable adapter",
        )
    require_each_override_write_count_mapping(
        combined_windows, "windows", "Windows durable adapter"
    )
    require_windows_write_override_shape(combined_windows, "Windows durable adapter")
    indirect_write_mutant = combined_windows.replace(
        "DWORD written = 0;",
        """const auto indirect_write = &::WriteFile;
        DWORD ignored = 0;
        indirect_write(handle_, bytes.data(), request, &ignored, nullptr);
        DWORD written = 0;""",
        1,
    )
    if indirect_write_mutant == combined_windows:
        raise AssertionError("Windows write shape mutant was not materialized")
    try:
        require_windows_write_override_shape(indirect_write_mutant, "mutant")
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted an indirect Windows write")
    namespace_marker = "namespace lemon::residency::detail {"
    overload_prefix, overload_separator, overload_suffix = combined_windows.rpartition(
        namespace_marker
    )
    if not overload_separator:
        raise AssertionError("Windows namespace marker is unavailable")
    overload_mutant = overload_prefix + """
BOOL WriteFile(HANDLE, const char *, DWORD request,
               DWORD *written, std::nullptr_t) {
    *written = request;
    return TRUE;
}
""" + overload_separator + overload_suffix
    try:
        require_each_override_write_count_mapping(overload_mutant, "windows", "mutant")
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted a redirected Windows write")
    success_macro_mutant = combined_windows.replace(
        "#define NOMINMAX",
        "#define NOMINMAX\n#define Succeeded Interrupted",
        1,
    )
    if success_macro_mutant == combined_windows:
        raise AssertionError("Windows success macro mutant was not materialized")
    try:
        require_each_override_write_count_mapping(
            success_macro_mutant, "windows", "mutant"
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted a shadowed Windows success")
    require_each_override_read_count_mapping(
        combined_windows, "windows", "Windows durable adapter"
    )
    require_each_override_retryable_result_mapping(
        combined_windows,
        "DurableFileChannel",
        "flush",
        "FlushFileBuffers",
        "ERROR_OPERATION_ABORTED",
        "Windows durable adapter",
    )
    for primitive in ("SetFilePointerEx", "SetEndOfFile"):
        require_each_override_retryable_result_mapping(
            combined_windows,
            "DurableFileChannel",
            "truncate",
            primitive,
            "ERROR_OPERATION_ABORTED",
            "Windows durable adapter",
        )
    require_each_override_close_result_mapping(
        combined_windows, "CloseHandle", "Windows durable adapter"
    )
    require_each_override_close_result_mapping(
        combined_windows,
        "CloseHandle",
        "Windows durable adapter",
        base="DurableReadChannel",
    )
    require_each_override_call(
        combined_windows,
        "DurableFileChannel",
        "truncate",
        "SetFilePointerEx",
        "Windows durable adapter",
    )
    require_each_override_call_exactly_once(
        combined_windows,
        "DurableFileChannel",
        "truncate",
        "SetFilePointerEx",
        "Windows durable adapter",
    )
    require_each_override_call_exactly_once(
        combined_windows,
        "DurableFileChannel",
        "close",
        "CloseHandle",
        "Windows durable adapter",
    )
    require_each_override_call_exactly_once(
        combined_windows,
        "DurableReadChannel",
        "read_some",
        "ReadFile",
        "Windows durable adapter",
    )
    require_each_override_call_exactly_once(
        combined_windows,
        "DurableReadChannel",
        "close",
        "CloseHandle",
        "Windows durable adapter",
    )
    require_each_override_call_exactly_once(
        combined_windows,
        "DurableFileAdapter",
        "unlock_authority",
        "CloseHandle",
        "Windows durable adapter",
    )
    require_each_override_omits_calls(
        combined_windows,
        "DurableFileAdapter",
        "replace_journal",
        (
            "SetFilePointerEx",
            "SetEndOfFile",
            "truncate_retrying_interrupts",
        ),
        "Windows durable adapter",
    )
    for entry in ("replace_journal", "replace_root"):
        require_each_override_fail_closed_checks(
            combined_windows,
            "DurableFileAdapter",
            entry,
            ("MoveFileExW",),
            "Windows durable adapter replacement",
        )
    require_each_override_omits_calls(
        combined_windows,
        "DurableFileAdapter",
        "truncate_journal",
        ("MoveFileExW",),
        "Windows durable adapter",
    )

    narration = """
// fsync renameat flock F_FULLFSYNC MoveFileExW FlushFileBuffers
const char *claims = "openat LockFileEx MOVEFILE_WRITE_THROUGH";
"""
    if any(
        token in strip_cpp_comments_and_literals(narration)
        for token in ("fsync", "MoveFileExW", "LockFileEx", "F_FULLFSYNC")
    ):
        raise AssertionError("source-shape scanner accepted narrated platform tokens")
    spliced_narration = "// narrated fsync \\\nMoveFileExW LockFileEx F_FULLFSYNC\n"
    if any(
        token in strip_cpp_comments_and_literals(spliced_narration)
        for token in ("fsync", "MoveFileExW", "LockFileEx", "F_FULLFSYNC")
    ):
        raise AssertionError("source scanner ignored C++ line splicing")
    try:
        require_call_expression(
            "DurableFileResult write_all(Channel &, Bytes) { return {}; }",
            "write_all",
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("shared-loop scanner accepted a dead definition")
    inactive_mutant = """
#if 0
write_all(channel, bytes);
openat(directory, name, O_APPEND | O_CLOEXEC | O_NOFOLLOW);
#endif
"""
    inactive_code = strip_cpp_comments_and_literals(inactive_mutant)
    if "write_all" in inactive_code or "openat" in inactive_code:
        raise AssertionError("source scanner accepted inactive preprocessor code")
    wrong_posix_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    void append_journal() override { openat(directory, name, O_RDONLY); }
};
void append_journal(int) {
    openat(directory, name, O_APPEND | O_CLOEXEC | O_NOFOLLOW);
}
"""
    try:
        require_each_override_call_arguments(
            wrong_posix_mutant,
            "DurableFileAdapter",
            "append_journal",
            "openat",
            ("O_APPEND", "O_CLOEXEC", "O_NOFOLLOW"),
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted dead POSIX flags")
    unsafe_posix_child_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    void read_root() override {
        openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    }
};
void dead_safety_check() {
    fstat(file, &metadata);
    if (!S_ISREG(metadata.st_mode)) { refuse(); }
}
"""
    for probe in (
        lambda: require_each_override_call(
            unsafe_posix_child_mutant,
            "DurableFileAdapter",
            "read_root",
            "fstat",
            "mutant",
        ),
        lambda: require_each_override_pattern(
            unsafe_posix_child_mutant,
            "DurableFileAdapter",
            "read_root",
            r"\bS_ISREG\s*\(",
            "mutant",
        ),
    ):
        try:
            probe()
        except AssertionError:
            pass
        else:
            raise AssertionError("source scanner accepted a dead POSIX safety check")
    ignored_posix_validation_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    void read_root() override {
        openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        fstat(file, &metadata);
        (void)S_ISREG(metadata.st_mode);
    }
};
"""
    try:
        require_each_override_fail_closed_checks(
            ignored_posix_validation_mutant,
            "DurableFileAdapter",
            "read_root",
            ("fstat", "S_ISREG"),
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted ignored POSIX validation")
    inverted_posix_validation_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    void read_root() override {
        if (fstat(file, &metadata) == 0) { return Succeeded; }
        if (S_ISREG(metadata.st_mode)) { return Succeeded; }
        return FailedBeforeEffect;
    }
};
"""
    try:
        require_each_override_fail_closed_checks(
            inverted_posix_validation_mutant,
            "DurableFileAdapter",
            "read_root",
            ("fstat", "S_ISREG"),
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted inverted POSIX validation")
    transitive_overload_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    void append_journal() override { do_append(0); }
};
void do_append(int) { openat(directory, name, O_RDONLY); }
void do_append() {
    openat(directory, name, O_APPEND | O_CLOEXEC | O_NOFOLLOW);
}
"""
    try:
        require_each_override_call_arguments(
            transitive_overload_mutant,
            "DurableFileAdapter",
            "append_journal",
            "openat",
            ("O_APPEND", "O_CLOEXEC", "O_NOFOLLOW"),
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner unioned transitive overloads")
    retrying_lock_control = """
class LockCall final : public DurableInterruptibleCall {
public:
    explicit LockCall(int lock_fd) : lock_fd_(lock_fd) {}
    DurableFileResult attempt() override {
        if (flock(lock_fd_, LOCK_EX) != 0) {
            if (errno == EINTR) { return Interrupted; }
            return FailedBeforeEffect;
        }
        return Succeeded;
    }
private:
    int lock_fd_;
};
DurableFileResult lock_file_retrying_interrupts(int lock_fd) {
    LockCall call(lock_fd);
    return retry_interrupted(call);
}
"""
    require_retrying_posix_flock(
        retrying_lock_control,
        "lock_file_retrying_interrupts",
        "LOCK_EX",
        "control",
    )
    unrelated_lock_retry_mutant = """
class UnrelatedCall final : public DurableInterruptibleCall {
public:
    DurableFileResult attempt() override { return FailedBeforeEffect; }
};
DurableFileResult lock_file_retrying_interrupts(int lock_fd) {
    flock(lock_fd, LOCK_EX);
    UnrelatedCall call;
    return retry_interrupted(call);
}
"""
    try:
        require_retrying_posix_flock(
            unrelated_lock_retry_mutant,
            "lock_file_retrying_interrupts",
            "LOCK_EX",
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted unrelated flock retry")
    ignored_posix_lock_mutants = (
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableFileResult lock_authority() override {
        lock_file_retrying_interrupts(lock_fd);
        return Succeeded;
    }
};
""",
            "lock_authority",
        ),
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableFileResult unlock_authority() override {
        unlock_file_retrying_interrupts(lock_fd);
        close(lock_fd);
        return Succeeded;
    }
};
""",
            "unlock_authority",
        ),
    )
    for mutant, entry in ignored_posix_lock_mutants:
        try:
            if entry == "lock_authority":
                require_each_override_result_call_checked(
                    mutant,
                    "DurableFileAdapter",
                    entry,
                    "lock_file_retrying_interrupts",
                    "mutant",
                )
            else:
                require_posix_unlock_result_mapping(mutant, "mutant")
        except AssertionError:
            pass
        else:
            raise AssertionError("source scanner accepted ignored POSIX lock result")
    lock_identity_mutants = (
        """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableFileResult lock_authority() override {
        lock_file_retrying_interrupts(lock_fd);
        if (fstat(lock_fd, &opened) != 0) { return FailedBeforeEffect; }
        if (fstatat(directory, lock_name, &current,
                    AT_SYMLINK_NOFOLLOW) != 0) { return FailedBeforeEffect; }
        if (!S_ISREG(current.st_mode)) { return FailedBeforeEffect; }
        return Succeeded;
    }
};
""",
        """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableFileResult lock_authority() override {
        lock_file_retrying_interrupts(lock_fd);
        if (fstat(lock_fd, &opened) != 0) { return FailedBeforeEffect; }
        if (fstatat(directory, lock_name, &current,
                    AT_SYMLINK_NOFOLLOW) != 0) { return FailedBeforeEffect; }
        if (!S_ISREG(current.st_mode)) { return FailedBeforeEffect; }
        if (current.st_nlink == 0 || current.st_dev != opened.st_dev) {
            return FailedBeforeEffect;
        }
        return Succeeded;
    }
};
""",
    )
    for mutant in lock_identity_mutants:
        try:
            require_each_override_fail_closed_checks(
                mutant,
                "DurableFileAdapter",
                "lock_authority",
                ("fstat", "fstatat", "S_ISREG", "st_nlink", "st_dev", "st_ino"),
                "mutant",
            )
        except AssertionError:
            pass
        else:
            raise AssertionError("source scanner accepted split stable lock")
    wrong_windows_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    void lock_authority() override {
        OVERLAPPED live;
        CreateFileW(path, GENERIC_WRITE, FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        LockFileEx(handle, 0, 0, 1, 1, &live);
    }
};
void lock_authority(int) {
    OVERLAPPED unused{};
    CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD,
               &unused);
}
"""
    for probe in (
        lambda: require_each_override_call_arguments(
            wrong_windows_mutant,
            "DurableFileAdapter",
            "lock_authority",
            "CreateFileW",
            (
                "FILE_SHARE_READ",
                "FILE_SHARE_WRITE",
                "OPEN_ALWAYS",
                "FILE_FLAG_OPEN_REPARSE_POINT",
            ),
            "mutant",
        ),
        lambda: require_each_override_call_arguments(
            wrong_windows_mutant,
            "DurableFileAdapter",
            "lock_authority",
            "LockFileEx",
            ("LOCKFILE_EXCLUSIVE_LOCK", "MAXDWORD", "MAXDWORD"),
            "mutant",
        ),
        lambda: require_each_override_pattern(
            wrong_windows_mutant,
            "DurableFileAdapter",
            "lock_authority",
            r"\bOVERLAPPED\s+\w+\s*(?:\{\s*\}|=\s*\{\s*\})",
            "mutant",
        ),
        lambda: require_windows_lock_overlapped_binding(wrong_windows_mutant),
    ):
        try:
            probe()
        except AssertionError:
            pass
        else:
            raise AssertionError("source scanner accepted dead Windows flags")
    ignored_windows_lock_mutants = (
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableFileResult lock_authority() override {
        OVERLAPPED lock_overlapped{};
        LockFileEx(lock, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD,
                   &lock_overlapped);
        return Succeeded;
    }
};
""",
            "lock_authority",
            ("LockFileEx",),
        ),
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableFileResult unlock_authority() override {
        UnlockFileEx(lock, 0, MAXDWORD, MAXDWORD, &lock_overlapped);
        CloseHandle(lock);
        return Succeeded;
    }
};
""",
            "unlock_authority",
            ("UnlockFileEx", "CloseHandle"),
        ),
    )
    for mutant, entry, predicates in ignored_windows_lock_mutants:
        try:
            if entry == "unlock_authority":
                require_windows_unlock_result_mapping(mutant)
            else:
                require_each_override_fail_closed_checks(
                    mutant,
                    "DurableFileAdapter",
                    entry,
                    predicates,
                    "mutant",
                )
        except AssertionError:
            pass
        else:
            raise AssertionError("source scanner accepted ignored Windows lock result")
    missing_windows_identity_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableIdentityResult authority_identity() override {
        FILE_ID_INFO directory_identity{};
        GetFileInformationByHandleEx(directory, FileIdInfo,
                                     &directory_identity,
                                     sizeof(directory_identity));
        return make_identity(directory_identity);
    }
};
"""
    try:
        require_windows_composite_identity(missing_windows_identity_mutant)
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted incomplete Windows identity")
    missing_windows_lock_comparison_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableFileResult lock_authority() override {
        if (!LockFileEx(lock, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD,
                        &lock_overlapped)) { return FailedBeforeEffect; }
        if (!GetFileInformationByHandleEx(lock, FileIdInfo, &opened,
                                          sizeof(opened))) {
            return FailedBeforeEffect;
        }
        if (!GetFileInformationByHandleEx(current, FileIdInfo, &current_info,
                                          sizeof(current_info))) {
            return FailedBeforeEffect;
        }
        return Succeeded;
    }
};
"""
    try:
        require_each_override_fail_closed_checks(
            missing_windows_lock_comparison_mutant,
            "DurableFileAdapter",
            "lock_authority",
            ("VolumeSerialNumber", "FileId", "NumberOfLinks"),
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted stale Windows lock identity")
    unsafe_windows_child_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    void read_root() override {
        CreateFileW(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
    }
};
void dead_safety_check() {
    CreateFileW(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    FILE_ATTRIBUTE_TAG_INFO info{};
    GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info,
                                 sizeof(info));
    if (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { refuse(); }
}
"""
    for probe in (
        lambda: require_each_override_call_arguments(
            unsafe_windows_child_mutant,
            "DurableFileAdapter",
            "read_root",
            "CreateFileW",
            ("FILE_FLAG_OPEN_REPARSE_POINT",),
            "mutant",
        ),
        lambda: require_each_override_call_arguments(
            unsafe_windows_child_mutant,
            "DurableFileAdapter",
            "read_root",
            "GetFileInformationByHandleEx",
            ("FileAttributeTagInfo",),
            "mutant",
        ),
        lambda: require_each_override_pattern(
            unsafe_windows_child_mutant,
            "DurableFileAdapter",
            "read_root",
            r"\bFILE_ATTRIBUTE_REPARSE_POINT\b",
            "mutant",
        ),
    ):
        try:
            probe()
        except AssertionError:
            pass
        else:
            raise AssertionError("source scanner accepted a dead Windows safety check")
    ignored_windows_validation_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    void read_root() override {
        CreateFileW(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        FILE_ATTRIBUTE_TAG_INFO info{};
        GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info,
                                     sizeof(info));
        (void)(info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
        (void)(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    }
};
"""
    try:
        require_each_override_fail_closed_checks(
            ignored_windows_validation_mutant,
            "DurableFileAdapter",
            "read_root",
            (
                "GetFileInformationByHandleEx",
                "FILE_ATTRIBUTE_REPARSE_POINT",
                "FILE_ATTRIBUTE_DIRECTORY",
            ),
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted ignored Windows validation")
    inverted_windows_validation_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    void read_root() override {
        if (GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info,
                                         sizeof(info))) {
            return Succeeded;
        }
        if (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            return Succeeded;
        }
        if (info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            return Succeeded;
        }
        return FailedBeforeEffect;
    }
};
"""
    try:
        require_each_override_fail_closed_checks(
            inverted_windows_validation_mutant,
            "DurableFileAdapter",
            "read_root",
            (
                "GetFileInformationByHandleEx",
                "FILE_ATTRIBUTE_REPARSE_POINT",
                "FILE_ATTRIBUTE_DIRECTORY",
            ),
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted inverted Windows validation")
    wrong_replace_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    void replace_root() override {
        MoveFileExW(stage, root, MOVEFILE_COPY_ALLOWED);
    }
};
void replace_root(int) {
    MoveFileExW(stage, root,
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
"""
    try:
        require_each_override_call_arguments(
            wrong_replace_mutant,
            "DurableFileAdapter",
            "replace_root",
            "MoveFileExW",
            ("MOVEFILE_REPLACE_EXISTING", "MOVEFILE_WRITE_THROUGH"),
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted dead replacement flags")
    in_place_compaction_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    void replace_journal() override {
        ftruncate(journal, exact_size);
        renameat(directory, harmless, directory, harmless);
    }
};
"""
    try:
        require_each_override_omits_calls(
            in_place_compaction_mutant,
            "DurableFileAdapter",
            "replace_journal",
            ("ftruncate", "truncate_retrying_interrupts"),
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted in-place compaction")
    replacement_tail_repair_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    void truncate_journal() override {
        MoveFileExW(stage, journal,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
};
"""
    try:
        require_each_override_omits_calls(
            replacement_tail_repair_mutant,
            "DurableFileAdapter",
            "truncate_journal",
            ("MoveFileExW",),
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted replacement tail repair")
    ordering_mutants = (
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    void append_journal() override {
        flush_retrying_interrupts(channel);
        write_all(channel, bytes);
        close_once(channel);
    }
};
""",
            "append_journal",
            ("write_all", "flush_retrying_interrupts", "close_once"),
        ),
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    void replace_root() override {
        renameat(directory, stage, directory, root);
        write_all(channel, bytes);
        flush_retrying_interrupts(channel);
        close_once(channel);
        fsync(directory);
    }
};
""",
            "replace_root",
            (
                "write_all",
                "flush_retrying_interrupts",
                "close_once",
                "renameat",
                "sync_directory_retrying_interrupts",
            ),
        ),
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    void truncate_journal() override {
        flush_retrying_interrupts(channel);
        truncate_retrying_interrupts(channel, boundary);
        close_once(channel);
    }
};
""",
            "truncate_journal",
            (
                "truncate_retrying_interrupts",
                "flush_retrying_interrupts",
                "close_once",
            ),
        ),
    )
    for mutant, entry, calls in ordering_mutants:
        try:
            require_each_override_ordered_calls(
                mutant,
                "DurableFileAdapter",
                entry,
                calls,
                "mutant",
            )
        except AssertionError:
            pass
        else:
            raise AssertionError("source scanner accepted unsafe durability order")
    duplicate_mutation_mutants = (
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    void replace_root() override {
        renameat(directory, stage, directory, root);
        write_all(channel, bytes);
        flush_retrying_interrupts(channel);
        close_once(channel);
        renameat(directory, stage, directory, root);
        sync_directory_retrying_interrupts(directory);
    }
};
""",
            "replace_root",
            "renameat",
        ),
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    void truncate_journal() override {
        truncate_retrying_interrupts(channel, boundary);
        truncate_retrying_interrupts(channel, boundary);
        flush_retrying_interrupts(channel);
        close_once(channel);
    }
};
""",
            "truncate_journal",
            "truncate_retrying_interrupts",
        ),
    )
    for mutant, entry, call in duplicate_mutation_mutants:
        try:
            require_each_override_call_exactly_once(
                mutant,
                "DurableFileAdapter",
                entry,
                call,
                "mutant",
            )
        except AssertionError:
            pass
        else:
            raise AssertionError("source scanner accepted duplicate mutation")
    wrong_macos_mutant = """
class LiveChannel final : public DurableFileChannel {
public:
    void flush() override { fsync(file); }
};
void flush(int) { fcntl(file, F_FULLFSYNC); }
"""
    try:
        require_each_override_call_arguments(
            wrong_macos_mutant,
            "DurableFileChannel",
            "flush",
            "fcntl",
            ("F_FULLFSYNC",),
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted dead macOS persistence")
    missing_linux_flush_mutant = """
class LiveChannel final : public DurableFileChannel {
public:
    void flush() override { return_success(); }
};
void unused_flush() { fsync(file); }
"""
    try:
        require_each_override_call(
            missing_linux_flush_mutant,
            "DurableFileChannel",
            "flush",
            "fsync",
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted a non-durable Linux flush")
    native_read_controls = (
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        std::string buffer(max_bytes, '\0');
        const auto count = read(file, buffer.data(), max_bytes);
        if (count < 0) {
            if (errno == EINTR) return Interrupted;
            return FailedBeforeEffect;
        }
        return {Succeeded, std::string(buffer.data(), count), count == 0};
    }
};
""",
            "posix",
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        const auto request = std::min(max_bytes, std::size_t(MAXDWORD));
        std::string buffer(request, '\0');
        DWORD read_count = 0;
        if (!ReadFile(file, buffer.data(), request, &read_count, nullptr)) {
            if (GetLastError() == ERROR_OPERATION_ABORTED) return Interrupted;
            return FailedBeforeEffect;
        }
        return {Succeeded, std::string(buffer.data(), read_count),
                read_count == 0};
    }
};
""",
            "windows",
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        const DWORD request = static_cast<DWORD>(
            std::min(max_bytes, std::size_t(MAXDWORD)));
        std::string buffer(request, '\0');
        DWORD read_count = 0;
        if (!ReadFile(file, buffer.data(), request, &read_count, nullptr)) {
            if (GetLastError() == ERROR_OPERATION_ABORTED) return Interrupted;
            return FailedBeforeEffect;
        }
        return {Succeeded, std::string(buffer.data(), read_count),
                read_count == 0};
    }
};
""",
            "windows",
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        const DWORD request = static_cast<DWORD>(
            std::min(std::size_t(MAXDWORD), max_bytes));
        std::string buffer(request, '\0');
        DWORD read_count = 0;
        if (!ReadFile(file, buffer.data(), request, &read_count, nullptr)) {
            if (GetLastError() == ERROR_OPERATION_ABORTED) return Interrupted;
            return FailedBeforeEffect;
        }
        return {Succeeded, std::string(buffer.data(), read_count),
                read_count == 0};
    }
};
""",
            "windows",
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        const std::uint32_t request = static_cast<std::uint32_t>(std::min(
            max_bytes, std::numeric_limits<std::uint32_t>::max()));
        std::string buffer(request, '\0');
        DWORD read_count = 0;
        if (!ReadFile(file, buffer.data(), request, &read_count, nullptr)) {
            if (GetLastError() == ERROR_OPERATION_ABORTED) return Interrupted;
            return FailedBeforeEffect;
        }
        return {Succeeded, std::string(buffer.data(), read_count),
                read_count == 0};
    }
};
""",
            "windows",
        ),
    )
    for control, platform in native_read_controls:
        require_each_override_read_count_mapping(control, platform, "control")

    unsafe_narrowed_read_requests = (
        "static_cast<DWORD>(max_bytes)",
        "std::min(static_cast<DWORD>(max_bytes), MAXDWORD)",
        (
            "static_cast<DWORD>(std::min("
            "max_bytes, std::numeric_limits<std::size_t>::max()))"
        ),
        "static_cast<DWORD>(std::min(max_bytes, std::size_t(MAXDWORD) + 1))",
        "static_cast<DWORD>(std::min(max_bytes, 0x100000000ULL))",
    )
    for request_expression in unsafe_narrowed_read_requests:
        narrowed_read_mutant = f"""
class LiveReadChannel final : public DurableReadChannel {{
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {{
        const DWORD request = {request_expression};
        std::string buffer(request, '\0');
        DWORD read_count = 0;
        if (!ReadFile(file, buffer.data(), request, &read_count, nullptr)) {{
            if (GetLastError() == ERROR_OPERATION_ABORTED) return Interrupted;
            return FailedBeforeEffect;
        }}
        return {{Succeeded, std::string(buffer.data(), read_count),
                read_count == 0}};
    }}
}};
"""
        try:
            require_each_override_read_count_mapping(
                narrowed_read_mutant, "windows", "mutant"
            )
        except AssertionError:
            pass
        else:
            raise AssertionError("source scanner accepted unsafe narrowed read")

    native_result_mutants = (
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        write(file, bytes.data(), bytes.size());
        return {Succeeded, bytes.size()};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "posix", "mutant"
            ),
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        read(file, buffer, max_bytes);
        return {Succeeded, std::string(buffer, max_bytes), false};
    }
};
""",
            lambda source: require_each_override_read_count_mapping(
                source, "posix", "mutant"
            ),
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        std::string buffer(max_bytes * 1000, '\0');
        const auto count =
            read(file, buffer.data(), std::min(max_bytes, sizeof(buffer)));
        if (count < 0) {
            if (errno == EINTR) return Interrupted;
            return FailedBeforeEffect;
        }
        return {Succeeded, std::string(buffer.data(), count), count == 0};
    }
};
""",
            lambda source: require_each_override_read_count_mapping(
                source, "posix", "mutant"
            ),
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        std::string buffer(max_bytes * 1000, '\0');
        const auto count = read(file, buffer.data(), max_bytes * 1000);
        if (count < 0) {
            if (errno == EINTR) return Interrupted;
            return FailedBeforeEffect;
        }
        return {Succeeded, std::string(buffer.data(), count), count == 0};
    }
};
""",
            lambda source: require_each_override_read_count_mapping(
                source, "posix", "mutant"
            ),
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        std::string buffer(max_bytes, '\0');
        const auto count = read(file, buffer.data(), max_bytes);
        if (count < 0) {
            if (errno == EINTR) return Interrupted;
            return FailedBeforeEffect;
        }
        return {Succeeded, std::string(buffer.data(), count), count < max_bytes};
    }
};
""",
            lambda source: require_each_override_read_count_mapping(
                source, "posix", "mutant"
            ),
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        std::string buffer(max_bytes, '\0');
        const auto count = read(file, buffer.data(), max_bytes);
        if (count < 0) {
            if (errno == EINTR) return Interrupted;
            return FailedBeforeEffect;
        }
        return {Succeeded, std::string(buffer.data(), count), true};
    }
};
""",
            lambda source: require_each_override_read_count_mapping(
                source, "posix", "mutant"
            ),
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        std::string buffer(1073741824, '\0');
        const auto count = read(file, buffer.data(), 1073741824);
        if (count < 0) {
            if (errno == EINTR) return Interrupted;
            return FailedBeforeEffect;
        }
        return {Succeeded, std::string(buffer.data(), count), count == 0};
    }
};
""",
            lambda source: require_each_override_read_count_mapping(
                source, "posix", "mutant"
            ),
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        const auto request = std::min(max_bytes, std::size_t(MAXDWORD));
        std::string buffer(request, '\0');
        DWORD read_count = 0;
        if (!ReadFile(file, buffer.data(), request, &read_count, nullptr)) {
            if (GetLastError() == ERROR_OPERATION_ABORTED) return Interrupted;
            return FailedBeforeEffect;
        }
        return {Succeeded, std::string(buffer.data(), read_count), false};
    }
};
""",
            lambda source: require_each_override_read_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        const auto request = max_bytes * 1000;
        std::string buffer(request, '\0');
        DWORD read_count = 0;
        if (!ReadFile(file, buffer.data(), request, &read_count, nullptr)) {
            if (GetLastError() == ERROR_OPERATION_ABORTED) return Interrupted;
            return FailedBeforeEffect;
        }
        return {Succeeded, std::string(buffer.data(), read_count),
                read_count == 0};
    }
};
""",
            lambda source: require_each_override_read_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        const auto request = std::min(max_bytes, std::size_t(MAXDWORD));
        std::string buffer(request, '\0');
        DWORD read_count = 0;
        if (!ReadFile(file, buffer.data(), request, &read_count, nullptr)) {
            if (GetLastError() == ERROR_OPERATION_ABORTED) return Interrupted;
            return FailedBeforeEffect;
        }
        return {Succeeded, std::string(buffer.data(), read_count),
                read_count < request};
    }
};
""",
            lambda source: require_each_override_read_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableFileResult flush() override { fsync(file); return Succeeded; }
};
""",
            lambda source: require_each_override_retryable_result_mapping(
                source,
                "DurableFileChannel",
                "flush",
                "fsync",
                "EINTR",
                "mutant",
            ),
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        DWORD read_count = 0;
        ReadFile(file, buffer, max_bytes, &read_count, nullptr);
        return {Succeeded, std::string(buffer, max_bytes), false};
    }
};
""",
            lambda source: require_each_override_read_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveReadChannel final : public DurableReadChannel {
public:
    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        std::string buffer(1073741824, '\0');
        DWORD read_count = 0;
        if (!ReadFile(file, buffer.data(), 1073741824,
                      &read_count, nullptr)) {
            if (GetLastError() == ERROR_OPERATION_ABORTED) return Interrupted;
            return FailedBeforeEffect;
        }
        return {Succeeded, std::string(buffer.data(), read_count),
                read_count == 0};
    }
};
""",
            lambda source: require_each_override_read_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableFileResult truncate() override {
        ftruncate(file, boundary);
        return Succeeded;
    }
};
""",
            lambda source: require_each_override_retryable_result_mapping(
                source,
                "DurableFileChannel",
                "truncate",
                "ftruncate",
                "EINTR",
                "mutant",
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableFileResult close() override { close(file); return Succeeded; }
};
""",
            lambda source: require_each_override_close_result_mapping(
                source, "close", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableFileResult flush() override {
        fcntl(file, F_FULLFSYNC);
        return Succeeded;
    }
};
""",
            lambda source: require_each_override_retryable_result_mapping(
                source,
                "DurableFileChannel",
                "flush",
                "fcntl",
                "EINTR",
                "mutant",
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        DWORD written = 0;
        ::WriteFile(file, bytes.data(), bytes.size(), &written, nullptr);
        return {Succeeded, bytes.size()};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        const auto WriteFile = [](auto &&...) { return FALSE; };
        DWORD written = 0;
        if (!WriteFile(file, bytes.data(), bytes.size(), &written, nullptr)) {
            const auto error = ::GetLastError();
            return {::lemon::residency::detail::DurableFileResult{
                        ::lemon::residency::detail::DurableFileStatus::
                            EffectMayHaveOccurred,
                        ::std::to_string(error)},
                    0};
        }
        return {Succeeded, written};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
#define WriteFile(...) FALSE
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data(), bytes.size(), &written, nullptr)) {
            const auto error = ::GetLastError();
            return {::lemon::residency::detail::DurableFileResult{
                        ::lemon::residency::detail::DurableFileStatus::
                            EffectMayHaveOccurred,
                        ::std::to_string(error)},
                    0};
        }
        return {Succeeded, written};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
%:define EffectMayHaveOccurred Interrupted
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data(), bytes.size(), &written, nullptr)) {
            const auto error = ::GetLastError();
            return {::lemon::residency::detail::DurableFileResult{
                        ::lemon::residency::detail::DurableFileStatus::
                            EffectMayHaveOccurred,
                        ::std::to_string(error)},
                    0};
        }
        return {Succeeded, written};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
struct RetryStatus {
    inline static constexpr auto EffectMayHaveOccurred =
        ::lemon::residency::detail::DurableFileStatus::Interrupted;
};
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        using DurableFileStatus = RetryStatus;
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data(), bytes.size(), &written, nullptr)) {
            const auto error = ::GetLastError();
            return {DurableFileResult{
                        DurableFileStatus::EffectMayHaveOccurred,
                        std::to_string(error)},
                    0};
        }
        return {Succeeded, written};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data(), bytes.size(), &written, nullptr)) {
            const auto error = GetLastError();
            throw error;
            return {make_windows_result(
                        DurableFileStatus::EffectMayHaveOccurred, error),
                    0};
        }
        return {Succeeded, written};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data(), bytes.size(), &written, nullptr) &&
            should_report()) {
            const auto error = GetLastError();
            return {make_windows_result(EffectMayHaveOccurred, error), 0};
        }
        return {Succeeded, written};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data(), bytes.size(), &written, nullptr)) {
            const auto error = GetLastError();
            return error == ERROR_OPERATION_ABORTED
                       ? DurableWriteResult{Interrupted, 0}
                       : DurableWriteResult{EffectMayHaveOccurred, 0};
        }
        return {Succeeded, written};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data(), bytes.size(), &written, nullptr)) {
            const auto error = GetLastError();
            return (EffectMayHaveOccurred, interrupted_write_failure(error));
        }
        return {Succeeded, written};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data(), bytes.size(), &written, nullptr)) {
            if (GetLastError() == ERROR_DISK_FULL) {
                return {EffectMayHaveOccurred, 0};
            }
        }
        return {Succeeded, written};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data(), bytes.size(), &written, nullptr)) {
            switch (GetLastError()) {
            case ERROR_OPERATION_ABORTED:
                return Interrupted;
            default:
                return {EffectMayHaveOccurred, 0};
            }
        }
        return {Succeeded, written};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        const auto retry = Interrupted;
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data(), bytes.size(), &written, nullptr)) {
            return retry;
        }
        return {Succeeded, written};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data(), bytes.size(), &written, nullptr)) {
            return {EffectMayHaveOccurred, 0};
        }
        if (GetLastError() == ERROR_OPERATION_ABORTED) return Interrupted;
        return {Succeeded, written};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data(), bytes.size(), &written, nullptr)) {
            return {FailedBeforeEffect, 0};
        }
        return {Succeeded, written};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableWriteResult write_some(std::string_view bytes) override {
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data(), bytes.size(), &written, nullptr)) {
            if (GetLastError() == ERROR_OPERATION_ABORTED) return Interrupted;
            return {EffectMayHaveOccurred, 0};
        }
        return {Succeeded, written};
    }
};
""",
            lambda source: require_each_override_write_count_mapping(
                source, "windows", "mutant"
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableFileResult flush() override {
        FlushFileBuffers(file);
        return Succeeded;
    }
};
""",
            lambda source: require_each_override_retryable_result_mapping(
                source,
                "DurableFileChannel",
                "flush",
                "FlushFileBuffers",
                "ERROR_OPERATION_ABORTED",
                "mutant",
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableFileResult truncate() override {
        SetFilePointerEx(file, boundary, nullptr, FILE_BEGIN);
        SetEndOfFile(file);
        return Succeeded;
    }
};
""",
            lambda source: require_each_override_retryable_result_mapping(
                source,
                "DurableFileChannel",
                "truncate",
                "SetEndOfFile",
                "ERROR_OPERATION_ABORTED",
                "mutant",
            ),
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    DurableFileResult close() override { CloseHandle(file); return Succeeded; }
};
""",
            lambda source: require_each_override_close_result_mapping(
                source, "CloseHandle", "mutant"
            ),
        ),
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableFileResult replace_root() override {
        renameat(directory, stage, directory, root);
        return Succeeded;
    }
};
""",
            lambda source: require_each_override_fail_closed_checks(
                source,
                "DurableFileAdapter",
                "replace_root",
                ("renameat",),
                "mutant",
            ),
        ),
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableFileResult replace_root() override {
        MoveFileExW(stage, root,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        return Succeeded;
    }
};
""",
            lambda source: require_each_override_fail_closed_checks(
                source,
                "DurableFileAdapter",
                "replace_root",
                ("MoveFileExW",),
                "mutant",
            ),
        ),
    )
    for mutant, probe in native_result_mutants:
        try:
            probe(mutant)
        except AssertionError:
            pass
        else:
            raise AssertionError("source scanner accepted ignored native result")
    transaction_helper_control = """
DurableFileResult write_flush_close(DurableFileChannel &channel,
                                    std::string_view bytes) {
    const auto write_result = write_all(channel, bytes);
    const auto flush_result = flush_retrying_interrupts(channel);
    const auto close_result = close_once(channel);
    if (write_result.effect_may_have_occurred()) {
        return {DurableFileStatus::EffectMayHaveOccurred, {}};
    }
    return combine_results(write_result, flush_result, close_result);
}
DurableFileResult truncate_flush_close(DurableFileChannel &channel,
                                       std::size_t bytes) {
    const auto truncate_result = truncate_retrying_interrupts(channel, bytes);
    const auto flush_result = flush_retrying_interrupts(channel);
    const auto close_result = close_once(channel);
    if (truncate_result.effect_may_have_occurred()) {
        return {DurableFileStatus::EffectMayHaveOccurred, {}};
    }
    return combine_results(truncate_result, flush_result, close_result);
}
"""
    require_transaction_helper_contract(transaction_helper_control)
    bounded_read_control = """
inline constexpr std::size_t durable_read_chunk_bytes = 64 * 1024;
DurableReadResult read_bounded_close(DurableReadChannel &channel,
                                     std::size_t max_bytes) {
    std::string retained;
    const auto requested = std::min(
        max_bytes + 1 - retained.size(), durable_read_chunk_bytes);
    const auto read_result = channel.read_some(requested);
    const bool interrupted =
        read_result.result.status == DurableFileStatus::Interrupted;
    const bool truncated = !read_result.end_of_file;
    const auto close_result = close_read_once(channel);
    if (!close_result.succeeded()) {
        return {close_result, {}, false};
    }
    return {read_result.result, read_result.bytes,
            truncated && !interrupted};
}
"""
    require_bounded_read_helper_contract(bounded_read_control)
    unbounded_read_mutant = """
inline constexpr std::size_t durable_read_chunk_bytes = 64 * 1024;
DurableReadResult read_bounded_close(DurableReadChannel &channel,
                                     std::size_t max_bytes) {
    (void)max_bytes;
    const auto read_result = channel.read_some(SIZE_MAX);
    const bool interrupted =
        read_result.result.status == DurableFileStatus::Interrupted;
    const bool truncated = !read_result.end_of_file;
    const auto close_result = close_read_once(channel);
    if (!close_result.succeeded()) {
        return {close_result, {}, false};
    }
    return {read_result.result, read_result.bytes,
            truncated && !interrupted};
}
"""
    try:
        require_bounded_read_helper_contract(unbounded_read_mutant)
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted an unbounded native read")
    inflated_chunk_mutant = bounded_read_control.replace(
        "durable_read_chunk_bytes);", "durable_read_chunk_bytes * 2);", 1
    )
    try:
        require_bounded_read_helper_contract(inflated_chunk_mutant)
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted an inflated read chunk")
    aggregate_result_control = """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableFileResult append_journal(std::string_view bytes) override {
        return write_flush_close(channel, bytes);
    }
};
"""
    require_each_override_call_result_propagated(
        aggregate_result_control,
        "DurableFileAdapter",
        "append_journal",
        "write_flush_close",
        "control",
    )
    ignored_transaction_helper_mutant = """
DurableFileResult write_flush_close(DurableFileChannel &channel,
                                    std::string_view bytes) {
    const auto write_result = write_all(channel, bytes);
    const auto flush_result = flush_retrying_interrupts(channel);
    const auto close_result = close_once(channel);
    return succeeded();
}
DurableFileResult truncate_flush_close(DurableFileChannel &channel,
                                       std::size_t bytes) {
    const auto truncate_result = truncate_retrying_interrupts(channel, bytes);
    const auto flush_result = flush_retrying_interrupts(channel);
    const auto close_result = close_once(channel);
    return succeeded();
}
"""
    try:
        require_transaction_helper_contract(ignored_transaction_helper_mutant)
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted ignored transaction results")
    ignored_aggregate_result_mutants = (
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableFileResult append_journal(std::string_view bytes) override {
        write_flush_close(channel, bytes);
        return succeeded();
    }
};
""",
            "append_journal",
            "write_flush_close",
        ),
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableFileResult truncate_journal(std::size_t bytes) override {
        const auto ignored = truncate_flush_close(channel, bytes);
        return succeeded();
    }
};
""",
            "truncate_journal",
            "truncate_flush_close",
        ),
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableReadResult read_root(std::size_t max_bytes) override {
        const auto ignored = close_once(channel);
        return {succeeded(), bytes, false};
    }
};
""",
            "read_root",
            "close_once",
        ),
    )
    for mutant, entry, call in ignored_aggregate_result_mutants:
        try:
            require_each_override_call_result_propagated(
                mutant, "DurableFileAdapter", entry, call, "mutant"
            )
        except AssertionError:
            pass
        else:
            raise AssertionError("source scanner accepted ignored aggregate result")
    missing_namespace_sync_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    void replace_journal() override {
        renameat(directory, stage, directory, journal);
    }
};
void unused_namespace_sync() { fsync(directory); }
"""
    try:
        require_each_override_call(
            missing_namespace_sync_mutant,
            "DurableFileAdapter",
            "replace_journal",
            "sync_directory_retrying_interrupts",
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted an unsynced namespace")
    retrying_namespace_control = """
class DirectorySyncCall final : public DurableInterruptibleCall {
public:
    explicit DirectorySyncCall(int directory_fd)
        : directory_fd_(directory_fd) {}
    DurableFileResult attempt() override {
        if (fsync(directory_fd_) != 0) {
            if (errno == EINTR) { return Interrupted; }
            return FailedBeforeEffect;
        }
        return Succeeded;
    }
private:
    int directory_fd_;
};
DurableFileResult sync_directory_retrying_interrupts(int directory_fd) {
    DirectorySyncCall call(directory_fd);
    return retry_interrupted(call);
}
"""
    require_retrying_directory_sync(retrying_namespace_control, "control")
    invalid_namespace_helpers = (
        """
class UnrelatedCall final : public DurableInterruptibleCall {
public:
    DurableFileResult attempt() override { return FailedBeforeEffect; }
};
DurableFileResult sync_directory_retrying_interrupts(int directory_fd) {
    UnrelatedCall call;
    retry_interrupted(call);
    return fsync(directory_fd);
}
""",
        """
class DirectorySyncCall final : public DurableInterruptibleCall {
public:
    explicit DirectorySyncCall(int directory_fd)
        : directory_fd_(directory_fd) {}
    DurableFileResult attempt() override {
        fsync(directory_fd_);
        return Succeeded;
    }
private:
    int directory_fd_;
};
DurableFileResult sync_directory_retrying_interrupts(int directory_fd) {
    DirectorySyncCall call(directory_fd);
    return retry_interrupted(call);
}
""",
    )
    for mutant in invalid_namespace_helpers:
        try:
            require_retrying_directory_sync(mutant, "mutant")
        except AssertionError:
            pass
        else:
            raise AssertionError("source scanner accepted unsafe directory fsync")
    wrong_directory_sync_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableFileResult replace_root() override {
        renameat(authority_fd, stage, authority_fd, root);
        sync_directory_retrying_interrupts(unrelated_fd);
        return Succeeded;
    }
};
"""
    try:
        require_each_override_directory_sync_binding(
            wrong_directory_sync_mutant, "replace_root", "mutant"
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted the wrong namespace fd")
    ignored_directory_sync_result_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    DurableFileResult replace_root() override {
        renameat(authority_fd, stage, authority_fd, root);
        sync_directory_retrying_interrupts(authority_fd);
        return Succeeded;
    }
};
"""
    try:
        require_each_override_result_call_checked(
            ignored_directory_sync_result_mutant,
            "DurableFileAdapter",
            "replace_root",
            "sync_directory_retrying_interrupts",
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted ignored namespace sync")
    overlapped_lock_mutant = """
CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_ALWAYS, FILE_FLAG_OVERLAPPED, nullptr);
"""
    try:
        require_absent_tokens(
            overlapped_lock_mutant,
            ("FILE_FLAG_OVERLAPPED",),
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted an overlapped lock handle")
    lying_preflight_mutant = """
class LiveAdapter final : public DurableFileAdapter {
public:
    void preflight_capabilities() override { return_success(); }
    void replace_root() override {
        write_all(channel, bytes);
        flush_retrying_interrupts(channel);
        truncate_retrying_interrupts(channel, 0);
        close_once(channel);
        renameat(directory, stage, directory, root);
        fsync(directory);
    }
};
"""
    try:
        require_each_override_call(
            lying_preflight_mutant,
            "DurableFileAdapter",
            "preflight_capabilities",
            "write_all",
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted a declarative preflight")
    unrelated_channel_mutant = """
class LiveChannel final : public DurableFileChannel {
public:
    void flush() override { fsync(file); }
};
class UnusedChannel final : public DurableFileChannel {
public:
    void flush() override { fcntl(file, F_FULLFSYNC); }
};
"""
    try:
        require_each_override_call_arguments(
            unrelated_channel_mutant,
            "DurableFileChannel",
            "flush",
            "fcntl",
            ("F_FULLFSYNC",),
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner borrowed an unrelated channel policy")
    helper_overload_mutant_header = """
DurableFileResult write_all(DurableFileChannel &channel,
                            std::string_view bytes);
"""
    helper_overload_mutant_source = """
DurableFileResult write_all(DurableFileChannel &channel,
                            std::string_view bytes) { return {}; }
DurableFileResult write_all(DurableFileChannel &channel,
                            const char *bytes) { return {}; }
"""
    try:
        require_unique_helper_definition(
            helper_overload_mutant_header,
            helper_overload_mutant_source,
            "write_all",
            "DurableFileResult write_all(DurableFileChannel &channel, "
            "std::string_view bytes);",
            "mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("source scanner accepted an overloaded shared helper")
    duplicate_close_mutants = (
        (
            """
class LiveAdapter final : public DurableFileAdapter {
public:
    void append_journal() override {
        close_once(channel);
        close_once(channel);
    }
};
""",
            "DurableFileAdapter",
            "append_journal",
            "close_once",
        ),
        (
            """
class LiveChannel final : public DurableFileChannel {
public:
    void close() override {
        close(fd);
        close(fd);
    }
};
""",
            "DurableFileChannel",
            "close",
            "close",
        ),
    )
    for mutant, base, entry, call in duplicate_close_mutants:
        try:
            require_each_override_call_exactly_once(mutant, base, entry, call, "mutant")
        except AssertionError:
            pass
        else:
            raise AssertionError("source scanner accepted a duplicate close")


def strip_cpp_comments_and_literals(source: str) -> str:
    source = normalize_cpp_line_splices(source)
    source = strip_inactive_cpp_blocks(source)
    pattern = re.compile(
        r"//[^\n]*|/\*.*?\*/|R\"(?P<tag>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=tag)\"|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
        re.DOTALL,
    )
    return pattern.sub(lambda match: "\n" * match.group(0).count("\n"), source)


def normalize_cpp_declaration(declaration: str) -> str:
    normalized = re.sub(r"\s+", " ", declaration.strip())
    normalized = re.sub(r"\s*([(),;&=])\s*", r"\1", normalized)
    return normalized


def exactly_macro_guarded(source: str, token: str, macro: str) -> bool:
    pattern = re.compile(
        rf"(?m)^\s*#\s*ifdef\s+{re.escape(macro)}\s*$"
        rf"(?:\s*\n)*\s*{re.escape(token)}\s*$"
        r"(?:\s*\n)*\s*#\s*endif\s*$"
    )
    return pattern.search(source) is not None


def exactly_macro_guarded_region(
    source: str,
    macro: str,
    tokens: tuple[str, ...],
) -> bool:
    pattern = re.compile(
        rf"(?ms)^\s*#\s*ifdef\s+{re.escape(macro)}\s*$"
        r"(?P<body>.*?)"
        r"^\s*#\s*endif\s*$"
    )
    matches = []
    for match in pattern.finditer(source):
        body = match.group("body")
        if re.search(r"(?m)^\s*#\s*(?:if|ifdef|ifndef|else|elif|endif)\b", body):
            continue
        if all(token in body for token in tokens):
            matches.append(match)
    return len(matches) == 1


def require_single_public_section(body: str, label: str) -> None:
    if len(re.findall(r"\bpublic\s*:", body)) != 1:
        raise AssertionError(f"{label} has multiple public surfaces")
    if re.search(r"\bprotected\s*:", body):
        raise AssertionError(f"{label} exposes a protected construction surface")


def private_class_surface(body: str, label: str) -> str:
    if len(re.findall(r"\bprivate\s*:", body)) != 1:
        raise AssertionError(f"{label} private construction surface drifted")
    private = re.search(r"\bprivate\s*:(?P<private>.*)\Z", body, re.DOTALL)
    if private is None:
        raise AssertionError(f"{label} private construction surface is unavailable")
    return private.group("private")


def require_private_construction_path(body: str, name: str) -> tuple[str, str]:
    private = private_class_surface(body, name)
    constructors = tuple(
        re.finditer(
            rf"\bexplicit\s+{re.escape(name)}\s*"
            r"\((?P<parameters>[^;{}]*)\)\s*(?:noexcept\s*)?;",
            private,
        )
    )
    if len(constructors) != 1 or constructors[0].group("parameters").strip() in {
        "",
        "void",
    }:
        raise AssertionError(f"{name} private construction path drifted")
    member = re.search(
        r"(?m)^\s*(?!#|friend\b|class\b|struct\b|enum\b|using\b|explicit\b)"
        r"[^;()]+\s+[A-Za-z_]\w*\s*(?:=[^;]+|\{[^;]*\})?;\s*$",
        private,
    )
    if member is None:
        raise AssertionError(f"{name} has no private construction state")
    return private, constructors[0].group(0)


def require_published_journal_public_shape(source: str) -> None:
    match = re.search(
        r"class\s+PublishedJournal\s*\{(?P<body>.*?)\n\};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError("PublishedJournal public declaration is unavailable")
    body = match.group("body")
    require_single_public_section(body, "PublishedJournal")
    public_match = re.search(
        r"\bpublic\s*:(?P<public>.*?)(?:\bprivate\s*:|\bprotected\s*:)",
        body,
        re.DOTALL,
    )
    if public_match is None:
        raise AssertionError("PublishedJournal public surface is unavailable")
    declarations = tuple(
        normalize_cpp_declaration(declaration + ";")
        for declaration in public_match.group("public").split(";")
        if declaration.strip()
    )
    expected = tuple(
        normalize_cpp_declaration(declaration)
        for declaration in (
            "PublishedJournal() = delete;",
            "PublishedJournal(const PublishedJournal &) = delete;",
            "PublishedJournal &operator=(const PublishedJournal &) = delete;",
            "PublishedJournal(PublishedJournal &&) noexcept;",
            "PublishedJournal &operator=(PublishedJournal &&) noexcept;",
            "~PublishedJournal();",
            "bool available() const noexcept;",
            "std::string_view root_bytes() const noexcept;",
            "std::string_view journal_id() const noexcept;",
            "std::uint64_t tip_sequence() const noexcept;",
        )
    )
    if declarations != expected:
        raise AssertionError("PublishedJournal public construction surface drifted")
    _, private_constructor = require_private_construction_path(body, "PublishedJournal")
    insertion = match.start("body") + public_match.end("public")
    extra_constructor_mutant = (
        source[:insertion] + " PublishedJournal(int); " + source[insertion:]
    )
    mutated = re.search(
        r"class\s+PublishedJournal\s*\{(?P<body>.*?)\n\};",
        extra_constructor_mutant,
        re.DOTALL,
    )
    if mutated is None:
        raise AssertionError("PublishedJournal mutant was unavailable")
    mutated_public = re.search(
        r"\bpublic\s*:(?P<public>.*?)(?:\bprivate\s*:|\bprotected\s*:)",
        mutated.group("body"),
        re.DOTALL,
    )
    if mutated_public is None:
        raise AssertionError("PublishedJournal mutant public surface was unavailable")
    mutated_declarations = tuple(
        normalize_cpp_declaration(declaration + ";")
        for declaration in mutated_public.group("public").split(";")
        if declaration.strip()
    )
    if mutated_declarations == expected:
        raise AssertionError("PublishedJournal audit accepted an extra constructor")
    try:
        require_single_public_section(
            body + "\npublic:\nPublishedJournal(int);\n",
            "PublishedJournal mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("PublishedJournal audit accepted a second public section")

    missing_private_constructor = source.replace(private_constructor, "", 1)
    missing_match = re.search(
        r"class\s+PublishedJournal\s*\{(?P<body>.*?)\n\};",
        missing_private_constructor,
        re.DOTALL,
    )
    try:
        require_private_construction_path(
            missing_match.group("body"), "PublishedJournal"
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("PublishedJournal audit accepted no minting path")


def require_durable_journal_public_shape(source: str) -> None:
    match = re.search(
        r"class\s+DurableJournal\s*\{(?P<body>.*?)\n\};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError("DurableJournal public declaration is unavailable")
    require_single_public_section(match.group("body"), "DurableJournal")
    public_match = re.search(
        r"\bpublic\s*:(?P<public>.*?)(?:\bprivate\s*:|\bprotected\s*:)",
        match.group("body"),
        re.DOTALL,
    )
    if public_match is None:
        raise AssertionError("DurableJournal public surface is unavailable")
    declarations = tuple(
        normalize_cpp_declaration(declaration + ";")
        for declaration in public_match.group("public").split(";")
        if declaration.strip()
    )
    expected = tuple(
        normalize_cpp_declaration(declaration)
        for declaration in (
            "DurableJournal() = delete;",
            "DurableJournal(const DurableJournal &) = delete;",
            "DurableJournal &operator=(const DurableJournal &) = delete;",
            "DurableJournal(DurableJournal &&) noexcept;",
            "DurableJournal &operator=(DurableJournal &&) noexcept;",
            "~DurableJournal();",
            "static DurableJournal native(std::filesystem::path directory, JournalLimits limits);",
            "DurableJournalResult create_new(TrustedReplayFloor floor, const ParsedJournalRecord &genesis, const RecoveryOriginVerifier *verifier = nullptr);",
            "DurableJournalResult recover_existing(TrustedReplayFloor floor, const RecoveryOriginVerifier *verifier = nullptr);",
            "DurableJournalResult append_and_publish(PublishedJournal &&authority, const ParsedJournalRecord &candidate, const RecoveryOriginVerifier *verifier = nullptr);",
            "DurableJournalResult compact_physical(PublishedJournal &&authority);",
            "ExactSchemaExportResult export_exact_schema_candidate(const PublishedJournal &authority, SchemaVersion target, JournalQuiescence quiescence);",
        )
    )
    if declarations != expected:
        raise AssertionError("DurableJournal public authority surface drifted")
    private, private_constructor = require_private_construction_path(
        match.group("body"), "DurableJournal"
    )
    if re.findall(r"\bfriend\b[^;]*;", private) != [
        "friend class detail::DurableJournalTestFactory;"
    ]:
        raise AssertionError("DurableJournal has a forgeable friend graph")
    mint_declarations = tuple(
        re.finditer(
            r"\bDurableJournalResult\s+mint_published\s*"
            r"\((?P<parameters>[^;{}]*)\)\s*;",
            private,
        )
    )
    if len(mint_declarations) != 1:
        raise AssertionError("DurableJournal private mint sink drifted")
    mint_parameters = split_cpp_arguments(mint_declarations[0].group("parameters"))
    if (
        len(mint_parameters) != 3
        or re.search(r"\bJournalHistory\s*&&\s*[A-Za-z_]\w*\b", mint_parameters[0])
        is None
        or re.search(
            r"\bAuthorityRootCandidate\s*(?:&&\s*)?[A-Za-z_]\w*\b",
            mint_parameters[1],
        )
        is None
        or re.search(r"\b(?:persistence|state)[A-Za-z_]*\b", mint_parameters[2]) is None
    ):
        raise AssertionError("DurableJournal private mint sink shape drifted")
    minting_mutant = source.replace(
        "~DurableJournal();",
        "~DurableJournal(); static PublishedJournal from_record(const ParsedJournalRecord &);",
        1,
    )
    if minting_mutant == source:
        raise AssertionError("DurableJournal minting mutant was unavailable")
    mutated_match = re.search(
        r"class\s+DurableJournal\s*\{(?P<body>.*?)\n\};",
        minting_mutant,
        re.DOTALL,
    )
    mutated_public = re.search(
        r"\bpublic\s*:(?P<public>.*?)(?:\bprivate\s*:|\bprotected\s*:)",
        mutated_match.group("body"),
        re.DOTALL,
    )
    mutated_declarations = tuple(
        normalize_cpp_declaration(declaration + ";")
        for declaration in mutated_public.group("public").split(";")
        if declaration.strip()
    )
    if mutated_declarations == expected:
        raise AssertionError("DurableJournal audit accepted a minting factory")
    try:
        require_single_public_section(
            match.group("body")
            + "\npublic:\nDurableJournalResult mint(const ParsedJournalRecord &);\n",
            "DurableJournal mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("DurableJournal audit accepted a second public section")

    missing_private_constructor = source.replace(private_constructor, "", 1)
    missing_match = re.search(
        r"class\s+DurableJournal\s*\{(?P<body>.*?)\n\};",
        missing_private_constructor,
        re.DOTALL,
    )
    try:
        require_private_construction_path(missing_match.group("body"), "DurableJournal")
    except AssertionError:
        pass
    else:
        raise AssertionError("DurableJournal audit accepted no construction path")


def require_trusted_replay_floor_public_shape(source: str) -> None:
    match = re.search(
        r"class\s+TrustedReplayFloor\s*\{(?P<body>.*?)\n\};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError("TrustedReplayFloor public declaration is unavailable")
    require_single_public_section(match.group("body"), "TrustedReplayFloor")
    public_match = re.search(
        r"\bpublic\s*:(?P<public>.*?)(?:\bprivate\s*:|\bprotected\s*:)",
        match.group("body"),
        re.DOTALL,
    )
    if public_match is None:
        raise AssertionError("TrustedReplayFloor public surface is unavailable")
    declarations = tuple(
        normalize_cpp_declaration(declaration + ";")
        for declaration in public_match.group("public").split(";")
        if declaration.strip()
    )
    expected = tuple(
        normalize_cpp_declaration(declaration)
        for declaration in (
            "TrustedReplayFloor() = delete;",
            "static TrustedReplayFloor uninitialized();",
            "static TrustedReplayFloor exact_root(std::string root);",
        )
    )
    if declarations != expected:
        raise AssertionError("TrustedReplayFloor public construction surface drifted")
    private, private_constructor = require_private_construction_path(
        match.group("body"), "TrustedReplayFloor"
    )
    friend_declarations = re.findall(r"\bfriend\b[^;]*;", private)
    if friend_declarations != ["friend class DurableJournal;"]:
        raise AssertionError("TrustedReplayFloor has a forgeable friend graph")
    try:
        require_single_public_section(
            match.group("body") + "\npublic:\nTrustedReplayFloor(int);\n",
            "TrustedReplayFloor mutant",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError(
            "TrustedReplayFloor audit accepted a second public section"
        )

    missing_private_constructor = source.replace(private_constructor, "", 1)
    missing_match = re.search(
        r"class\s+TrustedReplayFloor\s*\{(?P<body>.*?)\n\};",
        missing_private_constructor,
        re.DOTALL,
    )
    try:
        require_private_construction_path(
            missing_match.group("body"), "TrustedReplayFloor"
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("TrustedReplayFloor audit accepted no state constructor")


def require_exact_struct_shape(
    source: str,
    name: str,
    expected: tuple[str, ...],
) -> None:
    match = re.search(
        rf"struct\s+{re.escape(name)}\s*\{{(?P<body>.*?)\}}\s*;",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"{name} public declaration is unavailable")
    declarations = tuple(
        normalize_cpp_declaration(declaration + ";")
        for declaration in match.group("body").split(";")
        if declaration.strip()
    )
    normalized_expected = tuple(
        normalize_cpp_declaration(declaration) for declaration in expected
    )
    if declarations != normalized_expected:
        raise AssertionError(f"{name} public result shape drifted")


def require_exact_enum_members(
    source: str,
    name: str,
    expected: tuple[str, ...],
) -> None:
    match = re.search(
        rf"enum\s+class\s+{re.escape(name)}(?:\s*:\s*[^{{]+)?\s*\{{(?P<body>.*?)\}}\s*;",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"{name} public enum is unavailable")
    members = tuple(
        re.match(r"\s*([A-Za-z_]\w*)", member).group(1)
        for member in match.group("body").split(",")
        if member.strip()
    )
    if members != expected:
        raise AssertionError(f"{name} public outcomes drifted")


def strip_cpp_comments_preserve_literals(source: str) -> str:
    source = normalize_cpp_line_splices(source)
    source = strip_inactive_cpp_blocks(source)
    pattern = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)
    return pattern.sub(lambda match: "\n" * match.group(0).count("\n"), source)


def normalize_cpp_line_splices(source: str) -> str:
    return re.sub(r"\\\r?\n", "", source)


def strip_inactive_cpp_blocks(source: str) -> str:
    pattern = re.compile(r"(?ms)^\s*#\s*if\s+(?:0|false)\b.*?^\s*#\s*endif\b[^\n]*")
    previous = None
    while source != previous:
        previous = source
        source = pattern.sub(lambda match: "\n" * match.group(0).count("\n"), source)
    return source


def require_call_argument(source: str, call: str, argument: str, label: str) -> None:
    pattern = re.compile(
        rf"\b{re.escape(call)}\s*\([^;]*\b{re.escape(argument)}\b[^;]*\)"
    )
    if pattern.search(source) is None:
        raise AssertionError(f"{label} omitted {call}(...{argument}...)")


def require_call_arguments(
    source: str,
    call: str,
    arguments: tuple[str, ...],
    label: str,
) -> None:
    for match in re.finditer(rf"\b{re.escape(call)}\s*\((?P<args>[^;]*)\)", source):
        call_arguments = match.group("args")
        search_from = 0
        for argument in arguments:
            found_at = call_arguments.find(argument, search_from)
            if found_at < 0:
                break
            search_from = found_at + len(argument)
        else:
            return
    raise AssertionError(f"{label} omitted ordered arguments {arguments}")


def require_call_arguments_unordered(
    source: str,
    call: str,
    arguments: tuple[str, ...],
    label: str,
) -> None:
    for match in re.finditer(rf"\b{re.escape(call)}\s*\((?P<args>[^;]*)\)", source):
        call_arguments = match.group("args")
        if all(argument in call_arguments for argument in arguments):
            return
    raise AssertionError(f"{label} omitted call arguments {arguments}")


def require_call_expression(source: str, call: str, label: str) -> None:
    pattern = re.compile(rf"\b{re.escape(call)}\s*\([^;{{}}]*\)\s*;")
    if pattern.search(source) is None:
        raise AssertionError(f"{label} does not call the shared {call} seam")


def function_bodies(source: str, function: str) -> list[str]:
    declaration = re.compile(
        rf"\b{re.escape(function)}\s*\([^;{{}}]*\)\s*"
        r"(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?\{"
    )
    bodies: list[str] = []
    for match in declaration.finditer(source):
        opening = match.end() - 1
        depth = 1
        index = opening + 1
        while index < len(source) and depth:
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
            index += 1
        if depth == 0:
            bodies.append(source[opening + 1 : index - 1])
    return bodies


def class_blocks(source: str, base: str) -> list[tuple[str, str]]:
    declaration = re.compile(
        rf"\bclass\s+(?P<name>[A-Za-z_]\w*)\s*(?:final\s*)?:\s*"
        rf"public\s+(?:detail\s*::\s*)?{re.escape(base)}\s*\{{"
    )
    blocks: list[tuple[str, str]] = []
    for match in declaration.finditer(source):
        opening = match.end() - 1
        depth = 1
        index = opening + 1
        while index < len(source) and depth:
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
            index += 1
        if depth == 0:
            blocks.append((match.group("name"), source[opening + 1 : index - 1]))
    return blocks


def qualified_function_bodies(
    source: str,
    class_name: str,
    function: str,
) -> list[str]:
    declaration = re.compile(
        rf"\b{re.escape(class_name)}\s*::\s*{re.escape(function)}\s*"
        r"\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{"
    )
    bodies: list[str] = []
    for match in declaration.finditer(source):
        opening = match.end() - 1
        depth = 1
        index = opening + 1
        while index < len(source) and depth:
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
            index += 1
        if depth == 0:
            bodies.append(source[opening + 1 : index - 1])
    return bodies


def overriding_method_bodies(source: str, base: str, function: str) -> list[str]:
    bodies: list[str] = []
    for class_name, class_body in class_blocks(source, base):
        declaration = re.compile(
            rf"\b{re.escape(function)}\s*\([^;{{}}]*\)\s*"
            r"[^;{}]*\boverride\b[^;{}]*(?P<end>[;{])"
        )
        for match in declaration.finditer(class_body):
            if match.group("end") == "{":
                opening = match.end() - 1
                depth = 1
                index = opening + 1
                while index < len(class_body) and depth:
                    if class_body[index] == "{":
                        depth += 1
                    elif class_body[index] == "}":
                        depth -= 1
                    index += 1
                if depth == 0:
                    bodies.append(class_body[opening + 1 : index - 1])
            else:
                bodies.extend(qualified_function_bodies(source, class_name, function))
    return bodies


def require_unique_helper_definition(
    header: str,
    source: str,
    function: str,
    declaration: str,
    label: str,
    return_type: str = "DurableFileResult",
) -> None:
    header_matches = re.findall(
        rf"[^;{{}}]*\b{re.escape(function)}\s*\([^;{{}}]*\)\s*;",
        header,
    )
    expected = normalize_cpp_declaration(declaration)
    if (
        len(header_matches) != 1
        or normalize_cpp_declaration(header_matches[0]) != expected
    ):
        raise AssertionError(f"{label} helper declaration drifted")
    bodies = function_bodies(source, function)
    if len(bodies) != 1:
        raise AssertionError(f"{label} helper is overloaded or multiply defined")
    definition = re.compile(
        rf"\b{re.escape(return_type)}\s+{re.escape(function)}\s*"
        r"\([^;{}]*\)\s*(?:noexcept\s*)?\{"
    )
    if len(definition.findall(source)) != 1:
        raise AssertionError(f"{label} helper definition signature drifted")


def require_transaction_helper_contract(source: str) -> None:
    for helper, operation in (
        ("write_flush_close", "write_all"),
        ("truncate_flush_close", "truncate_retrying_interrupts"),
    ):
        bodies = function_bodies(source, helper)
        if len(bodies) != 1:
            raise AssertionError(f"durable adapter {helper} definition drifted")
        body = bodies[0]
        ordered_calls = (operation, "flush_retrying_interrupts", "close_once")
        positions: list[int] = []
        results: list[str] = []
        for call in ordered_calls:
            matches = list(
                re.finditer(
                    rf"\b(?:const\s+)?(?:auto|DurableFileResult)\s+"
                    rf"(?P<result>[A-Za-z_]\w*)\s*=\s*"
                    rf"{re.escape(call)}\s*\(",
                    body,
                )
            )
            if len(matches) != 1:
                raise AssertionError(
                    f"durable adapter {helper} does not capture {call} once"
                )
            positions.append(matches[0].start())
            results.append(matches[0].group("result"))
        if positions != sorted(positions):
            raise AssertionError(f"durable adapter {helper} operation order drifted")
        if re.search(r"\breturn\b", body[: positions[-1]]) is not None:
            raise AssertionError(
                f"durable adapter {helper} can bypass its checked close"
            )
        returned = re.findall(r"\breturn\b(?P<value>[^;]*);", body[positions[-1] :])
        if not any(all(result in value for result in results) for value in returned):
            raise AssertionError(
                f"durable adapter {helper} does not combine every result"
            )
        if not any(
            token in body
            for token in ("effect_may_have_occurred", "EffectMayHaveOccurred")
        ):
            raise AssertionError(
                f"durable adapter {helper} does not preserve indeterminate effects"
            )


def require_bounded_read_helper_contract(source: str) -> None:
    if (
        re.search(
            r"\binline\s+constexpr\s+std::size_t\s+"
            r"durable_read_chunk_bytes\s*=\s*64\s*\*\s*1024\s*;",
            source,
        )
        is None
    ):
        raise AssertionError("durable bounded-read chunk cap drifted")
    bodies = function_bodies(source, "read_bounded_close")
    if len(bodies) != 1:
        raise AssertionError("durable bounded-read helper definition drifted")
    body = bodies[0]
    read = re.search(
        r"\b(?:const\s+)?(?:auto|DurableReadChunkResult)\s+"
        r"(?P<result>[A-Za-z_]\w*)\s*=\s*"
        r"(?:[A-Za-z_]\w*\s*\.\s*)?read_some\s*\(",
        body,
    )
    close = re.search(
        r"\b(?:const\s+)?(?:auto|DurableFileResult)\s+"
        r"(?P<result>[A-Za-z_]\w*)\s*=\s*close_read_once\s*\(",
        body,
    )
    if read is None or close is None or read.start() >= close.start():
        raise AssertionError("durable bounded-read helper omits read then close")
    if (
        len(re.findall(r"\bread_some\s*\(", body)) != 1
        or len(re.findall(r"\bclose_read_once\s*\(", body)) != 1
    ):
        raise AssertionError("durable bounded-read helper call count drifted")
    read_arguments = call_argument_lists(body, "read_some")
    if len(read_arguments) != 1 or len(read_arguments[0]) != 1:
        raise AssertionError("durable bounded-read request shape drifted")
    requested = read_arguments[0][0].strip()
    if (
        "max_bytes" not in requested
        and re.search(
            rf"\b(?:const\s+)?(?:auto|std::size_t)\s+{re.escape(requested)}\s*="
            r"[^;]*\bmax_bytes\b",
            body[: read.start()],
        )
        is None
    ):
        raise AssertionError("durable bounded read is not capped by max_bytes")
    requested_assignment = re.search(
        rf"\b(?:const\s+)?(?:auto|std::size_t)\s+{re.escape(requested)}\s*="
        r"\s*(?P<value>[^;]+);",
        body[: read.start()],
        re.DOTALL,
    )
    if requested_assignment is None:
        raise AssertionError("durable bounded-read request is not assigned")
    minimums = call_argument_lists(requested_assignment.group("value"), "min")
    if (
        len(minimums) != 1
        or len(minimums[0]) != 2
        or normalized_cpp_expression(minimums[0][1]) != "durable_read_chunk_bytes"
        or "max_bytes" not in minimums[0][0]
        or "retained.size()" not in normalized_cpp_expression(minimums[0][0])
    ):
        raise AssertionError("durable bounded read omits its fixed chunk cap")
    if re.search(r"\breturn\b", body[: close.start()]) is not None:
        raise AssertionError("durable bounded-read helper can bypass close")
    if not all(
        token in body
        for token in ("max_bytes", "end_of_file", "Interrupted", "truncated")
    ):
        raise AssertionError("durable bounded-read helper omits bounded EOF flow")
    close_result = close.group("result")
    if (
        not any(
            close_result in condition
            and re.search(rf"\breturn\b[^;]*\b{re.escape(close_result)}\b", statement)
            for condition, statement in fail_closed_if_branches(body[close.start() :])
        )
        and re.search(
            rf"\breturn\b[^;]*\b{re.escape(close_result)}\b[^;]*;",
            body[close.start() :],
        )
        is None
    ):
        raise AssertionError("durable bounded-read helper ignores close result")


def require_close_read_once_contract(source: str) -> None:
    bodies = function_bodies(source, "close_read_once")
    if len(bodies) != 1:
        raise AssertionError("durable read-close helper definition drifted")
    body = bodies[0]
    if (
        len(re.findall(r"\bclose\s*\(", body)) != 1
        or re.fullmatch(r"\s*return\s+\w+\s*\.\s*close\s*\(\s*\)\s*;\s*", body) is None
        or "retry_interrupted" in body
    ):
        raise AssertionError("durable read-close helper does not close once")


def require_unique_function_call_arguments(
    source: str,
    function: str,
    call: str,
    arguments: tuple[str, ...],
    label: str,
) -> None:
    bodies = function_bodies(source, function)
    if len(bodies) != 1:
        raise AssertionError(f"{label} {function} is overloaded or multiply defined")
    matches = re.finditer(rf"\b{re.escape(call)}\s*\((?P<args>[^;]*)\)", bodies[0])
    if not any(
        all(argument in match.group("args") for argument in arguments)
        for match in matches
    ):
        raise AssertionError(f"{label} {function} omits {call}{arguments}")


def require_each_override_call(
    source: str,
    base: str,
    entry: str,
    required_call: str,
    label: str,
) -> None:
    bodies = overriding_method_bodies(source, base, entry)
    if not bodies or any(
        re.search(rf"\b{re.escape(required_call)}\s*\(", body) is None
        for body in bodies
    ):
        raise AssertionError(
            f"{label} {base}::{entry} does not directly call {required_call}"
        )


def require_each_override_call_exactly_once(
    source: str,
    base: str,
    entry: str,
    required_call: str,
    label: str,
) -> None:
    bodies = overriding_method_bodies(source, base, entry)
    if not bodies:
        raise AssertionError(f"{label} has no concrete {base}::{entry} override")
    for body in bodies:
        count = len(re.findall(rf"\b{re.escape(required_call)}\s*\(", body))
        if count != 1:
            raise AssertionError(
                f"{label} {base}::{entry} calls {required_call} {count} times"
            )


def require_each_override_call_result_propagated(
    source: str,
    base: str,
    entry: str,
    call: str,
    label: str,
    minimum_calls: int = 1,
) -> None:
    bodies = overriding_method_bodies(source, base, entry)
    if not bodies:
        raise AssertionError(f"{label} has no concrete {base}::{entry} override")
    for body in bodies:
        calls = list(re.finditer(rf"\b{re.escape(call)}\s*\(", body))
        if len(calls) < minimum_calls:
            raise AssertionError(f"{label} {base}::{entry} omits {call}")
        direct_returns = {
            match.start("call")
            for match in re.finditer(
                rf"\breturn\s+(?P<call>{re.escape(call)})\s*\(", body
            )
        }
        assignments = list(
            re.finditer(
                rf"\b(?:const\s+)?(?:auto|DurableFileResult)\s+"
                rf"(?P<result>[A-Za-z_]\w*)\s*=\s*"
                rf"(?P<call>{re.escape(call)})\s*\(",
                body,
            )
        )
        assigned_calls = {match.start("call") for match in assignments}
        if any(
            call_match.start() not in direct_returns | assigned_calls
            for call_match in calls
        ):
            raise AssertionError(f"{label} {base}::{entry} discards {call}'s result")
        for assignment in assignments:
            result = assignment.group("result")
            suffix = body[assignment.end() :]
            if re.search(rf"\breturn\s+{re.escape(result)}\s*;", suffix):
                continue
            if not any(
                result in condition
                and re.search(rf"\breturn\s+{re.escape(result)}\s*;", statement)
                for condition, statement in fail_closed_if_branches(suffix)
            ):
                raise AssertionError(
                    f"{label} {base}::{entry} does not propagate {call}'s result"
                )


def require_each_override_omits_calls(
    source: str,
    base: str,
    entry: str,
    forbidden_calls: tuple[str, ...],
    label: str,
) -> None:
    bodies = overriding_method_bodies(source, base, entry)
    if not bodies:
        raise AssertionError(f"{label} has no concrete {base}::{entry} override")
    for body in bodies:
        present = [
            call
            for call in forbidden_calls
            if re.search(rf"\b{re.escape(call)}\s*\(", body)
        ]
        if present:
            raise AssertionError(f"{label} {base}::{entry} uses {present}")


def require_each_override_ordered_calls(
    source: str,
    base: str,
    entry: str,
    calls: tuple[str, ...],
    label: str,
) -> None:
    bodies = overriding_method_bodies(source, base, entry)
    if not bodies:
        raise AssertionError(f"{label} has no concrete {base}::{entry} override")
    for body in bodies:
        search_from = 0
        for call in calls:
            match = re.search(rf"\b{re.escape(call)}\s*\(", body[search_from:])
            if match is None:
                raise AssertionError(f"{label} {base}::{entry} omits ordered {calls}")
            search_from += match.end()


def require_each_override_call_arguments(
    source: str,
    base: str,
    entry: str,
    call: str,
    arguments: tuple[str, ...],
    label: str,
) -> None:
    bodies = overriding_method_bodies(source, base, entry)
    if not bodies:
        raise AssertionError(f"{label} has no concrete {base}::{entry} override")
    for body in bodies:
        matches = re.finditer(rf"\b{re.escape(call)}\s*\((?P<args>[^;]*)\)", body)
        if not any(
            all(argument in match.group("args") for argument in arguments)
            for match in matches
        ):
            raise AssertionError(f"{label} {base}::{entry} omits {call}{arguments}")


def require_each_override_pattern(
    source: str,
    base: str,
    entry: str,
    pattern: str,
    label: str,
) -> None:
    bodies = overriding_method_bodies(source, base, entry)
    if not bodies or any(re.search(pattern, body) is None for body in bodies):
        raise AssertionError(f"{label} {base}::{entry} omits required shape")


def matching_delimiter(
    source: str,
    opening: int,
    open_character: str,
    close_character: str,
) -> int:
    depth = 1
    index = opening + 1
    while index < len(source) and depth:
        if source[index] == open_character:
            depth += 1
        elif source[index] == close_character:
            depth -= 1
        index += 1
    if depth:
        raise AssertionError("C++ source has an unbalanced delimiter")
    return index - 1


def fail_closed_if_branches(body: str) -> tuple[tuple[str, str], ...]:
    branches: list[tuple[str, str]] = []
    for match in re.finditer(r"\bif\s*\(", body):
        opening = body.find("(", match.start())
        closing = matching_delimiter(body, opening, "(", ")")
        statement_start = closing + 1
        while statement_start < len(body) and body[statement_start].isspace():
            statement_start += 1
        if statement_start == len(body):
            continue
        if body[statement_start] == "{":
            statement_end = matching_delimiter(body, statement_start, "{", "}")
            statement = body[statement_start + 1 : statement_end]
        else:
            statement_end = body.find(";", statement_start)
            if statement_end < 0:
                continue
            statement = body[statement_start : statement_end + 1]
        if re.search(r"\breturn\b", statement):
            branches.append((body[opening + 1 : closing], statement))
    return tuple(branches)


def branch_returns_non_success(statement: str) -> bool:
    returns = re.findall(r"\breturn\b(?P<value>[^;]*);", statement)
    return bool(returns) and all(
        "succeeded" not in returned.lower() and "success" not in returned.lower()
        for returned in returns
    )


def condition_rejects_predicate(condition: str, predicate: str) -> bool:
    compact = re.sub(r"\s+", "", condition)
    if predicate not in condition:
        return False
    if predicate in (
        "fstat",
        "fstatat",
        "fsync",
        "fcntl",
        "ftruncate",
        "close",
        "renameat",
        "flock",
    ):
        if re.search(rf"{predicate}\([^;]*\)==(?:0|true)", compact):
            return False
        return bool(
            re.search(
                rf"(?:!(?:::)?{predicate}\(|{predicate}\([^;]*\)(?:!=0|<0|==-1))",
                compact,
            )
            or re.fullmatch(rf"(?:::)?{predicate}\([^;]*\)", compact)
        )
    if predicate == "S_ISREG":
        return bool(
            re.search(r"!S_ISREG\(", compact)
            or re.search(r"S_ISREG\([^;]*\)==(?:0|false)", compact)
        )
    if predicate in (
        "GetFileInformationByHandleEx",
        "WriteFile",
        "ReadFile",
        "LockFileEx",
        "UnlockFileEx",
        "FlushFileBuffers",
        "SetFilePointerEx",
        "SetEndOfFile",
        "CloseHandle",
        "MoveFileExW",
    ):
        return bool(
            re.search(rf"!(?:::)?{predicate}\(", compact)
            or re.search(rf"{predicate}\([^;]*\)==(?:0|FALSE|false)", compact)
        )
    if predicate in ("FILE_ATTRIBUTE_REPARSE_POINT", "FILE_ATTRIBUTE_DIRECTORY"):
        return not bool(
            re.search(rf"{predicate}[^;]*(?:==0|==false|==FALSE)", compact)
            or re.search(rf"!\([^;]*{predicate}", compact)
        )
    if predicate == "st_nlink":
        return bool(re.search(r"st_nlink(?:==0|!=1|<1)", compact))
    if predicate in ("st_dev", "st_ino"):
        return "!=" in compact
    if predicate in ("VolumeSerialNumber", "FileId"):
        return "!=" in compact or "memcmp(" in compact
    if predicate == "NumberOfLinks":
        return bool(re.search(r"NumberOfLinks(?:==0|<1)", compact))
    return False


def require_each_override_fail_closed_checks(
    source: str,
    base: str,
    entry: str,
    predicates: tuple[str, ...],
    label: str,
) -> None:
    bodies = overriding_method_bodies(source, base, entry)
    if not bodies:
        raise AssertionError(f"{label} has no concrete {base}::{entry} override")
    for body in bodies:
        branches = fail_closed_if_branches(body)
        missing = [
            predicate
            for predicate in predicates
            if not any(
                condition_rejects_predicate(condition, predicate)
                and branch_returns_non_success(statement)
                for condition, statement in branches
            )
        ]
        if missing:
            raise AssertionError(
                f"{label} {base}::{entry} does not fail closed on {missing}"
            )


def require_retrying_directory_sync(source: str, label: str) -> None:
    helper = "sync_directory_retrying_interrupts"
    signature = re.compile(
        rf"\bDurableFileResult\s+{helper}\s*\(\s*int\s+directory_fd\s*\)\s*\{{"
    )
    helper_bodies = function_bodies(source, helper)
    if len(signature.findall(source)) != 1 or len(helper_bodies) != 1:
        raise AssertionError(f"{label} directory-sync helper shape drifted")
    helper_body = helper_bodies[0]
    if len(re.findall(r"\bretry_interrupted\s*\(", helper_body)) != 1:
        raise AssertionError(f"{label} directory sync does not retry exactly once")

    bindings = 0
    for class_name, class_body in class_blocks(source, "DurableInterruptibleCall"):
        attempts = function_bodies(class_body, "attempt")
        if len(attempts) != 1:
            continue
        attempt = attempts[0]
        if (
            len(re.findall(r"\bfsync\s*\(\s*directory_fd_\s*\)", attempt)) != 1
            or not any(
                condition_rejects_predicate(condition, "fsync")
                and branch_returns_non_success(statement)
                for condition, statement in fail_closed_if_branches(attempt)
            )
            or not any(
                "errno" in condition
                and "EINTR" in condition
                and "Interrupted" in statement
                for condition, statement in fail_closed_if_branches(attempt)
            )
            or "Succeeded" not in attempt
            or not any(
                status in attempt
                for status in (
                    "FailedBeforeEffect",
                    "EffectMayHaveOccurred",
                    "Unsupported",
                )
            )
        ):
            continue
        constructor = re.compile(
            rf"\bexplicit\s+{re.escape(class_name)}\s*"
            r"\(\s*int\s+directory_fd\s*\)\s*:\s*"
            r"directory_fd_\s*\(\s*directory_fd\s*\)"
        )
        if constructor.search(class_body) is None:
            continue
        for instance in re.finditer(
            rf"\b{re.escape(class_name)}\s+(?P<name>[A-Za-z_]\w*)\s*"
            r"[({]\s*directory_fd\s*[)}]",
            helper_body,
        ):
            variable = instance.group("name")
            if re.search(
                rf"\bretry_interrupted\s*\(\s*{re.escape(variable)}\s*\)",
                helper_body,
            ):
                bindings += 1
    if bindings != 1:
        raise AssertionError(f"{label} directory fsync is not bound to its retry")


def require_retrying_posix_flock(
    source: str,
    helper: str,
    flag: str,
    label: str,
) -> None:
    signature = re.compile(
        rf"\bDurableFileResult\s+{re.escape(helper)}\s*"
        r"\(\s*int\s+(?P<parameter>[A-Za-z_]\w*)\s*\)\s*\{"
    )
    signatures = tuple(signature.finditer(source))
    helper_bodies = function_bodies(source, helper)
    if len(signatures) != 1 or len(helper_bodies) != 1:
        raise AssertionError(f"{label} flock helper shape drifted")
    parameter = signatures[0].group("parameter")
    member = f"{parameter}_"
    helper_body = helper_bodies[0]
    if len(re.findall(r"\bretry_interrupted\s*\(", helper_body)) != 1:
        raise AssertionError(f"{label} flock does not retry exactly once")
    bindings = 0
    for class_name, class_body in class_blocks(source, "DurableInterruptibleCall"):
        attempts = function_bodies(class_body, "attempt")
        if len(attempts) != 1:
            continue
        attempt = attempts[0]
        flock_pattern = (
            rf"\bflock\s*\(\s*{re.escape(member)}\s*,\s*" rf"{re.escape(flag)}\s*\)"
        )
        attempt_branches = fail_closed_if_branches(attempt)
        expected_failures = {
            f"flock({member},{flag})!=0",
            f"::flock({member},{flag})!=0",
        }
        failure_branches = tuple(
            statement
            for condition, statement in attempt_branches
            if compact_cpp(condition) in expected_failures
        )
        returns = re.findall(r"\breturn\b(?P<value>[^;]*);", attempt)
        final_return = normalized_cpp_expression(returns[-1]) if returns else ""
        if (
            len(re.findall(flock_pattern, attempt)) != 1
            or len(failure_branches) != 1
            or not branch_returns_non_success(failure_branches[0])
            or not any(
                compact_cpp(condition) == "errno==EINTR" and "Interrupted" in statement
                for condition, statement in attempt_branches
            )
            or len(returns) < 3
            or final_return
            not in {"Succeeded", "make_result(DurableFileStatus::Succeeded)"}
        ):
            continue
        constructor = re.compile(
            rf"\bexplicit\s+{re.escape(class_name)}\s*"
            rf"\(\s*int\s+{re.escape(parameter)}\s*\)\s*:\s*"
            rf"{re.escape(member)}\s*\(\s*{re.escape(parameter)}\s*\)"
        )
        if constructor.search(class_body) is None:
            continue
        for instance in re.finditer(
            rf"\b{re.escape(class_name)}\s+(?P<name>[A-Za-z_]\w*)\s*"
            rf"[({{]\s*{re.escape(parameter)}\s*[)}})]",
            helper_body,
        ):
            variable = instance.group("name")
            if re.search(
                rf"\bretry_interrupted\s*\(\s*{re.escape(variable)}\s*\)",
                helper_body,
            ):
                bindings += 1
    if bindings != 1:
        raise AssertionError(f"{label} flock is not bound to its retry")


def require_retrying_posix_flock_mutant(
    source: str,
    helper: str,
    flag: str,
    label: str,
) -> None:
    def disable_flock(body: str) -> str:
        pattern = re.compile(
            rf"if\s*\(\s*(?P<failure>::flock\s*\([^;]*?{re.escape(flag)}\s*\)"
            r"\s*!=\s*0)\s*\)"
        )
        mutated, replacements = pattern.subn(
            r"if (false && \g<failure>)", body, count=1
        )
        if replacements != 1:
            raise AssertionError(f"{label} dead-flock mutant drifted")
        return mutated

    mutant = replace_unique_function_body(source, helper, disable_flock)
    try:
        require_retrying_posix_flock(mutant, helper, flag, label)
    except AssertionError:
        pass
    else:
        raise AssertionError(f"{label} accepted a dead flock predicate")


def require_posix_live_lock_rebinding(source: str, label: str) -> None:
    bodies = overriding_method_bodies(
        source, "DurableFileAdapter", "authority_identity"
    )
    if len(bodies) != 1:
        raise AssertionError(f"{label} authority identity override drifted")
    body = bodies[0]
    if (
        len(call_argument_lists(body, "fstat")) != 2
        or len(call_argument_lists(body, "fstatat")) != 1
    ):
        raise AssertionError(f"{label} does not re-read the live lock name")
    fstatat_arguments = call_argument_lists(body, "fstatat")[0]
    if (
        len(fstatat_arguments) != 4
        or "directory_fd_" not in fstatat_arguments[0]
        or "lock_name" not in fstatat_arguments[1]
        or "current_lock_information" not in fstatat_arguments[2]
        or "AT_SYMLINK_NOFOLLOW" not in fstatat_arguments[3]
    ):
        raise AssertionError(f"{label} live lock lookup can follow another path")
    required_predicates = (
        r"!\s*S_ISREG\s*\(\s*lock_information\.st_mode\s*\)",
        r"!\s*S_ISREG\s*\(\s*current_lock_information\.st_mode\s*\)",
        r"lock_information\.st_nlink\s*==\s*0",
        r"current_lock_information\.st_nlink\s*==\s*0",
        r"lock_information\.st_dev\s*!=\s*current_lock_information\.st_dev",
        r"lock_information\.st_ino\s*!=\s*current_lock_information\.st_ino",
    )
    branches = fail_closed_if_branches(body)
    if any(
        not any(
            re.search(predicate, condition) and branch_returns_non_success(statement)
            for condition, statement in branches
        )
        for predicate in required_predicates
    ):
        raise AssertionError(f"{label} does not reject a rebound live lock path")


def require_posix_lock_path_binding_mutants(source: str, label: str) -> None:
    def rejected(mutant: str, name: str) -> None:
        try:
            require_each_override_call_exactly_once(
                mutant,
                "DurableFileAdapter",
                "lock_authority",
                "lock_directory_retrying_interrupts",
                label,
            )
            require_posix_live_lock_rebinding(mutant, label)
        except AssertionError:
            return
        raise AssertionError(f"{label} accepted {name} lock-path mutant")

    def lock_file_only(body: str) -> str:
        return body.replace(
            "lock_directory_retrying_interrupts(directory_fd_)",
            "make_result(DurableFileStatus::Succeeded)",
            1,
        )

    rejected(
        replace_unique_override_body(source, "lock_authority", lock_file_only),
        "lock-file-only",
    )

    def held_descriptor_only(body: str) -> str:
        live_lookup = """struct stat current_lock_information {};
        if (::fstatat(directory_fd_, lock_name.data(),
                      &current_lock_information, AT_SYMLINK_NOFOLLOW) != 0) {
            return {make_errno_result(DurableFileStatus::Unsupported, errno),
                    {}};
        }"""
        mutated = body.replace(
            live_lookup,
            "const auto current_lock_information = lock_information;",
            1,
        )
        if mutated == body or "::fstatat(" in mutated:
            raise AssertionError(f"{label} held-descriptor mutant drifted")
        return mutated

    rejected(
        replace_unique_override_body(
            source, "authority_identity", held_descriptor_only
        ),
        "held-descriptor-only",
    )

    def inode_only(body: str) -> str:
        return body.replace(
            "lock_information.st_dev != current_lock_information.st_dev",
            "false",
            1,
        )

    rejected(
        replace_unique_override_body(source, "authority_identity", inode_only),
        "partial-identity",
    )


def require_windows_lock_overlapped_binding(source: str) -> None:
    bodies = overriding_method_bodies(source, "DurableFileAdapter", "lock_authority")
    if not bodies:
        raise AssertionError("Windows lock override is unavailable")
    for body in bodies:
        calls = list(re.finditer(r"\bLockFileEx\s*\(", body))
        if len(calls) != 1:
            raise AssertionError("Windows lock does not call LockFileEx exactly once")
        opening = body.find("(", calls[0].start())
        closing = matching_delimiter(body, opening, "(", ")")
        arguments = body[opening + 1 : closing]
        variable_match = re.search(r"&\s*(?P<name>[A-Za-z_]\w*)\s*$", arguments)
        if variable_match is None:
            raise AssertionError("Windows lock omits its OVERLAPPED argument")
        variable = variable_match.group("name")
        if (
            re.search(
                rf"\bOVERLAPPED\s+{re.escape(variable)}\s*" r"(?:\{\s*\}|=\s*\{\s*\})",
                body[: calls[0].start()],
            )
            is None
        ):
            raise AssertionError("Windows LockFileEx OVERLAPPED is not zeroed")


def require_each_override_result_call_checked(
    source: str,
    base: str,
    entry: str,
    call: str,
    label: str,
) -> None:
    bodies = overriding_method_bodies(source, base, entry)
    if not bodies:
        raise AssertionError(f"{label} has no concrete {base}::{entry} override")
    for body in bodies:
        assignment = re.search(
            rf"\b(?:const\s+)?(?:auto|DurableFileResult)\s+"
            rf"(?P<result>[A-Za-z_]\w*)\s*=\s*{re.escape(call)}\s*\(",
            body,
        )
        if assignment is None:
            raise AssertionError(f"{label} does not capture {call}'s result")
        result = assignment.group("result")
        if not any(
            result in condition
            and (
                re.search(
                    rf"!\s*{re.escape(result)}\s*\.\s*succeeded\s*\(",
                    condition,
                )
                or re.search(
                    rf"{re.escape(result)}[^=]*(?:!=\s*DurableFileStatus::Succeeded|\.status\s*!=)",
                    condition,
                )
            )
            and branch_returns_non_success(statement)
            for condition, statement in fail_closed_if_branches(
                body[assignment.start() :]
            )
        ):
            raise AssertionError(f"{label} ignores {call}'s result")


def require_posix_unlock_result_mapping(source: str, label: str) -> None:
    bodies = overriding_method_bodies(source, "DurableFileAdapter", "unlock_authority")
    if not bodies:
        raise AssertionError(f"{label} unlock override is unavailable")
    for body in bodies:
        unlock = re.search(
            r"\b(?:const\s+)?auto\s+(?P<result>[A-Za-z_]\w*)\s*=\s*"
            r"unlock_file_retrying_interrupts\s*\(",
            body,
        )
        close = re.search(
            r"\b(?:const\s+)?(?:auto|int)\s+(?P<result>[A-Za-z_]\w*)\s*=\s*"
            r"(?:::)?close\s*\(",
            body,
        )
        if unlock is None or close is None or not unlock.start() < close.start():
            raise AssertionError(f"{label} does not capture unlock then close")
        unlock_result = unlock.group("result")
        close_result = close.group("result")
        if not any(
            unlock_result in condition
            and close_result in condition
            and (
                f"!{unlock_result}.succeeded(" in re.sub(r"\s+", "", condition)
                or f"{unlock_result}.status!=" in re.sub(r"\s+", "", condition)
            )
            and bool(
                re.search(
                    rf"\b{re.escape(close_result)}\s*(?:!=\s*0|<\s*0)",
                    condition,
                )
            )
            and branch_returns_non_success(statement)
            for condition, statement in fail_closed_if_branches(body[unlock.start() :])
        ):
            raise AssertionError(f"{label} does not propagate unlock/close failure")


def require_windows_unlock_result_mapping(source: str) -> None:
    bodies = overriding_method_bodies(source, "DurableFileAdapter", "unlock_authority")
    if not bodies:
        raise AssertionError("Windows unlock override is unavailable")
    for body in bodies:
        unlock = re.search(
            r"\b(?:const\s+)?(?:auto|BOOL|bool)\s+"
            r"(?P<result>[A-Za-z_]\w*)\s*=\s*UnlockFileEx\s*\(",
            body,
        )
        close = re.search(
            r"\b(?:const\s+)?(?:auto|BOOL|bool)\s+"
            r"(?P<result>[A-Za-z_]\w*)\s*=\s*CloseHandle\s*\(",
            body,
        )
        if unlock is None or close is None or not unlock.start() < close.start():
            raise AssertionError("Windows unlock does not capture unlock then close")
        unlock_result = unlock.group("result")
        close_result = close.group("result")
        if not any(
            bool(re.search(rf"!\s*{re.escape(unlock_result)}\b", condition))
            and bool(re.search(rf"!\s*{re.escape(close_result)}\b", condition))
            and branch_returns_non_success(statement)
            for condition, statement in fail_closed_if_branches(body[unlock.start() :])
        ):
            raise AssertionError("Windows unlock/close failure is ignored")


def split_cpp_arguments(arguments: str) -> tuple[str, ...]:
    parts: list[str] = []
    start = 0
    depths = {"(": 0, "[": 0, "{": 0}
    pairs = {")": "(", "]": "[", "}": "{"}
    for index, character in enumerate(arguments):
        if character in depths:
            depths[character] += 1
        elif character in pairs:
            depths[pairs[character]] -= 1
        elif character == "," and not any(depths.values()):
            parts.append(arguments[start:index].strip())
            start = index + 1
    parts.append(arguments[start:].strip())
    return tuple(parts)


def call_argument_lists(body: str, call: str) -> tuple[tuple[str, ...], ...]:
    arguments: list[tuple[str, ...]] = []
    for match in re.finditer(rf"\b{re.escape(call)}\s*\(", body):
        opening = body.find("(", match.start())
        closing = matching_delimiter(body, opening, "(", ")")
        arguments.append(split_cpp_arguments(body[opening + 1 : closing]))
    return tuple(arguments)


def replace_unique_function_body(
    source: str,
    function: str,
    transform,
) -> str:
    declaration = re.compile(
        rf"\b{re.escape(function)}\s*\([^;{{}}]*\)\s*"
        r"(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?\{"
    )
    matches = tuple(declaration.finditer(source))
    if len(matches) != 1:
        raise AssertionError(f"{function} mutation target is not unique")
    opening = matches[0].end() - 1
    closing = matching_delimiter(source, opening, "{", "}")
    body = source[opening + 1 : closing]
    transformed = transform(body)
    if transformed == body:
        raise AssertionError(f"{function} mutation did not change its body")
    return source[: opening + 1] + transformed + source[closing:]


def replace_unique_bool_function_body(
    source: str,
    function: str,
    transform,
) -> str:
    declaration = re.compile(
        rf"\bbool\s+{re.escape(function)}\s*\([^;{{}}]*\)\s*"
        r"(?:const\s*)?(?:noexcept\s*)?\{"
    )
    matches = tuple(declaration.finditer(source))
    if len(matches) != 1:
        raise AssertionError(f"{function} bool mutation target is not unique")
    opening = matches[0].end() - 1
    closing = matching_delimiter(source, opening, "{", "}")
    body = source[opening + 1 : closing]
    transformed = transform(body)
    if transformed == body:
        raise AssertionError(f"{function} bool mutation did not change its body")
    return source[: opening + 1] + transformed + source[closing:]


def bool_function_bodies(source: str, function: str) -> tuple[str, ...]:
    declaration = re.compile(
        rf"\bbool\s+{re.escape(function)}\s*\([^;{{}}]*\)\s*"
        r"(?:const\s*)?(?:noexcept\s*)?\{"
    )
    bodies: list[str] = []
    for match in declaration.finditer(source):
        opening = match.end() - 1
        closing = matching_delimiter(source, opening, "{", "}")
        bodies.append(source[opening + 1 : closing])
    return tuple(bodies)


def replace_unique_override_body(source: str, entry: str, transform) -> str:
    declaration = re.compile(
        rf"\b{re.escape(entry)}\s*\([^;{{}}]*\)\s*" r"[^;{}]*\boverride\b[^;{}]*\{"
    )
    matches = tuple(declaration.finditer(source))
    if len(matches) != 1:
        raise AssertionError(f"{entry} mutation target is not unique")
    opening = matches[0].end() - 1
    closing = matching_delimiter(source, opening, "{", "}")
    body = source[opening + 1 : closing]
    transformed = transform(body)
    if transformed == body:
        raise AssertionError(f"{entry} mutation did not change its body")
    return source[: opening + 1] + transformed + source[closing:]


def compact_cpp(expression: str) -> str:
    return re.sub(r"\s+", "", expression)


def cpp_object_expression(expression: str) -> str:
    return re.sub(r"\.(?:data|c_str)\(\)$", "", compact_cpp(expression))


def require_post_replace_verification(
    source: str,
    platform: str,
    label: str,
) -> None:
    helper_bodies = function_bodies(source, "verify_replaced_target")
    if len(helper_bodies) != 1:
        raise AssertionError(f"{label} post-replace verifier is not unique")
    helper = helper_bodies[0]
    returns = re.findall(r"\breturn\b(?P<value>[^;]*);", helper, re.DOTALL)
    if not returns or any(
        "EffectMayHaveOccurred" not in value and "Succeeded" not in value
        for value in returns
    ):
        raise AssertionError(f"{label} verifier does not fail effect-may")
    if helper.count("DurableFileStatus::Succeeded") != 1:
        raise AssertionError(f"{label} verifier maps a failure to success")
    if any(
        token in helper
        for token in (
            "DurableFileStatus::Unsupported",
            "DurableFileStatus::FailedBeforeEffect",
        )
    ):
        raise AssertionError(f"{label} verifier downgrades a replacement failure")
    reads = call_argument_lists(helper, "read_bounded_close")
    read_assignments = tuple(
        re.finditer(
            r"\b(?:const\s+)?(?:auto|DurableReadResult)\s+"
            r"(?P<result>[A-Za-z_]\w*)\s*=\s*read_bounded_close\s*\(",
            helper,
        )
    )
    if (
        len(reads) != 1
        or len(read_assignments) != 1
        or len(reads[0]) != 2
        or compact_cpp(reads[0][1]) != "expected_bytes.size()"
    ):
        raise AssertionError(f"{label} verifier does not read exact bounded bytes")
    read_result = read_assignments[0].group("result")
    compared_views = tuple(
        re.finditer(
            r"\b(?:const\s+)?(?:auto|std::string_view)\s+"
            r"(?P<view>[A-Za-z_]\w*)\s*=\s*std::string_view\s*\(\s*"
            rf"{re.escape(read_result)}\s*\.\s*bytes\s*\.\s*data\s*\(\s*\)\s*,\s*"
            rf"{re.escape(read_result)}\s*\.\s*bytes\s*\.\s*size\s*\(\s*\)\s*\)",
            helper,
        )
    )
    if len(compared_views) != 1:
        raise AssertionError(f"{label} verifier does not compare observed bytes")
    compared_view = compared_views[0].group("view")
    if (
        re.search(rf"\b{re.escape(read_result)}\s*\.\s*truncated\b", helper) is None
        or re.search(rf"\b{re.escape(compared_view)}\s*!=\s*expected_bytes\b", helper)
        is None
    ):
        raise AssertionError(f"{label} verifier ignores observed bounded bytes")

    if platform == "posix":
        link_count_default = re.search(
            r"\bverify_replaced_target\s*\([^;{}]*\bnlink_t\s+"
            r"expected_link_count\s*=\s*1\s*\)\s*\{",
            source,
        )
        opens = call_argument_lists(helper, "openat")
        if len(opens) != 1 or not all(
            token in "".join(opens[0])
            for token in ("O_RDONLY", "O_CLOEXEC", "O_NOFOLLOW")
        ):
            raise AssertionError(f"{label} verifier follows an unsafe target")
        if (
            re.search(r"\bfstat\s*\(", helper) is None
            or re.search(r"!\s*S_ISREG\s*\(", helper) is None
            or link_count_default is None
            or re.search(r"\bst_nlink\s*!=\s*expected_link_count\b", helper) is None
            or re.search(
                r"\bst_dev\s*!=\s*expected_identity\s*\.\s*device\b",
                helper,
            )
            is None
            or re.search(
                r"\bst_ino\s*!=\s*expected_identity\s*\.\s*inode\b",
                helper,
            )
            is None
            or "close_open_descriptor" not in helper
        ):
            raise AssertionError(f"{label} verifier omits safe identity validation")
    elif platform == "windows":
        opens = call_argument_lists(helper, "CreateFileW")
        if (
            len(opens) != 1
            or len(opens[0]) < 3
            or not all(
                token in "".join(opens[0])
                for token in (
                    "GENERIC_READ",
                    "OPEN_EXISTING",
                    "FILE_FLAG_OPEN_REPARSE_POINT",
                )
            )
            or "FILE_SHARE_DELETE" in opens[0][2]
        ):
            raise AssertionError(f"{label} verifier follows an unsafe target")
        for token in (
            "FileAttributeTagInfo",
            "FILE_ATTRIBUTE_REPARSE_POINT",
            "FILE_ATTRIBUTE_DIRECTORY",
            "FileStandardInfo",
            "NumberOfLinks",
            "FileIdInfo",
            "same_file_identity",
            "close_open_handle",
        ):
            if token not in helper:
                raise AssertionError(f"{label} verifier omits safe identity validation")
        if (
            re.search(r"\bNumberOfLinks\s*!=\s*1\b", helper) is None
            or re.search(
                r"!\s*same_file_identity\s*\(\s*identity\s*,\s*"
                r"expected_identity\s*\)",
                helper,
            )
            is None
        ):
            raise AssertionError(f"{label} verifier ignores target identity")
        identity_bodies = bool_function_bodies(source, "same_file_identity")
        if len(identity_bodies) != 1:
            raise AssertionError(f"{label} identity comparator is not unique")
        identity_body = compact_cpp(identity_bodies[0])
        expected_identity_return = (
            "returnleft.VolumeSerialNumber==right.VolumeSerialNumber&&"
            "std::memcmp(left.FileId.Identifier,right.FileId.Identifier,"
            "sizeof(left.FileId.Identifier))==0;"
        )
        if expected_identity_return not in identity_body:
            raise AssertionError(f"{label} compares an incomplete file identity")
    else:
        raise AssertionError(f"{label} verifier platform is unknown")

    replacement_call = "renameat" if platform == "posix" else "MoveFileExW"
    for entry in ("replace_journal", "replace_root"):
        bodies = overriding_method_bodies(source, "DurableFileAdapter", entry)
        if len(bodies) != 1:
            raise AssertionError(f"{label} {entry} override is not unique")
        body = bodies[0]
        verifier_calls = call_argument_lists(body, "verify_replaced_target")
        replacement_calls = call_argument_lists(body, replacement_call)
        if len(verifier_calls) != 1 or len(replacement_calls) != 1:
            raise AssertionError(f"{label} {entry} verifier call count drifted")
        write_at = body.find("write_flush_close")
        replace_at = body.find(replacement_call)
        verify_at = body.find("verify_replaced_target")
        if write_at < 0 or not write_at < replace_at < verify_at:
            raise AssertionError(f"{label} {entry} verifies before replacement")
        propagated_result = (
            r"\breturn\s+validate_completed_mutation\s*\(\s*"
            r"verify_replaced_target\s*\([^;]*\)\s*\)\s*;"
            if platform == "posix"
            else r"\breturn\s+verify_replaced_target\s*\([^;]*\)\s*;"
        )
        if re.search(propagated_result, body, re.DOTALL) is None:
            raise AssertionError(f"{label} {entry} ignores verifier result")
        verify = tuple(compact_cpp(value) for value in verifier_calls[0])
        replacement = tuple(compact_cpp(value) for value in replacement_calls[0])
        if platform == "posix":
            sync_at = body.find("sync_directory_retrying_interrupts")
            if not replace_at < sync_at < verify_at:
                raise AssertionError(
                    f"{label} {entry} verifies before namespace durability"
                )
            identity = re.search(
                r"\bconst\s+[A-Za-z_]\w*FileIdentity\s+"
                r"(?P<name>[A-Za-z_]\w*)\s*\{(?P<value>[^}]*)\}",
                body,
                re.DOTALL,
            )
            if (
                identity is None
                or identity.start() >= write_at
                or "st_dev" not in identity.group("value")
                or "st_ino" not in identity.group("value")
                or len(verify) != 4
                or len(replacement) != 4
                or verify[0] != replacement[0]
                or cpp_object_expression(verify[1])
                != cpp_object_expression(replacement[3])
                or verify[2] != identity.group("name")
                or verify[3] != "bytes"
            ):
                raise AssertionError(
                    f"{label} {entry} does not carry staged identity forward"
                )
        else:
            identity = re.search(
                r"\bFILE_ID_INFO\s+(?P<name>[A-Za-z_]\w*)\s*\{\s*\}",
                body,
            )
            if identity is None or identity.start() >= write_at:
                raise AssertionError(
                    f"{label} {entry} does not capture staged identity"
                )
            identity_calls = [
                arguments
                for arguments in call_argument_lists(
                    body[:write_at], "GetFileInformationByHandleEx"
                )
                if "FileIdInfo" in arguments
                and any(
                    compact_cpp(argument) == f"&{identity.group('name')}"
                    for argument in arguments
                )
            ]
            if (
                len(identity_calls) != 1
                or len(verify) != 3
                or len(replacement) < 2
                or cpp_object_expression(verify[0])
                != cpp_object_expression(replacement[1])
                or verify[1] != identity.group("name")
                or verify[2] != "bytes"
            ):
                raise AssertionError(
                    f"{label} {entry} does not carry staged identity forward"
                )


def require_post_replace_verification_mutants(
    source: str,
    platform: str,
    label: str,
) -> None:
    def rejected(mutant: str, name: str) -> None:
        try:
            require_post_replace_verification(mutant, platform, f"{label} mutant")
        except AssertionError:
            return
        raise AssertionError(f"{label} verifier accepted {name} mutant")

    propagated_result = (
        r"\breturn\s+validate_completed_mutation\s*\(\s*"
        r"verify_replaced_target\s*\([^;]*\)\s*\)\s*;"
        if platform == "posix"
        else r"\breturn\s+verify_replaced_target\s*\([^;]*\)\s*;"
    )

    def missing(body: str) -> str:
        return re.sub(
            propagated_result,
            "return make_result(DurableFileStatus::Succeeded);",
            body,
            count=1,
            flags=re.DOTALL,
        )

    rejected(
        replace_unique_override_body(source, "replace_journal", missing),
        "missing",
    )

    def early(body: str) -> str:
        match = re.search(
            propagated_result,
            body,
            re.DOTALL,
        )
        if match is None:
            return body
        marker = "if (::renameat" if platform == "posix" else "if (!::MoveFileExW"
        without = body[: match.start()] + body[match.end() :]
        return without.replace(marker, match.group(0) + "\n" + marker, 1)

    rejected(
        replace_unique_override_body(source, "replace_journal", early),
        "before-replace",
    )

    if platform == "posix":

        def before_binding_validation(body: str) -> str:
            return re.sub(
                propagated_result,
                lambda match: re.sub(
                    r"validate_completed_mutation\s*\(\s*(.*)\s*\)\s*;",
                    r"\1;",
                    match.group(0),
                    count=1,
                    flags=re.DOTALL,
                ),
                body,
                count=1,
                flags=re.DOTALL,
            )

        rejected(
            replace_unique_override_body(
                source, "replace_journal", before_binding_validation
            ),
            "pre-verification-binding-only",
        )

    def identity_only(body: str) -> str:
        if platform == "posix":
            return re.sub(
                r"information\s*\.\s*st_dev\s*!=\s*"
                r"expected_identity\s*\.\s*device\s*\|\|\s*"
                r"information\s*\.\s*st_ino\s*!=\s*"
                r"expected_identity\s*\.\s*inode",
                "false",
                body,
                count=1,
            )
        return re.sub(
            r"!\s*same_file_identity\s*\(\s*identity\s*,\s*" r"expected_identity\s*\)",
            "false",
            body,
            count=1,
        )

    rejected(
        replace_unique_function_body(source, "verify_replaced_target", identity_only),
        "content-only",
    )

    def content_only(body: str) -> str:
        return re.sub(
            r"observed_bytes\s*!=\s*expected_bytes",
            "false",
            body,
            count=1,
        )

    rejected(
        replace_unique_function_body(source, "verify_replaced_target", content_only),
        "identity-only",
    )

    def unsafe(body: str) -> str:
        token = "O_NOFOLLOW" if platform == "posix" else "FILE_FLAG_OPEN_REPARSE_POINT"
        return body.replace(token, "0", 1)

    rejected(
        replace_unique_function_body(source, "verify_replaced_target", unsafe),
        "unsafe-follow",
    )

    def unbounded(body: str) -> str:
        return body.replace(
            "expected_bytes.size()",
            "std::numeric_limits<std::size_t>::max()",
            1,
        )

    rejected(
        replace_unique_function_body(source, "verify_replaced_target", unbounded),
        "unbounded-read",
    )

    def downgraded(body: str) -> str:
        return body.replace(
            "DurableFileStatus::EffectMayHaveOccurred",
            "DurableFileStatus::Succeeded",
            1,
        )

    rejected(
        replace_unique_function_body(source, "verify_replaced_target", downgraded),
        "success-mapped-failure",
    )

    def rebound_observed_bytes(body: str) -> str:
        return body.replace(
            "std::string_view(observed.bytes.data(), observed.bytes.size())",
            "std::string_view(expected_bytes.data(), expected_bytes.size())",
            1,
        )

    rejected(
        replace_unique_function_body(
            source, "verify_replaced_target", rebound_observed_bytes
        ),
        "expected-bytes-rebind",
    )

    if platform == "windows":

        def always_equal(_body: str) -> str:
            return "\n    return true;\n"

        rejected(
            replace_unique_bool_function_body(
                source, "same_file_identity", always_equal
            ),
            "always-equal-identity",
        )

        def volume_only(_body: str) -> str:
            return (
                "\n    return left.VolumeSerialNumber == " "right.VolumeSerialNumber;\n"
            )

        rejected(
            replace_unique_bool_function_body(
                source, "same_file_identity", volume_only
            ),
            "volume-only-identity",
        )

        def file_id_only(_body: str) -> str:
            return (
                "\n    return std::memcmp(left.FileId.Identifier, "
                "right.FileId.Identifier, sizeof(left.FileId.Identifier)) == 0;\n"
            )

        rejected(
            replace_unique_bool_function_body(
                source, "same_file_identity", file_id_only
            ),
            "file-id-only-identity",
        )

        def delete_shared(body: str) -> str:
            return body.replace(
                "FILE_SHARE_READ | FILE_SHARE_WRITE",
                "FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE",
                1,
            )

        rejected(
            replace_unique_function_body(
                source, "verify_replaced_target", delete_shared
            ),
            "delete-shared-verifier",
        )


def require_windows_bound_directory_factory(source: str) -> None:
    factory_bodies = function_bodies(source, "make_platform_durable_file_adapter")
    if len(factory_bodies) != 1:
        raise AssertionError("Windows adapter factory is not unique")
    factory = factory_bodies[0]
    opened_at = factory.find("CreateFileW")
    attributes_at = factory.find("GetFileInformationByHandleEx")
    final_at = factory.find("final_directory_path")
    constructed_at = factory.rfind("make_unique<WindowsDurableFileAdapter>")
    if not 0 <= opened_at < attributes_at < final_at < constructed_at:
        raise AssertionError("Windows adapter binds its directory out of order")
    opens = call_argument_lists(factory, "CreateFileW")
    if len(opens) != 1 or not all(
        token in "".join(opens[0])
        for token in (
            "directory.c_str()",
            "OPEN_EXISTING",
            "FILE_FLAG_BACKUP_SEMANTICS",
            "FILE_FLAG_OPEN_REPARSE_POINT",
        )
    ):
        raise AssertionError("Windows adapter does not open the caller directory")
    if not all(
        token in factory
        for token in (
            "FileAttributeTagInfo",
            "FILE_ATTRIBUTE_REPARSE_POINT",
            "FILE_ATTRIBUTE_DIRECTORY",
            "std::move(*bound_directory)",
            "INVALID_HANDLE_VALUE",
        )
    ):
        raise AssertionError("Windows adapter does not validate its bound directory")
    if len(re.findall(r"std::filesystem::path\s*\{\s*\}", factory)) < 2:
        raise AssertionError("Windows adapter keeps an invalid caller path")
    if re.search(
        r"make_unique\s*<\s*WindowsDurableFileAdapter\s*>\s*" r"\(\s*directory\b",
        factory,
    ):
        raise AssertionError("Windows adapter retains the caller-relative path")
    final_bodies = function_bodies(source, "final_directory_path")
    if len(final_bodies) != 1:
        raise AssertionError("Windows final-directory resolver is not unique")
    final_body = final_bodies[0]
    if (
        "FILE_NAME_NORMALIZED" not in final_body
        or "VOLUME_NAME_DOS" not in final_body
        or len(call_argument_lists(final_body, "GetFinalPathNameByHandleW")) < 2
    ):
        raise AssertionError("Windows adapter does not resolve a normalized path")
    for arguments in call_argument_lists(final_body, "GetFinalPathNameByHandleW"):
        if not any(compact_cpp(argument) == "flags" for argument in arguments):
            raise AssertionError("Windows final-path call ignores normalized flags")


def require_fixed_namespace_factory_contract(
    header: str,
    common: str,
    posix: str,
    macos: str,
    windows: str,
) -> None:
    declaration = re.compile(
        r"\bmake_platform_durable_file_adapter_in_fixed_namespace\s*\(\s*"
        r"const\s+std::filesystem::path\s*&\s*parent_directory\s*,\s*"
        r"std::string_view\s+child_namespace\s*\)\s*;"
    )
    if declaration.search(header) is None:
        raise AssertionError("fixed-namespace adapter factory is unavailable")
    validators = function_bodies(common, "durable_fixed_namespace_name_is_valid")
    if len(validators) != 1 or not all(
        token in validators[0]
        for token in ("name.empty()", "name.size()", "name.front()", "name.back()")
    ):
        raise AssertionError("fixed-namespace name validation drifted")

    for source, label in (
        (posix, "POSIX durable adapter"),
        (macos, "macOS durable adapter"),
    ):
        factories = function_bodies(
            source, "make_platform_durable_file_adapter_in_fixed_namespace"
        )
        binders = function_bodies(source, "bind_fixed_namespace_directory")
        if (
            len(factories) != 1
            or len(binders) != 1
            or "bind_fixed_namespace_directory" not in factories[0]
        ):
            raise AssertionError(f"{label} fixed-namespace factory is not unique")
        binder = binders[0]
        required = (
            "durable_fixed_namespace_name_is_valid",
            "O_DIRECTORY",
            "O_NOFOLLOW",
            "lock_directory_retrying_interrupts",
            "mkdirat",
            "AT_SYMLINK_NOFOLLOW",
            "openat",
            "fstat",
            "S_ISDIR",
            "sync_directory_retrying_interrupts",
            "unlock_directory_retrying_interrupts",
            "st_dev",
            "st_ino",
        )
        if not all(token in binder for token in required):
            raise AssertionError(f"{label} fixed namespace omits safe durable binding")
        create_at = binder.find("mkdirat")
        bind_at = binder.find("openat")
        sync_at = binder.find("sync_directory_retrying_interrupts")
        if not 0 <= create_at < bind_at < sync_at:
            raise AssertionError(
                f"{label} fixed namespace is bound or returned before durability"
            )
        open_calls = call_argument_lists(binder, "openat")
        if len(open_calls) != 1 or not all(
            token in "".join(open_calls[0])
            for token in ("parent_fd", "child_name", "O_DIRECTORY", "O_NOFOLLOW")
        ):
            raise AssertionError(f"{label} fixed namespace follows its child")

    factories = function_bodies(windows, "make_windows_fixed_namespace_adapter")
    public_factories = function_bodies(
        windows, "make_platform_durable_file_adapter_in_fixed_namespace"
    )
    binders = function_bodies(windows, "bind_windows_directory")
    emptiness_checks = function_bodies(windows, "windows_directory_is_empty")
    if (
        len(factories) != 1
        or len(public_factories) != 1
        or len(binders) != 1
        or len(emptiness_checks) != 1
        or "make_windows_fixed_namespace_adapter" not in public_factories[0]
    ):
        raise AssertionError("Windows fixed-namespace seams are not unique")
    factory = factories[0]
    binder = binders[0]
    emptiness = emptiness_checks[0]
    if not all(
        token in binder
        for token in (
            "CreateFileW",
            "FILE_FLAG_BACKUP_SEMANTICS",
            "FILE_FLAG_OPEN_REPARSE_POINT",
            "FileAttributeTagInfo",
            "FILE_ATTRIBUTE_REPARSE_POINT",
            "FILE_ATTRIBUTE_DIRECTORY",
            "FileIdInfo",
            "final_directory_path",
        )
    ):
        raise AssertionError("Windows fixed namespace does not bind safe directories")
    if not all(
        token in emptiness for token in ("FindFirstFileW", "FindNextFileW", "FindClose")
    ):
        raise AssertionError("Windows fixed namespace does not prove stage emptiness")
    required_factory_tokens = (
        "durable_fixed_namespace_name_is_valid",
        "bind_windows_directory(parent_directory)",
        "cleanup_stage",
        "CreateDirectoryW",
        "ERROR_ALREADY_EXISTS",
        "windows_directory_is_empty",
        "MoveFileExW",
        "MOVEFILE_WRITE_THROUGH",
        "same_file_identity",
    )
    if not all(token in factory for token in required_factory_tokens):
        raise AssertionError("Windows fixed namespace omits durable staged publication")
    moves = call_argument_lists(factory, "MoveFileExW")
    if (
        len(moves) != 1
        or len(moves[0]) != 3
        or compact_cpp(moves[0][2]) != "MOVEFILE_WRITE_THROUGH"
        or "MOVEFILE_REPLACE_EXISTING" in "".join(moves[0])
    ):
        raise AssertionError("Windows fixed namespace replaces an existing child")
    existing_at = factory.find("bind_windows_directory(child)")
    stage_at = factory.find("CreateDirectoryW")
    move_at = factory.find("MoveFileExW")
    published_at = factory.rfind("bind_windows_directory(child)")
    if not 0 <= existing_at < stage_at < move_at < published_at:
        raise AssertionError("Windows fixed namespace mishandles concurrent creators")

    binder_opens = call_argument_lists(binder, "CreateFileW")
    if len(binder_opens) != 1 or len(binder_opens[0]) < 3:
        raise AssertionError("Windows fixed namespace directory open is ambiguous")
    share_mode = compact_cpp(binder_opens[0][2])
    if (
        "FILE_SHARE_READ" not in share_mode
        or "FILE_SHARE_WRITE" not in share_mode
        or "FILE_SHARE_DELETE" in share_mode
    ):
        raise AssertionError("Windows fixed namespace does not pin directory renames")
    release_at = factory.find("if (!release_parent())")
    publish_at = factory.find("std::make_unique<WindowsDurableFileAdapter>", release_at)
    if not 0 <= release_at < publish_at:
        raise AssertionError(
            "Windows fixed namespace does not release its parent before publication"
        )
    assigned_stage_closes = re.findall(
        r"\b(?:const\s+)?auto\s+[A-Za-z_]\w*\s*=\s*"
        r"close_windows_directory\s*\(\s*bound_stage\s*\)\s*;",
        factory,
    )
    if len(assigned_stage_closes) < 2 or re.search(
        r"\|\|\s*!\s*close_windows_directory\s*\(\s*bound_stage\s*\)",
        factory,
    ):
        raise AssertionError("Windows fixed namespace can skip a bound-stage close")

    require_windows_fixed_namespace_convergence_contract(windows)


def require_windows_fixed_namespace_convergence_contract(source: str) -> None:
    factories = function_bodies(source, "make_windows_fixed_namespace_adapter")
    if len(factories) != 1:
        raise AssertionError("Windows fixed-namespace convergence seam is not unique")
    factory = factories[0]
    compact = compact_cpp(factory)
    required = (
        "fixed_namespace_convergence_attempts",
        "yield_fixed_namespace_convergence",
        "WindowsDirectoryBindStatus::Retry",
        "WindowsStageCleanupStatus::Retry",
        "after_publish_attempt(attempt,moved)",
    )
    if not all(token in compact for token in required):
        raise AssertionError("Windows fixed namespace omits bounded convergence")
    if (
        re.search(
            r"for\s*\([^;]*\battempt\b[^;]*;\s*"
            r"\battempt\s*<\s*fixed_namespace_convergence_attempts\s*;",
            factory,
        )
        is None
    ):
        raise AssertionError("Windows fixed namespace does not bound its retries")
    if factory.count("bind_windows_directory(child)") < 2:
        raise AssertionError("Windows fixed namespace does not bind the winner")
    if factory.count("yield_fixed_namespace_convergence();") < 2:
        raise AssertionError("Windows fixed namespace spins during convergence")


def require_windows_fixed_namespace_convergence_mutants(source: str) -> None:
    def rejected(mutant: str, name: str) -> None:
        try:
            require_windows_fixed_namespace_convergence_contract(mutant)
        except AssertionError:
            return
        raise AssertionError(
            f"Windows fixed-namespace convergence accepted {name} mutant"
        )

    rejected(
        replace_unique_function_body(
            source,
            "make_windows_fixed_namespace_adapter",
            lambda body: body.replace(
                "attempt < fixed_namespace_convergence_attempts", "attempt < 1", 1
            ),
        ),
        "one-shot",
    )
    rejected(
        replace_unique_function_body(
            source,
            "make_windows_fixed_namespace_adapter",
            lambda body: body.replace("yield_fixed_namespace_convergence();", ""),
        ),
        "busy-spin",
    )

    def skip_winner_rebind(body: str) -> str:
        target = "bind_windows_directory(child)"
        occurrence = body.rfind(target)
        if occurrence < 0:
            return body
        return (
            body[:occurrence]
            + "WindowsDirectoryBinding{}"
            + body[occurrence + len(target) :]
        )

    rejected(
        replace_unique_function_body(
            source,
            "make_windows_fixed_namespace_adapter",
            skip_winner_rebind,
        ),
        "winner-rebind",
    )


def require_test_storage_identity_capture_contract(source: str) -> None:
    helpers = [
        body
        for body in function_bodies(source, "capture_bound_directory_identity")
        if "state->named_directory_identity" in body
        and "state->directory_identity" in body
    ]
    if len(helpers) != 1:
        raise AssertionError("fake storage identity capture seam is not unique")
    helper = helpers[0]
    compact_helper = compact_cpp(helper)
    lock_at = compact_helper.find("std::lock_guardlock(state->mutex)")
    named_at = compact_helper.find("state->named_directory_identity")
    directory_at = compact_helper.find("state->directory_identity")
    if not 0 <= lock_at < named_at < directory_at:
        raise AssertionError("fake storage captures its bound identity without locking")
    if (
        "bound_directory_identity_(capture_bound_directory_identity("
        "state_,fixed_namespace_))" not in compact_cpp(source)
    ):
        raise AssertionError(
            "fake adapter constructor bypasses locked identity capture"
        )


def require_test_storage_identity_capture_mutant(source: str) -> None:
    mutant, replacements = re.subn(
        r"std::lock_guard\s+lock\s*\(\s*state->mutex\s*\)\s*;",
        "",
        source,
        count=1,
    )
    if replacements != 1:
        raise AssertionError("fake storage identity lock mutation target drifted")
    try:
        require_test_storage_identity_capture_contract(mutant)
    except AssertionError:
        return
    raise AssertionError("fake storage identity capture accepted an unlocked mutant")


def require_windows_bound_directory_factory_mutants(source: str) -> None:
    def rejected(mutant: str, name: str) -> None:
        try:
            require_windows_bound_directory_factory(mutant)
        except AssertionError:
            return
        raise AssertionError(f"Windows directory binding accepted {name} mutant")

    def caller_path(body: str) -> str:
        return body.replace(
            "std::move(*bound_directory), directory_handle",
            "directory, directory_handle",
            1,
        )

    rejected(
        replace_unique_function_body(
            source, "make_platform_durable_file_adapter", caller_path
        ),
        "caller-path",
    )

    def unnormalized(body: str) -> str:
        return body.replace("FILE_NAME_NORMALIZED", "0", 1)

    rejected(
        replace_unique_function_body(source, "final_directory_path", unnormalized),
        "unnormalized-path",
    )

    def invalid_keeps_path(body: str) -> str:
        return body.replace("std::filesystem::path{}", "directory", 1)

    rejected(
        replace_unique_function_body(
            source, "make_platform_durable_file_adapter", invalid_keeps_path
        ),
        "invalid-caller-path",
    )


def require_each_override_directory_sync_binding(
    source: str,
    entry: str,
    label: str,
) -> None:
    bodies = overriding_method_bodies(source, "DurableFileAdapter", entry)
    if not bodies:
        raise AssertionError(f"{label} has no concrete {entry} override")
    for body in bodies:
        renames = call_argument_lists(body, "renameat")
        syncs = call_argument_lists(body, "sync_directory_retrying_interrupts")
        if len(renames) != 1 or len(syncs) != 1:
            raise AssertionError(f"{label} replacement/sync call count drifted")
        rename = tuple(re.sub(r"\s+", "", argument) for argument in renames[0])
        sync = tuple(re.sub(r"\s+", "", argument) for argument in syncs[0])
        if (
            len(rename) != 4
            or len(sync) != 1
            or rename[0] != rename[2]
            or sync[0] != rename[0]
        ):
            raise AssertionError(f"{label} syncs a different authority directory")


def require_each_override_minimum_call_argument(
    source: str,
    base: str,
    entry: str,
    call: str,
    argument: str,
    minimum: int,
    label: str,
) -> None:
    bodies = overriding_method_bodies(source, base, entry)
    if not bodies:
        raise AssertionError(f"{label} has no concrete {base}::{entry} override")
    for body in bodies:
        count = sum(
            argument in arguments for arguments in call_argument_lists(body, call)
        )
        if count < minimum:
            raise AssertionError(f"{label} omits repeated {call}({argument})")


def require_windows_composite_identity(source: str) -> None:
    bodies = overriding_method_bodies(
        source, "DurableFileAdapter", "authority_identity"
    )
    if not bodies:
        raise AssertionError("Windows authority identity override is unavailable")
    for body in bodies:
        declarations = re.findall(
            r"\bFILE_ID_INFO\s+(?P<name>[A-Za-z_]\w*)\s*\{\s*\}", body
        )
        bound: list[str] = []
        for arguments in call_argument_lists(body, "GetFileInformationByHandleEx"):
            if "FileIdInfo" not in arguments or len(arguments) < 3:
                continue
            match = re.fullmatch(r"&\s*(?P<name>[A-Za-z_]\w*)", arguments[2])
            if match is not None and match.group("name") in declarations:
                bound.append(match.group("name"))
        if len(set(bound)) < 2:
            raise AssertionError("Windows identity omits directory or lock file ID")
        returns = re.findall(r"\breturn\b(?P<value>[^;]*);", body)
        if not any(all(name in value for name in set(bound)) for value in returns):
            raise AssertionError("Windows identity does not bind both file IDs")


def require_each_override_retryable_result_mapping(
    source: str,
    base: str,
    entry: str,
    primitive: str,
    interruption_token: str,
    label: str,
) -> None:
    bodies = overriding_method_bodies(source, base, entry)
    if not bodies:
        raise AssertionError(f"{label} has no concrete {base}::{entry} override")
    for body in bodies:
        branches = fail_closed_if_branches(body)
        if not any(
            condition_rejects_predicate(condition, primitive)
            and branch_returns_non_success(statement)
            for condition, statement in branches
        ):
            raise AssertionError(f"{label} ignores {primitive} failure")
        if not any(
            interruption_token in condition and "Interrupted" in statement
            for condition, statement in branches
        ):
            raise AssertionError(
                f"{label} does not classify {interruption_token} for retry"
            )
        if "Succeeded" not in body:
            raise AssertionError(f"{label} has no explicit success result")


def require_each_override_close_result_mapping(
    source: str,
    primitive: str,
    label: str,
    base: str = "DurableFileChannel",
) -> None:
    require_each_override_fail_closed_checks(
        source,
        base,
        "close",
        (primitive,),
        label,
    )
    require_each_override_omits_calls(
        source,
        base,
        "close",
        ("retry_interrupted",),
        label,
    )
    for body in overriding_method_bodies(source, base, "close"):
        if "Succeeded" not in body:
            raise AssertionError(f"{label} close has no explicit success result")


def require_each_override_write_count_mapping(
    source: str,
    platform: str,
    label: str,
) -> None:
    bodies = overriding_method_bodies(source, "DurableFileChannel", "write_some")
    if not bodies:
        raise AssertionError(f"{label} has no concrete write_some override")
    for body in bodies:
        if platform == "posix":
            assignment = re.search(
                r"\b(?:const\s+)?(?:auto|ssize_t)\s+(?P<count>[A-Za-z_]\w*)\s*"
                r"=\s*(?:::)?write\s*\(",
                body,
            )
            interruption_token = "EINTR"
            primitive = "write"
        else:
            assignment = re.search(
                r"\b(?:DWORD|std::uint32_t|auto)\s+(?P<count>[A-Za-z_]\w*)\s*"
                r"(?:=\s*0)?\s*;[^{}]*\bWriteFile\s*\([^;]*&\s*"
                r"(?P=count)\b",
                body,
                re.DOTALL,
            )
            interruption_token = "ERROR_OPERATION_ABORTED"
            primitive = "WriteFile"
        if assignment is None:
            raise AssertionError(f"{label} does not capture the OS write count")
        count = assignment.group("count")
        branches = fail_closed_if_branches(body)
        if platform == "posix":
            failure_is_checked = any(
                count in condition
                and bool(
                    re.search(rf"\b{re.escape(count)}\s*(?:<\s*0|==\s*-1)", condition)
                )
                and branch_returns_non_success(statement)
                for condition, statement in branches
            )
            if not any(
                interruption_token in condition and "Interrupted" in statement
                for condition, statement in branches
            ):
                raise AssertionError(
                    f"{label} does not classify {interruption_token} for retry"
                )
        else:
            if len(re.findall(r"\bWriteFile\b", source)) != 1:
                raise AssertionError(
                    f"{label} has an indirect, duplicate, or redirected Windows write"
                )
            macro_source = source.replace("%:", "#")
            defined_macros = re.findall(
                r"(?m)^\s*#\s*define\s+([A-Za-z_]\w*)\b", macro_source
            )
            if any(name != "NOMINMAX" for name in defined_macros):
                raise AssertionError(f"{label} shadows the Windows write contract")

            def direct_write_failure(condition: str) -> bool:
                compact = re.sub(r"\s+", "", condition)
                call = re.match(r"!::WriteFile\(", compact)
                if call is None:
                    return False
                opening = compact.find("(", call.start())
                return (
                    matching_delimiter(compact, opening, "(", ")") == len(compact) - 1
                )

            failure_statements = [
                statement
                for condition, statement in branches
                if direct_write_failure(condition)
            ]
            if len(failure_statements) != 1:
                raise AssertionError(
                    f"{label} does not have one fail-closed {primitive} branch"
                )
            failure_statement = failure_statements[0]
            canonical_failure = re.fullmatch(
                r"\s*const\s+auto\s+(?P<error>[A-Za-z_]\w*)\s*=\s*"
                r"(?:::)?GetLastError\(\)\s*;\s*"
                r"return\s+\{\s*::lemon::residency::detail::"
                r"DurableFileResult\s*\{\s*::lemon::residency::detail::"
                r"DurableFileStatus::\s*EffectMayHaveOccurred\s*,\s*"
                r"::std::to_string\(\s*(?P=error)\s*\)\s*\}\s*,\s*"
                r"0\s*\}\s*;\s*",
                failure_statement,
                re.DOTALL,
            )
            if canonical_failure is None:
                raise AssertionError(
                    f"{label} does not unconditionally classify {primitive} "
                    "failure as uncertain"
                )
            if re.search(r"\bInterrupted\b", body):
                raise AssertionError(
                    f"{label} retries an uncertain {primitive} failure"
                )
            failure_is_checked = branch_returns_non_success(failure_statement)
        if not failure_is_checked:
            raise AssertionError(f"{label} ignores {primitive} failure")
        if re.search(rf"\breturn\b[^;]*\b{re.escape(count)}\b[^;]*;", body) is None:
            raise AssertionError(f"{label} does not return the OS write count")


def require_windows_write_override_shape(source: str, label: str) -> None:
    bodies = overriding_method_bodies(source, "DurableFileChannel", "write_some")
    if len(bodies) != 1:
        raise AssertionError(f"{label} has an ambiguous Windows write override")
    actual = re.sub(r"\s+", "", bodies[0])
    expected = re.sub(
        r"\s+",
        "",
        """
        const auto request = static_cast<DWORD>(
            std::min(bytes.size(), std::size_t(MAXDWORD)));
        DWORD written = 0;
        if (!::WriteFile(handle_, bytes.data(), request, &written, nullptr)) {
            const auto error = ::GetLastError();
            return {::lemon::residency::detail::DurableFileResult{
                        ::lemon::residency::detail::DurableFileStatus::
                            EffectMayHaveOccurred,
                        ::std::to_string(error)},
                    0};
        }
        return {::lemon::residency::detail::DurableFileResult{
                    ::lemon::residency::detail::DurableFileStatus::Succeeded,
                    {}},
                written};
        """,
    )
    if actual != expected:
        raise AssertionError(f"{label} Windows write override shape drifted")


def require_each_override_read_count_mapping(
    source: str,
    platform: str,
    label: str,
) -> None:
    def fixed_read_cap(expression: str) -> bool:
        return bool(
            re.fullmatch(
                r"(?:"
                r"(?:0[xX][0-9A-Fa-f]+|[0-9]+)[uUlL]*|"
                r"MAXDWORD|"
                r"sizeof\([^()]+\)|"
                r"(?:std::)?size_t\((?:MAXDWORD|[0-9]+[uUlL]*)\)|"
                r"static_cast<(?:std::)?size_t>\((?:MAXDWORD|[0-9]+[uUlL]*)\)|"
                r"std::numeric_limits<(?:DWORD|std::uint32_t|std::size_t)>::max\(\)"
                r")",
                expression,
            )
        )

    def capped_read_request(expression: str) -> bool:
        narrowed = re.fullmatch(
            r"static_cast<(?:DWORD|std::uint32_t)>\((?P<value>.*)\)",
            expression,
        )
        if narrowed is not None:
            expression = narrowed.group("value")
        if expression == "max_bytes":
            return narrowed is None
        minimum = re.fullmatch(r"(?:std::)?min\((?P<arguments>.*)\)", expression)
        if minimum is None:
            return False
        arguments = tuple(
            re.sub(r"\s+", "", argument)
            for argument in split_cpp_arguments(minimum.group("arguments"))
        )
        if len(arguments) != 2:
            return False
        if arguments[0] == "max_bytes":
            cap = arguments[1]
        elif arguments[1] == "max_bytes":
            cap = arguments[0]
        else:
            return False
        if not fixed_read_cap(cap):
            return False
        if narrowed is None:
            return True
        if cap in {
            "MAXDWORD",
            "size_t(MAXDWORD)",
            "std::size_t(MAXDWORD)",
            "static_cast<size_t>(MAXDWORD)",
            "static_cast<std::size_t>(MAXDWORD)",
            "std::numeric_limits<DWORD>::max()",
            "std::numeric_limits<std::uint32_t>::max()",
        }:
            return True
        literal = re.fullmatch(r"(?P<value>0[xX][0-9A-Fa-f]+|[0-9]+)(?:[uUlL]*)", cap)
        if literal is None:
            return False
        base = 16 if literal.group("value").lower().startswith("0x") else 10
        return int(literal.group("value"), base) <= 0xFFFFFFFF

    bodies = overriding_method_bodies(source, "DurableReadChannel", "read_some")
    if not bodies:
        raise AssertionError(f"{label} has no concrete read_some override")
    for body in bodies:
        if platform == "posix":
            assignment = re.search(
                r"\b(?:const\s+)?(?:auto|ssize_t)\s+"
                r"(?P<count>[A-Za-z_]\w*)\s*=\s*(?:::)?read\s*\(",
                body,
            )
            interruption_token = "EINTR"
            primitive = "read"
        else:
            assignment = re.search(
                r"\b(?:DWORD|std::uint32_t|auto)\s+"
                r"(?P<count>[A-Za-z_]\w*)\s*(?:=\s*0)?\s*;"
                r"[^{}]*\bReadFile\s*\([^;]*&\s*(?P=count)\b",
                body,
                re.DOTALL,
            )
            interruption_token = "ERROR_OPERATION_ABORTED"
            primitive = "ReadFile"
        if assignment is None:
            raise AssertionError(f"{label} does not capture the OS read count")
        count = assignment.group("count")
        calls = call_argument_lists(body, primitive)
        if len(calls) != 1:
            raise AssertionError(f"{label} native read call count drifted")
        arguments = calls[0]
        if len(arguments) < 3:
            raise AssertionError(f"{label} native read arguments drifted")
        buffer_expression = re.sub(r"\s+", "", arguments[1])
        request_expression = re.sub(r"\s+", "", arguments[2])
        request_bound = capped_read_request(request_expression)
        request_name = re.fullmatch(r"[A-Za-z_]\w*", request_expression)
        request_initializer = ""
        if request_name is not None:
            declaration = re.search(
                rf"\b(?:const\s+)?(?:auto|std::size_t|DWORD|std::uint32_t)\s+"
                rf"{re.escape(request_name.group(0))}\s*=\s*(?P<value>[^;]+);",
                body[: assignment.start()],
            )
            if declaration is not None:
                request_initializer = re.sub(r"\s+", "", declaration.group("value"))
                request_bound = capped_read_request(request_initializer)
        if not request_bound:
            raise AssertionError(f"{label} native read is not capped by max_bytes")
        buffer_match = re.search(r"(?P<name>[A-Za-z_]\w*)", buffer_expression)
        if buffer_match is None:
            raise AssertionError(f"{label} native read buffer is unavailable")
        buffer_name = buffer_match.group("name")
        allocation_bound = False
        allocation = re.search(
            rf"\b(?:std::string|std::vector\s*<[^;>]+>)\s+"
            rf"{re.escape(buffer_name)}\s*\(",
            body[: assignment.start()],
        )
        if allocation is not None:
            opening = body.find("(", allocation.start())
            closing = matching_delimiter(body, opening, "(", ")")
            allocation_arguments = split_cpp_arguments(body[opening + 1 : closing])
            allocation_bound = bool(allocation_arguments) and (
                re.sub(r"\s+", "", allocation_arguments[0])
                in {request_expression, request_initializer or request_expression}
            )
        if allocation is not None and not allocation_bound:
            raise AssertionError(f"{label} native read buffer is not request-bounded")
        if allocation is None and (
            f"sizeof({buffer_name})" not in request_expression
            and f"sizeof({buffer_name})" not in request_initializer
        ):
            raise AssertionError(f"{label} native read buffer is not request-bounded")
        branches = fail_closed_if_branches(body)
        if platform == "posix":
            failure_is_checked = any(
                count in condition
                and bool(
                    re.search(
                        rf"\b{re.escape(count)}\s*(?:<\s*0|==\s*-1)",
                        condition,
                    )
                )
                and branch_returns_non_success(statement)
                for condition, statement in branches
            )
        else:
            failure_is_checked = any(
                condition_rejects_predicate(condition, primitive)
                and branch_returns_non_success(statement)
                for condition, statement in branches
            )
        if not failure_is_checked:
            raise AssertionError(f"{label} ignores {primitive} failure")
        if not any(
            interruption_token in condition and "Interrupted" in statement
            for condition, statement in branches
        ):
            raise AssertionError(
                f"{label} does not classify {interruption_token} for retry"
            )
        returns = re.findall(r"\breturn\b(?P<value>[^;]*);", body)
        if not any(count in value and buffer_name in value for value in returns):
            raise AssertionError(f"{label} does not bind bytes to the OS read count")
        normalized_returns = [re.sub(r"\s+", "", value) for value in returns]
        eof_expression = rf"{re.escape(count)}==0"
        eof_bound = any(
            re.search(eof_expression, value) for value in normalized_returns
        )
        if not eof_bound:
            eof_declarations = re.finditer(
                r"\b(?:const\s+)?bool\s+(?P<name>[A-Za-z_]\w*)\s*="
                rf"\s*{re.escape(count)}\s*==\s*0\s*;",
                body,
            )
            eof_bound = any(
                any(match.group("name") in value for value in normalized_returns)
                for match in eof_declarations
            )
        if not eof_bound:
            raise AssertionError(f"{label} does not bind EOF to the OS read count")


def assigned_call(
    body: str,
    call: str,
    label: str,
) -> tuple[str, str, int]:
    pattern = re.compile(
        r"\b(?:const\s+)?(?:auto|ParsedJournalRecordResult|"
        r"JournalHistoryResult|AuthorityRootCandidateResult)\s+"
        rf"(?P<result>[A-Za-z_]\w*)\s*=\s*"
        rf"(?:lemon::residency::)?{re.escape(call)}\s*\("
    )
    matches = list(pattern.finditer(body))
    if len(matches) != 1:
        raise AssertionError(f"{label} does not call {call} exactly once")
    match = matches[0]
    opening = body.find("(", match.start())
    closing = matching_delimiter(body, opening, "(", ")")
    return match.group("result"), body[opening + 1 : closing], match.start()


def require_task019_result_binding(
    body: str,
    call: str,
    member: str,
    label: str,
    required_argument: str | None = None,
) -> tuple[int, str, tuple[str, ...]]:
    result, arguments, position = assigned_call(body, call, label)
    if required_argument is not None and required_argument not in arguments:
        raise AssertionError(f"{label} omits {required_argument} from {call}")
    rejected = any(
        result in condition
        and (
            f"!{result}.accepted(" in re.sub(r"\s+", "", condition)
            or f"!{result}.{member}" in re.sub(r"\s+", "", condition)
        )
        and branch_returns_non_success(statement)
        for condition, statement in fail_closed_if_branches(body[position:])
    )
    if not rejected:
        raise AssertionError(f"{label} does not reject the {call} result")
    if (
        re.search(
            rf"\*\s*{re.escape(result)}\s*\.\s*{re.escape(member)}\b",
            body[position:],
        )
        is None
    ):
        raise AssertionError(f"{label} does not carry {call}'s {member} forward")
    return position, result, split_cpp_arguments(arguments)


def normalized_cpp_expression(expression: str) -> str:
    return re.sub(r"\s+", "", expression)


def require_mint_published_definition(source: str) -> None:
    definitions = tuple(
        re.finditer(
            r"\bDurableJournalResult\s+DurableJournal::mint_published\s*"
            r"\((?P<parameters>[^;{}]*)\)\s*(?:noexcept\s*)?\{",
            source,
        )
    )
    if len(definitions) != 1:
        raise AssertionError("DurableJournal private mint sink is not unique")
    parameters = split_cpp_arguments(definitions[0].group("parameters"))
    if (
        len(parameters) != 3
        or re.search(r"\bJournalHistory\s*&&\s*[A-Za-z_]\w*\b", parameters[0]) is None
        or re.search(
            r"\bAuthorityRootCandidate\s*(?:&&\s*)?[A-Za-z_]\w*\b",
            parameters[1],
        )
        is None
        or re.search(r"\b(?:persistence|state)[A-Za-z_]*\b", parameters[2]) is None
    ):
        raise AssertionError("DurableJournal private mint sink signature drifted")


def require_mint_published_return(
    body: str,
    history_expression: str,
    root_expression: str,
    label: str,
) -> int:
    calls = tuple(re.finditer(r"\bmint_published\s*\(", body))
    arguments = call_argument_lists(body, "mint_published")
    if len(calls) != 1 or len(arguments) != 1 or len(arguments[0]) != 3:
        raise AssertionError(f"{label} does not use one private mint sink")
    start = calls[0].start()
    statement_start = (
        max(
            body.rfind(";", 0, start),
            body.rfind("{", 0, start),
            body.rfind("}", 0, start),
        )
        + 1
    )
    prefix = body[statement_start:start]
    if re.fullmatch(r"\s*return\s+", prefix) is None:
        raise AssertionError(f"{label} does not return the private mint result")
    observed = tuple(normalized_cpp_expression(item) for item in arguments[0])
    expected_history = normalized_cpp_expression(f"std::move({history_expression})")
    expected_root = normalized_cpp_expression(f"std::move({root_expression})")
    if (
        observed[0] != expected_history
        or observed[1] != expected_root
        or re.fullmatch(r"(?:[A-Za-z_]\w*|std::move\([A-Za-z_]\w*\))", observed[2])
        is None
    ):
        raise AssertionError(f"{label} mints from nonauthoritative replay state")
    return start


def require_root_replacement_binding(
    body: str,
    root_result: str,
    label: str,
) -> int:
    calls = tuple(re.finditer(r"\breplace_root\s*\(", body))
    arguments = call_argument_lists(body, "replace_root")
    if len(calls) != 1 or len(arguments) != 1 or len(arguments[0]) != 1:
        raise AssertionError(f"{label} root replacement call count drifted")
    observed = normalized_cpp_expression(arguments[0][0])
    canonical = normalized_cpp_expression(f"{root_result}.candidate->canonical_bytes()")
    if observed not in {
        canonical,
        f"std::string({canonical})",
        f"std::string{{{canonical}}}",
    }:
        raise AssertionError(f"{label} replaces a nonauthoritative root")
    return calls[0].start()


def require_task019_live_contract(source: str) -> None:
    require_task019_live_contract_without_mutants(source)

    task019_control = """
DurableJournalResult DurableJournal::recover_existing() {
    auto bytes = read_root();
    auto journal = read_journal();
    const auto parsed = parse_authority_root_candidate(bytes, history);
    if (!parsed.candidate) { return corrupt(); }
    return mint_published(std::move(history), std::move(*parsed.candidate),
                          persistence_state);
}
DurableJournalResult DurableJournal::append_and_publish() {
    const auto advanced = advance_history(std::move(history), candidate, verifier);
    if (!advanced.history) { return corrupt(); }
    append_journal(frame);
    const auto root = seal_authority_root_candidate(*advanced.history, previous);
    if (!root.candidate) { return corrupt(); }
    replace_root(root.candidate->canonical_bytes());
    return mint_published(std::move(*advanced.history),
                          std::move(*root.candidate), persistence_state);
}
DurableJournalResult DurableJournal::create_new() {
    const auto history = begin_history(genesis, verifier);
    if (!history.history) { return corrupt(); }
    const auto root = seal_authority_root_candidate(*history.history);
    if (!root.candidate) { return corrupt(); }
    create_journal(frame);
    replace_root(root.candidate->canonical_bytes());
    return mint_published(std::move(*history.history),
                          std::move(*root.candidate), persistence_state);
}
DurableJournalResult DurableJournal::mint_published(
    JournalHistory &&history, AuthorityRootCandidate &&root,
    PersistenceState persistence_state) { return DurableJournalResult{}; }
"""
    require_task019_live_contract_without_mutants(task019_control)
    typed_result_control = (
        task019_control.replace(
            "const auto parsed = parse_authority_root_candidate",
            "AuthorityRootCandidateResult parsed = parse_authority_root_candidate",
        )
        .replace(
            "const auto advanced = advance_history",
            "JournalHistoryResult advanced = advance_history",
        )
        .replace(
            "const auto history = begin_history",
            "JournalHistoryResult history = begin_history",
        )
        .replace(
            "const auto root = seal_authority_root_candidate",
            "AuthorityRootCandidateResult root = seal_authority_root_candidate",
        )
    )
    require_task019_live_contract_without_mutants(typed_result_control)
    delegated_control = """
DurableJournalResult deep_recover() {
    auto bytes = read_root();
    auto journal = read_journal();
    const auto parsed = parse_authority_root_candidate(bytes, history);
    if (!parsed.candidate) { return corrupt(); }
    return mint_published(std::move(history), std::move(*parsed.candidate),
                          persistence_state);
}
DurableJournalResult deep_append() {
    const auto advanced = advance_history(std::move(history), candidate, verifier);
    if (!advanced.history) { return corrupt(); }
    append_journal(frame);
    const auto root = seal_authority_root_candidate(*advanced.history, previous);
    if (!root.candidate) { return corrupt(); }
    replace_root(root.candidate->canonical_bytes());
    return mint_published(std::move(*advanced.history),
                          std::move(*root.candidate), persistence_state);
}
DurableJournalResult deep_create() {
    const auto history = begin_history(genesis, verifier);
    if (!history.history) { return corrupt(); }
    const auto root = seal_authority_root_candidate(*history.history);
    if (!root.candidate) { return corrupt(); }
    create_journal(frame);
    replace_root(root.candidate->canonical_bytes());
    return mint_published(std::move(*history.history),
                          std::move(*root.candidate), persistence_state);
}
DurableJournalResult DurableJournal::recover_existing() { return deep_recover(); }
DurableJournalResult DurableJournal::append_and_publish() { return deep_append(); }
DurableJournalResult DurableJournal::create_new() { return deep_create(); }
DurableJournalResult DurableJournal::mint_published(
    JournalHistory &&history, AuthorityRootCandidate &&root,
    PersistenceState persistence_state) { return DurableJournalResult{}; }
"""
    require_task019_live_contract_without_mutants(delegated_control)
    dead_parser_mutant = task019_control.replace(
        "const auto parsed = parse_authority_root_candidate(bytes, history);",
        "const auto unused = parse_authority_root_candidate(bytes, history);\n"
        "    const auto parsed = custom_decode_root(bytes, history);",
    )
    duplicate_transition_mutant = task019_control.replace(
        "const auto advanced = advance_history(std::move(history), candidate, verifier);\n"
        "    if (!advanced.history) { return corrupt(); }",
        "const auto unused_advance = advance_history(std::move(history), candidate, verifier);\n"
        "    const auto advanced = custom_advance(history, candidate);\n"
        "    if (!advanced.history) { return corrupt(); }",
    )
    duplicate_sealer_mutant = task019_control.replace(
        "const auto root = seal_authority_root_candidate(*advanced.history, previous);",
        "const auto unused_root = seal_authority_root_candidate(*advanced.history, previous);\n"
        "    const auto root = custom_seal(*advanced.history);",
        1,
    )
    delegated_dead_parser = delegated_control.replace(
        "const auto parsed = parse_authority_root_candidate(bytes, history);",
        "const auto unused = parse_authority_root_candidate(bytes, history);\n"
        "    const auto parsed = custom_decode_root(bytes, history);",
    )
    duplicate_helper = delegated_control + "\nDurableJournalResult deep_recover(int);\n"
    duplicate_helper = duplicate_helper.replace(
        "DurableJournalResult deep_recover(int);",
        "DurableJournalResult deep_recover(int) { return corrupt(); }",
    )
    typed_dead_call = typed_result_control.replace(
        "AuthorityRootCandidateResult parsed = "
        "parse_authority_root_candidate(bytes, history);",
        "AuthorityRootCandidateResult unused = "
        "parse_authority_root_candidate(bytes, history);\n"
        "    const auto parsed = custom_decode_root(bytes, history);",
        1,
    )
    typed_duplicate_call = typed_result_control.replace(
        "AuthorityRootCandidateResult parsed = "
        "parse_authority_root_candidate(bytes, history);",
        "AuthorityRootCandidateResult ignored = "
        "parse_authority_root_candidate(bytes, history);\n"
        "    AuthorityRootCandidateResult parsed = "
        "parse_authority_root_candidate(bytes, history);",
        1,
    )
    typed_unused_result = typed_result_control.replace(
        "if (!parsed.candidate) { return corrupt(); }",
        "if (false) { return corrupt(); }",
        1,
    )
    checked_but_nonauthoritative = task019_control.replace(
        "return mint_published(std::move(history), "
        "std::move(*parsed.candidate),\n"
        "                          persistence_state);",
        "observe(*parsed.candidate);\n"
        "    const auto custom = custom_decode_root(bytes, history);\n"
        "    return mint_published(std::move(history), std::move(custom),\n"
        "                          persistence_state);",
        1,
    ).replace(
        "replace_root(root.candidate->canonical_bytes());",
        "observe(*root.candidate);\n"
        "    const auto custom = custom_seal(history);\n"
        "    replace_root(custom.canonical_bytes());",
        1,
    )
    for mutant in (
        dead_parser_mutant,
        duplicate_transition_mutant,
        duplicate_sealer_mutant,
        delegated_dead_parser,
        duplicate_helper,
        typed_dead_call,
        typed_duplicate_call,
        typed_unused_result,
        checked_but_nonauthoritative,
    ):
        try:
            require_task019_live_contract_without_mutants(mutant)
        except AssertionError:
            pass
        else:
            raise AssertionError("source scanner accepted a dead TASK-019 call")


def require_task019_recovery_flow(recover: str) -> None:
    parsed_root, parsed, parse_arguments = require_task019_result_binding(
        recover,
        "parse_authority_root_candidate",
        "candidate",
        "DurableJournal recovery",
    )
    if len(parse_arguments) < 2:
        raise AssertionError("DurableJournal recovery omits replay history")
    minted = require_mint_published_return(
        recover,
        parse_arguments[1],
        f"*{parsed}.candidate",
        "DurableJournal recovery",
    )
    root_read = recover.find("read_root(")
    journal_read = recover.find("read_journal(")
    if (
        min(root_read, journal_read) < 0
        or not root_read < journal_read < parsed_root < minted
    ):
        raise AssertionError("DurableJournal recovery does not bind root-first replay")


def require_task019_append_flow(append: str) -> None:
    advanced, history_result, _ = require_task019_result_binding(
        append,
        "advance_history",
        "history",
        "DurableJournal append",
        "verifier",
    )
    sealed, root_result, _ = require_task019_result_binding(
        append,
        "seal_authority_root_candidate",
        "candidate",
        "DurableJournal append",
    )
    journal_append = append.find("append_journal(")
    root_replace = require_root_replacement_binding(
        append, root_result, "DurableJournal append"
    )
    minted = require_mint_published_return(
        append,
        f"*{history_result}.history",
        f"*{root_result}.candidate",
        "DurableJournal append",
    )
    if (
        min(journal_append, root_replace) < 0
        or not advanced < journal_append < sealed < root_replace < minted
    ):
        raise AssertionError("DurableJournal append bypasses TASK-019 ordering")


def require_task019_create_flow(create: str) -> None:
    history, history_result, _ = require_task019_result_binding(
        create,
        "begin_history",
        "history",
        "DurableJournal create",
        "verifier",
    )
    root, root_result, _ = require_task019_result_binding(
        create,
        "seal_authority_root_candidate",
        "candidate",
        "DurableJournal create",
    )
    journal_create = create.find("create_journal(")
    root_replace = require_root_replacement_binding(
        create, root_result, "DurableJournal create"
    )
    minted = require_mint_published_return(
        create,
        f"*{history_result}.history",
        f"*{root_result}.candidate",
        "DurableJournal create",
    )
    if (
        min(journal_create, root_replace) < 0
        or not history < root < journal_create < root_replace < minted
    ):
        raise AssertionError("DurableJournal create bypasses TASK-019 ordering")


def require_direct_or_unique_helper(
    source: str,
    entry: str,
    flow_validator,
) -> None:
    public_bodies = qualified_function_bodies(source, "DurableJournal", entry)
    if len(public_bodies) != 1:
        raise AssertionError("DurableJournal live TASK-019 entry shape drifted")
    public_body = public_bodies[0]
    try:
        flow_validator(public_body)
        return
    except AssertionError:
        pass

    delegation = re.fullmatch(
        r"\s*return\s+"
        r"(?:(?:[A-Za-z_]\w*)\s*(?:::|->|\.)\s*)*"
        r"(?P<helper>[A-Za-z_]\w*)\s*\([^;{}]*\)\s*;\s*",
        public_body,
    )
    if delegation is None:
        raise AssertionError("DurableJournal entry is not a thin private delegation")
    helper = delegation.group("helper")
    if helper in {"create_new", "recover_existing", "append_and_publish"}:
        raise AssertionError("DurableJournal helper is not uniquely named")
    helper_bodies = function_bodies(source, helper)
    if len(helper_bodies) != 1:
        raise AssertionError("DurableJournal private helper is not unique")
    flow_validator(helper_bodies[0])


def require_task019_live_contract_without_mutants(source: str) -> None:
    require_mint_published_definition(source)
    require_direct_or_unique_helper(
        source, "recover_existing", require_task019_recovery_flow
    )
    require_direct_or_unique_helper(
        source, "append_and_publish", require_task019_append_flow
    )
    require_direct_or_unique_helper(source, "create_new", require_task019_create_flow)


def require_compaction_prefix_binding(source: str) -> None:
    public_bodies = qualified_function_bodies(
        source, "DurableJournal", "compact_physical"
    )
    if len(public_bodies) != 1:
        raise AssertionError("DurableJournal compaction entry shape drifted")
    public = public_bodies[0]
    try:
        require_owned_compaction_prefix_flow(public)
    except AssertionError:
        delegation = re.fullmatch(
            r"\s*return\s+(?P<helper>[A-Za-z_]\w*)\s*"
            r"\(\s*std::move\s*\(\s*(?P<authority>[A-Za-z_]\w*)\s*\)\s*\)"
            r"\s*;\s*",
            public,
        )
        if delegation is None:
            raise AssertionError("DurableJournal compaction is not a thin delegation")
        helper = delegation.group("helper")
        helper_bodies = function_bodies(source, helper)
        if len(helper_bodies) != 1:
            raise AssertionError("DurableJournal compaction helper is not unique")
        require_owned_compaction_prefix_flow(helper_bodies[0])

    control = """
DurableJournalResult DurableJournal::compact_physical(
    PublishedJournal &&authority) {
    return compact_owned_prefix(std::move(authority));
}
DurableJournalResult compact_owned_prefix(PublishedJournal &&authority) {
    return replace_journal(authority.impl_->committed_prefix_bytes);
}
"""
    if source != control:
        require_compaction_prefix_binding(control)
    mutant = """
DurableJournalResult DurableJournal::compact_physical(
    PublishedJournal &&authority) {
    std::string committed_prefix_bytes;
    for (const auto &record : authority.impl_->history.records()) {
        committed_prefix_bytes += record.canonical_bytes();
        committed_prefix_bytes.push_back('\n');
    }
    return replace_journal(committed_prefix_bytes);
}
"""
    if source != mutant:
        try:
            require_compaction_prefix_binding(mutant)
        except AssertionError:
            pass
        else:
            raise AssertionError("compaction scanner accepted record reserialization")
    transformed_prefix_mutant = """
DurableJournalResult DurableJournal::compact_physical(
    PublishedJournal &&authority) {
    return replace_journal(
        rebuild_from_records(authority.impl_->committed_prefix_bytes));
}
"""
    if source != transformed_prefix_mutant:
        try:
            require_compaction_prefix_binding(transformed_prefix_mutant)
        except AssertionError:
            pass
        else:
            raise AssertionError("compaction scanner accepted a transformed prefix")


def require_directory_lineage_fence_registry_without_mutants(source: str) -> None:
    if (
        re.search(
            r"\bstd::unordered_map\s*<\s*std::string\s*,\s*"
            r"IdentityFenceToken\s*>\s*"
            r"fenced_identities\s*;",
            source,
        )
        is None
    ):
        raise AssertionError("process fence registry does not retain lineage state")

    for function in ("fence_identity", "identity_fence", "clear_identity_fence"):
        bodies = function_bodies(source, function)
        if len(bodies) != 1:
            raise AssertionError(f"process fence {function} definition drifted")
        if "directory_identity(identity)" not in compact_cpp(bodies[0]):
            raise AssertionError(f"process fence {function} is not lineage keyed")

    fence_body = compact_cpp(function_bodies(source, "fence_identity")[0])
    if (
        "insert_or_assign(std::string(directory_identity(identity)),token)"
        not in fence_body
    ):
        raise AssertionError("process fence does not retain the lineage epoch")


def require_directory_lineage_fence_registry(source: str) -> None:
    require_directory_lineage_fence_registry_without_mutants(source)
    for function in ("fence_identity", "identity_fence", "clear_identity_fence"):
        mutant = replace_unique_function_body(
            source,
            function,
            lambda body: body.replace("directory_identity(identity)", "identity", 1),
        )
        try:
            require_directory_lineage_fence_registry_without_mutants(mutant)
        except AssertionError:
            pass
        else:
            raise AssertionError(
                f"process fence accepted full-identity {function} lookup"
            )


def require_final_authority_revalidation_without_mutants(source: str) -> None:
    definitions = tuple(
        re.finditer(
            r"\bbool\s+revalidate_authority\s*" r"\((?P<parameters>[^;{}]*)\)\s*\{",
            source,
        )
    )
    bodies = bool_function_bodies(source, "revalidate_authority")
    if len(definitions) != 1 or len(bodies) != 1:
        raise AssertionError("final authority revalidation helper drifted")

    parameters = split_cpp_arguments(definitions[0].group("parameters"))
    if len(parameters) != 6:
        raise AssertionError("final authority revalidation signature drifted")
    parameter_patterns = (
        r"std::string_view\s+(?P<name>[A-Za-z_]\w*)",
        r"std::string_view\s+(?P<name>[A-Za-z_]\w*)",
        r"std::string_view\s+(?P<name>[A-Za-z_]\w*)",
        r"bool\s+(?P<name>[A-Za-z_]\w*)",
        r"bool\s+(?P<name>[A-Za-z_]\w*)",
        r"std::size_t\s+(?P<name>[A-Za-z_]\w*)",
    )
    parameter_names: list[str] = []
    for parameter, pattern in zip(parameters, parameter_patterns, strict=True):
        match = re.fullmatch(pattern, parameter.strip())
        if match is None:
            raise AssertionError("final authority revalidation signature drifted")
        parameter_names.append(match.group("name"))
    (
        expected_identity,
        expected_root,
        expected_journal,
        expected_journal_stage,
        expected_root_stage,
        journal_read_limit,
    ) = parameter_names

    body = bodies[0]
    branches = fail_closed_if_branches(body)
    if not any(
        compact_cpp(condition) == "!lock_held"
        and compact_cpp(statement) == "returnfalse;"
        for condition, statement in branches
    ):
        raise AssertionError("final authority revalidation is not lock-bound")

    def assigned_member_call(call: str) -> tuple[str, int]:
        matches = tuple(
            re.finditer(
                r"\b(?:const\s+)?auto\s+(?P<result>[A-Za-z_]\w*)\s*=\s*"
                rf"(?:[A-Za-z_]\w*\s*(?:->|\.)\s*)?{re.escape(call)}\s*\(",
                body,
            )
        )
        if len(matches) != 1:
            raise AssertionError(
                f"final authority revalidation does not call {call} exactly once"
            )
        return matches[0].group("result"), matches[0].start()

    identity, identity_position = assigned_member_call("authority_identity")
    fixed, fixed_position = assigned_member_call("inspect_fixed_namespace")
    root, root_position = assigned_member_call("read_root")
    journal, journal_position = assigned_member_call("read_journal")
    if not identity_position < fixed_position < root_position < journal_position:
        raise AssertionError("final authority revalidation order drifted")

    root_arguments = call_argument_lists(body, "read_root")
    journal_arguments = call_argument_lists(body, "read_journal")
    if (
        len(root_arguments) != 1
        or len(root_arguments[0]) != 1
        or normalized_cpp_expression(root_arguments[0][0]) != "max_journal_input_bytes"
        or len(journal_arguments) != 1
        or len(journal_arguments[0]) != 1
        or normalized_cpp_expression(journal_arguments[0][0]) != journal_read_limit
    ):
        raise AssertionError("final authority revalidation read bounds drifted")

    required_failure_predicates = (
        (
            identity_position,
            (
                f"!{identity}.result.succeeded()||"
                f"{identity}.identity!={expected_identity}"
            ),
            "composite identity",
        ),
        (
            fixed_position,
            (
                f"!{fixed}.result.succeeded()||"
                f"!{fixed}.journal_present||"
                f"!{fixed}.root_present||"
                f"!{fixed}.lock_present||"
                f"{fixed}.journal_stage_present!={expected_journal_stage}||"
                f"{fixed}.root_stage_present!={expected_root_stage}"
            ),
            "fixed namespace",
        ),
        (
            root_position,
            (
                f"!{root}.result.succeeded()||{root}.truncated||"
                f"{root}.bytes!={expected_root}"
            ),
            "authority root",
        ),
    )
    for position, expected_condition, label in required_failure_predicates:
        if not any(
            compact_cpp(condition) == expected_condition
            and compact_cpp(statement) == "returnfalse;"
            for condition, statement in fail_closed_if_branches(body[position:])
        ):
            raise AssertionError(f"final authority revalidation does not bind {label}")

    final_returns = re.findall(
        r"\breturn\b(?P<value>[^;]*);", body[journal_position:], re.DOTALL
    )
    expected_return = (
        f"{journal}.result.succeeded()&&!{journal}.truncated&&"
        f"{journal}.bytes=={expected_journal}"
    )
    if (
        len(final_returns) != 1
        or normalized_cpp_expression(final_returns[0]) != expected_return
    ):
        raise AssertionError("final authority revalidation does not bind journal bytes")

    flow_contracts = {
        "create_new": (
            (
                "identity",
                "root_result.candidate->canonical_bytes()",
                "framed",
                "false",
                "false",
                "journal_read_limit",
            ),
            ("create_journal", "replace_root"),
        ),
        "recover_existing": (
            (
                "identity",
                "parsed.candidate->canonical_bytes()",
                "replay.persistence.committed_prefix_bytes",
                "fixed.journal_stage_present",
                "fixed.root_stage_present",
                "journal_read_limit",
            ),
            ("read_root", "read_journal", "truncate_journal"),
        ),
        "append_and_publish": (
            (
                "identity",
                "root_result.candidate->canonical_bytes()",
                "expected_journal",
                "authority_state->journal_stage_present",
                "false",
                "journal_read_limit",
            ),
            ("append_journal", "replace_root"),
        ),
        "compact_physical": (
            (
                "identity",
                "authority_state->root.canonical_bytes()",
                "authority_state->committed_prefix_bytes",
                "false",
                "authority_state->root_stage_present",
                "journal_read_limit",
            ),
            ("replace_journal",),
        ),
    }
    for entry, (expected_arguments, predecessors) in flow_contracts.items():
        public_bodies = qualified_function_bodies(source, "DurableJournal", entry)
        if len(public_bodies) != 1:
            raise AssertionError(f"DurableJournal {entry} entry shape drifted")
        flow = public_bodies[0]
        revalidation_arguments = call_argument_lists(flow, "revalidate_authority")
        if len(revalidation_arguments) != 1:
            raise AssertionError(
                f"DurableJournal {entry} omits final authority revalidation"
            )
        observed_arguments = tuple(
            normalized_cpp_expression(argument)
            for argument in revalidation_arguments[0]
        )
        if observed_arguments != expected_arguments:
            raise AssertionError(
                f"DurableJournal {entry} revalidates stale authority state"
            )
        revalidation_position = flow.find("revalidate_authority(")
        mint_position = flow.find("mint_published(")
        predecessor_positions = tuple(flow.find(f"{call}(") for call in predecessors)
        if (
            min(predecessor_positions) < 0
            or mint_position < 0
            or not max(predecessor_positions) < revalidation_position < mint_position
        ):
            raise AssertionError(
                f"DurableJournal {entry} final revalidation order drifted"
            )
        if not any(
            "!impl_->revalidate_authority(" in compact_cpp(condition)
            and branch_returns_non_success(statement)
            for condition, statement in fail_closed_if_branches(flow)
        ):
            raise AssertionError(
                f"DurableJournal {entry} ignores final revalidation failure"
            )


def require_final_authority_revalidation(source: str) -> None:
    require_final_authority_revalidation_without_mutants(source)
    bypass_mutant = replace_unique_bool_function_body(
        source, "revalidate_authority", lambda _body: " return true; "
    )
    try:
        require_final_authority_revalidation_without_mutants(bypass_mutant)
    except AssertionError:
        pass
    else:
        raise AssertionError(
            "final authority scanner accepted an unconditional success helper"
        )
    helper_mutations = (
        (
            "unlocked",
            lambda body: body.replace("if (!lock_held)", "if (false)", 1),
        ),
        (
            "identity-blind",
            lambda body: body.replace(
                "identity.identity != expected_identity", "false", 1
            ),
        ),
        (
            "stage-blind",
            lambda body: body.replace(
                "fixed.journal_stage_present != expected_journal_stage",
                "false",
                1,
            ),
        ),
        (
            "root-blind",
            lambda body: body.replace("root.bytes != expected_root", "false", 1),
        ),
        (
            "dead-root-check",
            lambda body: body.replace(
                "root.bytes != expected_root",
                "(false && root.bytes != expected_root)",
                1,
            ),
        ),
        (
            "journal-blind",
            lambda body: body.replace("journal.bytes == expected_journal", "true", 1),
        ),
        (
            "inflated-read",
            lambda body: body.replace(
                "read_journal(journal_read_limit)",
                "read_journal(max_journal_input_bytes)",
                1,
            ),
        ),
    )
    for label, transform in helper_mutations:
        mutant = replace_unique_bool_function_body(
            source, "revalidate_authority", transform
        )
        try:
            require_final_authority_revalidation_without_mutants(mutant)
        except AssertionError:
            pass
        else:
            raise AssertionError(f"final authority scanner accepted its {label} mutant")
    for entry in (
        "create_new",
        "recover_existing",
        "append_and_publish",
        "compact_physical",
    ):
        bypassed_flow = replace_unique_function_body(
            source,
            entry,
            lambda body: body.replace(
                "!impl_->revalidate_authority(",
                "false && impl_->revalidate_authority(",
                1,
            ),
        )
        try:
            require_final_authority_revalidation_without_mutants(bypassed_flow)
        except AssertionError:
            pass
        else:
            raise AssertionError(
                f"final authority scanner accepted a bypassed {entry} check"
            )


def require_owned_compaction_prefix_flow(body: str) -> None:
    calls = call_argument_lists(body, "replace_journal")
    if len(calls) != 1 or len(calls[0]) != 1:
        raise AssertionError("compaction does not replace the journal once")
    argument = normalized_cpp_expression(calls[0][0])
    string_copy = re.fullmatch(r"std::string\((?P<argument>.*)\)", argument)
    if string_copy is not None:
        argument = string_copy.group("argument")
    owned_prefix = re.fullmatch(
        r"(?:authority|published|token)[A-Za-z_0-9]*"
        r"(?:(?:\.|->)[A-Za-z_]\w*)*"
        r"(?:\.|->)(?:committed_prefix_bytes|authorized_prefix_bytes|"
        r"committed_prefix|authorized_prefix)",
        argument,
    )
    if owned_prefix is None:
        raise AssertionError("compaction does not pass the exact owned prefix")


def require_tokens(source: str, tokens: tuple[str, ...], label: str) -> None:
    missing = [token for token in tokens if token not in source]
    if missing:
        raise AssertionError(f"{label} omitted {missing}")


def require_absent_tokens(source: str, tokens: tuple[str, ...], label: str) -> None:
    present = [token for token in tokens if token in source]
    if present:
        raise AssertionError(f"{label} uses forbidden {present}")


def require_build_contract() -> None:
    windows_root = PureWindowsPath("D:/a/lemonade/lemonade")
    windows_runner = windows_root / (
        "test/residency/recovery/test_durable_journal_public_seam.py"
    )
    expected_runner = "test/residency/recovery/test_durable_journal_public_seam.py"
    if cmake_relative_path(windows_runner, windows_root) != expected_runner:
        raise AssertionError("CMake runner path is host-dependent")
    if str(windows_runner.relative_to(windows_root)) == expected_runner:
        raise AssertionError("CMake Windows-path control is ineffective")

    cmake = CMAKE.read_text(encoding="utf-8")
    cmake_code = strip_cmake_comments(cmake)
    require_tokens(
        cmake_code,
        (
            "test_durable_residency_journal",
            "ResidencyDurableJournal",
            "add_cpp_ci_test",
            "durable_file_adapter_posix.cpp",
            "durable_file_adapter_windows.cpp",
            "durable_file_adapter_macos.cpp",
            TESTING_DEFINITION,
        ),
        "CMake durable-journal test contract",
    )
    block_start = cmake_code.find("test_durable_residency_journal")
    block = cmake_code[max(0, block_start - 1500) : block_start + 5000]
    if "BUILD_TESTING" not in block or "CI ON" not in block:
        raise AssertionError("durable journal C++ seam is not an explicit CI test")
    if not any(token in block for token in ("WIN32", "APPLE", "UNIX")):
        raise AssertionError("durable journal adapters are not platform-selected")
    testing_definition = re.compile(
        rf"target_compile_definitions\s*\(\s*test_durable_residency_journal\s+PRIVATE\s+{TESTING_DEFINITION}\s*\)",
        re.DOTALL,
    )
    if testing_definition.search(block) is None:
        raise AssertionError("durable test injection is not target-confined")
    overlay_testing_definition = re.compile(
        rf"target_compile_definitions\s*\(\s*test_durable_local_overlay\s+"
        rf"PRIVATE\s+{TESTING_DEFINITION}\s*\)",
        re.DOTALL,
    )
    if overlay_testing_definition.search(cmake_code) is None:
        raise AssertionError("overlay durability test injection is not target-confined")
    if cmake_code.count(TESTING_DEFINITION) != 2:
        raise AssertionError("durable test injection escaped its test targets")
    quoted_hash_control = """
if(TRUE)
    set(version_pattern "^#define CLI11_VERSION ")
endif()
# if(FALSE)
#     add_executable(test_durable_residency_journal narrated.cpp)
# endif()
"""
    stripped_hash_control = strip_cmake_comments_preserve_arguments(quoted_hash_control)
    if (
        '"^#define CLI11_VERSION "' not in stripped_hash_control
        or len(cmake_command_spans(stripped_hash_control, "if")) != 1
        or len(cmake_command_spans(stripped_hash_control, "endif")) != 1
        or cmake_command_spans(stripped_hash_control, "add_executable")
    ):
        raise AssertionError("CMake quoted hashes and comments are misclassified")
    require_durable_target_sources(cmake)
    require_private_authority_gate_contract(cmake)
    require_durable_cmake_invocation(cmake)
    require_local_overlay_ci_registration(
        cmake,
        (
            (
                "BUILD_TESTING",
                "AND",
                "EXISTS",
                "${CMAKE_CURRENT_SOURCE_DIR}/test/residency/recovery/durable_journal_public_seam.cpp",
            ),
            (
                "EXISTS",
                "${CMAKE_CURRENT_SOURCE_DIR}/test/cpp/test_durable_local_overlay.cpp",
            ),
        ),
    )

    overlay_ci_control = """
add_cpp_ci_test(
    ResidencyDurableLocalOverlay
    CI ON
    COMMAND test_durable_local_overlay
)
"""
    require_local_overlay_ci_registration(overlay_ci_control)
    overlay_ci_mutants = (
        "",
        overlay_ci_control.replace("CI ON", "CI OFF"),
        overlay_ci_control
        + "\nset_tests_properties(ResidencyDurableLocalOverlay PROPERTIES "
        "DISABLED TRUE)\n",
        "if(FALSE)\n" + overlay_ci_control + "\nendif()\n",
        "function(inert_overlay_test)\n" + overlay_ci_control + "\nendfunction()\n",
        "return()\n" + overlay_ci_control,
    )
    for mutant in overlay_ci_mutants:
        try:
            require_local_overlay_ci_registration(mutant)
        except AssertionError:
            pass
        else:
            raise AssertionError("local-overlay CTest registration mutant was accepted")

    private_gate_control = """
if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/residency/recovery/durable_journal_public_seam.cpp")
add_library(test_durable_residency_private_authority_gate OBJECT
    test/residency/recovery/durable_journal_private_authority_gate.cpp
)
target_include_directories(test_durable_residency_private_authority_gate PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cpp/server/residency
)
add_dependencies(test_durable_residency_journal
        test_durable_residency_private_authority_gate
)
add_cpp_ci_test(
        ResidencyDurableJournal
)
endif()
"""
    require_private_authority_gate_contract(private_gate_control)
    private_gate_mutants = (
        private_gate_control.replace(" OBJECT\n", " STATIC\n"),
        private_gate_control.replace(
            "test_durable_residency_private_authority_gate\n)\nadd_cpp_ci_test",
            "unrelated_gate\n)\nadd_cpp_ci_test",
        ),
        private_gate_control.replace(
            "add_dependencies(test_durable_residency_journal",
            "target_compile_definitions("
            "test_durable_residency_private_authority_gate PRIVATE "
            f"{TESTING_DEFINITION})\n"
            "add_dependencies(test_durable_residency_journal",
        ),
        private_gate_control.replace("BUILD_TESTING AND EXISTS", "FALSE AND EXISTS", 1),
        private_gate_control.replace(
            "BUILD_TESTING AND EXISTS",
            "BUILD_TESTING AND TASK020_GATE_DISABLED AND EXISTS",
        ),
        "function(task020_disabled)\n" + private_gate_control + "\nendfunction()\n",
        "return()\n" + private_gate_control,
        "FUNCTION(task020_disabled)\n" + private_gate_control + "\nENDFUNCTION()\n",
        "RETURN()\n" + private_gate_control,
    )
    for mutant in private_gate_mutants:
        try:
            require_private_authority_gate_contract(mutant)
        except AssertionError:
            pass
        else:
            raise AssertionError("private-authority build-gate mutant was accepted")

    workflow = WORKFLOW.read_text(encoding="utf-8")
    windows_job_source = extract_yaml_job(
        workflow,
        "build-lemonade-server-installer",
        "build-lemonade-desktop-installer",
    )
    build_command = re.compile(
        r"(?m)^\s*cmake\s+--build\s+build\s+--config\s+Release\s+--target\s+test_durable_residency_journal\s+test_durable_local_overlay\s*$"
    )
    test_command = re.compile(
        r'(?m)^\s*ctest\s+--test-dir\s+build\s+-C\s+Release\s+--no-tests=error\s+--output-on-failure\s+-R\s+"\^\(ResidencyDurableJournal\|ResidencyDurableLocalOverlay\)\$"\s*$'
    )
    require_live_windows_workflow_commands(
        windows_job_source, build_command, test_command
    )
    failure_propagation_control = r"""
  build-lemonade-server-installer:
    runs-on: windows-latest
    steps:
      - name: Durable build
        shell: pwsh
        run: |
          $ErrorActionPreference = "Stop"
          if (-not (Test-Path build)) { throw "build directory missing" }
          cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay
          if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
          }
      - name: Durable test
        shell: PowerShell
        run: |
          ctest --test-dir build -C Release --no-tests=error --output-on-failure -R "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
          $testExitCode = $LASTEXITCODE
          if ($testExitCode -ne 0) {
            exit $testExitCode
          }
"""
    require_live_windows_workflow_commands(
        failure_propagation_control, build_command, test_command
    )
    omitted_windows_target_or_test_mutants = (
        failure_propagation_control.replace(
            "test_durable_residency_journal test_durable_local_overlay",
            "test_durable_residency_journal",
        ),
        failure_propagation_control.replace(
            "test_durable_residency_journal test_durable_local_overlay",
            "test_durable_local_overlay",
        ),
        failure_propagation_control.replace(
            "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$",
            "^ResidencyDurableJournal$",
        ),
        failure_propagation_control.replace(
            "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$",
            "^ResidencyDurableLocalOverlay$",
        ),
    )
    for mutant in omitted_windows_target_or_test_mutants:
        try:
            require_live_windows_workflow_commands(mutant, build_command, test_command)
        except AssertionError:
            pass
        else:
            raise AssertionError(
                "workflow scanner accepted an incomplete durable test pair"
            )

    cmake_narration = """
# test_durable_residency_journal ResidencyDurableJournal add_cpp_ci_test CI ON LEMONADE_RESIDENCY_DURABLE_TESTING
message("durable_file_adapter_windows.cpp durable_file_adapter_macos.cpp")
#[[
test_durable_residency_journal ResidencyDurableJournal add_cpp_ci_test CI ON
durable_file_adapter_posix.cpp durable_file_adapter_windows.cpp durable_file_adapter_macos.cpp
]]
set(narrated [[
test_durable_residency_journal ResidencyDurableJournal add_cpp_ci_test CI ON
durable_file_adapter_posix.cpp durable_file_adapter_windows.cpp durable_file_adapter_macos.cpp
]])
"""
    stripped_cmake_narration = strip_cmake_comments(cmake_narration)
    if any(
        token in stripped_cmake_narration
        for token in (
            "ResidencyDurableJournal",
            "test_durable_residency_journal",
            "durable_file_adapter_windows.cpp",
        )
    ):
        raise AssertionError("CMake scanner accepted narrated test wiring")
    yaml_narration = """
# test_durable_residency_journal test_durable_local_overlay ctest \"^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$\"
name: "test_durable_residency_journal test_durable_local_overlay ctest ^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
run: echo "cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay ctest --test-dir build -C Release --no-tests=error --output-on-failure -R ^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
run: |
  Write-Host 'cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay'
  Write-Host 'ctest --test-dir build -C Release --no-tests=error --output-on-failure -R "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"'
env:
  NARRATION: |
    cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay
    ctest --test-dir build -C Release --no-tests=error --output-on-failure -R "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
run: |
  $narration = @'
  cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay
  ctest --test-dir build -C Release --no-tests=error --output-on-failure -R "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
  '@
  cat <<'TASK020'
  cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay
  ctest --test-dir build -C Release --no-tests=error --output-on-failure -R "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
  TASK020
"""
    stripped_narration = strip_shell_heredocs(extract_yaml_run_bodies(yaml_narration))
    if build_command.search(stripped_narration) or test_command.search(
        stripped_narration
    ):
        raise AssertionError("workflow scanner accepted narrated test wiring")
    skipped_step_mutant = r"""
  build-lemonade-server-installer:
    runs-on: windows-latest
    steps:
      - name: Narrated durable test
        if: ${{ false }}
        continue-on-error: true
        run: |
          cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay
          ctest --test-dir build -C Release --no-tests=error --output-on-failure -R "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
"""
    disabled_job_mutant = r"""
  build-lemonade-server-installer:
    if: ${{ false }}
    continue-on-error: true
    runs-on: windows-latest
    steps:
      - name: Durable test
        run: |
          cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay
          ctest --test-dir build -C Release --no-tests=error --output-on-failure -R "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
"""
    guarded_commands_mutant = r"""
  build-lemonade-server-installer:
    runs-on: windows-latest
    steps:
      - name: Guarded durable test
        run: |
          if ($false) {
            cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay
            ctest --test-dir build -C Release --no-tests=error --output-on-failure -R "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
          }
"""
    early_control_transfer_mutant = r"""
  build-lemonade-server-installer:
    runs-on: windows-latest
    steps:
      - name: Bypassed durable build
        run: |
          exit 0
          cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay
      - name: Bypassed durable test
        run: |
          return
          ctest --test-dir build -C Release --no-tests=error --output-on-failure -R "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
"""
    conditional_control_transfer_mutant = r"""
  build-lemonade-server-installer:
    runs-on: windows-latest
    steps:
      - name: Bypassed durable build
        run: |
          if ($true) { return; }
          cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
      - name: Bypassed durable test
        run: |
          if ($true) { return; }
          ctest --test-dir build -C Release --no-tests=error --output-on-failure -R "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
"""
    linux_runner_mutant = r"""
  build-lemonade-server-installer:
    runs-on: ubuntu-latest
    steps:
      - name: Durable build
        run: |
          cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay
      - name: Durable test
        run: |
          ctest --test-dir build -C Release --no-tests=error --output-on-failure -R "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
"""
    missing_no_tests_error_mutant = r"""
  build-lemonade-server-installer:
    runs-on: windows-latest
    steps:
      - name: Durable build
        run: |
          cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay
      - name: Durable test
        run: |
          ctest --test-dir build -C Release --output-on-failure -R "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
"""
    missing_exit_propagation_mutant = r"""
  build-lemonade-server-installer:
    runs-on: windows-latest
    steps:
      - name: Durable build
        shell: PowerShell
        run: |
          cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay
      - name: Durable test
        shell: PowerShell
        run: |
          ctest --test-dir build -C Release --no-tests=error --output-on-failure -R "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
"""
    dead_exit_propagation_mutant = r"""
  build-lemonade-server-installer:
    runs-on: windows-latest
    steps:
      - name: Durable build
        shell: PowerShell
        run: |
          cmake --build build --config Release --target test_durable_residency_journal test_durable_local_overlay
          if ($false) {
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
          }
      - name: Durable test
        shell: PowerShell
        run: |
          ctest --test-dir build -C Release --no-tests=error --output-on-failure -R "^(ResidencyDurableJournal|ResidencyDurableLocalOverlay)$"
          if ($false) {
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
          }
"""
    for mutant in (
        skipped_step_mutant,
        disabled_job_mutant,
        guarded_commands_mutant,
        early_control_transfer_mutant,
        conditional_control_transfer_mutant,
        linux_runner_mutant,
        missing_no_tests_error_mutant,
        missing_exit_propagation_mutant,
        dead_exit_propagation_mutant,
    ):
        try:
            require_live_windows_workflow_commands(mutant, build_command, test_command)
        except AssertionError:
            pass
        else:
            raise AssertionError("workflow scanner accepted a disabled durable test")
    commented_job_mutant = """
#   build-lemonade-server-installer:
#     run: cmake --build build --config Release --target test_durable_residency_journal
  build-lemonade-desktop-installer:
    runs-on: windows-latest
"""
    try:
        extract_yaml_job(
            commented_job_mutant,
            "build-lemonade-server-installer",
            "build-lemonade-desktop-installer",
        )
    except AssertionError:
        pass
    else:
        raise AssertionError("workflow scanner accepted a commented job")

    direct_binary_mutant = """
if(BUILD_TESTING)
    add_executable(test_durable_residency_journal fixture.cpp)
    add_cpp_ci_test(
        ResidencyDurableJournal
        CI ON
        COMMAND test_durable_residency_journal
        DEPENDS test_durable_residency_journal)
endif()
"""
    incomplete_wrapper_mutant = f"""
if(BUILD_TESTING)
    add_executable(test_durable_residency_journal fixture.cpp)
    add_cpp_ci_test(
        ResidencyDurableJournal
        CI ON
        COMMAND ${{Python3_EXECUTABLE}} {RUNNER}
            --existing-executable $<TARGET_FILE:test_durable_residency_journal>
        DEPENDS test_durable_residency_journal)
endif()
"""
    inert_command_prefix_mutant = f"""
if(BUILD_TESTING)
    add_executable(test_durable_residency_journal fixture.cpp)
    add_cpp_ci_test(
        ResidencyDurableJournal
        CI ON
        COMMAND ${{CMAKE_COMMAND}} -E echo
            ${{Python3_EXECUTABLE}} {RUNNER.relative_to(REPO_ROOT)}
            --existing-executable $<TARGET_FILE:test_durable_residency_journal>
            --compiler ${{CMAKE_CXX_COMPILER}}
        DEPENDS test_durable_residency_journal)
endif()
"""
    inactive_enclosure_mutant = f"""
if(BUILD_TESTING AND FALSE)
    add_executable(test_durable_residency_journal fixture.cpp)
    add_cpp_ci_test(
        ResidencyDurableJournal
        CI ON
        COMMAND ${{Python3_EXECUTABLE}} {RUNNER.relative_to(REPO_ROOT)}
            --existing-executable $<TARGET_FILE:test_durable_residency_journal>
            --compiler ${{CMAKE_CXX_COMPILER}}
        DEPENDS test_durable_residency_journal)
endif()
"""
    masked_test_mutant = f"""
if(BUILD_TESTING)
    add_executable(test_durable_residency_journal fixture.cpp)
    add_cpp_ci_test(
        ResidencyDurableJournal
        CI ON
        COMMAND ${{Python3_EXECUTABLE}} {RUNNER.relative_to(REPO_ROOT)}
            --existing-executable $<TARGET_FILE:test_durable_residency_journal>
            --compiler ${{CMAKE_CXX_COMPILER}}
        DEPENDS test_durable_residency_journal)
    set_tests_properties(ResidencyDurableJournal PROPERTIES DISABLED TRUE)
endif()
"""
    for mutant in (
        direct_binary_mutant,
        incomplete_wrapper_mutant,
        inert_command_prefix_mutant,
        inactive_enclosure_mutant,
        masked_test_mutant,
    ):
        try:
            require_durable_cmake_invocation(mutant)
        except AssertionError:
            pass
        else:
            raise AssertionError("CTest can bypass the frozen Python race wrapper")
    target_source_control = """
if(WIN32)
    set(_TASK020_PLATFORM_SOURCE
        src/cpp/server/residency/durable_file_adapter_windows.cpp)
elseif(APPLE)
    set(_TASK020_PLATFORM_SOURCE
        src/cpp/server/residency/durable_file_adapter_macos.cpp)
else()
    set(_TASK020_PLATFORM_SOURCE
        src/cpp/server/residency/durable_file_adapter_posix.cpp)
endif()
add_executable(test_durable_residency_journal
    test/residency/recovery/durable_journal_public_seam.cpp
    test/residency/recovery/journal_persistence_test_support.cpp
    src/cpp/server/residency/durable_journal.cpp
    src/cpp/server/residency/durable_file_adapter_common.cpp
    src/cpp/server/residency/journal.cpp
    ${_TASK020_PLATFORM_SOURCE})
target_include_directories(test_durable_residency_journal PRIVATE src/cpp/include)
target_link_libraries(test_durable_residency_journal PRIVATE lemonade-digest-crypto)
target_compile_definitions(test_durable_residency_journal PRIVATE
    LEMONADE_RESIDENCY_DURABLE_TESTING)
"""
    require_durable_target_sources(target_source_control)
    unconditional_platform_mutant = re.sub(
        r"if\(WIN32\).*?endif\(\)",
        """set(_TASK020_PLATFORM_SOURCE
    src/cpp/server/residency/durable_file_adapter_windows.cpp)
set(_TASK020_PLATFORM_SOURCE
    src/cpp/server/residency/durable_file_adapter_macos.cpp)
set(_TASK020_PLATFORM_SOURCE
    src/cpp/server/residency/durable_file_adapter_posix.cpp)""",
        target_source_control,
        count=1,
        flags=re.DOTALL,
    )
    try:
        require_durable_target_sources(unconditional_platform_mutant)
    except AssertionError:
        pass
    else:
        raise AssertionError("CMake platform sources are selected sequentially")
    dummy_target_mutant = target_source_control.replace(
        "test/residency/recovery/durable_journal_public_seam.cpp",
        "test/residency/recovery/fixture.cpp",
    )
    try:
        require_durable_target_sources(dummy_target_mutant)
    except AssertionError:
        pass
    else:
        raise AssertionError("CMake target bypassed the frozen semantic fixture")
    narrated_wrapper_mutant = f"""
message("add_cpp_ci_test(ResidencyDurableJournal CI ON COMMAND "
        "${{Python3_EXECUTABLE}} {RUNNER.relative_to(REPO_ROOT)} "
        "--existing-executable $<TARGET_FILE:test_durable_residency_journal> "
        "--compiler ${{CMAKE_CXX_COMPILER}} DEPENDS "
        "test_durable_residency_journal)")
add_cpp_ci_test(
    ResidencyDurableJournal
    CI ON
    COMMAND test_durable_residency_journal
    DEPENDS test_durable_residency_journal)
"""
    try:
        require_durable_cmake_invocation(narrated_wrapper_mutant)
    except AssertionError:
        pass
    else:
        raise AssertionError("CTest scanner accepted a narrated wrapper call")


def cmake_tokens(body: str) -> tuple[str, ...]:
    return tuple(
        token[1:-1] if token[:1] == token[-1:] and token[:1] in {'"', "'"} else token
        for token in re.findall(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\S+', body)
    )


def require_durable_target_sources(source: str) -> None:
    code = strip_cmake_comments_preserve_arguments(source)
    target_calls = [
        body
        for body in cmake_command_arguments(code, "add_executable")
        if cmake_tokens(body)[:1] == ("test_durable_residency_journal",)
    ]
    if len(target_calls) != 1:
        raise AssertionError("durable semantic executable target is unavailable")
    arguments = cmake_tokens(target_calls[0])
    required = {
        "test/residency/recovery/durable_journal_public_seam.cpp",
        "test/residency/recovery/journal_persistence_test_support.cpp",
        "src/cpp/server/residency/durable_journal.cpp",
        "src/cpp/server/residency/durable_file_adapter_common.cpp",
        "src/cpp/server/residency/journal.cpp",
    }
    set_calls = [cmake_tokens(body) for body in cmake_command_arguments(code, "set")]
    variables = {
        match.group("name")
        for argument in arguments
        if (match := re.fullmatch(r"\$\{(?P<name>[A-Za-z_][A-Za-z0-9_]*)\}", argument))
    }
    conditional_events = sorted(
        (
            start,
            command,
            cmake_tokens(body),
        )
        for command in ("if", "elseif", "else", "endif", "set")
        for body, start, _end in cmake_command_spans(code, command)
    )
    conditional_stack: list[tuple[int, str]] = []
    branch_sequences: dict[int, list[str]] = {}
    positioned_sets: list[tuple[tuple[str, ...], tuple[int, str] | None]] = []
    next_chain = 0
    for _position, command, tokens in conditional_events:
        if command == "if":
            branch = "".join(tokens).upper()
            branch_sequences[next_chain] = [branch]
            conditional_stack.append((next_chain, branch))
            next_chain += 1
        elif command == "elseif":
            if not conditional_stack:
                raise AssertionError("CMake platform selection is unbalanced")
            chain, _previous = conditional_stack[-1]
            branch = "".join(tokens).upper()
            branch_sequences[chain].append(branch)
            conditional_stack[-1] = (chain, branch)
        elif command == "else":
            if not conditional_stack:
                raise AssertionError("CMake platform selection is unbalanced")
            chain, _previous = conditional_stack[-1]
            branch_sequences[chain].append("ELSE")
            conditional_stack[-1] = (chain, "ELSE")
        elif command == "endif":
            if not conditional_stack:
                raise AssertionError("CMake platform selection is unbalanced")
            conditional_stack.pop()
        else:
            positioned_sets.append(
                (tokens, conditional_stack[-1] if conditional_stack else None)
            )
    if conditional_stack:
        raise AssertionError("CMake platform selection is unbalanced")

    def repo_relative(token: str) -> str:
        return re.sub(r"^\$\{CMAKE_CURRENT_SOURCE_DIR\}/", "", token)

    resolved_arguments = {repo_relative(token) for token in arguments}
    for variable in variables:
        resolved_arguments.update(
            repo_relative(token)
            for tokens in set_calls
            if tokens[:1] == (variable,)
            for token in tokens[1:]
        )
    if not required.issubset(resolved_arguments) or any(
        token.endswith(("fixture.cpp", "dummy.cpp")) for token in resolved_arguments
    ):
        raise AssertionError("durable semantic executable omits frozen sources")
    selected: list[str] = []
    platform_files = {
        "durable_file_adapter_posix.cpp",
        "durable_file_adapter_windows.cpp",
        "durable_file_adapter_macos.cpp",
    }
    for variable in variables:
        assignments = [tokens for tokens in set_calls if tokens[:1] == (variable,)]
        values = {Path(token).name for tokens in assignments for token in tokens[1:]}
        guarded_assignments = [
            (tokens, context)
            for tokens, context in positioned_sets
            if tokens[:1] == (variable,)
        ]
        if len(guarded_assignments) != 3 or any(
            context is None or len(tokens) != 2
            for tokens, context in guarded_assignments
        ):
            continue
        chains = {context[0] for _tokens, context in guarded_assignments}
        if len(chains) != 1:
            continue
        branch_files = {
            context[1]: Path(tokens[1]).name for tokens, context in guarded_assignments
        }
        posix_branch = "ELSE" if "ELSE" in branch_files else "UNIX"
        chain = next(iter(chains))
        if (
            platform_files.issubset(values)
            and branch_sequences[chain] == ["WIN32", "APPLE", posix_branch]
            and branch_files
            == {
                "WIN32": "durable_file_adapter_windows.cpp",
                "APPLE": "durable_file_adapter_macos.cpp",
                posix_branch: "durable_file_adapter_posix.cpp",
            }
        ):
            selected.append(variable)
    if len(selected) != 1 or any(
        Path(argument).name in platform_files for argument in arguments
    ):
        raise AssertionError("durable semantic target does not select one platform")
    for command in (
        "target_include_directories",
        "target_link_libraries",
        "target_compile_definitions",
    ):
        calls = [
            body
            for body in cmake_command_arguments(code, command)
            if cmake_tokens(body)[:1] == ("test_durable_residency_journal",)
        ]
        if len(calls) != 1:
            raise AssertionError(f"durable semantic target {command} drifted")


def require_private_authority_gate_contract(source: str) -> None:
    code = strip_cmake_comments_preserve_arguments(source)
    target = "test_durable_residency_private_authority_gate"
    expected_calls = {
        "add_library": (
            target,
            "OBJECT",
            "test/residency/recovery/durable_journal_private_authority_gate.cpp",
        ),
        "add_dependencies": (
            "test_durable_residency_journal",
            target,
        ),
    }
    positions: dict[str, int] = {}
    for command, expected in expected_calls.items():
        matching = [
            (cmake_tokens(body), start)
            for body, start, _end in cmake_command_spans(code, command)
            if target in cmake_tokens(body)
        ]
        if len(matching) != 1 or matching[0][0] != expected:
            raise AssertionError("private-authority build gate is incomplete")
        positions[command] = matching[0][1]
        require_live_cmake_test_enclosure(code, matching[0][1])

    if any(
        cmake_tokens(body)[:1] == (target,) and TESTING_DEFINITION in cmake_tokens(body)
        for body in cmake_command_arguments(code, "target_compile_definitions")
    ):
        raise AssertionError("private-authority gate gained test-only access")

    invocations = [
        start
        for body, start, _end in cmake_command_spans(code, "add_cpp_ci_test")
        if cmake_tokens(body)[:1] == ("ResidencyDurableJournal",)
    ]
    if len(invocations) != 1 or positions["add_dependencies"] >= invocations[0]:
        raise AssertionError("private-authority gate is not a build prerequisite")


def require_durable_cmake_invocation(source: str) -> None:
    code = strip_cmake_comments_preserve_arguments(source)
    matching_calls = [
        (body, start)
        for body, start, _end in cmake_command_spans(code, "add_cpp_ci_test")
        if re.match(r"\s*ResidencyDurableJournal\b", body)
    ]
    if len(matching_calls) != 1:
        raise AssertionError("durable CTest wrapper call is unavailable")
    body, call_start = matching_calls[0]
    arguments = cmake_tokens(body)
    expected = (
        "ResidencyDurableJournal",
        "CI",
        "ON",
        "COMMAND",
        "${Python3_EXECUTABLE}",
        cmake_relative_path(RUNNER, REPO_ROOT),
        "--existing-executable",
        "$<TARGET_FILE:test_durable_residency_journal>",
        "--compiler",
        "${CMAKE_CXX_COMPILER}",
        "--private-authority-gate",
        "$<TARGET_OBJECTS:test_durable_residency_private_authority_gate>",
        "DEPENDS",
        "test_durable_residency_journal",
    )
    if arguments != expected:
        raise AssertionError("durable CTest wrapper/race argv is incomplete")
    require_live_cmake_test_enclosure(code, call_start)
    require_unmasked_cmake_test(code, "ResidencyDurableJournal")


def require_local_overlay_ci_registration(
    source: str,
    expected_enclosures: tuple[tuple[str, ...], ...] = (),
) -> None:
    code = strip_cmake_comments_preserve_arguments(source)
    matching_calls = [
        (body, start)
        for body, start, _end in cmake_command_spans(code, "add_cpp_ci_test")
        if cmake_tokens(body)[:1] == ("ResidencyDurableLocalOverlay",)
    ]
    expected = (
        "ResidencyDurableLocalOverlay",
        "CI",
        "ON",
        "COMMAND",
        "test_durable_local_overlay",
    )
    if len(matching_calls) != 1 or cmake_tokens(matching_calls[0][0]) != expected:
        raise AssertionError("local-overlay CTest is not explicitly registered for CI")
    require_live_cmake_test_enclosure(code, matching_calls[0][1], expected_enclosures)
    require_unmasked_cmake_test(code, "ResidencyDurableLocalOverlay")


def require_live_cmake_test_enclosure(
    source: str,
    call_start: int,
    expected_enclosures: tuple[tuple[str, ...], ...] | None = None,
) -> None:
    commands: list[tuple[int, str, str]] = []
    for command in ("if", "elseif", "else", "endif"):
        commands.extend(
            (start, command, body)
            for body, start, _end in cmake_command_spans(source, command)
            if start < call_start
        )
    stack: list[str] = []
    for _start, command, body in sorted(commands):
        if command == "if":
            stack.append(body)
        elif command in ("elseif", "else"):
            if not stack:
                raise AssertionError("CMake conditional structure is unbalanced")
            stack[-1] = "CONDITIONAL_BRANCH"
        else:
            if not stack:
                raise AssertionError("CMake conditional structure is unbalanced")
            stack.pop()
    if expected_enclosures is None:
        expected_enclosures = (
            (
                "BUILD_TESTING",
                "AND",
                "EXISTS",
                "${CMAKE_CURRENT_SOURCE_DIR}/test/residency/recovery/durable_journal_public_seam.cpp",
            ),
        )
    if tuple(cmake_tokens(enclosure) for enclosure in stack) != expected_enclosures:
        raise AssertionError("durable CTest does not have its exact live enclosure")
    scope_pairs = {
        "endfunction": "function",
        "endmacro": "macro",
        "endforeach": "foreach",
        "endwhile": "while",
    }
    scope_commands: list[tuple[int, str]] = []
    for command in (*scope_pairs.values(), *scope_pairs):
        scope_commands.extend(
            (start, command)
            for _body, start, _end in cmake_command_spans(source, command)
            if start < call_start
        )
    scope_stack: list[str] = []
    for _start, command in sorted(scope_commands):
        if command in scope_pairs.values():
            scope_stack.append(command)
        else:
            if not scope_stack or scope_stack[-1] != scope_pairs[command]:
                raise AssertionError("CMake executable scope is unbalanced")
            scope_stack.pop()
    if scope_stack:
        raise AssertionError("durable CTest is not at global executable scope")
    if any(
        start < call_start
        for _body, start, _end in cmake_command_spans(source, "return")
    ):
        raise AssertionError("durable CTest is unreachable after CMake return")


def require_unmasked_cmake_test(source: str, test: str) -> None:
    forbidden = {
        "DISABLED",
        "WILL_FAIL",
        "SKIP_RETURN_CODE",
        "PASS_REGULAR_EXPRESSION",
        "SKIP_REGULAR_EXPRESSION",
    }
    for command in ("set_tests_properties", "set_property"):
        for body in cmake_command_arguments(source, command):
            tokens = {
                token.strip("\"'").upper()
                for token in re.findall(
                    r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\S+', body
                )
            }
            if test.upper() in tokens and tokens & forbidden:
                raise AssertionError("durable CTest outcome is masked")


def strip_cmake_comments(source: str) -> str:
    without_bracket_comments = re.sub(
        r"#\[(?P<comment_equals>=*)\[.*?\](?P=comment_equals)\]",
        "",
        source,
        flags=re.DOTALL,
    )
    without_bracket_arguments = re.sub(
        r"(?<!#)\[(?P<argument_equals>=*)\[.*?\](?P=argument_equals)\]",
        "",
        without_bracket_comments,
        flags=re.DOTALL,
    )
    without_comments = re.sub(r"(?m)#.*$", "", without_bracket_arguments)
    return re.sub(r'"(?:\\.|[^"\\])*"', "", without_comments)


def strip_cmake_comments_preserve_arguments(source: str) -> str:
    without_bracket_comments = re.sub(
        r"#\[(?P<comment_equals>=*)\[.*?\](?P=comment_equals)\]",
        "",
        source,
        flags=re.DOTALL,
    )
    without_bracket_arguments = re.sub(
        r"(?<!#)\[(?P<argument_equals>=*)\[.*?\](?P=argument_equals)\]",
        "",
        without_bracket_comments,
        flags=re.DOTALL,
    )
    stripped: list[str] = []
    index = 0
    quoted = False
    escaped = False
    while index < len(without_bracket_arguments):
        character = without_bracket_arguments[index]
        if quoted:
            stripped.append(character)
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                quoted = False
            index += 1
            continue
        if character == '"':
            quoted = True
            stripped.append(character)
            index += 1
            continue
        if character == "#":
            while (
                index < len(without_bracket_arguments)
                and without_bracket_arguments[index] not in "\r\n"
            ):
                index += 1
            continue
        stripped.append(character)
        index += 1
    return "".join(stripped)


def cmake_command_spans(source: str, command: str) -> list[tuple[str, int, int]]:
    calls: list[tuple[str, int, int]] = []
    folded_source = source.casefold()
    folded_command = command.casefold()
    index = 0
    quoted = False
    escaped = False
    while index < len(source):
        character = source[index]
        if quoted:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                quoted = False
            index += 1
            continue
        if character == '"':
            quoted = True
            index += 1
            continue
        if folded_source.startswith(folded_command, index):
            before = source[index - 1] if index else ""
            after_index = index + len(command)
            after = source[after_index] if after_index < len(source) else ""
            if (before and (before.isalnum() or before == "_")) or (
                after and (after.isalnum() or after == "_")
            ):
                index += 1
                continue
            opening = after_index
            while opening < len(source) and source[opening].isspace():
                opening += 1
            if opening >= len(source) or source[opening] != "(":
                index += 1
                continue
            depth = 1
            cursor = opening + 1
            inner_quoted = False
            inner_escaped = False
            while cursor < len(source) and depth:
                current = source[cursor]
                if inner_quoted:
                    if inner_escaped:
                        inner_escaped = False
                    elif current == "\\":
                        inner_escaped = True
                    elif current == '"':
                        inner_quoted = False
                elif current == '"':
                    inner_quoted = True
                elif current == "(":
                    depth += 1
                elif current == ")":
                    depth -= 1
                cursor += 1
            if depth != 0:
                raise AssertionError("CMake command has unbalanced arguments")
            calls.append((source[opening + 1 : cursor - 1], index, cursor))
            index = cursor
            continue
        index += 1
    return calls


def cmake_command_arguments(source: str, command: str) -> list[str]:
    return [body for body, _start, _end in cmake_command_spans(source, command)]


def extract_yaml_job(source: str, job: str, following_job: str) -> str:
    job_matches = list(re.finditer(rf"(?m)^  {re.escape(job)}\s*:\s*(?:#.*)?$", source))
    following_matches = list(
        re.finditer(rf"(?m)^  {re.escape(following_job)}\s*:\s*(?:#.*)?$", source)
    )
    if len(job_matches) != 1 or len(following_matches) != 1:
        raise AssertionError("Windows installer job is unavailable")
    start = job_matches[0].start()
    end = following_matches[0].start()
    if end <= start:
        raise AssertionError("Windows installer job order drifted")
    return source[start:end]


def extract_yaml_run_bodies(source: str) -> str:
    lines = source.splitlines(keepends=True)
    bodies: list[str] = []
    index = 0
    run_line = re.compile(
        r"^(?P<indent>\s*)(?:-\s+)?run\s*:\s*(?P<value>.*?)(?:\r?\n)?$"
    )
    while index < len(lines):
        match = run_line.match(lines[index])
        if match is None:
            index += 1
            continue
        value = match.group("value").strip()
        if re.fullmatch(r"[|>][+-]?\d?", value):
            base_indent = len(match.group("indent"))
            index += 1
            block: list[str] = []
            while index < len(lines):
                line = lines[index]
                if not line.strip():
                    block.append(line)
                    index += 1
                    continue
                indentation = len(line) - len(line.lstrip())
                if indentation <= base_indent:
                    break
                block.append(line)
                index += 1
            bodies.extend(block)
            continue
        bodies.append(value + "\n")
        index += 1
    return "".join(bodies)


def direct_yaml_metadata(
    source: str,
    indentation: int,
    strip_sequence_marker: bool = False,
) -> dict[str, str]:
    metadata: dict[str, str] = {}
    for line in source.splitlines():
        leading = len(line) - len(line.lstrip())
        if leading != indentation:
            continue
        content = line.lstrip()
        if strip_sequence_marker and content.startswith("- "):
            content = content[2:].lstrip()
        match = re.match(r"(?P<key>[A-Za-z0-9_-]+)\s*:\s*(?P<value>.*)$", content)
        if match is not None:
            metadata[match.group("key")] = match.group("value").strip()
    return metadata


def extract_yaml_step_blocks(job_source: str) -> tuple[tuple[str, int], ...]:
    lines = job_source.splitlines(keepends=True)
    steps = [
        (index, len(match.group("indent")))
        for index, line in enumerate(lines)
        if (match := re.match(r"^(?P<indent>\s*)steps\s*:\s*(?:#.*)?$", line))
    ]
    if len(steps) != 1:
        raise AssertionError("Windows installer steps are unavailable")
    steps_index, steps_indent = steps[0]
    item_indent = steps_indent + 2
    starts = [
        index
        for index in range(steps_index + 1, len(lines))
        if re.match(rf"^\s{{{item_indent}}}-\s+", lines[index])
    ]
    if not starts:
        raise AssertionError("Windows installer has no executable steps")
    blocks: list[tuple[str, int]] = []
    for offset, start in enumerate(starts):
        end = starts[offset + 1] if offset + 1 < len(starts) else len(lines)
        while end > start + 1 and not lines[end - 1].strip():
            end -= 1
        blocks.append(("".join(lines[start:end]), item_indent))
    return tuple(blocks)


def yaml_execution_metadata_is_live(metadata: dict[str, str]) -> bool:
    if "if" in metadata:
        return False
    continue_on_error = metadata.get("continue-on-error")
    return (
        continue_on_error is None or continue_on_error.strip("'\"").lower() == "false"
    )


def direct_top_level_shell_statements(source: str) -> tuple[str, ...]:
    lines = [line for line in source.splitlines() if line.strip()]
    if not lines:
        return ()
    direct_indent = min(len(line) - len(line.lstrip()) for line in lines)
    statements: list[str] = []
    statement: list[str] = []
    brace_depth = 0
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("#"):
            continue
        code = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', "", stripped)
        indentation = len(line) - len(line.lstrip())
        if not statement and indentation != direct_indent:
            continue
        statement.append(stripped)
        brace_depth += code.count("{") - code.count("}")
        if brace_depth <= 0:
            statements.append("\n".join(statement))
            statement = []
            brace_depth = 0
    if statement:
        statements.append("\n".join(statement))
    return tuple(statements)


def is_powershell_exit_check(statement: str, variable: str) -> bool:
    reference = re.escape(variable)
    return (
        re.fullmatch(
            rf"(?is)^\s*if\s*\(\s*{reference}\s+-ne\s+0\s*\)"
            rf"\s*\{{\s*exit\s+{reference}\s*\}}\s*$",
            statement,
        )
        is not None
    )


def powershell_can_bypass_successfully(source: str) -> bool:
    transfer_prefix = r"(?i)(?:^|[;{}\n])\s*"
    if re.search(transfer_prefix + r"return(?=\s|[;}]|$)", source):
        return True
    for exit_statement in re.finditer(
        transfer_prefix + r"exit(?:\s+(?P<status>[^;}\n]+))?", source
    ):
        status = (exit_statement.group("status") or "").strip()
        if re.fullmatch(r"[+-]?\d+", status) is None or int(status) == 0:
            return True
    return False


def has_immediate_powershell_exit_propagation(
    source: str,
    command: re.Pattern,
) -> bool:
    statements = direct_top_level_shell_statements(source)
    for index, statement in enumerate(statements):
        if command.fullmatch(statement) is None:
            continue
        prior_code = "\n".join(statements[:index])
        prior_code = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', "", prior_code)
        prior_code = "\n".join(
            line.split("#", 1)[0] for line in prior_code.splitlines()
        )
        if powershell_can_bypass_successfully(prior_code):
            continue
        next_index = index + 1
        if next_index >= len(statements):
            continue
        if is_powershell_exit_check(statements[next_index], "$LASTEXITCODE"):
            return True
        capture = re.fullmatch(
            r"(?i)^\s*(?P<variable>\$[A-Za-z_]\w*)\s*=\s*\$LASTEXITCODE\s*$",
            statements[next_index],
        )
        if capture is None or next_index + 1 >= len(statements):
            continue
        if is_powershell_exit_check(
            statements[next_index + 1], capture.group("variable")
        ):
            return True
    return False


def require_live_windows_workflow_commands(
    job_source: str,
    build_command: re.Pattern,
    test_command: re.Pattern,
) -> None:
    job_metadata = direct_yaml_metadata(job_source, 4)
    if not yaml_execution_metadata_is_live(job_metadata):
        raise AssertionError(
            "Windows durable-residency test job is disabled or ignored"
        )
    runner = job_metadata.get("runs-on", "").strip("'\"").lower()
    if runner != "windows-latest":
        raise AssertionError("durable-residency test job is not bound to Windows")
    live_build = False
    live_test = False
    for block, item_indent in extract_yaml_step_blocks(job_source):
        first = direct_yaml_metadata(block, item_indent, True)
        remaining = direct_yaml_metadata(block, item_indent + 2)
        metadata = {**first, **remaining}
        shell = metadata.get("shell", "").strip("'\"").lower()
        if shell and not re.match(r"^(?:powershell|pwsh)(?:\s|$)", shell):
            continue
        run_body = strip_shell_heredocs(extract_yaml_run_bodies(block))
        has_build = has_immediate_powershell_exit_propagation(run_body, build_command)
        has_test = has_immediate_powershell_exit_propagation(run_body, test_command)
        if not (has_build or has_test):
            continue
        if not yaml_execution_metadata_is_live(metadata):
            continue
        live_build |= has_build
        live_test |= has_test
    if not (live_build and live_test):
        raise AssertionError(
            "Windows durable-residency test commands are unavailable in live steps"
        )


def strip_shell_heredocs(source: str) -> str:
    lines = source.splitlines(keepends=True)
    kept: list[str] = []
    index = 0
    while index < len(lines):
        stripped = lines[index].strip()
        here_string = re.search(r"@(?P<quote>['\"])\s*$", stripped)
        if here_string:
            terminator = here_string.group("quote") + "@"
            index += 1
            while index < len(lines) and lines[index].strip() != terminator:
                index += 1
            index += index < len(lines)
            continue
        heredoc = re.search(
            r"<<-?\s*['\"]?(?P<end>[A-Za-z_][A-Za-z0-9_]*)['\"]?", lines[index]
        )
        if heredoc:
            terminator = heredoc.group("end")
            index += 1
            while index < len(lines) and lines[index].strip() != terminator:
                index += 1
            index += index < len(lines)
            continue
        kept.append(lines[index])
        index += 1
    return "".join(kept)


def require_compiler_selection_contract() -> None:
    absolute_wrapper = "/fixture/toolchains/task020/c++ wrapper"
    fallback = "c++"
    lookups: list[str] = []

    def fake_which(command: str) -> str | None:
        lookups.append(command)
        if command == absolute_wrapper:
            return command
        if command == fallback:
            raise AssertionError("configured compiler fell back to PATH")
        return None

    configured = configured_compiler(
        environment={"CXX": f'"{absolute_wrapper}" --task020-wrapper'},
        which=fake_which,
    )
    if configured != [absolute_wrapper, "--task020-wrapper"]:
        raise AssertionError("absolute configured compiler was not preserved")
    if fallback in lookups:
        raise AssertionError("configured compiler consulted the PATH fallback")

    observed: list[tuple[list[str], Path]] = []

    def recording_runner(command, **kwargs):
        observed.append((command, kwargs["cwd"]))
        return subprocess.CompletedProcess(command, 0, b"", b"")

    output = Path("/fixture/output/durable-journal-public-seam")
    failure = execute_public_seam(
        output,
        configured=configured,
        environment={"MBEDCRYPTO_LIBRARY": "/fixture/libmbedcrypto.a"},
        runner=recording_runner,
    )
    if failure is not None:
        raise AssertionError(
            "absolute configured compiler could not drive the public seam"
        )
    compile_commands = [
        command for command, _cwd in observed if str(JOURNAL_SOURCE) in command
    ]
    if not compile_commands or any(
        command[0] != absolute_wrapper for command in compile_commands
    ):
        raise AssertionError("public seam ignored the absolute configured compiler")
    if any(command and command[0] == fallback for command, _cwd in observed):
        raise AssertionError("public seam invoked the PATH fallback compiler")
    if any(cwd != output.parent for _command, cwd in observed):
        raise AssertionError("public seam escaped its temporary output directory")


def require_command_contract() -> None:
    output = Path("durable-journal-public-seam")
    if os.name != "nt":
        fallback = compiler_command(output, configured=["c++"], environment={})
        if (
            "-lmbedcrypto" not in fallback
            or "-pthread" not in fallback
            or f"-D{TESTING_DEFINITION}" not in fallback
        ):
            raise AssertionError("POSIX public seam omitted required link flags")
        environment: Mapping[str, str] = {
            "NLOHMANN_JSON_INCLUDE_DIR": "/fixture/nlohmann",
            "MBEDTLS_INCLUDE_DIR": "/fixture/mbedtls",
            "MBEDCRYPTO_LIBRARY": "/fixture/libmbedcrypto.a",
        }
        configured = compiler_command(
            output, configured=["c++"], environment=environment
        )
        for include in (
            str(INCLUDE_ROOT),
            str(INTERNAL_INCLUDE_ROOT),
            str(TEST_INCLUDE_ROOT),
            "/fixture/nlohmann",
            "/fixture/mbedtls",
        ):
            if include not in configured:
                raise AssertionError(f"public seam omitted include {include}")
        if "/fixture/libmbedcrypto.a" not in configured or "-lmbedcrypto" in configured:
            raise AssertionError("public seam ignored configured mbedcrypto library")

    environment = {
        "NLOHMANN_JSON_INCLUDE_DIR": "C:/fixture/nlohmann",
        "MBEDTLS_INCLUDE_DIR": "C:/fixture/mbedtls",
        "MBEDCRYPTO_LIBRARY": "C:/fixture/mbedcrypto.lib",
    }
    command = compiler_command(output, configured=["cl.exe"], environment=environment)
    for include in (
        str(INCLUDE_ROOT),
        str(INTERNAL_INCLUDE_ROOT),
        str(TEST_INCLUDE_ROOT),
        "C:/fixture/nlohmann",
        "C:/fixture/mbedtls",
    ):
        if f"/I{include}" not in command:
            raise AssertionError(f"MSVC public seam omitted include {include}")
    if "C:/fixture/mbedcrypto.lib" not in command:
        raise AssertionError("MSVC public seam ignored configured mbedcrypto library")
    if f"/D{TESTING_DEFINITION}" not in command:
        raise AssertionError("MSVC public seam omitted its test-only definition")

    negative_posix = compile_only_command(
        Path("forgery.cpp"), output, configured=["c++"]
    )
    ordered_system_includes = compile_only_command(
        Path("forgery.cpp"),
        output,
        configured=["c++"],
        system_includes=("/fixture/first", "/fixture/second"),
    )
    negative_msvc = compile_only_command(
        Path("forgery.cpp"), output, configured=["cl.exe"]
    )
    if f"-U{TESTING_DEFINITION}" not in negative_posix or any(
        token == f"-D{TESTING_DEFINITION}" for token in negative_posix
    ):
        raise AssertionError("POSIX forgery compile enabled test injection")
    if f"/U{TESTING_DEFINITION}" not in negative_msvc or any(
        token == f"/D{TESTING_DEFINITION}" for token in negative_msvc
    ):
        raise AssertionError("MSVC forgery compile enabled test injection")
    if ordered_system_includes.index("/fixture/first") > (
        ordered_system_includes.index("/fixture/second")
    ):
        raise AssertionError("POSIX system include precedence was reversed")
    try:
        compiler_command(output, configured=["cl.exe"], environment={})
    except RuntimeError:
        pass
    else:
        raise AssertionError("MSVC public seam accepted a missing mbedcrypto library")

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
    workdir: Path,
    failed: str,
    timed_out: str,
    unavailable: str,
    runner=subprocess.run,
) -> str | None:
    try:
        completed = runner(
            command,
            cwd=workdir,
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
        workdir=output.parent,
        failed=COMPILATION_FAILED,
        timed_out=COMPILATION_TIMED_OUT,
        unavailable=COMPILER_UNAVAILABLE,
        runner=runner,
    )
    if failure is not None:
        return failure
    return run_stage(
        [str(output)],
        workdir=output.parent,
        failed=CONTRACT_ASSERTION_FAILED,
        timed_out=EXECUTION_TIMED_OUT,
        unavailable=EXECUTABLE_UNAVAILABLE,
        runner=runner,
    )


def race_worker_status(stdout: bytes) -> bytes | None:
    match = re.fullmatch(rb"(conflict_before_write|published)\r?\n", stdout)
    return None if match is None else match.group(1)


def require_race_worker_status_contract() -> None:
    for status in (b"conflict_before_write", b"published"):
        if race_worker_status(status + b"\n") != status:
            raise AssertionError("POSIX race-worker status is unavailable")
        if race_worker_status(status + b"\r\n") != status:
            raise AssertionError("Windows race-worker status is unavailable")
    for invalid in (
        b"published",
        b"published\n\n",
        b"published\r\r\n",
        b"published\nconflict_before_write\n",
        b"unknown\n",
    ):
        if race_worker_status(invalid) is not None:
            raise AssertionError("malformed race-worker status was accepted")


def require_cross_process_cas(output: Path) -> str | None:
    with tempfile.TemporaryDirectory(
        prefix="durable-residency-journal-process-race-",
        dir=output.parent,
    ) as directory:
        root = Path(directory)
        authority = root / "authority"
        controls = root / "controls"
        authority.mkdir()
        controls.mkdir()
        floor = controls / "trusted-floor.json"
        init_failure = run_stage(
            [str(output), "--race-init", str(authority), str(floor)],
            workdir=output.parent,
            failed=CONTRACT_ASSERTION_FAILED,
            timed_out=EXECUTION_TIMED_OUT,
            unavailable=EXECUTABLE_UNAVAILABLE,
        )
        if init_failure is not None:
            return init_failure

        ready_files = [controls / "left.ready", controls / "right.ready"]
        go_file = controls / "go"
        workers: list[subprocess.Popen] = []
        try:
            for ready_file in ready_files:
                workers.append(
                    subprocess.Popen(
                        [
                            str(output),
                            "--race-worker",
                            str(authority),
                            str(floor),
                            str(ready_file),
                            str(go_file),
                        ],
                        cwd=output.parent,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                    )
                )

            ready_deadline = time.monotonic() + 30
            while not all(path.is_file() for path in ready_files):
                if any(worker.poll() is not None for worker in workers):
                    return CONTRACT_ASSERTION_FAILED
                if time.monotonic() >= ready_deadline:
                    return EXECUTION_TIMED_OUT
                time.sleep(0.01)

            go_file.write_bytes(b"go\n")
            outputs: list[bytes] = []
            for worker in workers:
                stdout, stderr = worker.communicate(timeout=PROCESS_TIMEOUT_SECONDS)
                if worker.returncode != 0 or stderr:
                    return CONTRACT_ASSERTION_FAILED
                status = race_worker_status(stdout)
                if status is None:
                    return CONTRACT_ASSERTION_FAILED
                outputs.append(status)
            if sorted(outputs) != [b"conflict_before_write", b"published"]:
                return CONTRACT_ASSERTION_FAILED
        except subprocess.TimeoutExpired:
            return EXECUTION_TIMED_OUT
        except (OSError, UnicodeError):
            return EXECUTABLE_UNAVAILABLE
        finally:
            for worker in workers:
                if worker.poll() is None:
                    worker.kill()
                    try:
                        worker.communicate(timeout=5)
                    except subprocess.TimeoutExpired:
                        pass

        return run_stage(
            [str(output), "--race-verify", str(authority), str(floor)],
            workdir=output.parent,
            failed=CONTRACT_ASSERTION_FAILED,
            timed_out=EXECUTION_TIMED_OUT,
            unavailable=EXECUTABLE_UNAVAILABLE,
        )


def execute_existing_public_seam(
    output: Path,
    *,
    stage_runner=run_stage,
    race_runner=require_cross_process_cas,
) -> str | None:
    failure = stage_runner(
        [str(output)],
        workdir=output.parent,
        failed=CONTRACT_ASSERTION_FAILED,
        timed_out=EXECUTION_TIMED_OUT,
        unavailable=EXECUTABLE_UNAVAILABLE,
    )
    if failure is not None:
        return failure
    return race_runner(output)


def execute_hosted_public_seam(
    output: Path,
    compiler: list[str],
    directory: Path,
    *,
    private_authority_gate: Path | None = None,
    compile_runner=subprocess.run,
    stage_runner=run_stage,
    race_runner=require_cross_process_cas,
) -> str | None:
    if not (private_authority_gate is not None and is_msvc_frontend(compiler)):
        failure = require_private_authority_construction(
            directory,
            configured=compiler,
            runner=compile_runner,
        )
        if failure is not None:
            return failure
    return execute_existing_public_seam(
        output,
        stage_runner=stage_runner,
        race_runner=race_runner,
    )


def require_existing_executable_contract() -> None:
    with tempfile.TemporaryDirectory(
        prefix="durable-residency-journal-hosted-contract-"
    ) as temporary:
        directory = Path(temporary)
        output = directory / (
            "durable-journal-public-seam.exe"
            if os.name == "nt"
            else "durable-journal-public-seam"
        )
        compiler = directory / "c++"
        output.write_bytes(b"fixture executable")
        compiler.write_bytes(b"fixture compiler")
        gate_artifact = directory / "durable_journal_private_authority_gate.cpp.o"
        gate_artifact.write_bytes(b"fixture object")
        msvc_gate_artifact = directory / "durable_journal_private_authority_gate.obj"
        msvc_gate_artifact.write_bytes(b"fixture object")
        if os.name != "nt":
            output.chmod(0o700)
            compiler.chmod(0o700)

        parsed_output, parsed_compiler, private_authority_gate = existing_executable(
            [
                "--existing-executable",
                str(output),
                "--compiler",
                str(compiler),
                "--private-authority-gate",
                str(gate_artifact),
            ]
        )
        if (
            parsed_output != output
            or parsed_compiler != [str(compiler)]
            or private_authority_gate != gate_artifact
        ):
            raise AssertionError("hosted arguments changed executable identity")
        _, _, parsed_msvc_gate = existing_executable(
            [
                "--existing-executable",
                str(output),
                "--compiler",
                str(compiler),
                "--private-authority-gate",
                str(msvc_gate_artifact),
            ]
        )
        if parsed_msvc_gate != msvc_gate_artifact:
            raise AssertionError("MSVC build-gate artifact spelling was rejected")
        if os.name != "nt":
            executable_link = directory / "selected-executable-link"
            compiler_link = directory / "selected-compiler-link"
            executable_link.symlink_to(output)
            compiler_link.symlink_to(compiler)
            linked_output, linked_compiler, linked_private_authority_gate = (
                existing_executable(
                    [
                        "--existing-executable",
                        str(executable_link),
                        "--compiler",
                        str(compiler_link),
                    ]
                )
            )
            if (
                linked_output != executable_link
                or linked_compiler != [str(compiler_link)]
                or linked_private_authority_gate
            ):
                raise AssertionError("hosted mode bypassed a selected wrapper")

        invalid_arguments = (
            [],
            ["--existing-executable", str(output)],
            [
                "--existing-executable",
                str(output),
                "--compiler",
                str(compiler),
                "extra",
            ],
            [
                "--compiler",
                str(compiler),
                "--existing-executable",
                str(output),
            ],
            [
                "--existing-executable",
                output.name,
                "--compiler",
                str(compiler),
            ],
            [
                "--existing-executable",
                str(directory),
                "--compiler",
                str(compiler),
            ],
            [
                "--existing-executable",
                str(directory / "missing-executable"),
                "--compiler",
                str(compiler),
            ],
            [
                "--existing-executable",
                str(output),
                "--compiler",
                compiler.name,
            ],
            [
                "--existing-executable",
                str(output),
                "--compiler",
                str(directory),
            ],
            [
                "--existing-executable",
                str(output),
                "--compiler",
                str(directory / "missing-compiler"),
            ],
            [
                "--existing-executable",
                str(output),
                "--compiler",
                str(compiler),
                "--unknown-gate",
                str(gate_artifact),
            ],
            [
                "--existing-executable",
                str(output),
                "--compiler",
                str(compiler),
                "--private-authority-gate",
            ],
            [
                "--existing-executable",
                str(output),
                "--compiler",
                str(compiler),
                "--private-authority-gate",
                "relative/gate.obj",
            ],
            [
                "--existing-executable",
                str(output),
                "--compiler",
                str(compiler),
                "--private-authority-gate",
                str(directory / "missing-gate.obj"),
            ],
            [
                "--existing-executable",
                str(output),
                "--compiler",
                str(compiler),
                "--private-authority-gate",
                str(directory),
            ],
            [
                "--existing-executable",
                str(output),
                "--compiler",
                str(compiler),
                "--private-authority-gate",
                str(output),
            ],
        )
        for arguments in invalid_arguments:
            try:
                existing_executable(list(arguments))
            except (AssertionError, RuntimeError):
                pass
            else:
                raise AssertionError("hosted mode accepted invalid arguments")

        observed: list[tuple[str, Path]] = []
        compile_commands: list[list[str]] = []

        def fake_compile(command, **kwargs):
            compile_commands.append(command)
            source = next(
                (Path(token) for token in command if str(token).endswith(".cpp")),
                None,
            )
            succeeded = (
                source is not None
                and source.name == "published_journal_positive_control.cpp"
            )
            return subprocess.CompletedProcess(
                command,
                0 if succeeded else 1,
                b"",
                b"",
            )

        def fake_stage(command, **kwargs):
            observed.append(("semantic", Path(command[0])))
            if kwargs["workdir"] != output.parent:
                raise AssertionError("existing executable escaped its build directory")

        def fake_race(executable):
            observed.append(("race", executable))

        failure = execute_hosted_public_seam(
            output,
            parsed_compiler,
            directory,
            private_authority_gate=private_authority_gate,
            compile_runner=fake_compile,
            stage_runner=fake_stage,
            race_runner=fake_race,
        )
        if failure is not None or observed != [
            ("semantic", output),
            ("race", output),
        ]:
            raise AssertionError(
                "existing-executable mode bypassed the semantic process race"
            )
        if not compile_commands or any(
            command[0] != str(compiler) for command in compile_commands
        ):
            raise AssertionError("hosted mode ignored its explicit compiler")
        forbidden_full_seam_inputs = {
            str(JOURNAL_SOURCE),
            str(DURABLE_SOURCE),
            str(ADAPTER_COMMON),
            str(native_adapter_source()),
            str(TEST_SUPPORT),
            str(PUBLIC_SEAM),
        }
        if any(
            forbidden_full_seam_inputs.intersection(command)
            for command in compile_commands
        ):
            raise AssertionError("hosted mode rebuilt the public seam")

        msvc_compiler = directory / "cl.exe"
        msvc_compiler.write_bytes(b"fixture compiler")
        if os.name != "nt":
            msvc_compiler.chmod(0o700)
        compile_count = len(compile_commands)
        observed.clear()
        failure = execute_hosted_public_seam(
            output,
            [str(msvc_compiler)],
            directory,
            private_authority_gate=msvc_gate_artifact,
            compile_runner=fake_compile,
            stage_runner=fake_stage,
            race_runner=fake_race,
        )
        if (
            failure is not None
            or len(compile_commands) != compile_count
            or observed != [("semantic", output), ("race", output)]
        ):
            raise AssertionError("MSVC hosted mode ignored its CMake compile gate")
        observed.clear()
        failure = execute_hosted_public_seam(
            output,
            [str(msvc_compiler)],
            directory,
            private_authority_gate=None,
            compile_runner=fake_compile,
            stage_runner=fake_stage,
            race_runner=fake_race,
        )
        if failure is not None or len(compile_commands) == compile_count:
            raise AssertionError("MSVC hosted mode skipped an unproven compile gate")

        race_called = False

        def rejected_semantic(_command, **_kwargs):
            return CONTRACT_ASSERTION_FAILED

        def forbidden_race(_executable):
            nonlocal race_called
            race_called = True

        failure = execute_existing_public_seam(
            output,
            stage_runner=rejected_semantic,
            race_runner=forbidden_race,
        )
        if failure != CONTRACT_ASSERTION_FAILED or race_called:
            raise AssertionError("hosted mode raced after semantic failure")


def existing_executable(
    arguments: list[str],
) -> tuple[Path, list[str], Path | None]:
    if (
        len(arguments) not in (4, 6)
        or arguments[0] != "--existing-executable"
        or arguments[2] != "--compiler"
        or (len(arguments) == 6 and arguments[4] != "--private-authority-gate")
    ):
        raise AssertionError("existing-executable arguments are invalid")
    candidate = Path(arguments[1])
    if not candidate.is_absolute() or not candidate.is_file():
        raise AssertionError("existing executable is not an absolute regular file")
    if not candidate.is_file() or (
        os.name != "nt" and not os.access(candidate, os.X_OK)
    ):
        raise AssertionError("existing executable is not runnable")
    private_authority_gate = None
    if len(arguments) == 6:
        private_authority_gate = Path(arguments[5])
        if (
            not private_authority_gate.is_absolute()
            or not private_authority_gate.is_file()
            or re.fullmatch(
                r"durable_journal_private_authority_gate(?:\.cpp)?\.(?:o|obj)",
                private_authority_gate.name,
            )
            is None
        ):
            raise AssertionError("private-authority build artifact is invalid")
    return candidate, explicit_compiler(arguments[3]), private_authority_gate


def failure_line(message: str) -> str:
    rendered = f"{message}\n"
    if rendered.count("\n") != 1 or len(rendered.encode("utf-8")) > 256:
        raise AssertionError("public-seam failure line is not bounded")
    return rendered


def require_runner_failure_contract() -> None:
    secret = "private-compiler-path/boom"
    raw_output = f"raw tool output {secret}".encode()

    class FakeRunner:
        def __init__(self, outcomes):
            self.outcomes = iter(outcomes)

        def __call__(self, _command, **_kwargs):
            outcome = next(self.outcomes)
            if isinstance(outcome, BaseException):
                raise outcome
            return outcome

    successful = subprocess.CompletedProcess([], 0, b"", b"")
    failed = subprocess.CompletedProcess([], 17, raw_output, raw_output)
    timeout = subprocess.TimeoutExpired([secret], 1, output=raw_output)
    invalid_utf8 = UnicodeDecodeError("utf-8", b"\xff", 0, 1, "invalid")
    cases = [
        ([OSError(secret)], COMPILER_UNAVAILABLE),
        ([invalid_utf8], COMPILER_UNAVAILABLE),
        ([failed], COMPILATION_FAILED),
        ([timeout], COMPILATION_TIMED_OUT),
        ([successful, failed], CONTRACT_ASSERTION_FAILED),
        ([successful, timeout], EXECUTION_TIMED_OUT),
        ([successful, OSError(secret)], EXECUTABLE_UNAVAILABLE),
    ]
    environment = {"MBEDCRYPTO_LIBRARY": "/fixture/libmbedcrypto.a"}
    for outcomes, expected in cases:
        observed = execute_public_seam(
            Path("durable-journal-public-seam"),
            configured=[f"/{secret}/c++"],
            environment=environment,
            runner=FakeRunner(outcomes),
        )
        if observed != expected:
            raise AssertionError("public-seam failure classification drifted")
        rendered = failure_line(observed)
        if secret in rendered or "Traceback" in rendered:
            raise AssertionError("public-seam failure leaked process details")


def required_test_files() -> tuple[Path, ...]:
    return (
        RUNNER,
        PUBLIC_SEAM,
        PRIVATE_AUTHORITY_GATE,
        TEST_SUPPORT,
        TEST_SUPPORT_HEADER,
    )


def required_production_files() -> tuple[Path, ...]:
    return (
        DURABLE_HEADER,
        DURABLE_SOURCE,
        ADAPTER_HEADER,
        ADAPTER_COMMON,
        ADAPTER_POSIX,
        ADAPTER_WINDOWS,
        ADAPTER_MACOS,
    )


def main() -> int:
    if any(not path.is_file() for path in required_test_files()):
        sys.stderr.write(failure_line(CONTRACT_ASSERTION_FAILED))
        return 1
    if any(not path.is_file() for path in required_production_files()):
        sys.stderr.write(failure_line(UNAVAILABLE))
        return 1

    try:
        arguments = sys.argv[1:]
        hosted: tuple[Path, list[str], Path | None] | None = None
        if arguments:
            hosted = existing_executable(arguments)
        require_command_contract()
        require_compiler_selection_contract()
        require_runner_failure_contract()
        require_race_worker_status_contract()
        require_existing_executable_contract()
        require_platform_source_contract()
        require_build_contract()
        with tempfile.TemporaryDirectory(
            prefix="durable-residency-journal-"
        ) as directory:
            temporary = Path(directory)
            if hosted is not None:
                output, configured, private_authority_gate = hosted
                failure = execute_hosted_public_seam(
                    output,
                    configured,
                    temporary,
                    private_authority_gate=private_authority_gate,
                )
            else:
                configured = configured_compiler()
                suffix = ".exe" if os.name == "nt" else ""
                output = temporary / f"durable_journal_public_seam{suffix}"
                failure = execute_public_seam(
                    output,
                    configured=configured,
                    environment=os.environ,
                )
                if failure is None:
                    failure = require_private_authority_construction(
                        temporary,
                        configured=configured,
                    )
                if failure is None:
                    failure = require_cross_process_cas(output)
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
