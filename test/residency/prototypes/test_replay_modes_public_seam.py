import importlib.util
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
FLAG = "--attest-recorded-observation"
SEAMS = {
    "TASK-014": (
        "test/residency/prototypes/test_task014_durable_process_public_seam.py",
        "TASK-014 durable-root and process-ownership prototype is unavailable",
    ),
    "TASK-015": (
        "test/residency/prototypes/test_task015_hatchery_attribution_public_seam.py",
        "TASK-015 Hatchery causal attribution prototype is unavailable",
    ),
    "TASK-016": (
        "test/residency/prototypes/test_task016_llamacpp_rocm_sizing_public_seam.py",
        "TASK-016 pure-offline llama.cpp/ROCm sizing prototype is unavailable",
    ),
    "TASK-017": (
        "test/residency/prototypes/test_task017_llamacpp_soft_release_public_seam.py",
        "TASK-017 llama.cpp soft-release prototype is unavailable",
    ),
    "TASK-018": (
        "test/residency/prototypes/test_task018_flm_service_membership_public_seam.py",
        "TASK-018 FLM service-membership and ownership prototype is unavailable",
    ),
}


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_seam(path: Path, *arguments: str, environment=None):
    return subprocess.run(
        [sys.executable, "-S", str(path), *arguments],
        cwd=ROOT,
        env=environment,
        capture_output=True,
        text=True,
        check=False,
        timeout=120,
    )


def write_failing_compiler(directory: str, platform_name: str = os.name) -> Path:
    root = Path(directory)
    if platform_name == "nt":
        compiler = root / "g++.cmd"
        compiler.write_text(
            "@echo off\n"
            'if "%~1"=="--version" (\n'
            "  echo g++ ^(Fake GCC^) 0.0\n"
            "  exit /b 0\n"
            ")\n"
            "echo leaked stdout %~dp0\n"
            "echo leaked stderr %~dp0 1>&2\n"
            "exit /b 17\n",
            encoding="utf-8",
        )
        return compiler
    compiler = root / "g++"
    compiler.write_text(
        "#!/bin/sh\n"
        'if [ "$1" = "--version" ]; then\n'
        "  printf '%s\\n' 'g++ (Fake GCC) 0.0'\n"
        "  exit 0\n"
        "fi\n"
        f"printf '%s\\n' 'leaked stdout {directory}'\n"
        f"printf '%s\\n' 'leaked stderr {directory}' >&2\n"
        "exit 17\n",
        encoding="utf-8",
    )
    compiler.chmod(0o755)
    return compiler


