import hashlib
import importlib.util
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Mapping
from pathlib import Path
from typing import Any

repo_root = Path(__file__).resolve().parents[3]
failure = "TASK-013 residency cross-component matrix is unavailable\n"
expected_catalog_sha256 = (
    "c3b8fcd06079c8733515b04a8082c164562636681c197e7ee686406a95f2bd55"
)
generated_paths = (
    "docs/api/schemas/residency/artifact_quarantine_record.schema.json",
    "docs/api/schemas/residency/artifact_writer_job_revision.schema.json",
    "docs/api/schemas/residency/artifact_writer_request_result.schema.json",
    "docs/api/schemas/residency/authority_transaction_result.schema.json",
    "docs/api/schemas/residency/coordinator_step_result.schema.json",
    "docs/api/schemas/residency/deployment_local_overlay_object.schema.json",
    "docs/api/schemas/residency/operation_revision.schema.json",
    "docs/api/schemas/residency/overlay_activation_root.schema.json",
    "docs/api/schemas/residency/profiling_input_envelope.schema.json",
    "docs/api/schemas/residency/profiling_phase_attestation.schema.json",
    "docs/api/schemas/residency/reason.schema.json",
    "docs/api/schemas/residency/request_error.schema.json",
    "docs/api/schemas/residency/residency_profiles.schema.json",
    "docs/api/schemas/residency/resource_diagnostic.schema.json",
    "docs/api/schemas/residency/response_diagnostic.schema.json",
    "docs/api/schemas/residency/staged_import_session_record.schema.json",
    "src/cpp/include/lemon/residency/generated_contract.h",
    "src/cpp/resources/residency_profiles.json",
    "src/cpp/server/residency/generated_contract.cpp",
    "test/residency/contract/generated/catalog.json",
    "test/residency/contract/generated/http_auth.json",
    "test/residency/contract/generated/reasons.json",
    "test/residency/contract/generated/schema_examples.json",
)
expected_schema_ids = (
    "residency.artifact_quarantine_record/1.0",
    "residency.artifact_writer_job_revision/1.0",
    "residency.artifact_writer_request_result/1.0",
    "residency.authority_transaction_result/1.0",
    "residency.coordinator_step_result/1.0",
    "residency.deployment_local_overlay_object/1.0",
    "residency.operation_revision/1.0",
    "residency.overlay_activation_root/1.0",
    "residency.profiling_input_envelope/1.0",
    "residency.profiling_phase_attestation/1.0",
    "residency.reason/1.0",
    "residency.request_error/1.0",
    "residency.profiles/1.0",
    "residency.resource_diagnostic/1.0",
    "residency.response_diagnostic/1.0",
    "residency.staged_import_session_record/1.0",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def strict_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def strict_json(content: bytes) -> Any:
    return json.loads(content.decode("utf-8"), object_pairs_hook=strict_object)


def read(relative: str) -> bytes:
    return (repo_root / relative).read_bytes()


def residency_matrix_cmake_block(cmake: str) -> str:
    match = re.search(
        r"^set\(_RESIDENCY_CONTRACT_MATRIX_TEST_SRC\s*\n"
        r'\s+"\$\{CMAKE_CURRENT_SOURCE_DIR\}/test/cpp/'
        r'test_residency_contract_matrix\.cpp"\s*\n'
        r"^\)\s*\n"
        r'(?P<block>^if\(BUILD_TESTING AND EXISTS "\$\{'
        r'_RESIDENCY_CONTRACT_MATRIX_TEST_SRC\}"\)\s*\n'
        r".*?^endif\(\)\s*$)",
        cmake,
        flags=re.MULTILINE | re.DOTALL,
    )
    require(match is not None, "residency matrix CMake block is unavailable")
    return match.group("block")


def collect(root: Path) -> dict[str, bytes]:
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in sorted(path for path in root.rglob("*") if path.is_file())
    }


