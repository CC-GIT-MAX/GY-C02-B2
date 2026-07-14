import subprocess
import sys
import unittest
from pathlib import Path

from tools import check_doxygen

SAMPLE_C = """/**
 * @brief   Fetch the most recent raw frame for an IPK CAN id
 * @brief   \u83b7\u53d6\u67d0 IPK CAN id \u6700\u8fd1\u4e00\u6b21\u7684\u539f\u59cb\u5e27
 *
 * @param[in]   can_id  Standard 11-bit IPK can_id
 * @param[out]  out     Populated with the cached frame on success
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Frame returned (may be stale)
 */
c02b2_result_t CanRx_GetLastRawFrame(u32 can_id, can_msg_t *out)
{
    return C02B2_OK;
}
"""

SAMPLE_C_NO_PARAMS = """/**
 * @brief   Init the module
 * @brief   \u521d\u59cb\u5316\u6a21\u5757
 */
c02b2_result_t CanRx_Reset(void)
{
    return C02B2_OK;
}
"""

SAMPLE_C_NO_BRIEF_ZH = """/**
 * @brief   Only English brief
 */
c02b2_result_t CanRx_GetLastRawFrame(u32 can_id, can_msg_t *out)
{
    return C02B2_OK;
}
"""


class CountBriefsTests(unittest.TestCase):
    def test_chinese_brief_counts(self):
        block = Path('x').read_text if False else SAMPLE_C
        en, zh = check_doxygen.count_briefs(block)
        self.assertEqual(en, 1)
        self.assertEqual(zh, 1)


class HasChineseBriefTests(unittest.TestCase):
    def test_chinese_present(self):
        self.assertTrue(check_doxygen.has_chinese_brief(SAMPLE_C))

    def test_english_only_fails(self):
        self.assertFalse(check_doxygen.has_chinese_brief(SAMPLE_C_NO_BRIEF_ZH))

    def test_no_brief_fails(self):
        self.assertFalse(check_doxygen.has_chinese_brief("/* no brief */"))

class TargetListTests(unittest.TestCase):
    def test_main_scans_only_targets_when_provided(self):
        # Pre-create a temp C file with a missing brief, plus ensure no other failures are reported.
        from pathlib import Path as _P
        good = _P("app/can/can_target_good.c")
        good.parent.mkdir(parents=True, exist_ok=True)
        good.write_text(
            "/**\n"
            " * @brief   \u6f14\u793a\n"
            " * @brief   demo\n"
            " */\n"
            "void target_good(void) {}\n",
            encoding="utf-8",
        )
        try:
            completed = subprocess.run(
                [sys.executable, "tools/check_doxygen.py", "app/can/can_target_good.c"],
                capture_output=True, text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            self.assertIn("staged target file", completed.stdout)
            # Ensure a file with a real fail is NOT reported when excluded.
            self.assertNotIn("Signal_Get", completed.stdout)
        finally:
            good.unlink(missing_ok=True)

    def test_main_falls_back_to_full_scan_without_args(self):
        completed = subprocess.run(
            [sys.executable, "tools/check_doxygen.py"],
            capture_output=True, text=True,
        )
        # exit code non-zero because the repo currently has unfilled Doxygen comments,
        # but stdout must report the full scan.
        self.assertIn("Scanning ", completed.stdout)
        self.assertIn("files in app/", completed.stdout)

if __name__ == "__main__":
    unittest.main()

class HasChineseDetailsTests(unittest.TestCase):
    def test_detects_chinese_details(self):
        block = (
            "/**\n"
            " * @brief   Fetch the most recent raw frame\n"
            " * @brief   \u83b7\u53d6\u6700\u8fd1\u4e00\u6b21\u7684\u539f\u59cb\u5e27\n"
            " *\n"
            " * @details RX tick \u5728\u6bcf\u4e2a can_id \u4e0a\u7f13\u5b58\u6700\u8fd1\u6536\u5230\u7684\u539f\u59cb payload\n"
            " *\n"
            " * @return  c02b2_result_t\n"
            " */"
        )
        self.assertTrue(check_doxygen.has_chinese_details(block))

    def test_missing_chinese_details_fails(self):
        block = (
            "/**\n"
            " * @brief   Fetch the most recent raw frame\n"
            " * @brief   \u83b7\u53d6\u6700\u8fd1\u4e00\u6b21\u7684\u539f\u59cb\u5e27\n"
            " * @details Only English detail line, no CJK content.\n"
            " * @return  c02b2_result_t\n"
            " */"
        )
        self.assertFalse(check_doxygen.has_chinese_details(block))

    def test_missing_details_block_fails(self):
        block = (
            "/**\n"
            " * @brief   Fetch the most recent raw frame\n"
            " * @brief   \u83b7\u53d6\u6700\u8fd1\u4e00\u6b21\u7684\u539f\u59cb\u5e27\n"
            " * @return  c02b2_result_t\n"
            " */"
        )
        self.assertFalse(check_doxygen.has_chinese_details(block))
