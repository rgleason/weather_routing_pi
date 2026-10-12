#!/usr/bin/env python3
"""Import a package and route in the installed stock Flatpak, in a private profile."""
import argparse
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import tarfile
import xml.etree.ElementTree as ET


def test(archive, output, xvfb, reject_archive=None):
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    profile = output / "app-home"
    profile.mkdir()
    sandbox = output / "sandbox"
    source = Path(__file__).resolve().parents[1]
    with tarfile.open(archive) as package:
        metadata = ET.fromstring(package.extractfile("metadata.xml").read())
    identity = "xweather_routing_pi" if metadata.findtext("name").strip() == "xWeatherRouting" else "weather_routing_pi"
    architecture = metadata.findtext("target-arch").strip()
    subprocess.run(["flatpak", "build-init", "--type=extension", str(sandbox),
                    "org.opencpn.OpenCPN.Plugin.Acceptance", f"org.freedesktop.Sdk/{architecture}/25.08",
                    f"org.opencpn.OpenCPN/{architecture}/stable"], check=True)
    config = output / "config/opencpn"
    config.mkdir(parents=True)
    base = ["flatpak", "build", "--share=ipc", "--share=network", "--socket=x11",
            f"--filesystem={source}", f"--filesystem={output}",
            f"--filesystem={archive.resolve().parent}:ro",
            f"--bind-mount={Path.home()}/.var/app/org.opencpn.OpenCPN={profile}",
            "--env=FLATPAK_ID=org.opencpn.OpenCPN",
            f"--env=XDG_CONFIG_HOME={config.parent}",
            f"--env=XDG_DATA_HOME={output / 'data'}", f"--env=XDG_CACHE_HOME={output / 'cache'}"]

    def cli(name, arguments, check=True):
        result = subprocess.run(base + [str(sandbox), "/app/bin/opencpn-cmd"] + arguments,
                                text=True, capture_output=True, timeout=60)
        (output / (name + ".log")).write_text(result.stdout + result.stderr)
        if check and result.returncode:
            raise RuntimeError(f"Flatpak {name} failed; see {output / (name + '.log')}")
        return result

    abi = cli("host-abi", ["print-abi"]).stdout.strip()
    expected = metadata.findtext("target").strip() + ":" + metadata.findtext("target-version").strip()
    if abi != expected:
        raise RuntimeError(f"Package ABI {expected} differs from real host {abi}")
    if reject_archive:
        rejected_package = output / "rejected-package.tar.gz"
        shutil.copy2(reject_archive, rejected_package)
        rejected = cli("old-import", ["import-plugin", str(rejected_package)], check=False)
        if rejected.returncode == 0 or "Incompatible plugin" not in rejected.stderr:
            raise RuntimeError("The previously broken package did not reproduce the compatibility failure")
    cli("import", ["import-plugin", str(archive.resolve())])
    library = profile / "lib/opencpn" / f"lib{identity}.so"
    if not library.is_file():
        raise RuntimeError("Import did not install the packaged library in the private profile")
    polar = profile / f"data/plugins/{identity}/data/polars/Example/Test-TWS-0-20+60.pol"
    if not polar.is_file():
        raise RuntimeError("Import did not install the example polar")
    boat = output / "boat.xml"
    root = ET.Element("OpenCPNWeatherRoutingBoat", version="1.10", creator="Flatpak acceptance")
    ET.SubElement(root, "Polar", FileName=str(polar), CrossOverPercentage="0")
    ET.ElementTree(root).write(boat, encoding="utf-8", xml_declaration=True)
    version = metadata.findtext("version").strip().split(".")
    config_version = str(int(version[0]) * 100 + int(version[1]))
    location = subprocess.check_output(["flatpak", "info", "--show-location", "org.opencpn.OpenCPN"], text=True).strip()
    binary = (Path(location) / "files/bin/opencpn").read_bytes()
    host_version = re.search(rb"5\.14\.\d+-\d+\+[a-f0-9]+\x00", binary)
    dates = re.findall(rb"20\d{2}-\d{2}-\d{2}\x00", binary)
    if not host_version or not dates:
        raise RuntimeError("Cannot determine the stock host welcome-dialog version")
    # Portable OpenCPN looks beside its executable for the shared resources.
    # Mount a private copy of the stock bin directory with resource links;
    # the installed app and the user's profile remain untouched.
    host_bin = output / "host-bin"
    shutil.copytree(Path(location) / "files/bin", host_bin, symlinks=True)
    for resource in (Path(location) / "files/share/opencpn").iterdir():
        link = host_bin / resource.name
        if not link.exists():
            link.symlink_to("/app/share/opencpn/" + resource.name)
    header = "[Settings]\nConfigVersionString=Version " + host_version[0][:-1].decode() + " Build " + max(dates)[:-1].decode() + "\n"
    header += "NavMessageShown=1\nOpenGL=0\nDisableOpenGL=1\nDataConnections=\n"
    header += f"[PlugIns/lib{identity}.so]\nbEnabled=1\n[PlugIns/libgrib_pi.so]\nbEnabled=1\n"
    header += f"[PlugIns/WeatherRouting]\nConfigVersion={config_version}\n[Plugins/WeatherRouting]\nConfigVersion={config_version}\n"
    plugin_data = output / "plugin-profile"
    plugin_data.mkdir()
    ET.ElementTree(ET.Element("OpenCPNWeatherRoutingConfiguration", version="1.10")).write(
        plugin_data / "WeatherRoutingConfiguration.xml")
    read_fd, write_fd = os.pipe()
    with (output / "xvfb.log").open("w") as log:
        display_server = subprocess.Popen([xvfb, "-displayfd", str(write_fd), "-screen", "0", "1280x900x24", "-nolisten", "tcp", "-ac"],
                                          pass_fds=(write_fd,), stdout=log, stderr=subprocess.STDOUT)
        os.close(write_fd)
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(read_fd, selectors.EVENT_READ)
                if not selector.select(10):
                    raise RuntimeError("Private Xvfb did not start")
                display = ":" + os.read(read_fd, 128).decode().strip()
            os.close(read_fd)
            receipts = []
            for engine in ("original", "quick", "main"):
                (config / "opencpn.conf").write_text(header)
                scenario = dict(schemaVersion=1, name="Flatpak stock host " + engine,
                    start=dict(name="offshore west", lat=53.4, lon=-5.5),
                    end=dict(name="offshore east", lat=53.4, lon=-5.3),
                    startTime="2026-09-28T00:00:00Z", departureOptimization=dict(enabled=False),
                    environment=dict(useGrib=True, useCurrents=False, allowClimatologyFallback=False),
                    route=dict(routingEngine=engine, boatFile=str(boat), timeStepSeconds=900,
                               headingStepDegrees=5, maxSwellMeters=0),
                    safety=dict(mode="none", enforce=False))
                scenario_file = output / (engine + "-scenario.json")
                scenario_file.write_text(json.dumps(scenario, indent=2) + "\n")
                result_file = output / (engine + "-result.json")
                environment = dict(DISPLAY=display, WR_HEADLESS_ROUTE_TEST="scenario",
                    WR_HEADLESS_SCENARIO=str(scenario_file), WR_HEADLESS_OUTPUT=str(result_file),
                    WR_HEADLESS_GRIB_FILE=str(source / "ci/testdata/irish-sea-uniform-wind.grb"),
                    WR_HEADLESS_DATA_DIR=str(plugin_data), WR_HEADLESS_TIMEOUT_MS="90000",
                    WR_SHORELINE_DATA_HOME=str(output / "shoreline"))
                command = base + [f"--env={key}={value}" for key, value in environment.items()]
                command += [f"--bind-mount=/app/bin={host_bin}"]
                command += [str(sandbox), "/app/bin/opencpn", "--portable", f"--configdir={config}", "--no_opengl"]
                with (output / (engine + "-stdout.log")).open("w") as stdout:
                    process = subprocess.Popen(command, stdout=stdout, stderr=subprocess.STDOUT)
                    try:
                        code = process.wait(timeout=120)
                    except subprocess.TimeoutExpired:
                        process.kill(); process.wait()
                        raise RuntimeError(f"Flatpak {engine} route timed out")
                if not result_file.is_file():
                    raise RuntimeError(f"Flatpak {engine} produced no route report")
                result = json.loads(result_file.read_text())
                if code or result.get("status") != "complete":
                    raise RuntimeError(f"Flatpak {engine} route failed: exit {code}, {result.get('status')}")
                receipts.append(dict(engine=engine, status=result["status"], exit_code=code))
                shutil.copy2(config / "opencpn.log", output / (engine + "-opencpn.log"))
        finally:
            display_server.terminate(); display_server.wait(timeout=5)
    (output / "acceptance.json").write_text(json.dumps(dict(host_abi=abi,
        stock_host=host_version[0][:-1].decode(), package=archive.name,
        isolated_application_home=True, import_passed=True, routes=receipts), indent=2) + "\n")
    print("Stock Flatpak import and Quick/Standard/Professional routes passed", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--xvfb", default=shutil.which("Xvfb") or "Xvfb")
    parser.add_argument("--reject-archive", type=Path)
    args = parser.parse_args()
    test(args.archive, args.output, args.xvfb, args.reject_archive)