def require_canonical_generated_contract() -> None:
    files = {path: read(path) for path in generated_paths}
    require(tuple(sorted(files)) == generated_paths, "generated path roster drifted")
    for path, content in files.items():
        require(not content.startswith(b"\xef\xbb\xbf"), f"{path} has a BOM")
        require(b"\r" not in content, f"{path} is not LF-normalized")
        require(content.endswith(b"\n"), f"{path} lacks a terminal newline")
        require(not content.endswith(b"\n\n"), f"{path} has extra terminal newlines")
        content.decode("utf-8")
        if path.endswith(".json"):
            strict_json(content)

    catalog_bytes = files["src/cpp/resources/residency_profiles.json"]
    require(
        catalog_bytes == files["test/residency/contract/generated/catalog.json"],
        "catalog and byte golden differ",
    )
    require(
        hashlib.sha256(catalog_bytes).hexdigest() == expected_catalog_sha256,
        "packaged catalog bytes drifted",
    )
    header = files["src/cpp/include/lemon/residency/generated_contract.h"].decode(
        "utf-8"
    )
    locked_digests = re.findall(
        r'packaged_catalog_sha256\s*=\s*"([0-9a-f]{64})"', header
    )
    require(
        locked_digests == [expected_catalog_sha256],
        "generated header does not lock the exact packaged catalog bytes",
    )

    catalog = strict_json(catalog_bytes)
    require(len(catalog["promotion_units"]) == 39, "promotion-unit count drifted")
    require(
        len({unit["id"] for unit in catalog["promotion_units"]}) == 39,
        "promotion-unit IDs are not unique",
    )
    require(
        {unit["capability_level"] for unit in catalog["promotion_units"]}
        == {"unsupported"},
        "packaged capability became active",
    )
    require(
        {unit["delivery_state"] for unit in catalog["promotion_units"]} == {"absent"},
        "packaged delivery became active",
    )
    require(len(catalog["fallbacks"]) == 14, "fallback count drifted")

    registry = catalog["contract_registry"]
    require(len(registry["reason_registry"]) == 87, "reason count drifted")
    require(
        len(registry["presentation_registry"]) == 27,
        "presentation count drifted",
    )
    schema_rows = registry["schema_registry"]
    require(
        tuple(row["schema_type"] for row in schema_rows.values())
        == expected_schema_ids,
        "schema ID roster drifted",
    )
    for key, row in schema_rows.items():
        schema = strict_json(files[row["output"]])
        require(schema["$id"] == row["schema_type"], f"{key} schema ID drifted")

    reasons = strict_json(files["test/residency/contract/generated/reasons.json"])[
        "reasons"
    ]
    require(len(reasons) == 87, "reason golden count drifted")
    require(
        {reason["code"] for reason in reasons} == set(registry["reason_registry"]),
        "reason golden and catalog registry differ",
    )
    examples = strict_json(
        files["test/residency/contract/generated/schema_examples.json"]
    )["examples"]
    require(set(examples) == set(schema_rows), "schema example closure drifted")
    http_auth = strict_json(files["test/residency/contract/generated/http_auth.json"])
    http_auth_keys = (
        "schema",
        "request_context_registry",
        "request_stage_registry",
        "reason_envelope_registry",
        "http_auth_registry",
    )
    require(
        set(http_auth) == set(http_auth_keys),
        "HTTP/auth fixture field closure drifted",
    )
    for key in http_auth_keys:
        require(http_auth[key] == registry[key], f"HTTP/auth {key} projection drifted")

    try:
        strict_json(b'{"duplicate":1,"duplicate":2}\n')
    except ValueError as error:
        require("duplicate JSON key" in str(error), "duplicate diagnostic drifted")
    else:
        raise AssertionError("strict JSON reader accepted a duplicate key")


def load_generator_cli() -> Any:
    tools_root = str(repo_root / "tools")
    if tools_root not in sys.path:
        sys.path.insert(0, tools_root)
    path = repo_root / "tools/generate_residency_contract.py"
    spec = importlib.util.spec_from_file_location("task013_generator_cli", path)
    require(
        spec is not None and spec.loader is not None, "generator CLI is unavailable"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    require(
        tuple(module.OUTPUT_PATHS) == generated_paths,
        "generator output roster drifted",
    )
    return module


def materialize(root: Path, outputs: dict[str, bytes]) -> None:
    for relative, content in outputs.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)


