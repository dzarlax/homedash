"""Regression coverage for stale generated configs and inherited GDMA settings."""
import unittest

from check_rgb_cache import check_config


SAFE = """#define CONFIG_IDF_TARGET_ESP32S3 1
#define CONFIG_SPIRAM 1
#define CONFIG_SPIRAM_FETCH_INSTRUCTIONS 1
#define CONFIG_SPIRAM_RODATA 1
"""


class RGBCacheConfigTest(unittest.TestCase):
    def test_cache_deferred_config(self):
        check_config(SAFE)

    def test_old_release_config_rejected(self):
        with self.assertRaisesRegex(ValueError, "CONFIG_LCD_RGB_ISR_IRAM_SAFE"):
            check_config(SAFE + "#define CONFIG_LCD_RGB_ISR_IRAM_SAFE 1\n"
                         "#define CONFIG_GDMA_ISR_IRAM_SAFE 1\n")

    def test_gdma_enabled_independently_rejected(self):
        with self.assertRaisesRegex(ValueError, "CONFIG_GDMA_ISR_IRAM_SAFE"):
            check_config(SAFE + "#define CONFIG_GDMA_ISR_IRAM_SAFE 1\n")

    def test_missing_psram_xip_rejected(self):
        for name in ("CONFIG_SPIRAM", "CONFIG_SPIRAM_FETCH_INSTRUCTIONS", "CONFIG_SPIRAM_RODATA"):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, name):
                check_config(SAFE.replace(f"#define {name} 1\n", ""))

    def test_wrong_or_empty_header_rejected(self):
        for header in ("", SAFE.replace("ESP32S3", "ESP32")):
            with self.subTest(header=header), self.assertRaises(ValueError):
                check_config(header)


if __name__ == "__main__":
    unittest.main()
