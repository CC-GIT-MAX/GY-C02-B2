import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from tools import codex_doxygen_staged as automation


class TargetSelectionTests(unittest.TestCase):
    def test_selects_only_maintained_c_and_h_files(self):
        staged = [
            "app/foo.c",
            "app/foo.h",
            "docs/README.md",
            "CMSIS/vendor.c",
            "middleware/third_party.h",
            "platform/drivers/src/clock/driver.c",
            "APP/UPPER.C",
        ]

        self.assertEqual(
            automation.select_targets(staged),
            ["app/foo.c", "app/foo.h", "APP/UPPER.C"],
        )

    def test_unstaged_target_detection_uses_normalized_paths(self):
        targets = ["app/foo.c", "app/foo.h"]
        unstaged = ["app\\foo.h", "README.md"]

        self.assertEqual(automation.find_unstaged_targets(targets, unstaged), ["app/foo.h"])


class PromptTests(unittest.TestCase):
    def test_prompt_constrains_codex_to_comments_and_targets(self):
        prompt = automation.build_prompt(["app/foo.c", "app/foo.h"])

        self.assertIn("docs/DOXYGEN_STYLE.md", prompt)
        self.assertIn("app/foo.c", prompt)
        self.assertIn("app/foo.h", prompt)
        self.assertIn("Do not change program behavior", prompt)
        self.assertIn("Only modify the listed files", prompt)


class MainFlowTests(unittest.TestCase):
    def make_runner(self, staged, unstaged=()):
        runner = Mock()
        runner.side_effect = [
            subprocess.CompletedProcess([], 0, stdout=str(Path.cwd()) + "\n", stderr=""),
            subprocess.CompletedProcess([], 0, stdout="\n".join(staged), stderr=""),
            subprocess.CompletedProcess([], 0, stdout="\n".join(unstaged), stderr=""),
        ]
        return runner

    def test_no_targets_exits_without_codex(self):
        runner = self.make_runner(["README.md"])

        result = automation.run_automation(runner=runner, environ={})

        self.assertEqual(result, 0)
        self.assertEqual(runner.call_count, 2)

    def test_unstaged_target_blocks_automation(self):
        runner = self.make_runner(["app/foo.c"], ["app/foo.c"])

        result = automation.run_automation(runner=runner, environ={})

        self.assertEqual(result, 2)
        self.assertEqual(runner.call_count, 3)

    def test_dry_run_does_not_invoke_codex(self):
        runner = self.make_runner(["app/foo.c"])

        result = automation.run_automation(
            runner=runner,
            environ={"CODEX_DOXYGEN_DRY_RUN": "1"},
        )

        self.assertEqual(result, 0)
        self.assertEqual(runner.call_count, 3)

    def test_success_invokes_codex_validates_and_restages(self):
        root = str(Path.cwd())
        runner = self.make_runner(["app/foo.c"])
        runner.side_effect = list(runner.side_effect) + [
            subprocess.CompletedProcess([], 0, stdout="", stderr=""),
            subprocess.CompletedProcess([], 0, stdout="PASSED", stderr=""),
            subprocess.CompletedProcess([], 0, stdout="", stderr=""),
        ]

        result = automation.run_automation(runner=runner, environ={})

        self.assertEqual(result, 0)
        commands = [call.args[0] for call in runner.call_args_list]
        codex_cmd = commands[3]
        # Windows launches via cmd.exe /c codex; POSIX uses the bare binary.
        # Either way the tail should be the standard codex exec invocation
        # with the prompt as a positional argument (no stdin).
        self.assertIn("exec", codex_cmd)
        self.assertIn("--ephemeral", codex_cmd)
        self.assertIn("-s", codex_cmd)
        self.assertIn("workspace-write", codex_cmd)
        self.assertIn("-C", codex_cmd)
        self.assertIn(root, codex_cmd)
        self.assertNotIn("input", runner.call_args_list[3].kwargs)
        prompt = codex_cmd[-1]
        self.assertIn("docs/DOXYGEN_STYLE.md", prompt)
        self.assertEqual(commands[4], [automation.python_executable(), "tools/check_doxygen.py", "app/foo.c"])
        self.assertEqual(commands[5], ["git", "add", "--", "app/foo.c"])


class InstallerTests(unittest.TestCase):
    def test_installer_sets_local_hooks_path(self):
        installer = Path(__file__).resolve().parents[1] / "tools" / "install_git_hooks.py"
        with tempfile.TemporaryDirectory() as temp_dir:
            subprocess.run(["git", "init", "-q", temp_dir], check=True)
            hooks = Path(temp_dir) / ".githooks"
            hooks.mkdir()
            (hooks / "pre-commit").write_text("#!/bin/sh\n", encoding="utf-8")

            completed = subprocess.run(
                [automation.python_executable(), str(installer)],
                cwd=temp_dir,
                text=True,
                capture_output=True,
            )
            configured = subprocess.run(
                ["git", "config", "--local", "--get", "core.hooksPath"],
                cwd=temp_dir,
                text=True,
                capture_output=True,
            )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(configured.stdout.strip(), ".githooks")


if __name__ == "__main__":
    unittest.main()


