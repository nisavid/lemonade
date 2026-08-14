"""Document update tests for the residency inventory validator."""

from __future__ import annotations

import json
import os
import stat
import unittest

from residency_capability_inventory_cli_case import ResidencyCapabilityInventoryCliCase

from tools.residency_inventory.contract import ResidencyInventoryError
from tools.residency_inventory.transaction import update_generated_documents


class ResidencyCapabilityInventoryCliTest(ResidencyCapabilityInventoryCliCase):
    """Exercise ordinary document update and alias behavior through the CLI."""

    def test_stale_generated_projection_fails_closed(self) -> None:
        self.matrix_path.write_text(
            self.matrix_path.read_text(encoding="utf-8").replace(
                "### Backend variants", "### Stale backend variants", 1
            ),
            encoding="utf-8",
        )

        result = self._run_cli()

        self.assert_invalid(result, "generated support inventory block is stale")

    def test_locked_crash_consistent_update_is_mode_preserving_and_idempotent(
        self,
    ) -> None:
        """Raw readers are not isolated between the two document replacements."""
        self.matrix_path.write_text(
            self.matrix_path.read_text(encoding="utf-8").replace(
                "### Backend variants", "### Stale backend variants", 1
            ),
            encoding="utf-8",
        )
        self.campaign_path.write_text(
            self.campaign_path.read_text(encoding="utf-8").replace(
                "#### Runtime exact cells", "#### Stale runtime exact cells", 1
            ),
            encoding="utf-8",
        )
        os.chmod(self.matrix_path, 0o640)
        os.chmod(self.campaign_path, 0o600)
        matrix_inode_before = self.matrix_path.stat().st_ino
        campaign_inode_before = self.campaign_path.stat().st_ino

        first = self._run_cli("--update")

        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertNotEqual(self.matrix_path.stat().st_ino, matrix_inode_before)
        self.assertNotEqual(self.campaign_path.stat().st_ino, campaign_inode_before)
        self.assertEqual(stat.S_IMODE(self.matrix_path.stat().st_mode), 0o640)
        self.assertEqual(stat.S_IMODE(self.campaign_path.stat().st_mode), 0o600)
        self.assertTrue(
            self.matrix_path.read_text(encoding="utf-8").startswith("matrix preface\n")
        )
        self.assertTrue(
            self.matrix_path.read_text(encoding="utf-8").endswith("\nmatrix epilogue\n")
        )
        matrix_after_first = self.matrix_path.read_bytes()
        campaign_after_first = self.campaign_path.read_bytes()
        matrix_stat_after_first = self.matrix_path.stat()
        campaign_stat_after_first = self.campaign_path.stat()

        second = self._run_cli("--update")

        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(self.matrix_path.read_bytes(), matrix_after_first)
        self.assertEqual(self.campaign_path.read_bytes(), campaign_after_first)
        self.assertEqual(self.matrix_path.stat().st_ino, matrix_stat_after_first.st_ino)
        self.assertEqual(
            self.campaign_path.stat().st_ino, campaign_stat_after_first.st_ino
        )

    def test_update_rejects_reversed_markers_before_staging(self) -> None:
        matrix = self.matrix_path.read_text(encoding="utf-8")
        begin = matrix.index("<!-- BEGIN GENERATED SUPPORT INVENTORY -->")
        end = matrix.index("<!-- END GENERATED SUPPORT INVENTORY -->")
        reversed_matrix = (
            matrix[:begin]
            + "<!-- END GENERATED SUPPORT INVENTORY -->"
            + matrix[begin + len("<!-- BEGIN GENERATED SUPPORT INVENTORY -->") : end]
            + "<!-- BEGIN GENERATED SUPPORT INVENTORY -->"
            + matrix[end + len("<!-- END GENERATED SUPPORT INVENTORY -->") :]
        )
        self.matrix_path.write_text(reversed_matrix, encoding="utf-8")
        matrix_before = self.matrix_path.read_bytes()
        campaign_before = self.campaign_path.read_bytes()

        result = self._run_cli("--update")

        self.assert_invalid(result, "end marker must follow its begin marker")
        self.assertNotIn("Traceback", result.stderr)
        self.assertEqual(self.matrix_path.read_bytes(), matrix_before)
        self.assertEqual(self.campaign_path.read_bytes(), campaign_before)
        self.assertFalse(
            self.inventory_path.with_name(
                f".{self.inventory_path.name}.update-v1.json"
            ).exists()
        )
        self.assertEqual(list(self.matrix_path.parent.glob(".*.residency-*")), [])

    def test_update_rejects_rendered_marker_injection_before_mutation(self) -> None:
        original_scope = self.inventory["exact_cells"][0]["scope"]
        self.inventory["exact_cells"][0]["scope"] = (
            "accepted text " + "<!-- END GENERATED SUPPORT INVENTORY -->"
        )
        self._write_inventory()
        matrix_before = self.matrix_path.read_bytes()
        campaign_before = self.campaign_path.read_bytes()

        result = self._run_cli("--update")

        self.assert_invalid(result, "reserved generated marker")
        self.assertEqual(self.matrix_path.read_bytes(), matrix_before)
        self.assertEqual(self.campaign_path.read_bytes(), campaign_before)
        self.assertFalse(
            self.inventory_path.with_name(
                f".{self.inventory_path.name}.update-v1.json"
            ).exists()
        )
        self.inventory["exact_cells"][0]["scope"] = original_scope
        self._write_inventory()
        retry = self._run_cli("--update")
        self.assertEqual(retry.returncode, 0, retry.stderr)

    def test_update_preserves_crlf_source_bytes_outside_generated_blocks(self) -> None:
        matrix = self.matrix_path.read_text(encoding="utf-8").replace(
            "### Backend variants", "### Stale backend variants", 1
        )
        campaign = self.campaign_path.read_text(encoding="utf-8").replace(
            "#### Runtime exact cells", "#### Stale runtime exact cells", 1
        )
        self.matrix_path.write_bytes(matrix.replace("\n", "\r\n").encode())
        self.campaign_path.write_bytes(campaign.replace("\n", "\r\n").encode())

        result = self._run_cli("--update")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(self.matrix_path.read_bytes().startswith(b"matrix preface\r\n"))
        self.assertIn(b"matrix epilogue\r\n", self.matrix_path.read_bytes())
        self.assertEqual(self._run_cli().returncode, 0)

    def test_update_rejects_equal_output_paths_without_mutation(self) -> None:
        self.matrix_path.write_text(
            self.matrix_path.read_text(encoding="utf-8").replace(
                "### Backend variants", "### Stale backend variants", 1
            ),
            encoding="utf-8",
        )
        before = self.matrix_path.read_bytes()

        result = self._run_cli(
            "--update",
            "--matrix",
            str(self.matrix_path),
            "--campaign",
            str(self.matrix_path),
        )

        self.assert_invalid(result, "must identify different files")
        self.assertEqual(self.matrix_path.read_bytes(), before)

    def test_update_rejects_samefile_output_alias_without_mutation(self) -> None:
        self.matrix_path.write_text(
            self.matrix_path.read_text(encoding="utf-8").replace(
                "### Backend variants", "### Stale backend variants", 1
            ),
            encoding="utf-8",
        )
        alias_path = self.matrix_path.with_name("matrix-hardlink-alias.md")
        os.link(self.matrix_path, alias_path)
        before = self.matrix_path.read_bytes()

        result = self._run_cli(
            "--update",
            "--matrix",
            str(self.matrix_path),
            "--campaign",
            str(alias_path),
        )

        self.assert_invalid(result, "must identify different files")
        self.assertEqual(self.matrix_path.read_bytes(), before)
        self.assertEqual(alias_path.read_bytes(), before)

    def test_update_rejects_inventory_output_alias_without_mutation(self) -> None:
        inventory_before = self.inventory_path.read_bytes()
        campaign_before = self.campaign_path.read_bytes()

        result = self._run_cli(
            "--update",
            "--matrix",
            str(self.inventory_path),
        )

        self.assert_invalid(
            result, "inventory, --matrix, and --campaign must identify different files"
        )
        self.assertEqual(self.inventory_path.read_bytes(), inventory_before)
        self.assertEqual(self.campaign_path.read_bytes(), campaign_before)

    def test_update_rejects_inventory_alias_before_pending_recovery(self) -> None:
        transaction_id = "a" * 32
        journal_path = self.inventory_path.with_name(
            f".{self.inventory_path.name}.update-v1.json"
        )
        journal_path.write_text(
            json.dumps(
                {
                    "inventory": {
                        "path": self.inventory_path.relative_to(self.repo).as_posix(),
                        "role": "inventory",
                    },
                    "phase": "staging",
                    "targets": [
                        {
                            "mode": stat.S_IMODE(self.inventory_path.stat().st_mode),
                            "new_sha256": "1" * 64,
                            "old_sha256": "0" * 64,
                            "path": self.inventory_path.relative_to(
                                self.repo
                            ).as_posix(),
                            "role": "matrix",
                        }
                    ],
                    "transaction_id": transaction_id,
                    "version": 1,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        old_artifact = self.inventory_path.with_name(
            f".{self.inventory_path.name}.residency-{transaction_id}.old"
        )
        old_artifact.write_bytes(b"pending staging artifact\n")
        inventory_before = self.inventory_path.read_bytes()
        matrix_before = self.matrix_path.read_bytes()
        campaign_before = self.campaign_path.read_bytes()
        journal_before = journal_path.read_bytes()
        artifact_before = old_artifact.read_bytes()

        with self.assertRaisesRegex(
            ResidencyInventoryError,
            "inventory, --matrix, and --campaign must identify different files",
        ):
            update_generated_documents(
                self.repo,
                self.inventory_path,
                self.inventory_path,
                self.campaign_path,
                "unused inventory render",
                "unused campaign render",
            )

        self.assertEqual(self.inventory_path.read_bytes(), inventory_before)
        self.assertEqual(self.matrix_path.read_bytes(), matrix_before)
        self.assertEqual(self.campaign_path.read_bytes(), campaign_before)
        self.assertEqual(journal_path.read_bytes(), journal_before)
        self.assertEqual(old_artifact.read_bytes(), artifact_before)

    def test_update_rejects_inventory_hardlink_output_alias(self) -> None:
        alias_path = self.inventory_path.with_name("inventory-hardlink-alias.md")
        os.link(self.inventory_path, alias_path)
        inventory_before = self.inventory_path.read_bytes()
        campaign_before = self.campaign_path.read_bytes()

        result = self._run_cli(
            "--update",
            "--matrix",
            str(alias_path),
        )

        self.assert_invalid(
            result, "inventory, --matrix, and --campaign must identify different files"
        )
        self.assertEqual(self.inventory_path.read_bytes(), inventory_before)
        self.assertEqual(alias_path.read_bytes(), inventory_before)
        self.assertEqual(self.campaign_path.read_bytes(), campaign_before)


if __name__ == "__main__":
    unittest.main()
