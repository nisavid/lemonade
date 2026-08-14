"""Derive normalized support from immutable upstream source objects."""

from __future__ import annotations

import ast
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .contract import (
    SourceSupportKey,
    fail,
    require_exact_keys,
    require_list,
    require_mapping,
    require_string,
    require_string_list,
    require_unique,
)
from .json_contract import parse_json_object

MAX_GIT_STDERR_LENGTH = 320


def _bounded_git_stderr(error: subprocess.CalledProcessError) -> str:
    stderr = error.stderr
    if isinstance(stderr, bytes):
        stderr = stderr.decode(errors="replace")
    detail = " ".join(stderr.split()) if isinstance(stderr, str) else ""
    if not detail:
        detail = f"git exited with status {error.returncode} without stderr"
    if len(detail) > MAX_GIT_STDERR_LENGTH:
        detail = f"{detail[: MAX_GIT_STDERR_LENGTH - 3]}..."
    return detail


@dataclass(frozen=True)
class ModelTypeRules:
    """Label-precedence rules derived from the frozen model_types.h."""

    precedence: tuple[tuple[frozenset[str], str], ...]
    default: str

    def resolve(self, labels: set[str]) -> str:
        for candidate_labels, model_type in self.precedence:
            if labels & candidate_labels:
                return model_type
        return self.default


@dataclass(frozen=True)
class FrozenSourceClosure:
    """Support and model-type facts derived from one immutable source commit."""

    baseline: str
    support: set[SourceSupportKey]
    empty_support_recipes: set[str]
    recipe_model_types: dict[str, set[str]]
    descriptor_recipes: set[str]
    collection_recipes: set[str]


def git_blob(repo: Path, commit: str, path: str) -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", f"{commit}:{path}"],
            cwd=repo,
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as error:
        fail(
            f"cannot resolve frozen source blob {path}; "
            f"git stderr: {_bounded_git_stderr(error)}"
        )
    return result.stdout.strip()