class BlockAboveTests(unittest.TestCase):
    """Regression tests for the doxygen-block lookup used by
    file_is_already_compliant(). The old version walked past intervening
    code (e.g. `#include`), returning None for files with a file-level
    header block before any function."""

    def test_finds_block_directly_above_function(self):
        text = (
            "/**\n"
            " * @brief hi\n"
            " */\n"
            "void Foo(void);\n"
        )
        block = automation._block_above(text.splitlines(), 4)
        self.assertIsNotNone(block)
        self.assertIn("@brief hi", block)

    def test_block_above_with_blank_lines(self):
        text = (
            "/**\n"
            " * @brief hi\n"
            " */\n"
            "\n"
            "\n"
            "void Foo(void);\n"
        )
        block = automation._block_above(text.splitlines(), 6)
        self.assertIsNotNone(block)
        self.assertIn("@brief hi", block)

    def test_returns_none_when_include_intervenes(self):
        # File header doxygen at top, then includes, then function with its
        # OWN doxygen block. The block directly above the function must be
        # found even if the header block exists.
        text = (
            "/**\n"
            " * @file foo.h\n"
            " */\n"
            "#include \"types.h\"\n"
            "\n"
            "/**\n"
            " * @brief hi\n"
            " */\n"
            "void Foo(void);\n"
        )
        block = automation._block_above(text.splitlines(), 9)
        self.assertIsNotNone(block)
        self.assertIn("@brief hi", block)
        self.assertNotIn("@file", block)

    def test_returns_none_when_no_block(self):
        text = (
            "void Foo(void);\n"
        )
        self.assertIsNone(automation._block_above(text.splitlines(), 1))

    def test_returns_none_for_plain_comment_block(self):
        # Plain /* ... */ (not /** ... */) is not a Doxygen block.
        text = (
            "/* not doxygen */\n"
            "void Foo(void);\n"
        )
        self.assertIsNone(automation._block_above(text.splitlines(), 2))


class ComplianceSkipTests(unittest.TestCase):
    """End-to-end checks that the hook bypasses Codex for already-compliant
    staged files (so a second commit does not waste minutes re-translating)."""

    def _make_runner(self, staged, unstaged=(), codex_rc=0, doxygen_rc=0):
        runner = Mock()
        results = [
            subprocess.CompletedProcess([], 0, stdout=str(Path.cwd()) + "\n", stderr=""),
            subprocess.CompletedProcess([], 0, stdout="\n".join(staged), stderr=""),
            subprocess.CompletedProcess([], 0, stdout="\n".join(unstaged), stderr=""),
            subprocess.CompletedProcess([], 0, stdout="", stderr=""),
            subprocess.CompletedProcess([], 0, stdout="PASSED" if doxygen_rc == 0 else "FAILED", stderr=""),
            subprocess.CompletedProcess([], 0, stdout="", stderr=""),
        ]
        runner.side_effect = results
        return runner

    def test_compliant_file_is_skipped(self):
        # We only patch the compliance check, so the file does not need
        # to actually exist; runner is fully mocked.
        runner = self._make_runner(["app/foo.c"])
        with patch(
            "tools.codex_doxygen_staged.file_is_already_compliant",
            return_value=True,
        ):
            result = automation.run_automation(runner=runner, environ={})
        self.assertEqual(result, 0)
        # 3 calls: toplevel, staged, unstaged. No codex, no check, no add.
        self.assertEqual(runner.call_count, 3)

    def test_force_flag_bypasses_compliance_skip(self):
        runner = Mock()
        runner.side_effect = [
            subprocess.CompletedProcess([], 0, stdout=str(Path.cwd()) + "\n", stderr=""),
            subprocess.CompletedProcess([], 0, stdout="app/foo.c\n", stderr=""),
            subprocess.CompletedProcess([], 0, stdout="", stderr=""),
            subprocess.CompletedProcess([], 0, stdout="", stderr=""),
            subprocess.CompletedProcess([], 0, stdout="PASSED", stderr=""),
            subprocess.CompletedProcess([], 0, stdout="", stderr=""),
        ]
        with patch(
            "tools.codex_doxygen_staged.file_is_already_compliant",
            return_value=True,
        ):
            result = automation.run_automation(
                runner=runner,
                environ={"CODEX_DOXYGEN_FORCE": "1"},
            )
        self.assertEqual(result, 0)
        # 6 calls (no skip).
        self.assertEqual(runner.call_count, 6)


class CodexCommandTests(unittest.TestCase):
    """The Windows-safe codex invocation shape."""

    def test_windows_uses_cmd_exe(self):
        with patch("tools.codex_doxygen_staged.sys.platform", "win32"), patch(
            "tools.codex_doxygen_staged.shutil.which", return_value=None
        ):
            cmd = automation.resolve_codex_command()
        self.assertEqual(cmd[:3], ["cmd.exe", "/c", "codex"])

    def test_posix_uses_resolved_binary(self):
        with patch("tools.codex_doxygen_staged.sys.platform", "linux"), patch(
            "tools.codex_doxygen_staged.shutil.which", return_value="/usr/local/bin/codex"
        ):
            cmd = automation.resolve_codex_command()
        self.assertEqual(cmd, ["/usr/local/bin/codex"])

    def test_codex_bin_env_override(self):
        with patch.dict(
            "os.environ",
            {"CODEX_BIN": "/custom/path/to/codex"},
        ):
            cmd = automation.resolve_codex_command()
        self.assertEqual(cmd, ["/custom/path/to/codex"])