def require_check_failure(
    module: Any,
    outputs: dict[str, bytes],
    mutate: Any,
    expected: str,
) -> None:
    with tempfile.TemporaryDirectory(prefix="residency-matrix-check-") as directory:
        root = Path(directory)
        materialize(root, outputs)
        mutate(root)
        before = collect(root)
        try:
            module._check(root, outputs)
        except ValueError as error:
            require(expected in str(error), "generator check classification drifted")
        else:
            raise AssertionError("generator check accepted synthetic drift")
        require(collect(root) == before, "generator check rewrote synthetic drift")


def require_generator_check_matrix() -> None:
    module = load_generator_cli()
    outputs = {path: read(path) for path in generated_paths}
    for relative in generated_paths:
        require_check_failure(
            module,
            outputs,
            lambda root, path=relative: (root / path).unlink(),
            f"missing=['{relative}']",
        )
        require_check_failure(
            module,
            outputs,
            lambda root, path=relative: (root / path).write_bytes(
                (root / path).read_bytes() + b"drift\n"
            ),
            f"drifted=['{relative}']",
        )

    for relative in (
        "docs/api/schemas/residency/unexpected.schema.json",
        "test/residency/contract/generated/unexpected.json",
    ):

        def add_extra(root: Path, path: str = relative) -> None:
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b"{}\n")

        require_check_failure(
            module,
            outputs,
            add_extra,
            f"unexpected=['{relative}']",
        )


def package_members_valid(
    members: list[tuple[str, bytes]], canonical_path: str, expected_digest: str
) -> bool:
    basename = canonical_path.rsplit("/", 1)[-1]
    matching = [
        member for member in members if member[0].rsplit("/", 1)[-1] == basename
    ]
    return (
        len(matching) == 1
        and matching[0][0] == canonical_path
        and hashlib.sha256(matching[0][1]).hexdigest() == expected_digest
    )


def require_package_member_matrix() -> None:
    canonical_path = "resources/residency_profiles.json"
    catalog = read("src/cpp/resources/residency_profiles.json")
    canonical = [(canonical_path, catalog)]
    require(
        package_members_valid(canonical, canonical_path, expected_catalog_sha256),
        "canonical package singleton was rejected",
    )
    rejected = (
        [],
        [(canonical_path, b"X" + catalog[1:])],
        [(canonical_path, catalog + b" ")],
        [(f"alternate/{canonical_path}", catalog)],
        [*canonical, *canonical],
        [*canonical, (f"alternate/{canonical_path}", catalog)],
    )
    for members in rejected:
        require(
            not package_members_valid(
                list(members), canonical_path, expected_catalog_sha256
            ),
            "invalid package member matrix was accepted",
        )
    require(
        not package_members_valid(canonical, canonical_path, "0" * 64),
        "wrong expected package digest was accepted",
    )


