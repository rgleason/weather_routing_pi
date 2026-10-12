# Normal installation qualification

The published archive is imported by stock OpenCPN's `opencpn-cmd`, which uses
the plugin manager's extraction and compatibility code. The GUI then loads the
imported library and runs the plugin's existing route scenario interface.
These tests use neither `-p` nor `--configdir`, and remove the plugin data and
shoreline overrides used by older headless fixtures.

Linux builds an unmodified, checksum-pinned OpenCPN 5.14.2 source archive in
each supported distribution and architecture. Windows uses the existing
pinned official x86/x64 installers. macOS extracts the checksum-pinned official
universal package and runs its native Apple Silicon executable. The pins and
their checksums are retained with the source.

Stock `print-abi` reports the compile-time target. On Ubuntu 22.04 it reports
`ubuntu-x86_64`, while `CompatOs` selects `ubuntu-wx32-x86_64` when using wx 3.2.
The test recognises this specific pair and leaves the real import compatibility
decision to the stock host. It sets no compatibility override; both target
labels are retained in the receipt.

Linux uses a fresh application home. Windows uses fresh local and roaming
application-data roots and its normal common application-data config directory
in a disposable CircleCI VM. macOS uses the disposable VM account's normal home
and configuration directories, since Cocoa can resolve these independently of
the HOME environment variable. Both native tests refuse to run outside
CircleCI, and all platforms refuse to overwrite an existing configuration.
The laptop's working OpenCPN and tablet are not test environments for these
scripts.

The normal-installation contract checks:

- Host compatibility without forcing an ABI override, followed by real import.
- Installed library and every bundled data file against the final tarball.
- Each installed translation catalog against its packaged bytes and locale
  directory; this establishes installation, not translated GUI rendering.
- Presence of the installed help HTML.
- Logged `GetPluginDataDir` lookup in the actual user installation.
- Normal first-launch copying of bundled boats and nested polars. Routes use
  the copied user polar, rather than an absolute path to the package's polar.
- Quick, Standard and Professional routes with Detect Land off and on, using
  the Intermediate shoreline dataset.
- Shoreline cache creation beneath the normal private application data root.
- Existing route visibility/recompute/clear lifecycle checks on Windows.
- Reimport and restart preserving added personal files and modified standard
  boats/polars.
- Orderly host exit after every case. The scenario interface keeps the host
  running after its result, then the test uses the stock `--remote --quit` IPC
  action. The main frame closes and plugins deactivate before application
  teardown, and the host removes its instance lock before the next launch.

Reimport exercises replacement of the same package. It does not establish an
upgrade between different versions or the interactive data-migration dialog.
The stock 5.14.2 command-line uninstall tool throws `std::bad_function_call`
because `PluginLoader` invokes an unset activation callback. Removal is
therefore recorded as untested in the runtime receipt. It is a separate core
issue, not evidence of GUI removal failure; no patched host is substituted to
make the normal-installation gate pass.
Portable Windows checks remain separate and run after normal installation
checks. Flatpak retains its separate real-host normal-profile tests. Android
retains its packaging and ELF gates; desktop host tests do not establish
Android device behavior.

Runtime receipts contain the package SHA256, actual host ABI/version, resolved
private data directory and each completed route case. Logs and receipts are
stored with the platform's CI artifacts. A missing or failing runtime contract
fails the job before any reviewed deployment can proceed. Ordinary validation
defaults to a workflow without deployment credentials.
