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
        self.assertEqual(
            commands[3],
            ["codex", "exec", "--ephemeral", "-s", "workspace-write", "-C", root, "-"],
        )
        self.assertEqual(commands[4], [automation.python_executable(), "tools/check_doxygen.py", "app/foo.c"])
        self.assertEqual(commands[5], ["git", "add", "--", "app/foo.c"])
        self.assertIn("docs/DOXYGEN_STYLE.md", runner.call_args_list[3].kwargs["input"])


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
