# Publish the corrected WeatherRouting packages

These packaging changes do not change routing or bump the plugin version.
A fresh pipeline creates a new Cloudsmith build version and new download URLs.
Do not move an existing release tag or replace the old tarballs in place.

1. Merge this packaging fix into `rgleason/weather_routing_pi:master` first.
   If releasing 1.32, also merge the 1.32 integration PR before starting the build.
   The version in `CMakeLists.txt` determines which version is built.
2. In CircleCI, trigger a **new pipeline on master** with:

   ```text
   run_workflow_deploy = true
   run_windows_only = false
   build_xweather_identity = false
   publication_channel = production
   release_workflow_id = empty (enter no characters)
   ```

   Do not rerun an old pipeline or supply its workflow UUID: its packages may
   contain the old metadata. Do not type quotation marks into an empty field.
3. Wait for the complete build/test matrix and `stage-publication` to pass.
   This stage checks the pinned OpenCPN catalogue schema against the final XML
   files and the `metadata.xml` inside each finished tarball. Both copies must
   be identical. Failure prevents the approval hold from becoming ready.
4. Open the `reviewed-publication` artifacts from `stage-publication`.
   Verify `publication.json` says `production`, `WeatherRouting`, the intended
   source revision, and `opencpn/weather-routing-prod`. Test importing the staged
   Windows x64 tarball into 64-bit OpenCPN before approving publication.
5. Approve `hold-for-publication-approval` in **that same pipeline**.
   The existing deployment credential must have write access to
   `opencpn/weather-routing-prod`. No credential needs to be pasted into a PR.
6. Wait for `publish-reviewed` to pass. Download the new Cloudsmith tarballs
   and XML, confirming their URLs and contents match the reviewed files.
7. Replace the superseded WeatherRouting metadata entries in a catalogue PR
   using these newly published XML files, unchanged. Do not reuse XML from the
   failed catalogue PR or edit URLs by hand. Wait for catalogue validation.

For Windows x64 the compatibility `target` is `msvc-64`, the deprecated
`build-target` is `msvc`, and `target-arch` is `x86_64`. These are distinct fields;
do not globally replace `msvc-64` with `msvc`.
