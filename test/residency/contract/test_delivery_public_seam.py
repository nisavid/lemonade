import json
import re
import sys
import uuid
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[3]
FAILURE = "TASK-012 residency contract delivery wiring is unavailable\n"
CATALOG_PATH = REPO_ROOT / "src/cpp/resources/residency_profiles.json"
CHECKOUT_V5_SHA = "fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09"


def strict_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


def require_match(pattern: str, content: str, message: str) -> re.Match[str]:
    match = re.search(pattern, content, flags=re.MULTILINE | re.DOTALL)
    require(match is not None, message)
    return match


def workflow_job(workflow: str, job_name: str) -> str:
    return require_match(
        rf"^  {re.escape(job_name)}:\s*$\n(?P<body>.*?)(?=^  [A-Za-z0-9_-]+:\s*$|\Z)",
        workflow,
        f"{job_name} job is unavailable",
    ).group("body")


def require_inactive_catalog() -> None:
    catalog = json.loads(
        CATALOG_PATH.read_text(encoding="utf-8"), object_pairs_hook=strict_object
    )
    require(catalog["schema"] == "residency.profiles/1.0", "catalog schema drifted")
    units = catalog["promotion_units"]
    require(isinstance(units, list), "promotion units are not a list")
    require(len(units) == 39, "promotion-unit count drifted")
    identifiers = [unit["id"] for unit in units]
    require(
        all(isinstance(identifier, str) and identifier for identifier in identifiers),
        "promotion-unit identity is invalid",
    )
    require(len(set(identifiers)) == 39, "promotion-unit identities are not unique")
    require(
        all(unit["capability_level"] == "unsupported" for unit in units),
        "a promotion unit raised capability",
    )
    require(
        all(unit["delivery_state"] == "absent" for unit in units),
        "a promotion unit raised delivery",
    )


def require_normal_and_embeddable_build_wiring() -> None:
    cmake = read("CMakeLists.txt")
    sources_core = require_match(
        r"^set\(SOURCES_CORE\s*\n(?P<body>.*?)^\)",
        cmake,
        "SOURCES_CORE is unavailable",
    ).group("body")
    for source in (
        "src/cpp/server/residency/generated_contract.cpp",
        "src/cpp/server/residency/catalog.cpp",
        "src/cpp/server/residency/explanations.cpp",
    ):
        require(
            re.search(rf"^\s*{re.escape(source)}\s*$", sources_core, re.MULTILINE)
            is not None,
            f"normal server build omits {source}",
        )

    embeddable = require_match(
        r"^add_custom_target\(embeddable\s*\n(?P<body>.*?)^\)",
        cmake,
        "embeddable target is unavailable",
    ).group("body")
    require_match(
        r"COMMAND\s+\$\{CMAKE_COMMAND\}\s+-E\s+copy\s+"
        r'"\$<TARGET_FILE_DIR:lemond>/resources/residency_profiles\.json"\s+'
        r'"\$\{EMBEDDABLE_STAGING_DIR\}/resources/"',
        embeddable,
        "embeddable archive omits the residency catalog",
    )


def require_windows_installer_wiring() -> None:
    wix = read("src/cpp/installer/Product.wxs.in")
    resource_group = require_match(
        r'<ComponentGroup\s+Id="ResourceComponents">(?P<body>.*?)</ComponentGroup>',
        wix,
        "WiX resource component group is unavailable",
    ).group("body")
    components = re.findall(
        r"<Component\b[^>]*>.*?</Component>", resource_group, re.DOTALL
    )
    catalog_components = [
        component
        for component in components
        if 'Source="$(var.BuildDir)\\Release\\resources\\residency_profiles.json"'
        in component
    ]
    require(len(catalog_components) == 1, "WiX does not include one residency catalog")
    component = catalog_components[0]
    component_header = require_match(
        r"<Component\b(?P<attributes>[^>]*)>",
        component,
        "WiX residency component is malformed",
    ).group("attributes")
    require(
        'Directory="ResourcesDir"' in component_header, "WiX catalog directory drifted"
    )
    guid_match = require_match(
        r'\bGuid="(?P<guid>[0-9A-Fa-f-]{36})"',
        component_header,
        "WiX residency component lacks a fixed GUID",
    )
    uuid.UUID(guid_match.group("guid"))
    require_match(
        r'<File\b[^>]*\bId="residency_profiles\.json"[^>]*\bKeyPath="yes"[^>]*/>',
        component,
        "WiX residency file is not the component key path",
    )

    msi_action = read(".github/actions/install-lemonade-server-msi/action.yml")
    expected_install_items = require_match(
        r"\$expectedItems\s*=\s*@\((?P<body>.*?)^\s*\)",
        msi_action,
        "MSI installed-file checks are unavailable",
    ).group("body")
    require(
        '"bin\\resources\\residency_profiles.json"' in expected_install_items,
        "MSI installed-file checks omit the residency catalog",
    )