def configured_compiler() -> list[str]:
    configured = shlex.split(os.environ.get("CXX", "c++"))
    require(
        bool(configured) and shutil.which(configured[0]) is not None,
        "C++ compiler is unavailable",
    )
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
    compiler = Path(configured[0]).name.lower()
    include_root = repo_root / "src/cpp/include"
    sources = (
        repo_root / "src/cpp/server/residency/catalog.cpp",
        repo_root / "src/cpp/server/residency/explanations.cpp",
        repo_root / "src/cpp/server/residency/generated_contract.cpp",
        repo_root / "test/cpp/test_residency_contract_matrix.cpp",
    )
    if compiler in {"cl", "cl.exe"}:
        command = [
            *configured,
            "/nologo",
            "/std:c++17",
            "/W4",
            "/WX",
            "/EHsc",
            f"/I{include_root}",
        ]
        if environment.get("NLOHMANN_JSON_INCLUDE_DIR"):
            command.append(f"/I{environment['NLOHMANN_JSON_INCLUDE_DIR']}")
        if environment.get("MBEDTLS_INCLUDE_DIR"):
            command.append(f"/I{environment['MBEDTLS_INCLUDE_DIR']}")
        library = environment.get("MBEDCRYPTO_LIBRARY")
        require(library is not None, "MSVC mbedcrypto library is unavailable")
        command.extend(
            [
                f"/Fo{output.parent}{os.sep}",
                f"/Fd{output.with_suffix('.pdb')}",
                *(str(source) for source in sources),
                library,
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
        str(include_root),
    ]
    if environment.get("NLOHMANN_JSON_INCLUDE_DIR"):
        command.extend(["-I", environment["NLOHMANN_JSON_INCLUDE_DIR"]])
    if environment.get("MBEDTLS_INCLUDE_DIR"):
        command.extend(["-I", environment["MBEDTLS_INCLUDE_DIR"]])
    library = environment.get("MBEDCRYPTO_LIBRARY") or "-lmbedcrypto"
    command.extend([*(str(source) for source in sources), library, "-o", str(output)])
    return command


def require_compiler_command_contract() -> None:
    output = Path("residency-contract-matrix")
    environment = {
        "NLOHMANN_JSON_INCLUDE_DIR": "/fixture/nlohmann",
        "MBEDTLS_INCLUDE_DIR": "/fixture/mbedtls",
        "MBEDCRYPTO_LIBRARY": "/fixture/libmbedcrypto.a",
    }
    command = compiler_command(output, configured=["c++"], environment=environment)
    for include in ("/fixture/nlohmann", "/fixture/mbedtls"):
        include_index = command.index(include)
        require(
            command[include_index - 1] == "-I",
            f"POSIX matrix seam miswired include {include}",
        )
    require(
        "/fixture/libmbedcrypto.a" in command,
        "POSIX matrix seam ignored MBEDCRYPTO_LIBRARY",
    )


def require_cpp_matrix() -> None:
    with tempfile.TemporaryDirectory(prefix="residency-contract-matrix-") as directory:
        suffix = ".exe" if os.name == "nt" else ""
        output = Path(directory) / f"residency_contract_matrix{suffix}"
        compiled = subprocess.run(
            compiler_command(output),
            cwd=repo_root,
            check=False,
            text=True,
            capture_output=True,
        )
        require(compiled.returncode == 0, compiled.stdout + compiled.stderr)
        executed = subprocess.run(
            [str(output), str(repo_root / "src/cpp/resources/residency_profiles.json")],
            cwd=repo_root,
            check=False,
            text=True,
            capture_output=True,
        )
        require(executed.returncode == 0, executed.stdout + executed.stderr)


def posix_payload_check(content: str, search_root: str, canonical_path: str) -> bool:
    scope = re.escape(search_root)
    canonical = re.escape(canonical_path)
    return (
        "src/cpp/resources/residency_profiles.json" in content
        and re.search(
            rf'CATALOG_FILES=\$\(find\s+"{scope}"\s+-type\s+f\s+-name\s+'
            r'["\']?residency_profiles\.json["\']?\)',
            content,
        )
        is not None
        and re.search(
            r'CATALOG_FILE_COUNT=\$\([^\n]*"\$CATALOG_FILES"[^\n]*'
            r"\bwc\s+-l\b[^\n]*\)",
            content,
        )
        is not None
        and re.search(r'test\s+"\$CATALOG_FILE_COUNT"\s+-eq\s+1', content) is not None
        and re.search(rf'test\s+"\$CATALOG_FILES"\s+=\s+"{canonical}"', content)
        is not None
        and re.search(
            r'cmp\s+-s\s+"\$CATALOG_FILES"\s+'
            r'"\$GITHUB_WORKSPACE/src/cpp/resources/residency_profiles\.json"',
            content,
        )
        is not None
    )


def shell_archive_member_check(
    content: str, canonical_pattern: str, listing_pattern: str
) -> bool:
    return (
        re.search(
            rf'CATALOG_MEMBER="{canonical_pattern}"\s*\n\s*'
            rf"CATALOG_MEMBERS=\$\({listing_pattern}\s*\|\s*"
            r"grep\s+-E\s+'\(\^\|/\)residency_profiles\\\.json\$'\)\s*\n\s*"
            r'CATALOG_MEMBER_COUNT=\$\([^\n]*"\$CATALOG_MEMBERS"[^\n]*'
            r"\bwc\s+-l\b[^\n]*\)\s*\n\s*"
            r'test\s+"\$CATALOG_MEMBER_COUNT"\s+-eq\s+1\s*\n\s*'
            r'test\s+"\$CATALOG_MEMBERS"\s+=\s+"\$CATALOG_MEMBER"',
            content,
        )
        is not None
    )


def powershell_payload_check(
    content: str, search_root: str, canonical_path: str
) -> bool:
    scope = re.escape(search_root)
    canonical = re.escape(canonical_path)
    return (
        re.search(
            rf"\$catalogFiles\s*=\s*@\(Get-ChildItem\s+-LiteralPath\s+{scope}\s+"
            r"-Recurse\s+-File\s+"
            r'-Filter\s+["\']?residency_profiles\.json["\']?[^\n]*\)',
            content,
        )
        is not None
        and re.search(r"\$catalogFiles\.Count\s+-ne\s+1\b", content) is not None
        and re.search(rf"\$canonicalCatalog\s*=\s*[^\n]*{canonical}", content)
        is not None
        and re.search(
            r"\$catalogFiles\[0\]\.FullName\s+-ne\s+\$canonicalCatalog",
            content,
        )
        is not None
        and re.search(
            r"\$sourceCatalog\s*=\s*[^\n]*src\\cpp\\resources\\"
            r"residency_profiles\.json",
            content,
        )
        is not None
        and re.search(
            r"\$packagedHash\s*=\s*\(Get-FileHash\s+-LiteralPath\s+"
            r"\$catalogFiles\[0\]\.FullName\s+-Algorithm\s+SHA256\)\.Hash",
            content,
        )
        is not None
        and re.search(
            r"\$sourceHash\s*=\s*\(Get-FileHash\s+-LiteralPath\s+"
            r"\$sourceCatalog\s+-Algorithm\s+SHA256\)\.Hash",
            content,
        )
        is not None
        and re.search(r"\$packagedHash\s+-ne\s+\$sourceHash", content) is not None
    )


def windows_archive_member_check(content: str) -> bool:
    return (
        "[System.IO.Compression.ZipFile]::OpenRead" in content
        and re.search(
            r'\$catalogArchivePath\s*=\s*"\$archiveDir/'
            r'resources/residency_profiles\.json"',
            content,
        )
        is not None
        and re.search(
            r"\$catalogEntries\s*=\s*@\(\$archive\.Entries\s*\|\s*"
            r"Where-Object\s*\{[^}]+GetFileName\([^}]+\)\s+-eq\s+"
            r'"residency_profiles\.json"[^}]+\}\)',
            content,
        )
        is not None
        and re.search(r"\$catalogEntries\.Count\s+-ne\s+1\b", content) is not None
        and re.search(
            r"\$catalogEntries\[0\]\.FullName[^\n]+-ne\s+" r"\$catalogArchivePath",
            content,
        )
        is not None
        and re.search(r"\$archive\.Dispose\(\)", content) is not None
    )


def macos_archive_member_check(content: str) -> bool:
    return (
        re.search(
            r'CATALOG_MEMBER="Library/Application Support/Lemonade/resources/'
            r'residency_profiles\.json"',
            content,
        )
        is not None
        and re.search(
            r'PKG_PAYLOAD_FILES=\$\(pkgutil\s+--payload-files\s+"\$PKG_FILE"\)',
            content,
        )
        is not None
        and re.search(
            r'CATALOG_MEMBERS=\$\([^\n]*"\$PKG_PAYLOAD_FILES"[^\n]*'
            r"grep\s+-E\s+'\(\^\|/\)residency_profiles\\\.json\$'\)",
            content,
        )
        is not None
        and re.search(
            r'CATALOG_MEMBER_COUNT=\$\([^\n]*"\$CATALOG_MEMBERS"'
            r"[^\n]*\bwc\s+-l\b[^\n]*\)",
            content,
        )
        is not None
        and re.search(r'test\s+"\$CATALOG_MEMBER_COUNT"\s+-eq\s+1', content) is not None
        and re.search(r'test\s+"\$CATALOG_MEMBERS"\s+=\s+"\$CATALOG_MEMBER"', content)
        is not None
        and re.search(
            r'pkgutil\s+--expand-full\s+"\$PKG_FILE"\s+"\$PKG_EXPAND_PATH"',
            content,
        )
        is not None
        and re.search(
            r'CATALOG_PAYLOAD_FILES=\$\(find\s+"\$PKG_EXPAND_PATH"[^\n]+'
            r"\*/Library/Application Support/Lemonade/resources/"
            r"residency_profiles\.json[^\n]*\)",
            content,
        )
        is not None
        and re.search(
            r'CATALOG_PAYLOAD_COUNT=\$\([^\n]*"\$CATALOG_PAYLOAD_FILES"'
            r"[^\n]*\bwc\s+-l\b[^\n]*\)",
            content,
        )
        is not None
        and re.search(r'test\s+"\$CATALOG_PAYLOAD_COUNT"\s+-eq\s+1', content)
        is not None
        and re.search(
            r'cmp\s+-s\s+"\$CATALOG_PAYLOAD_FILES"\s+'
            r'"\$GITHUB_WORKSPACE/src/cpp/resources/residency_profiles\.json"',
            content,
        )
        is not None
    )


def docker_payload_check(content: str) -> bool:
    return (
        "src/cpp/resources/residency_profiles.json" in content
        and re.search(
            r"CATALOG_FILES=\$\(docker\s+exec\s+lemonade-test\s+find\s+"
            r"/opt/lemonade\s+-type\s+f\s+-name\s+"
            r'["\']?residency_profiles\.json["\']?\)',
            content,
        )
        is not None
        and re.search(
            r'CATALOG_FILE_COUNT=\$\([^\n]*"\$CATALOG_FILES"[^\n]*'
            r"\bwc\s+-l\b[^\n]*\)",
            content,
        )
        is not None
        and re.search(r'test\s+"\$CATALOG_FILE_COUNT"\s+-eq\s+1', content) is not None
        and re.search(
            r'test\s+"\$CATALOG_FILES"\s+=\s+'
            r'"/opt/lemonade/resources/residency_profiles\.json"',
            content,
        )
        is not None
        and re.search(
            r'docker\s+cp\s+"lemonade-test:\$CATALOG_FILES"\s+'
            r'"\$RUNNER_TEMP/residency_profiles\.json"',
            content,
        )
        is not None
        and re.search(
            r'cmp\s+-s\s+"\$RUNNER_TEMP/residency_profiles\.json"\s+'
            r'"\$GITHUB_WORKSPACE/src/cpp/resources/residency_profiles\.json"',
            content,
        )
        is not None
    )


def release_delivery_blocks(release: str) -> tuple[str, str]:
    rpm_start = release.find("- name: Install and verify Lemonade (.rpm)")
    require(rpm_start >= 0, "rpm install step anchor is unavailable")
    rpm_end = release.find("\n      - name:", rpm_start + 1)
    rpm = release[rpm_start : rpm_end if rpm_end >= 0 else len(release)]
    windows_start = release.find("  test-embeddable-windows:")
    require(windows_start >= 0, "windows embeddable job anchor is unavailable")
    next_job = re.search(
        r"^  [A-Za-z0-9_-]+:\s*$",
        release[windows_start + 3 :],
        flags=re.MULTILINE,
    )
    windows_end = (
        windows_start + 3 + next_job.start() if next_job is not None else len(release)
    )
    windows = release[windows_start:windows_end]
    return rpm, windows


def require_delivery_anchor_guards(release: str) -> None:
    fixtures = (
        (
            "- name: Install and verify Lemonade (.rpm)",
            "rpm install step anchor is unavailable",
        ),
        (
            "  test-embeddable-windows:",
            "windows embeddable job anchor is unavailable",
        ),
    )
    for anchor, expected in fixtures:
        mutated = release.replace(anchor, "missing-delivery-anchor", 1)
        try:
            release_delivery_blocks(mutated)
        except AssertionError as error:
            require(str(error) == expected, "delivery anchor diagnostic drifted")
        else:
            raise AssertionError(expected)


def require_delivery_matrix_wiring() -> None:
    cmake = read("CMakeLists.txt").decode("utf-8")
    cpp_path = "test/cpp/test_residency_contract_matrix.cpp"
    cmake_block = residency_matrix_cmake_block(cmake)
    require(cmake.count(cpp_path) == 1, "matrix C++ source wiring is not unique")
    for source in (
        "src/cpp/server/residency/catalog.cpp",
        "src/cpp/server/residency/explanations.cpp",
        "src/cpp/server/residency/generated_contract.cpp",
    ):
        require(source in cmake_block, f"matrix CMake block omits {source}")
    require(
        "add_executable(test_residency_contract_matrix" in cmake_block,
        "matrix CMake executable is unavailable",
    )
    require(
        "lemonade-digest-crypto" in cmake_block,
        "matrix CMake block omits digest crypto",
    )
    require(
        "nlohmann_json::nlohmann_json" in cmake_block,
        "matrix CMake block omits JSON support",
    )
    require(
        re.search(
            r"add_cpp_ci_test\(\s*ResidencyContractMatrix\s+CI\s+ON\s+"
            r"COMMAND\s+test_residency_contract_matrix\s*\)",
            cmake_block,
            flags=re.DOTALL,
        )
        is not None,
        "matrix CMake test is not in cpp-ci",
    )

    docs = read(".github/workflows/docs_and_style.yml").decode("utf-8")
    command = (
        "python -S test/residency/contract/test_cross_component_matrix_public_seam.py"
    )
    require(
        docs.count(command) == 1,
        "hosted cross-component matrix wiring is unavailable",
    )

    embeddable = read(".github/actions/smoke-test-embeddable/action.yml").decode(
        "utf-8"
    )
    deb = read(".github/actions/install-lemonade-deb/action.yml").decode("utf-8")
    macos = read(".github/actions/install-lemonade-server-dmg/action.yml").decode(
        "utf-8"
    )
    msi = read(".github/actions/install-lemonade-server-msi/action.yml").decode("utf-8")
    docker = read(".github/workflows/docker-build-smoke-test.yml").decode("utf-8")
    release = read(".github/workflows/cpp_server_build_test_release.yml").decode(
        "utf-8"
    )
    require_delivery_anchor_guards(release)
    rpm, windows = release_delivery_blocks(release)
    require(
        posix_payload_check(
            embeddable,
            "$DIR",
            "$DIR/resources/residency_profiles.json",
        )
        and shell_archive_member_check(
            embeddable,
            r"\$DIR/resources/residency_profiles\.json",
            r'tar\s+tzf\s+"\$ARCHIVE"',
        ),
        "POSIX embeddable delivery matrix wiring is incomplete",
    )
    require(
        posix_payload_check(
            deb,
            "$EXTRACT_PATH",
            "$EXTRACT_PATH/usr/share/lemonade-server/resources/residency_profiles.json",
        )
        and shell_archive_member_check(
            deb,
            r"\./usr/share/lemonade-server/resources/residency_profiles\.json",
            r'dpkg-deb\s+--fsys-tarfile\s+"\$DEB_FILE"\s*\|\s*tar\s+-tf\s+-',
        ),
        "Debian delivery matrix wiring is incomplete",
    )
    require(
        posix_payload_check(
            macos,
            "/Library/Application Support/Lemonade",
            "/Library/Application Support/Lemonade/resources/residency_profiles.json",
        )
        and macos_archive_member_check(macos),
        "macOS delivery matrix wiring is incomplete",
    )
    require(
        powershell_payload_check(
            msi,
            "$installPath",
            r"bin\resources\residency_profiles.json",
        ),
        "MSI delivery matrix wiring is incomplete",
    )
    require(
        posix_payload_check(
            rpm,
            "/opt",
            "/opt/share/lemonade-server/resources/residency_profiles.json",
        )
        and shell_archive_member_check(
            rpm,
            r"/opt/share/lemonade-server/resources/residency_profiles\.json",
            r'rpm\s+-qpl\s+"\$RPM_FILE"',
        ),
        "RPM delivery matrix wiring is incomplete",
    )
    require(
        powershell_payload_check(
            windows,
            "$archiveDir",
            r"resources\residency_profiles.json",
        )
        and windows_archive_member_check(windows),
        "Windows embeddable delivery matrix wiring is incomplete",
    )
    require(
        docker_payload_check(docker),
        "Docker delivery matrix wiring is incomplete",
    )


def verify_cross_component_matrix() -> None:
    require_canonical_generated_contract()
    require_generator_check_matrix()
    require_package_member_matrix()
    require_compiler_command_contract()
    require_cpp_matrix()
    require_delivery_matrix_wiring()


def main() -> int:
    try:
        verify_cross_component_matrix()
    except (
        AssertionError,
        ImportError,
        KeyError,
        OSError,
        subprocess.SubprocessError,
        TypeError,
        ValueError,
    ) as error:
        sys.stderr.write(f"{type(error).__name__}: {error}\n")
        sys.stderr.write(failure)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
