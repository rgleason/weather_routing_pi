"""Run the real wxDC probe with a private Linux display, or native macOS GUI."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('installed_host', source / 'ci/test-installed-host.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
with tempfile.TemporaryDirectory(prefix='wr-software-dc-') as directory:
    with helper.display(dict(os.environ), Path(directory)) as environment:
        raise SystemExit(subprocess.call(sys.argv[1:], env=environment))
