# SPDX-License-Identifier: Apache-2.0
"""Unit tests of tools/wapppush.py."""
import gzip
import hashlib
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import wapppush  # noqa: E402

TAR = b"ustar" + bytes(range(256)) * 40


def sha256(data):
    return "sha256:" + hashlib.sha256(data).hexdigest()


class LayerAndConfig(unittest.TestCase):
    def setUp(self):
        self.layer, config = wapppush.layer_and_config(TAR)
        self.config = json.loads(config)

    def test_the_layer_holds_the_ustar(self):
        self.assertEqual(gzip.decompress(self.layer), TAR)

    def test_the_diff_id_is_the_digest_of_the_ustar(self):
        diff_ids = self.config["rootfs"]["diff_ids"]
        self.assertEqual(diff_ids, [sha256(TAR)])
        self.assertEqual(diff_ids, [sha256(gzip.decompress(self.layer))])

    def test_the_diff_id_is_not_the_digest_of_the_blob(self):
        self.assertNotEqual(self.config["rootfs"]["diff_ids"],
                            [wapppush.digest(self.layer)])

    def test_the_layer_does_not_depend_on_the_time(self):
        again, _ = wapppush.layer_and_config(TAR)
        self.assertEqual(again, self.layer)

    def test_the_config_has_one_layer(self):
        self.assertEqual(self.config["rootfs"]["type"], "layers")
        self.assertEqual(len(self.config["rootfs"]["diff_ids"]), 1)


if __name__ == "__main__":
    unittest.main()
