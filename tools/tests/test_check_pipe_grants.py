# SPDX-License-Identifier: Apache-2.0
"""Unit tests of tools/check_pipe_grants.py."""
import copy
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import check_pipe_grants  # noqa: E402

SPEC = os.path.join(os.path.dirname(__file__), "..", "..", "deploy",
                    "telegraph.json")


def pipe(options):
    return {"Name": "pipe", "Options": options}


def wapp(spec, name):
    return next(w for w in spec["wapps"] if w["name"] == name)


class DeploySpec(unittest.TestCase):
    def setUp(self):
        with open(SPEC) as f:
            self.spec = json.load(f)

    def test_the_shipped_spec_passes(self):
        self.assertEqual([], check_pipe_grants.problems(self.spec))

    def test_a_peer_missing_its_grant_is_named(self):
        spec = copy.deepcopy(self.spec)
        wapp(spec, "tg-time")["policy"]["Drivers"] = []
        found = check_pipe_grants.problems(spec)
        self.assertIn("tg-time: no `read=tg-time-rsp` for peer `time`", found)
        self.assertIn("tg-time: no `write=tg-time-req` for peer `time`", found)

    def test_a_broker_argument_without_a_grant_is_named(self):
        spec = copy.deepcopy(self.spec)
        wapp(spec, "tg-broker")["args"].append("probe:0x03")
        found = check_pipe_grants.problems(spec)
        self.assertIn("tg-broker: peer `probe` has no wapp", found)

    def test_a_renamed_client_needs_the_grant_of_its_name(self):
        spec = copy.deepcopy(self.spec)
        hass = wapp(spec, "tg-hass")
        hass["envs"].append("TELEGRAPH_CLIENT=mqtt")
        wapp(spec, "tg-display")["args"] = ["rest", "mqtt"]
        found = check_pipe_grants.problems(spec)
        self.assertIn("tg-hass: no `write=tgd-mqtt-req` for client `mqtt`", found)
        self.assertIn("tg-hass: `write=tgd-hass-req` matches no argument", found)

    def test_a_grant_over_eight_pipes_is_refused(self):
        spec = copy.deepcopy(self.spec)
        drivers = wapp(spec, "tg-display")["policy"]["Drivers"]
        drivers.append(pipe("read=tgd-a-req,write=tgd-a-rsp"))
        wapp(spec, "tg-display")["args"] = ["rest", "hass", "a"]
        spec["wapps"].append({"name": "tg-a", "policy": {"Drivers": [
            pipe("write=tgd-a-req,read=tgd-a-rsp")]}})
        self.assertEqual([], check_pipe_grants.problems(spec))
        drivers.append(pipe("read=tgd-b-req,write=tgd-b-rsp"))
        wapp(spec, "tg-display")["args"].append("b")
        spec["wapps"].append({"name": "tg-b", "policy": {"Drivers": [
            pipe("write=tgd-b-req,read=tgd-b-rsp")]}})
        self.assertIn("tg-display: the grant names over 8 pipes",
                      check_pipe_grants.problems(spec))


if __name__ == "__main__":
    unittest.main()
