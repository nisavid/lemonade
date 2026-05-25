import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND_UTILS = REPO_ROOT / "src/cpp/server/backends/backend_utils.cpp"


def _function_body(source: str, name: str) -> str:
    match = re.search(rf"bool BackendUtils::{name}\b[^\{{]*\{{", source)
    if not match:
        raise AssertionError(f"{name} not found")

    depth = 0
    for index in range(match.end() - 1, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[match.end() : index]
    raise AssertionError(f"{name} body not terminated")


class BackendArchiveExtractionSecurityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = BACKEND_UTILS.read_text(encoding="utf-8")

    def test_archive_extractors_execute_with_argument_vectors(self):
        for function in ("extract_zip", "extract_tarball"):
            with self.subTest(function=function):
                body = _function_body(self.source, function)
                self.assertIn("std::vector<std::string> args", body)
                self.assertIn("run_archive_process(executable, args, output)", body)
                self.assertNotIn("system(", body)

    def test_powershell_zip_fallback_uses_literal_path_arguments(self):
        body = _function_body(self.source, "extract_zip")
        self.assertIn(
            "Expand-Archive -LiteralPath $args[0] -DestinationPath $args[1] -Force",
            body,
        )
        self.assertNotIn("Expand-Archive -Path '\" + zip_path", body)


if __name__ == "__main__":
    unittest.main()
