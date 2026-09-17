import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools.fix_legacy_css import fix_css


class TestFixLegacyCss(unittest.TestCase):
    def test_var_fallback_preserves_single_closing_paren(self):
        css = ".bg-blue-600{--tw-bg-opacity: 1;background-color:rgb(37 99 235 / var(--tw-bg-opacity, 1))}"
        fixed, n = fix_css(css)
        self.assertEqual(n, 1)
        self.assertEqual(
            fixed,
            ".bg-blue-600{--tw-bg-opacity: 1;background-color:rgb(37,99,235)}",
        )

    def test_numeric_alpha_converts_to_rgba(self):
        css = ".bg-blue-500\\/50{background-color:rgb(59 130 246 / .5)}"
        fixed, n = fix_css(css)
        self.assertEqual(n, 1)
        self.assertEqual(
            fixed,
            ".bg-blue-500\\/50{background-color:rgba(59,130,246,.5)}",
        )

    def test_percentage_alpha_converts_to_rgba(self):
        css = "color: rgb(255 255 255 / 75%);"
        fixed, n = fix_css(css)
        self.assertEqual(n, 1)
        self.assertEqual(fixed, "color: rgba(255,255,255,75%);")

    def test_nested_var_fallback(self):
        css = "color: rgb(1 2 3 / var(--a, var(--b, 1)));"
        fixed, n = fix_css(css)
        self.assertEqual(n, 1)
        self.assertEqual(fixed, "color: rgb(1,2,3);")

    def test_existing_comma_syntax_untouched(self):
        css = "background-color: rgba(1, 2, 3, 0.4); color: rgb(10, 20, 30);"
        fixed, n = fix_css(css)
        self.assertEqual(n, 0)
        self.assertEqual(fixed, css)

    def test_hsl_support(self):
        css = "color: hsl(220 10% 50% / .5); background: hsl(220 10% 50% / var(--foo, 1));"
        fixed, n = fix_css(css)
        self.assertEqual(n, 2)
        self.assertEqual(
            fixed,
            "color: hsla(220,10%,50%,.5); background: hsl(220,10%,50%);",
        )


if __name__ == "__main__":
    unittest.main()