def require_archive_and_package_checks() -> None:
    posix_smoke = read(".github/actions/smoke-test-embeddable/action.yml")
    require(
        'test -f "$DIR/resources/residency_profiles.json"' in posix_smoke,
        "POSIX embeddable smoke check omits the residency catalog",
    )

    release_workflow = read(".github/workflows/cpp_server_build_test_release.yml")
    expected_arrays = re.findall(
        r"\$expected\s*=\s*@\((?P<body>.*?)^\s*\)",
        release_workflow,
        flags=re.MULTILINE | re.DOTALL,
    )
    embeddable_arrays = [
        array
        for array in expected_arrays
        if '"$archiveDir\\resources\\server_models.json"' in array
    ]
    require(
        len(embeddable_arrays) == 1,
        "Windows embeddable installed-file checks are unavailable",
    )
    windows_expected = embeddable_arrays[0]
    require(
        '"$archiveDir\\resources\\residency_profiles.json"' in windows_expected,
        "Windows embeddable smoke check omits the residency catalog",
    )

    deb_action = read(".github/actions/install-lemonade-deb/action.yml")
    require(
        'test -f "$EXTRACT_PATH/usr/share/lemonade-server/resources/residency_profiles.json"'
        in deb_action,
        "Debian package check omits the residency catalog",
    )
    require_match(
        r"DEB_FILE=\$\(ls\s+lemonade-server_\$\{VERSION\}\*_amd64\.deb\s+"
        r"2>/dev/null\s*\|\s*head\s+-n1\s*\|\|\s*true\)",
        deb_action,
        "Debian package discovery is not safe under pipefail",
    )

    rpm_catalog = "/opt/share/lemonade-server/resources/residency_profiles.json"
    require(
        f"test -f {rpm_catalog}" in release_workflow,
        "RPM installed-file check omits the residency catalog",
    )
    require_match(
        rf'rpm\s+-qpl\s+"\$RPM_FILE"\s*\|\s*grep\s+-Fxq?\s+"{re.escape(rpm_catalog)}"',
        release_workflow,
        "RPM payload check omits the residency catalog",
    )

    macos_action = read(".github/actions/install-lemonade-server-dmg/action.yml")
    require(
        'test -f "/Library/Application Support/Lemonade/resources/residency_profiles.json"'
        in macos_action,
        "macOS package check omits the residency catalog",
    )


def require_artifact_consumer_checkout_hardening() -> None:
    workflow = read(".github/workflows/cpp_server_build_test_release.yml")
    for job_name in ("test-rpm-package", "test-embeddable-windows"):
        job = workflow_job(workflow, job_name)
        checkout = require_match(
            rf"^      - uses: actions/checkout@{CHECKOUT_V5_SHA} # v5\s*$\n"
            r"^        with:\s*$\n(?P<inputs>(?:^          [^\n]+\n)+)",
            job,
            f"{job_name} does not use the immutable checkout v5 release",
        )
        require(
            re.search(
                r"^          persist-credentials:\s*false\s*$",
                checkout.group("inputs"),
                flags=re.MULTILINE,
            )
            is not None,
            f"{job_name} persists the checkout credential",
        )
        require(
            len(re.findall(r"actions/checkout@", job)) == 1,
            f"{job_name} has an unexpected checkout path",
        )


def require_hosted_generation_checks() -> None:
    workflow = read(".github/workflows/docs_and_style.yml")
    job = require_match(
        r"^  portable-residency-inventory:\s*\n(?P<body>.*?)(?=^  [A-Za-z0-9_-]+:\s*$)",
        workflow,
        "portable residency CI job is unavailable",
    ).group("body")

    pinned_fetch = job.find(
        "git fetch --no-tags --depth=1 https://github.com/lemonade-sdk/lemonade.git "
        "a505bbc702cc1fcd44ef73c44defabc98c36d505"
    )
    dependency = require_match(
        r"python(?:3)?\s+-m\s+pip\s+install\s+jsonschema\b",
        job,
        "generated-contract CI dependency is unavailable",
    ).start()
    generated_test = require_match(
        r"python(?:3)?(?:\s+-W\s+error)?\s+"
        r"test/residency/contract/test_generated_contract\.py\b",
        job,
        "generated-contract public test is absent from hosted CI",
    ).start()
    generator_check = require_match(
        r"python(?:3)?\s+tools/generate_residency_contract\.py\s+--check\b",
        job,
        "generator check is absent from hosted CI",
    ).start()
    delivery_test = require_match(
        r"python(?:3)?\s+-S\s+"
        r"test/residency/contract/test_delivery_public_seam\.py\b",
        job,
        "delivery public test is absent from hosted CI",
    ).start()
    clean_diff = require_match(
        r"git\s+diff\s+--exit-code\b",
        job,
        "generated-output clean-diff check is absent from hosted CI",
    ).start()
    require(pinned_fetch >= 0, "pinned upstream source fetch is unavailable")
    require(
        dependency < generated_test
        and pinned_fetch
        < generated_test
        < generator_check
        < delivery_test
        < clean_diff,
        "hosted generation checks do not follow the pinned upstream fetch",
    )


def verify_delivery_contract() -> None:
    require_inactive_catalog()
    require_normal_and_embeddable_build_wiring()
    require_windows_installer_wiring()
    require_archive_and_package_checks()
    require_artifact_consumer_checkout_hardening()
    require_hosted_generation_checks()


def main() -> int:
    try:
        verify_delivery_contract()
    except (AssertionError, KeyError, OSError, TypeError, ValueError) as error:
        sys.stderr.write(f"{type(error).__name__}: {error}\n")
        sys.stderr.write(FAILURE)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