class PrototypeReplayModesTest(unittest.TestCase):
    def test_failed_compiler_fixture_has_windows_command_shape(self):
        with tempfile.TemporaryDirectory(
            prefix="residency-replay-windows-compiler-"
        ) as directory:
            compiler = write_failing_compiler(directory, "nt")
            self.assertEqual(compiler.suffix, ".cmd")
            source = compiler.read_text(encoding="utf-8")
            self.assertIn('if "%~1"=="--version"', source)
            self.assertIn("exit /b 17", source)
            self.assertIn("%~dp0", source)

    def test_unknown_arguments_are_rejected_at_each_public_seam(self):
        for _, (relative_path, _) in SEAMS.items():
            path = ROOT / relative_path
            with self.subTest(path=relative_path):
                completed = run_seam(path, "--unknown-replay-mode")
                self.assertEqual(completed.returncode, 2)
                self.assertEqual(
                    completed.stderr,
                    f"{path.name}: unsupported arguments; expected no arguments or "
                    "--attest-recorded-observation\n",
                )
                self.assertEqual(completed.stdout, "")

    def test_flagged_red_fixture_checks_artifacts_before_environment(self):
        for _, (relative_path, unavailable) in SEAMS.items():
            source = ROOT / relative_path
            with self.subTest(path=relative_path), tempfile.TemporaryDirectory(
                prefix="residency-replay-red-"
            ) as directory:
                root = Path(directory)
                target = root / relative_path
                target.parent.mkdir(parents=True)
                shutil.copy2(source, target)
                completed = subprocess.run(
                    [sys.executable, "-S", str(target), FLAG],
                    cwd=root,
                    env={"PATH": ""},
                    capture_output=True,
                    text=True,
                    check=False,
                    timeout=30,
                )
                self.assertEqual(completed.returncode, 1)
                self.assertEqual(completed.stderr, unavailable + "\n")

    def test_behavioral_replay_reports_a_missing_compiler_at_each_public_seam(self):
        with tempfile.TemporaryDirectory(
            prefix="residency-replay-missing-compiler-"
        ) as directory:
            missing_compiler = Path(directory) / "missing-cxx"
            environment = os.environ.copy()
            environment["CXX"] = str(missing_compiler)
            for _, (relative_path, _) in SEAMS.items():
                path = ROOT / relative_path
                with self.subTest(path=relative_path):
                    completed = run_seam(path, environment=environment)
                    self.assertEqual(completed.returncode, 1)
                    self.assertEqual(
                        completed.stderr,
                        f"{path.name}: behavioral replay process is unavailable\n",
                    )
                    self.assertEqual(completed.stderr.count("\n"), 1)
                    self.assertNotIn("Traceback", completed.stderr)
                    self.assertNotIn(directory, completed.stderr)
                    self.assertEqual(completed.stdout, "")

    def test_behavioral_replay_bounds_failed_compiler_output_at_each_public_seam(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix="residency-replay-failed-compiler-"
        ) as directory:
            compiler = write_failing_compiler(directory)
            environment = os.environ.copy()
            environment["CXX"] = str(compiler)
            for _, (relative_path, _) in SEAMS.items():
                path = ROOT / relative_path
                with self.subTest(path=relative_path):
                    completed = run_seam(path, environment=environment)
                    self.assertEqual(completed.returncode, 1)
                    self.assertEqual(
                        completed.stderr,
                        f"{path.name}: behavioral replay process failed\n",
                    )
                    self.assertEqual(completed.stdout, "")
                    self.assertNotIn(directory, completed.stderr)

    @unittest.skipUnless(
        sys.platform.startswith("linux"),
        "TASK-014 auxiliary compiler integration requires Linux",
    )
    def test_behavioral_replay_bounds_failed_auxiliary_compiler_output(self):
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ is unavailable")
        relative_path, _ = SEAMS["TASK-014"]
        with tempfile.TemporaryDirectory(
            prefix="residency-replay-failed-auxiliary-compiler-"
        ) as directory:
            c_compiler = Path(directory) / "cc"
            c_compiler.write_text(
                "#!/bin/sh\n"
                f"printf '%s\\n' 'leaked stdout {directory}'\n"
                f"printf '%s\\n' 'leaked stderr {directory}' >&2\n"
                "exit 17\n",
                encoding="utf-8",
            )
            c_compiler.chmod(0o755)
            environment = os.environ.copy()
            environment.update({"CC": str(c_compiler), "CXX": compiler})
            path = ROOT / relative_path
            completed = run_seam(path, environment=environment)
            self.assertEqual(completed.returncode, 1)
            self.assertEqual(
                completed.stderr,
                f"{path.name}: behavioral replay process failed\n",
            )
            self.assertEqual(completed.stdout, "")
            self.assertNotIn(directory, completed.stderr)

    @unittest.skipUnless(
        sys.platform.startswith("linux"),
        "TASK-014 compiler integration requires Linux",
    )
    def test_behavioral_replay_uses_selected_compilers_without_path_fallback(self):
        cxx_compiler = shutil.which("g++")
        c_compiler = shutil.which("cc")
        if cxx_compiler is None or c_compiler is None:
            self.skipTest("C and C++ compilers are unavailable")
        relative_path, _ = SEAMS["TASK-014"]
        with tempfile.TemporaryDirectory(
            prefix="residency-replay-compiler-"
        ) as directory:
            root = Path(directory)
            selected = root / "selected"
            traps = root / "traps"
            selected.mkdir()
            traps.mkdir()
            cxx_wrapper = selected / "cxx"
            c_wrapper = selected / "cc"
            cxx_log = root / "cxx-compiler.log"
            c_log = root / "c-compiler.log"
            trap_log = root / "trap.log"
            cxx_wrapper.write_text(
                "#!/bin/sh\n"
                'printf "%s\\n" "$*" >> "$TASK014_COMPILER_LOG"\n'
                'if [ "$1" = "--version" ]; then\n'
                '  printf "%s\\n" "g++ (Fake GCC) 0.0"\n'
                "  exit 0\n"
                "fi\n"
                f'exec {shlex.quote(cxx_compiler)} "$@"\n',
                encoding="utf-8",
            )
            cxx_wrapper.chmod(0o755)
            c_wrapper.write_text(
                "#!/bin/sh\n"
                'printf "%s\\n" "$*" >> "$TASK014_C_COMPILER_LOG"\n'
                f'exec {shlex.quote(c_compiler)} "$@"\n',
                encoding="utf-8",
            )
            c_wrapper.chmod(0o755)
            for name in ("g++", "c++", "gcc", "cc"):
                trap = traps / name
                trap.write_text(
                    "#!/bin/sh\n"
                    'printf "%s\\n" "$0 $*" >> "$TASK014_TRAP_LOG"\n'
                    "exit 97\n",
                    encoding="utf-8",
                )
                trap.chmod(0o755)
            trap_log.touch()
            environment = os.environ.copy()
            environment.update(
                {
                    "CC": str(c_wrapper),
                    "CXX": str(cxx_wrapper),
                    "PATH": f"{traps}{os.pathsep}{environment['PATH']}",
                    "TASK014_COMPILER_LOG": str(cxx_log),
                    "TASK014_C_COMPILER_LOG": str(c_log),
                    "TASK014_TRAP_LOG": str(trap_log),
                }
            )
            completed = run_seam(ROOT / relative_path, environment=environment)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertTrue(
                any(
                    "test_residency_prototype_task014.cpp" in line
                    for line in cxx_log.read_text(encoding="utf-8").splitlines()
                ),
                "behavioral replay did not use the selected C++ compiler",
            )
            self.assertTrue(
                any(
                    argument.endswith(".c")
                    for line in c_log.read_text(encoding="utf-8").splitlines()
                    for argument in shlex.split(line)
                ),
                "behavioral replay did not use the selected C compiler",
            )
            self.assertEqual(trap_log.read_text(encoding="utf-8"), "")

    @unittest.skipUnless(
        sys.platform.startswith("linux"),
        "TASK-014 compiler integration requires Linux",
    )
    def test_behavioral_replay_reports_contract_assertions_without_a_traceback(self):
        relative_path, _ = SEAMS["TASK-014"]
        with tempfile.TemporaryDirectory(
            prefix="residency-replay-assertion-failure-"
        ) as directory:
            root = Path(directory)
            wrapper = root / "g++"
            wrapper.write_text(
                "#!/bin/sh\n"
                'if [ "$1" = "--version" ]; then\n'
                "  printf '%s\\n' 'g++ (Fake GCC) 0.0'\n"
                "  exit 0\n"
                "fi\n"
                "output=''\n"
                'while [ "$#" -gt 0 ]; do\n'
                '  if [ "$1" = "-o" ]; then\n'
                "    shift\n"
                '    output="$1"\n'
                "  fi\n"
                "  shift\n"
                "done\n"
                "cat > \"$output\" <<'PROBE'\n"
                "#!/bin/sh\n"
                "printf '%s\\n' 'unexpected=passed'\n"
                "PROBE\n"
                'chmod +x "$output"\n',
                encoding="utf-8",
            )
            wrapper.chmod(0o755)
            environment = os.environ.copy()
            environment["CXX"] = str(wrapper)
            completed = run_seam(ROOT / relative_path, environment=environment)
            self.assertEqual(completed.returncode, 1)
            self.assertEqual(
                completed.stderr,
                "test_task014_durable_process_public_seam.py: "
                "behavioral replay contract assertion failed: "
                "native probe did not bind its current platform\n",
            )
            self.assertEqual(completed.stderr.count("\n"), 1)
            self.assertNotIn("Traceback", completed.stderr)
            self.assertNotIn(directory, completed.stderr)
            self.assertEqual(completed.stdout, "")

    @unittest.skipUnless(
        sys.platform.startswith("linux"),
        "TASK-014 compiler integration requires Linux",
    )
    def test_behavioral_replay_redacts_paths_from_assertion_diagnostics(self):
        relative_path, _ = SEAMS["TASK-014"]
        with tempfile.TemporaryDirectory(
            prefix="residency-replay-path-assertion-"
        ) as directory:
            wrapper = Path(directory) / "g++"
            wrapper.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            wrapper.chmod(0o755)
            environment = os.environ.copy()
            environment["CXX"] = str(wrapper)
            completed = run_seam(ROOT / relative_path, environment=environment)
            self.assertEqual(completed.returncode, 1)
            self.assertEqual(
                completed.stderr,
                "test_task014_durable_process_public_seam.py: "
                "behavioral replay contract assertion failed: "
                "recorded compiler <path> emitted no version\n",
            )
            self.assertEqual(completed.stderr.count("\n"), 1)
            self.assertNotIn("Traceback", completed.stderr)
            self.assertNotIn(directory, completed.stderr)
            self.assertEqual(completed.stdout, "")

    @unittest.skipUnless(
        sys.platform.startswith("linux"),
        "TASK-014 compiler integration requires Linux",
    )
    def test_attestation_rejects_toolchain_before_compilation(self):
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ is unavailable")
        relative_path, _ = SEAMS["TASK-014"]
        with tempfile.TemporaryDirectory(
            prefix="residency-attestation-compiler-"
        ) as directory:
            root = Path(directory)
            wrapper = root / "g++"
            log = root / "compiler.log"
            wrapper.write_text(
                "#!/bin/sh\n"
                'printf "%s\\n" "$*" >> "$TASK014_COMPILER_LOG"\n'
                'if [ "$1" = "--version" ]; then\n'
                '  printf "%s\\n" "g++ (Fake GCC) 0.0"\n'
                "  exit 0\n"
                "fi\n"
                f'exec {shlex.quote(compiler)} "$@"\n',
                encoding="utf-8",
            )
            wrapper.chmod(0o755)
            environment = os.environ.copy()
            environment.update({"CXX": str(wrapper), "TASK014_COMPILER_LOG": str(log)})
            completed = run_seam(ROOT / relative_path, FLAG, environment=environment)
            self.assertEqual(completed.returncode, 1)
            self.assertEqual(
                completed.stderr,
                "test_task014_durable_process_public_seam.py: "
                "recorded-observation attestation compiler-version prerequisite "
                "differs\n",
            )
            self.assertEqual(completed.stderr.count("\n"), 1)
            self.assertNotIn("Traceback", completed.stderr)
            self.assertEqual(
                log.read_text(encoding="utf-8").splitlines(), ["--version"]
            )

    @unittest.skipUnless(
        sys.platform.startswith("linux"),
        "TASK-014 compiler integration requires Linux",
    )
    def test_attestation_reports_a_compile_process_failure_without_a_traceback(self):
        contract = load_module(
            ROOT / "test/residency/prototypes/result_contract.py",
            "prototype_result_contract_process_failure_test",
        )
        result = contract.load_task_result(ROOT, "TASK-014")
        observation = result["observations"][0]
        relative_path, _ = SEAMS["TASK-014"]
        with tempfile.TemporaryDirectory(
            prefix="residency-attestation-process-failure-"
        ) as directory:
            root = Path(directory)
            wrapper = root / observation["toolchain"]["compiler"]
            wrapper.write_text(
                "#!/bin/sh\n"
                'if [ "$1" = "--version" ]; then\n'
                f"  printf '%s\\n' {shlex.quote(observation['toolchain']['version'])}\n"
                "  exit 0\n"
                "fi\n"
                "exit 17\n",
                encoding="utf-8",
            )
            wrapper.chmod(0o755)
            environment = os.environ.copy()
            environment["CXX"] = str(wrapper)
            completed = run_seam(ROOT / relative_path, FLAG, environment=environment)
            self.assertEqual(completed.returncode, 1)
            self.assertEqual(
                completed.stderr,
                "test_task014_durable_process_public_seam.py: "
                "recorded-observation attestation process failed\n",
            )
            self.assertEqual(completed.stderr.count("\n"), 1)
            self.assertNotIn("Traceback", completed.stderr)
            self.assertNotIn(directory, completed.stderr)
            self.assertEqual(completed.stdout, "")

    @unittest.skipUnless(
        sys.platform.startswith("linux"),
        "TASK-014 compiler integration requires Linux",
    )
    def test_attestation_reports_an_unavailable_recorded_compiler(self):
        relative_path, _ = SEAMS["TASK-014"]
        with tempfile.TemporaryDirectory(
            prefix="residency-attestation-missing-compiler-"
        ) as directory:
            missing_compiler = Path(directory) / "g++"
            environment = os.environ.copy()
            environment["CXX"] = str(missing_compiler)
            completed = run_seam(ROOT / relative_path, FLAG, environment=environment)
            self.assertEqual(completed.returncode, 1)
            self.assertEqual(
                completed.stderr,
                "test_task014_durable_process_public_seam.py: "
                "recorded-observation attestation compiler-version prerequisite "
                "is unavailable\n",
            )
            self.assertEqual(completed.stderr.count("\n"), 1)
            self.assertNotIn("Traceback", completed.stderr)

    def test_attestation_prerequisites_are_closed(self):
        contract = load_module(
            ROOT / "test/residency/prototypes/result_contract.py",
            "prototype_result_contract_replay_test",
        )
        result = contract.load_task_result(ROOT, "TASK-015")
        observation = result["observations"][0]
        environment = observation["environment"]
        toolchain = observation["toolchain"]
        exact_profile = "exact"

        contract.require_recorded_observation_environment(
            observation,
            environment["platform"],
            environment["architecture"],
            exact_profile,
        )
        contract.require_recorded_observation_toolchain(
            observation, toolchain["compiler"], toolchain["version"]
        )
        observed_body = json.loads(json.dumps(observation))
        observed_body.pop("id")
        contract.require_recorded_observation_body(observation, observed_body)
        mismatched_body = json.loads(json.dumps(observed_body))
        mismatched_body["exit_code"] = 1
        with self.assertRaises(contract.PrototypeResultError) as drift:
            contract.require_recorded_observation_body(observation, mismatched_body)
        self.assertEqual(
            contract.public_diagnostic(drift.exception),
            "recorded-observation attestation complete observation body differs",
        )

        cases = (
            (
                contract.require_recorded_observation_environment,
                (None, environment["platform"], environment["architecture"], None),
                "recorded-observation attestation requires one recorded observation",
            ),
            (
                contract.require_recorded_observation_environment,
                (observation, "windows", environment["architecture"], None),
                "platform prerequisite differs",
            ),
            (
                contract.require_recorded_observation_environment,
                (observation, environment["platform"], "aarch64", None),
                "architecture prerequisite differs",
            ),
            (
                contract.require_recorded_observation_environment,
                (
                    observation,
                    environment["platform"],
                    environment["architecture"],
                    "generic",
                ),
                "native-profile prerequisite differs",
            ),
            (
                contract.require_recorded_observation_toolchain,
                (observation, "clang++", toolchain["version"]),
                "compiler-token prerequisite differs",
            ),
            (
                contract.require_recorded_observation_toolchain,
                (observation, toolchain["compiler"], "g++ (Fake GCC) 0.0"),
                "compiler-version prerequisite differs",
            ),
        )
        for function, arguments, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(
                contract.PrototypeResultError, message
            ):
                function(*arguments)

    def test_contract_diagnostic_is_one_bounded_line(self):
        contract = load_module(
            ROOT / "test/residency/prototypes/result_contract.py",
            "prototype_result_contract_diagnostic_test",
        )
        diagnostic = contract.public_diagnostic(
            contract.PrototypeResultError("x" * 600 + "\nsecond line")
        )
        self.assertLessEqual(len(diagnostic), contract.PUBLIC_DIAGNOSTIC_LIMIT)
        self.assertNotIn("\n", diagnostic)
        self.assertTrue(diagnostic.endswith("..."))

    def test_operational_failure_diagnostics_are_cross_platform_and_bounded(self):
        contract = load_module(
            ROOT / "test/residency/prototypes/result_contract.py",
            "prototype_result_contract_operational_diagnostic_test",
        )
        cases = (
            (
                False,
                AssertionError("probe emitted /tmp/residency-secret/probe"),
                "behavioral replay contract assertion failed: probe emitted <path>",
            ),
            (
                True,
                AssertionError(
                    r"probe emitted C:\Users\Ivan\AppData\Local\Temp\probe.exe"
                ),
                "recorded-observation attestation contract assertion failed: "
                "probe emitted <path>",
            ),
            (
                False,
                AssertionError("probe emitted /tmp/residency secret/probe"),
                "behavioral replay contract assertion failed: "
                "path-bearing assertion detail redacted",
            ),
            (
                True,
                AssertionError(
                    r"probe emitted C:\Users\Ivan D Vasin\AppData\Local\Temp\probe.exe"
                ),
                "recorded-observation attestation contract assertion failed: "
                "path-bearing assertion detail redacted",
            ),
            (
                True,
                AssertionError(r"probe emitted \\server\shared directory\probe.exe"),
                "recorded-observation attestation contract assertion failed: "
                "path-bearing assertion detail redacted",
            ),
            (
                True,
                subprocess.TimeoutExpired(["/tmp/residency-secret/probe"], 30),
                "recorded-observation attestation process timed out",
            ),
            (
                True,
                subprocess.CalledProcessError(17, ["/tmp/residency-secret/probe"]),
                "recorded-observation attestation process failed",
            ),
            (
                False,
                FileNotFoundError("/tmp/residency-secret/probe"),
                "behavioral replay process is unavailable",
            ),
        )
        for attestation, error, expected in cases:
            with self.subTest(expected=expected):
                diagnostic = contract.public_operational_failure(attestation, error)
                self.assertEqual(diagnostic, expected)
                self.assertNotIn("/tmp/residency-secret", diagnostic)
                self.assertNotIn(r"C:\Users\Ivan", diagnostic)
                self.assertNotIn("\n", diagnostic)

        assertion_prefix = "behavioral replay contract assertion failed: "
        bounded = contract.public_operational_failure(
            False, AssertionError("x" * 600 + "\nsecond line")
        )
        self.assertLessEqual(
            len(bounded), len(assertion_prefix) + contract.PUBLIC_DIAGNOSTIC_LIMIT
        )
        self.assertTrue(bounded.endswith("..."))

    def test_task015_selects_recorded_observation_before_profile_preflight(self):
        contract = load_module(
            ROOT / "test/residency/prototypes/result_contract.py",
            "prototype_result_contract_task015_test",
        )
        seam = load_module(
            ROOT
            / "test/residency/prototypes/test_task015_hatchery_attribution_public_seam.py",
            "task015_replay_mode_test",
        )
        result = contract.load_task_result(ROOT, "TASK-015")
        observation = seam.recorded_observation_for_platform(result, "linux")
        self.assertIs(observation, result["observations"][0])


if __name__ == "__main__":
    unittest.main()