def require_git_commit(repo: Path, commit: str) -> None:
    result = subprocess.run(
        ["git", "cat-file", "-e", f"{commit}^{{commit}}"],
        cwd=repo,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        fail(
            f"source support baseline {commit} is unavailable locally; "
            "run `git fetch --no-tags --depth=1 "
            f"https://github.com/lemonade-sdk/lemonade.git {commit}` and retry"
        )


def git_text(repo: Path, commit: str, path: str) -> str:
    try:
        result = subprocess.run(
            ["git", "show", f"{commit}:{path}"],
            cwd=repo,
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as error:
        fail(
            f"cannot read frozen source {path}; "
            f"git stderr: {_bounded_git_stderr(error)}"
        )
    return result.stdout


def strip_cmake_comment(line: str) -> str:
    in_string = False
    escaped = False
    result: list[str] = []
    for character in line:
        if escaped:
            result.append(character)
            escaped = False
            continue
        if character == "\\" and in_string:
            result.append(character)
            escaped = True
            continue
        if character == '"':
            in_string = not in_string
            result.append(character)
            continue
        if character == "#" and not in_string:
            break
        result.append(character)
    if in_string:
        fail("unterminated CMake string in LEMON_BACKENDS")
    return "".join(result).strip()


def parse_lemon_backends(cmake: str) -> list[tuple[str, str]]:
    lines = cmake.splitlines()
    start: int | None = None
    for index, line in enumerate(lines):
        if re.fullmatch(r"\s*set\s*\(\s*LEMON_BACKENDS\s*(?:#.*)?", line):
            if start is not None:
                fail("CMakeLists.txt contains multiple LEMON_BACKENDS declarations")
            start = index + 1
    if start is None:
        fail("CMakeLists.txt does not declare LEMON_BACKENDS in the supported form")

    backends: list[tuple[str, str]] = []
    for line in lines[start:]:
        stripped = strip_cmake_comment(line)
        if not stripped:
            continue
        if stripped == ")":
            if not backends:
                fail("LEMON_BACKENDS must not be empty")
            recipes = [recipe for recipe, _ in backends]
            stems = [stem for _, stem in backends]
            require_unique(recipes, "LEMON_BACKENDS recipes")
            require_unique(stems, "LEMON_BACKENDS stems")
            return backends
        match = re.fullmatch(r'"([^"|]+)\|([^"|]+)"', stripped)
        if match is None:
            fail(f"unsupported LEMON_BACKENDS entry: {stripped}")
        backends.append((match.group(1), match.group(2)))
    fail("unterminated LEMON_BACKENDS declaration")


class CppInitializerParser:
    """Parse the string-and-brace subset used by BackendDescriptor support."""

    def __init__(self, source: str, offset: int):
        self.source = source
        self.offset = offset

    def skip_trivia(self) -> None:
        while self.offset < len(self.source):
            if self.source[self.offset].isspace():
                self.offset += 1
                continue
            if self.source.startswith("//", self.offset):
                newline = self.source.find("\n", self.offset + 2)
                self.offset = len(self.source) if newline < 0 else newline + 1
                continue
            if self.source.startswith("/*", self.offset):
                end = self.source.find("*/", self.offset + 2)
                if end < 0:
                    fail("unterminated C++ block comment")
                self.offset = end + 2
                continue
            break

    def parse_string(self) -> str:
        start = self.offset
        self.offset += 1
        escaped = False
        while self.offset < len(self.source):
            character = self.source[self.offset]
            self.offset += 1
            if escaped:
                escaped = False
                continue
            if character == "\\":
                escaped = True
                continue
            if character == '"':
                literal = self.source[start : self.offset]
                try:
                    value = ast.literal_eval(literal)
                except (SyntaxError, ValueError) as error:
                    fail(f"unsupported C++ string literal {literal!r}: {error}")
                if not isinstance(value, str):
                    fail("C++ initializer string did not decode to text")
                return value
        fail("unterminated C++ string literal")

    def parse_value(self) -> Any:
        self.skip_trivia()
        if self.offset >= len(self.source):
            fail("unexpected end of C++ initializer")
        character = self.source[self.offset]
        if character == '"':
            return self.parse_string()
        if character != "{":
            excerpt = self.source[self.offset : self.offset + 24].splitlines()[0]
            fail(f"unsupported C++ initializer token near {excerpt!r}")

        self.offset += 1
        result: list[Any] = []
        expect_value = True
        while True:
            self.skip_trivia()
            if self.offset >= len(self.source):
                fail("unterminated C++ braced initializer")
            character = self.source[self.offset]
            if character == "}":
                self.offset += 1
                return result
            if not expect_value:
                if character != ",":
                    fail("C++ initializer members must be comma-separated")
                self.offset += 1
                self.skip_trivia()
                if self.offset < len(self.source) and self.source[self.offset] == "}":
                    self.offset += 1
                    return result
            result.append(self.parse_value())
            expect_value = False


def marker_offset(
    source: str, marker: str, label: str, *, required: bool = True
) -> int | None:
    count = source.count(marker)
    if count == 0 and not required:
        return None
    if count != 1:
        fail(f"{label} must contain exactly one {marker} field marker")
    return source.index(marker) + len(marker)


def parse_marked_value(
    source: str, marker: str, label: str, *, required: bool = True
) -> Any:
    offset = marker_offset(source, marker, label, required=required)
    if offset is None:
        return None
    return CppInitializerParser(source, offset).parse_value()


def initializer_string_list(value: Any, label: str) -> list[str]:
    members = require_list(value, label)
    if not all(isinstance(member, str) and member for member in members):
        fail(f"{label} must contain only nonempty string literals")
    result = list(members)
    require_unique(result, label)
    return result


def parse_descriptor_devices(raw_devices: Any, row_label: str) -> dict[str, list[str]]:
    device_rows = require_list(raw_devices, f"{row_label} devices")
    if not device_rows:
        fail(f"{row_label} devices must not be empty")

    devices: dict[str, list[str]] = {}
    for raw_device in device_rows:
        device = require_list(raw_device, f"{row_label} device entry")
        if len(device) != 2:
            fail(f"{row_label} device entry must have type and family set")
        accelerator = require_string(device[0], f"{row_label} accelerator")
        if accelerator in devices:
            fail(f"{row_label} repeats accelerator {accelerator}")
        architectures = initializer_string_list(
            device[1], f"{row_label} {accelerator} architectures"
        )
        devices[accelerator] = architectures or ["*"]
    return devices


def parse_descriptor_arch_gates(
    raw_gates: Any,
    row_label: str,
    operating_systems: list[str],
    rocm_channels: list[str],
) -> dict[str, tuple[set[str], set[str]]]:
    gates: dict[str, tuple[set[str], set[str]]] = {}
    for raw_gate in require_list(raw_gates, f"{row_label} arch gates"):
        gate = require_list(raw_gate, f"{row_label} arch gate")
        if len(gate) != 2:
            fail(f"{row_label} arch gate must have architecture and restrictions")
        architecture = require_string(gate[0], f"{row_label} gated architecture")
        if architecture in gates:
            fail(f"{row_label} repeats arch gate {architecture}")
        restrictions = require_list(gate[1], f"{row_label} {architecture} gate")
        if len(restrictions) != 2:
            fail(f"{row_label} arch gate must contain OS and channel sets")
        gate_os = set(
            initializer_string_list(
                restrictions[0], f"{row_label} {architecture} gate OS"
            )
        )
        gate_channels = set(
            initializer_string_list(
                restrictions[1], f"{row_label} {architecture} gate channels"
            )
        )
        if not gate_os <= set(operating_systems):
            fail(f"{row_label} {architecture} gate names an unsupported OS")
        if not gate_channels <= set(rocm_channels):
            fail(f"{row_label} {architecture} gate names an unknown ROCm channel")
        gates[architecture] = (gate_os, gate_channels)
    return gates


def validate_descriptor_gate_architectures(
    devices: dict[str, list[str]],
    gates: dict[str, tuple[set[str], set[str]]],
    row_label: str,
) -> None:
    declared_architectures = {
        architecture
        for architectures in devices.values()
        for architecture in architectures
        if architecture != "*"
    }
    if not set(gates) <= declared_architectures:
        fail(f"{row_label} has a gate for an undeclared architecture")


def normalize_descriptor_support_row(
    recipe: str,
    backend: str,
    operating_systems: list[str],
    devices: dict[str, list[str]],
    gates: dict[str, tuple[set[str], set[str]]],
    rocm_channels: list[str],
) -> list[SourceSupportKey]:
    row_channels = (
        rocm_channels if backend == "rocm" and len(rocm_channels) > 1 else ["single"]
    )
    normalized: list[SourceSupportKey] = []
    for operating_system in operating_systems:
        for accelerator, architectures in devices.items():
            for architecture in architectures:
                gate_os, gate_channels = gates.get(architecture, (set(), set()))
                if gate_os and operating_system not in gate_os:
                    continue
                for channel in row_channels:
                    channel_allowed = (
                        not gate_channels
                        or channel in gate_channels
                        or (
                            channel == "single"
                            and len(rocm_channels) <= 1
                            and gate_channels == set(rocm_channels)
                        )
                    )
                    if not channel_allowed:
                        continue
                    normalized.append(
                        (
                            recipe,
                            backend,
                            operating_system,
                            accelerator,
                            architecture,
                            channel,
                        )
                    )
    return normalized


def parse_descriptor_support_row(
    raw_row: Any,
    row_label: str,
    recipe: str,
    rocm_channels: list[str],
) -> list[SourceSupportKey]:
    row = require_list(raw_row, row_label)
    if len(row) not in {4, 5}:
        fail(f"{row_label} must have four fields plus optional arch gates")
    backend = require_string(row[0], f"{row_label} backend")
    operating_systems = initializer_string_list(row[1], f"{row_label} OS set")
    if not operating_systems:
        fail(f"{row_label} OS set must not be empty")
    devices = parse_descriptor_devices(row[2], row_label)
    require_string(row[3], f"{row_label} device summary")
    gates = (
        parse_descriptor_arch_gates(row[4], row_label, operating_systems, rocm_channels)
        if len(row) == 5
        else {}
    )
    validate_descriptor_gate_architectures(devices, gates, row_label)
    return normalize_descriptor_support_row(
        recipe,
        backend,
        operating_systems,
        devices,
        gates,
        rocm_channels,
    )


def parse_descriptor_support(
    source: str, expected_recipe: str, label: str
) -> list[SourceSupportKey]:
    recipe = parse_marked_value(source, "/*recipe*/", label)
    if recipe != expected_recipe:
        fail(f"{label} recipe is {recipe!r}, expected {expected_recipe!r}")
    support = require_list(
        parse_marked_value(source, "/*support*/", label), f"{label} support"
    )
    rocm_value = parse_marked_value(source, "/*rocm_channels*/", label, required=False)
    rocm_channels = (
        []
        if rocm_value is None
        else initializer_string_list(rocm_value, f"{label} rocm_channels")
    )

    normalized: list[SourceSupportKey] = []
    for row_index, raw_row in enumerate(support):
        row_label = f"{label} support row {row_index}"
        normalized.extend(
            parse_descriptor_support_row(raw_row, row_label, recipe, rocm_channels)
        )

    serialized = [source_support_text(key) for key in normalized]
    require_unique(serialized, f"{label} normalized source-support rows")
    return normalized


def source_support_text(key: SourceSupportKey) -> str:
    recipe, backend, operating_system, accelerator, architecture, channel = key
    return (
        f"{recipe}:{backend}/{operating_system}/{accelerator}/"
        f"{architecture}/{channel}"
    )


def source_support_from_mapping(value: Any, label: str) -> set[SourceSupportKey]:
    support = require_mapping(value, label)
    require_exact_keys(
        support,
        {"recipe", "backend", "os", "accelerator", "architectures", "channels"},
        label,
    )
    architectures = require_string_list(
        support["architectures"], f"{label}.architectures"
    )
    if "*" in architectures and architectures != ["*"]:
        fail(f"{label}.architectures may use '*' only as the sole member")
    channels = require_string_list(support["channels"], f"{label}.channels")
    recipe = require_string(support["recipe"], f"{label}.recipe")
    backend = require_string(support["backend"], f"{label}.backend")
    operating_system = require_string(support["os"], f"{label}.os")
    accelerator = require_string(support["accelerator"], f"{label}.accelerator")
    return {
        (recipe, backend, operating_system, accelerator, architecture, channel)
        for architecture in architectures
        for channel in channels
    }


def platform_source_support(
    recipe: str, backend: str, platform: dict[str, Any], label: str
) -> set[SourceSupportKey]:
    require_exact_keys(
        platform,
        {"os", "provider", "accelerator", "architectures", "channels", "topology"},
        label,
    )
    require_string(platform["provider"], f"{label}.provider")
    require_string(platform["topology"], f"{label}.topology")
    architectures = require_string_list(
        platform["architectures"], f"{label}.architectures"
    )
    if "*" in architectures and architectures != ["*"]:
        fail(f"{label}.architectures may use '*' only as the sole member")
    channels = require_string_list(platform["channels"], f"{label}.channels")
    operating_system = require_string(platform["os"], f"{label}.os")
    accelerator = require_string(platform["accelerator"], f"{label}.accelerator")
    return {
        (recipe, backend, operating_system, accelerator, architecture, channel)
        for architecture in architectures
        for channel in channels
    }


def parse_descriptor_default_labels(source: str, label: str) -> list[str]:
    return initializer_string_list(
        parse_marked_value(source, "/*default_labels*/", label),
        f"{label} default_labels",
    )


def model_type_rule_function(source: str) -> str:
    function_marker = "inline ModelType get_model_type_from_labels"
    function_start = source.find(function_marker)
    if function_start < 0:
        fail("model_types.h does not define get_model_type_from_labels")
    function_end = source.find("// Fallback device type", function_start)
    if function_end < 0:
        fail("model_types.h model-type rule boundary is missing")
    return source[function_start:function_end]


def model_type_enum_strings(source: str) -> dict[str, str]:
    enum_strings = dict(
        re.findall(r'case\s+ModelType::([A-Z_]+):\s+return\s+"([^"]+)"\s*;', source)
    )
    if not enum_strings:
        fail("model_types.h does not expose ModelType string mappings")
    return enum_strings


def parse_model_type_rule(
    match: re.Match[str], enum_strings: dict[str, str]
) -> tuple[frozenset[str], str]:
    condition = match.group("condition")
    supported_condition = re.compile(
        r'\s*label\s*==\s*"[^"]+"' r'(?:\s*\|\|\s*label\s*==\s*"[^"]+")*\s*'
    )
    if supported_condition.fullmatch(condition) is None:
        fail("model_types.h contains an unsupported label-rule condition")
    parsed_labels = re.findall(r'label\s*==\s*"([^"]+)"', condition)
    labels = frozenset(parsed_labels)
    if not labels:
        fail("model_types.h contains a label rule without literal labels")
    if len(labels) != len(parsed_labels):
        fail("model_types.h label rule repeats a literal label")
    enum_name = match.group("model_type")
    if enum_name not in enum_strings:
        fail(f"model_types.h label rule uses unmapped ModelType::{enum_name}")
    return labels, enum_strings[enum_name]


def parse_default_model_type(function: str, enum_strings: dict[str, str]) -> str:
    default_match = re.search(
        r"return\s+ModelType::([A-Z_]+)\s*;\s*\}\s*$", function.strip()
    )
    if default_match is None or default_match.group(1) not in enum_strings:
        fail("model_types.h contains no parseable default model type")
    return enum_strings[default_match.group(1)]


def parse_model_type_rules(source: str) -> ModelTypeRules:
    function = model_type_rule_function(source)
    enum_strings = model_type_enum_strings(source)

    rule_pattern = re.compile(
        r"if\s*\((?P<condition>.*?)\)\s*\{\s*"
        r"return\s+ModelType::(?P<model_type>[A-Z_]+)\s*;\s*\}",
        re.DOTALL,
    )
    rule_matches = list(rule_pattern.finditer(function))
    if len(rule_matches) != len(re.findall(r"\bif\s*\(", function)):
        fail("model_types.h contains an unparsed if body in its label rules")
    precedence = [parse_model_type_rule(match, enum_strings) for match in rule_matches]
    if not precedence:
        fail("model_types.h contains no parseable label-precedence rules")
    return ModelTypeRules(
        tuple(precedence), parse_default_model_type(function, enum_strings)
    )


def model_labels(model: dict[str, Any], label: str) -> set[str]:
    labels = set(
        require_string_list(model.get("labels", []), f"{label}.labels", nonempty=False)
    )
    for legacy_field, normalized_label in (
        ("reasoning", "reasoning"),
        ("vision", "vision"),
        ("embedding", "embeddings"),
        ("reranking", "reranking"),
    ):
        value = model.get(legacy_field, False)
        if not isinstance(value, bool):
            fail(f"{label}.{legacy_field} must be boolean when present")
        if value:
            labels.add(normalized_label)
    return labels


def deployment_model_type(
    labels: set[str], default_labels: list[str], rules: ModelTypeRules
) -> str:
    definitive_defaults = {
        rules.resolve({label})
        for label in default_labels
        if rules.resolve({label}) != rules.default
    }
    if len(definitive_defaults) > 1:
        fail("backend default labels resolve to conflicting deployment model types")
    if definitive_defaults:
        return next(iter(definitive_defaults))

    model_type = rules.resolve(labels)
    if model_type == "classification":
        model_type = rules.resolve(labels - {"classification", "classifier"})
    return model_type


def derive_static_recipe_model_types(
    server_models_source: str,
    descriptor_labels: dict[str, list[str]],
    rules: ModelTypeRules,
) -> dict[str, set[str]]:
    models = parse_json_object(server_models_source, "server_models.json")
    derived: dict[str, set[str]] = {}
    for model_name, raw_model in models.items():
        model = require_mapping(raw_model, f"server_models.json[{model_name}]")
        recipe = require_string(
            model.get("recipe"), f"server_models.json[{model_name}].recipe"
        )
        if recipe not in descriptor_labels:
            continue
        labels = model_labels(model, f"server_models.json[{model_name}]")
        derived.setdefault(recipe, set()).add(
            deployment_model_type(labels, descriptor_labels[recipe], rules)
        )
    return derived


def parse_collection_constants(source: str) -> tuple[dict[str, str], list[str]]:
    constant_matches = re.findall(
        r"constexpr\s+const\s+char\s*\*\s*"
        r'(COLLECTION_[A-Z0-9_]+_MODEL_RECIPE)\s*=\s*"([^"]+)"\s*;',
        source,
    )
    if not constant_matches:
        fail("model_types.h defines no collection recipe constants")
    constant_names = [name for name, _ in constant_matches]
    constant_values = [value for _, value in constant_matches]
    require_unique(constant_names, "collection recipe constant names")
    require_unique(constant_values, "collection recipe constant values")
    return dict(constant_matches), constant_values


def parse_collection_helpers(source: str) -> dict[str, str]:
    helper_pattern = re.compile(
        r"inline\s+bool\s+(is_[a-z0-9_]+_collection_recipe)\s*"
        r"\(\s*const\s+std::string&\s+recipe\s*\)\s*\{(?P<body>.*?)\}",
        re.DOTALL,
    )
    helpers = {
        match.group(1): match.group("body") for match in helper_pattern.finditer(source)
    }
    if "is_model_collection_recipe" not in helpers:
        fail("model_types.h does not define the collection aggregate predicate")
    return helpers


def parse_collection_helper_constants(
    leaf_helpers: dict[str, str], constants: dict[str, str]
) -> dict[str, str]:
    helper_constants: dict[str, str] = {}
    for helper, body in leaf_helpers.items():
        match = re.fullmatch(
            r"\s*return\s+recipe\s*==\s*"
            r"(COLLECTION_[A-Z0-9_]+_MODEL_RECIPE)\s*;\s*",
            body,
        )
        if match is None:
            fail(f"collection helper {helper} must be an exact equality predicate")
        constant = match.group(1)
        if constant not in constants:
            fail(f"collection helper {helper} references unknown constant {constant}")
        helper_constants[helper] = constant
    return helper_constants


def validate_collection_aggregate(aggregate: str, leaf_helpers: dict[str, str]) -> None:
    aggregate_match = re.fullmatch(r"\s*return\s+(.*?)\s*;\s*", aggregate, re.DOTALL)
    if aggregate_match is None:
        fail("collection aggregate predicate must contain one return expression")
    terms = [term.strip() for term in aggregate_match.group(1).split("||")]
    called_helpers: list[str] = []
    for term in terms:
        call = re.fullmatch(r"(is_[a-z0-9_]+_collection_recipe)\(recipe\)", term)
        if call is None:
            fail("collection aggregate predicate must OR exact helper calls")
        called_helpers.append(call.group(1))
    require_unique(called_helpers, "collection aggregate predicate helpers")
    if set(called_helpers) != set(leaf_helpers):
        fail("collection aggregate predicate must include every helper exactly once")


def parse_collection_recipes(source: str) -> set[str]:
    """Derive the closed collection recipe set and prove its helper predicates."""

    constants, constant_values = parse_collection_constants(source)
    helpers = parse_collection_helpers(source)
    leaf_helpers = {
        helper: body
        for helper, body in helpers.items()
        if helper != "is_model_collection_recipe"
    }
    if len(leaf_helpers) != len(constants):
        fail("collection helpers must correspond one-to-one with recipe constants")

    helper_constants = parse_collection_helper_constants(leaf_helpers, constants)
    require_unique(
        list(helper_constants.values()), "collection helper constant references"
    )
    if set(helper_constants.values()) != set(constants):
        fail("collection helpers must cover every recipe constant exactly once")

    validate_collection_aggregate(helpers["is_model_collection_recipe"], leaf_helpers)
    return set(constant_values)


def validate_flm_slot_policy(repo: Path, baseline: str) -> set[str]:
    descriptor = git_text(
        repo, baseline, "src/cpp/include/lemon/backends/fastflowlm/fastflowlm.h"
    )
    server_header = git_text(
        repo,
        baseline,
        "src/cpp/include/lemon/backends/fastflowlm/fastflowlm_server.h",
    )
    server_source = git_text(
        repo, baseline, "src/cpp/server/backends/fastflowlm/fastflowlm_server.cpp"
    )
    router = git_text(repo, baseline, "src/cpp/server/router.cpp")
    guide = git_text(repo, baseline, "docs/guide/configuration/multi-model.md")

    required_fragments = (
        (descriptor, "SlotPolicy::CoexistByType", "FLM descriptor slot policy"),
        (server_header, "IEmbeddingsServer", "FLM embedding capability"),
        (server_header, "ITranscriptionServer", "FLM transcription capability"),
        (
            server_source,
            "model_type_ == ModelType::TRANSCRIPTION",
            "FLM transcription deployment",
        ),
        (
            server_source,
            "model_type_ == ModelType::EMBEDDING",
            "FLM embedding deployment",
        ),
        (
            server_source,
            "model_type_ != ModelType::LLM",
            "FLM reranking-on-LLM behavior",
        ),
        (
            router,
            "max 1 per type: 1 LLM, 1 transcription, 1 embed",
            "FLM router slot policy",
        ),
        (
            guide,
            "1 ASR model, 1 LLM, and 1 embedding model",
            "FLM multi-model contract",
        ),
    )
    for source, fragment, label in required_fragments:
        if fragment not in source:
            fail(f"frozen source no longer proves {label}")
    if "ModelType::RERANKING" in server_source:
        fail("frozen FLM source exposes an independent reranking model-type slot")
    return {"llm", "embedding", "transcription"}


def validate_source_blobs(
    repo: Path,
    baseline: str,
    inventory: dict[str, Any],
    expected_paths: set[str],
) -> None:
    source_blobs = require_mapping(
        inventory.get("source_file_blobs"), "source_file_blobs"
    )
    if set(source_blobs) != expected_paths:
        fail(
            "source_file_blobs must exactly cover frozen descriptor inputs; "
            f"missing={sorted(expected_paths - set(source_blobs))}, "
            f"extra={sorted(set(source_blobs) - expected_paths)}"
        )
    for source_path, expected_blob_value in source_blobs.items():
        expected_blob = require_string(
            expected_blob_value, f"source_file_blobs[{source_path}]"
        )
        if re.fullmatch(r"[0-9a-f]{40}", expected_blob) is None:
            fail(f"source_file_blobs[{source_path}] must be a full lowercase blob id")
        actual_blob = git_blob(repo, baseline, source_path)
        if actual_blob != expected_blob:
            fail(
                f"source blob mismatch for {source_path}: "
                f"expected {expected_blob}, got {actual_blob}"
            )


def validate_source_closure(
    repo: Path, inventory: dict[str, Any]
) -> FrozenSourceClosure:
    baseline = inventory.get("source_support_baseline")
    if not isinstance(baseline, str) or not re.fullmatch(r"[0-9a-f]{40}", baseline):
        fail("source_support_baseline must be a full lowercase commit id")

    require_git_commit(repo, baseline)
    cmake = git_text(repo, baseline, "CMakeLists.txt")
    backends = parse_lemon_backends(cmake)
    descriptor_paths = {
        f"src/cpp/include/lemon/backends/{stem}/{stem}.h" for _, stem in backends
    }
    expected_paths = descriptor_paths | {
        "CMakeLists.txt",
        "src/cpp/include/lemon/backends/backend_descriptor.h",
        "src/cpp/include/lemon/recipe_backend_def.h",
        "src/cpp/include/lemon/model_types.h",
        "src/cpp/include/lemon/backends/fastflowlm/fastflowlm_server.h",
        "src/cpp/server/backends/backend_descriptor_registry.cpp",
        "src/cpp/server/backends/backend_descriptors_generated.h.in",
        "src/cpp/server/backends/fastflowlm/fastflowlm_server.cpp",
        "src/cpp/server/model_manager.cpp",
        "src/cpp/server/router.cpp",
        "src/cpp/server/system_info.cpp",
        "src/cpp/resources/backend_versions.json",
        "src/cpp/resources/server_models.json",
        "docs/guide/configuration/multi-model.md",
    }
    validate_source_blobs(repo, baseline, inventory, expected_paths)

    source_support: set[SourceSupportKey] = set()
    empty_support_recipes: set[str] = set()
    descriptor_labels: dict[str, list[str]] = {}
    for recipe, stem in backends:
        path = f"src/cpp/include/lemon/backends/{stem}/{stem}.h"
        descriptor = git_text(repo, baseline, path)
        rows = parse_descriptor_support(descriptor, recipe, path)
        descriptor_labels[recipe] = parse_descriptor_default_labels(descriptor, path)
        if not rows:
            empty_support_recipes.add(recipe)
        for row in rows:
            if row in source_support:
                fail(
                    f"frozen descriptors repeat source support {source_support_text(row)}"
                )
            source_support.add(row)
    model_types_source = git_text(repo, baseline, "src/cpp/include/lemon/model_types.h")
    rules = parse_model_type_rules(model_types_source)
    collection_recipes = parse_collection_recipes(model_types_source)
    recipe_model_types = derive_static_recipe_model_types(
        git_text(repo, baseline, "src/cpp/resources/server_models.json"),
        descriptor_labels,
        rules,
    )
    flm_slot_types = validate_flm_slot_policy(repo, baseline)
    if not recipe_model_types.get("flm", set()) <= flm_slot_types:
        fail("server_models FLM model types exceed the frozen FLM slot policy")
    recipe_model_types["flm"] = flm_slot_types
    return FrozenSourceClosure(
        baseline=baseline,
        support=source_support,
        empty_support_recipes=empty_support_recipes,
        recipe_model_types=recipe_model_types,
        descriptor_recipes=set(descriptor_labels),
        collection_recipes=collection_recipes,
    )
