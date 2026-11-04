"""Execute platform identity selection without downloading or building SDKs."""
import os
from pathlib import Path
import subprocess
import unittest


class BuildIdentity(unittest.TestCase):
    def test_explicit_ci_identity_overrides_repository_default(self):
        root = Path(__file__).parents[1]
        scripts = (
            ("build-linux-catalogue.sh", "identity_argument=", "identity_argument"),
            ("build-android-arm64.sh", "package_name=", "identity_arg"),
            ("circleci-build-flatpak.sh", "SET_IDENTITY=", "SET_IDENTITY"),
            ("circleci-build-macos-universal.sh", "if [[ \"${WEATHER_ROUTING_CI_XWEATHER", "common_args[0]"),
        )
        for filename, start, variable in scripts:
            source = (root / filename).read_text()
            # Run the real selection block and stop before platform setup/build.
            block = source[source.index(start):].split("\nfi", 1)[0] + "\nfi\n"
            for repository in ("weather_routing_pi", "xweather_routing_pi"):
                for requested in (None, "false", "true"):
                    with self.subTest(script=filename, repository=repository, requested=requested):
                        env = dict(os.environ, CIRCLE_PROJECT_REPONAME=repository)
                        env.pop("WEATHER_ROUTING_CI_XWEATHER", None)
                        env.pop("WEATHER_ROUTING_XWEATHER_IDENTITY", None)
                        if requested is not None:
                            env["WEATHER_ROUTING_CI_XWEATHER"] = requested
                        output = subprocess.check_output(
                            ["bash", "-eu", "-c", "common_args=()\n" + block +
                             'printf "%s" "${' + variable + '}"'], env=env, text=True)
                        expected = (requested == "true" if requested is not None else
                                    repository == "xweather_routing_pi")
                        self.assertEqual(output.rsplit("=", 1)[-1], "ON" if expected else "OFF")


if __name__ == "__main__":
    unittest.main()
