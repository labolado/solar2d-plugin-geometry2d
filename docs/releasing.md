# Explicit version releases

No release is created by editing these files or pushing an ordinary branch.
The initial planned tag is `v1` (`VERSION` contains `1`); it is not evidence of an existing release.

| Event | Result |
|---|---|
| Relevant branch push / pull request | Validate and build platform artifacts only |
| Documentation-only branch push / pull request | No automatic CI |
| workflow_dispatch, including on a tag | Validate and build only |
| Push vN matching VERSION, e.g. v1 or v2 | Build all platforms, then publish that tag |

`repository_dispatch` is no longer a release trigger. The workflow run number is
not a plugin version. N must be a positive integer without leading zeros.
Zero, dotted versions and pre-release suffixes are rejected. Choose each new
version explicitly (v1, v2, v3, ...); CI does not increment it automatically.
Invalid tags or mismatched example pins fail validation before platform builds.

## Preparing a future release

1. Choose the version explicitly in `VERSION` (without `v`) and update
   `plugin_version` in `examples/solar2d/build.settings` (with `v`). Keep them equal.
2. Run source/tooling checks and relevant native/Simulator tests. Use a manual CI
   build to verify every platform on the intended commit; inspect the artifacts.
3. Commit and push the intended source changes. This does not publish a release.
4. Only when ready to publish, create and push the matching version tag on that
   verified commit. Do not move an existing release tag.
5. Verify the six platform archives/downloads and example on the intended targets.

The publish job has the only `contents: write` permission. It requires all build
jobs to succeed, refuses an existing release (including drafts), creates a draft,
uploads all archives without overwrite, and publishes only after upload succeeds.
If upload fails, the draft remains for manual inspection; a retry does not silently
overwrite it. Decide how to recover before rerunning. Repository-side immutable
release protection is not configured by this change.

Directory-update notification occurs only after successful publication and only
if ISSUE_PAT is configured. This configuration change does not send that request.

## Version versus engine build

`v1` is the planned plugin release tag. `2025.3720` is the existing Solar2D packaging
build identifier. Archive names remain `2025.3720-<platform>.tgz`; incrementing the
plugin version does not by itself change engine ABI, SDK or archive naming.
All four Android ABIs (armeabi-v7a, arm64-v8a, x86, x86_64) remain required.

The example uses fixed release URLs under `labolado/solar2d-plugin-geometry2d`,
never localhost or latest. Local Simulator development uses the isolated local
plugin mode documented in [development](development.md), avoiding an old release
or installed cache being mistaken for the binary under test.

Workflow/source checks are not proof that GitHub runners, downloads or device
packaging have succeeded. Those require real runs when the maintainer is ready.
