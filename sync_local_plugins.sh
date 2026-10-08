#!/usr/bin/env bash

# Local-only helper: build selected non-Windows platforms, package the artifacts
# already written to plugins/, and install the archives into the developer's
# local Solar2D plugin server. This script does not edit plugins.txt.

set -euo pipefail

root_dir="$(cd "$(dirname "$0")" && pwd -P)"
plugin_build="${PLUGIN_BUILD:-2025.3720}"
config="${CONFIG:-Release}"
local_server_root=""
dev_config_args=()
plugins_root="$root_dir/plugins"
plugin_output_root="$plugins_root/$plugin_build"
local_server_plugins=""
staging_root=""
pending_archive=""
force=0
requested_platforms=()

usage() {
    cat <<'EOF'
Usage: ./sync_local_plugins.sh [--config FILE] [--force] [platform ...]

Platforms:
  mac-sim iphone iphone-sim appletvos android

With no platform arguments, the script builds and syncs every listed platform.
Requesting either iphone platform runs src/ios/build.sh once, which builds both
the device and Simulator libraries; only requested archives are installed.

Options:
  --config FILE  Select development JSON (default: dev.local.json).
  --force, -f  Skip the interactive confirmation.
  --help, -h   Show this help.

Environment overrides:
  PLUGIN_BUILD=2025.3720
  CONFIG=Release
  LOCAL_PLUGIN_SERVER_ROOT=/path/to/local_server_4_solar2d_plugins
EOF
}

cleanup() {
    local exit_status=$?
    trap - EXIT
    if [[ -n "$pending_archive" && -f "$pending_archive" ]]; then
        rm -f -- "$pending_archive"
    fi
    if [[ -n "$staging_root" && -d "$staging_root" ]]; then
        rm -rf -- "$staging_root"
    fi
    exit "$exit_status"
}
trap cleanup EXIT

fail() {
    echo "ERROR: $*" >&2
    exit 1
}

require_file() {
    [[ -f "$1" ]] || fail "missing plugin artifact: $1"
}

require_dir() {
    [[ -d "$1" ]] || fail "missing plugin artifact directory: $1"
}

reject_symlink() {
    [[ ! -L "$1" ]] || fail "refusing to use symbolic-link path: $1"
}

ensure_within() {
    local child="$1"
    local parent="$2"
    local child_real
    local parent_real

    child_real="$(cd "$child" && pwd -P)"
    parent_real="$(cd "$parent" && pwd -P)"
    [[ "$child_real" == "$parent_real/"* ]] || \
        fail "destination escaped its allowed root: $child_real"
}

validate_platform() {
    case "$1" in
        mac-sim|iphone|iphone-sim|appletvos|android) ;;
        *) fail "unsupported platform '$1' (run with --help for the platform list)" ;;
    esac
}

validate_configuration() {
    [[ "$plugin_build" =~ ^[0-9]{4}\.[0-9]+$ ]] || \
        fail "PLUGIN_BUILD must match YYYY.BUILD (for example 2025.3720)"
    [[ "$config" =~ ^[A-Za-z0-9._-]+$ ]] || \
        fail "CONFIG may contain only letters, digits, dot, underscore, and hyphen"
    mkdir -p "$plugins_root"
    reject_symlink "$plugins_root"

    [[ -d "$local_server_root" ]] || \
        fail "local plugin server does not exist: $local_server_root"
    local_server_root="$(cd "$local_server_root" && pwd -P)"
    [[ "$local_server_root" != "/" && "$local_server_root" != "$root_dir" ]] || \
        fail "refusing unsafe local plugin server root: $local_server_root"
    [[ -e "$local_server_root/.git" ]] || \
        fail "local plugin server is not a Git worktree: $local_server_root"

    local_server_plugins="$local_server_root/plugins"
    require_dir "$local_server_plugins"
    reject_symlink "$local_server_plugins"
}

confirm_sync() {
    echo "Plugin build: $plugin_build"
    echo "Configuration: $config"
    echo "Build scripts: src/{mac,ios,tvos,android}/build.sh as selected"
    echo "Artifact source: $plugin_output_root"
    echo "Local server: $local_server_plugins"
    echo "Platforms: ${platforms[*]}"
    echo "All selected builds will run before same-version local-server archives are replaced."

    if [[ "$force" -eq 1 ]]; then
        return
    fi
    [[ -t 0 ]] || fail "interactive confirmation required; rerun with --force"

    local answer
    read -r -p "Continue? [y/N] " answer
    case "$answer" in
        y|Y|yes|YES) ;;
        *) echo "Cancelled."; exit 0 ;;
    esac
}

run_build() {
    local directory="$1"
    echo "Building with src/$directory/build.sh"
    (
        cd "$root_dir/src/$directory"
        PLUGIN_BUILD="$plugin_build" CONFIG="$config" ./build.sh
    )
}

build_selected_platforms() {
    local need_mac=0
    local need_ios=0
    local need_tvos=0
    local need_android=0
    local platform

    for platform in "${platforms[@]}"; do
        case "$platform" in
            mac-sim) need_mac=1 ;;
            iphone|iphone-sim) need_ios=1 ;;
            appletvos) need_tvos=1 ;;
            android) need_android=1 ;;
        esac
    done

    [[ "$need_mac" -eq 0 ]] || run_build mac
    [[ "$need_ios" -eq 0 ]] || run_build ios
    [[ "$need_tvos" -eq 0 ]] || run_build tvos
    [[ "$need_android" -eq 0 ]] || run_build android
}

preflight_platform() {
    local source_dir="$plugin_output_root/$1"
    local abi

    require_dir "$source_dir"
    reject_symlink "$source_dir"
    case "$1" in
        mac-sim)
            require_file "$source_dir/plugin_geometry2d.dylib"
            ;;
        iphone|iphone-sim)
            require_file "$source_dir/libplugin_geometry2d.a"
            require_file "$source_dir/metadata.lua"
            ;;
        appletvos)
            require_dir "$source_dir/Corona_plugin_geometry2d.xcframework"
            require_file "$source_dir/Corona_plugin_geometry2d.xcframework/Info.plist"
            ;;
        android)
            for abi in armeabi-v7a arm64-v8a x86 x86_64; do
                require_file "$source_dir/jniLibs/$abi/libplugin.geometry2d.so"
            done
            require_file "$source_dir/libplugin.geometry2d.so"
            require_file "$source_dir/metadata.lua"
            ;;
    esac
}

create_archive() {
    local platform="$1"
    local source_dir="$plugin_output_root/$platform"
    local archive="$staging_root/$plugin_build-$platform.tgz"

    case "$platform" in
        mac-sim)
            tar -czf "$archive" -C "$source_dir" plugin_geometry2d.dylib
            ;;
        iphone|iphone-sim)
            tar -czf "$archive" -C "$source_dir" \
                libplugin_geometry2d.a metadata.lua
            ;;
        appletvos)
            tar -czf "$archive" -C "$source_dir" \
                Corona_plugin_geometry2d.xcframework
            ;;
        android)
            tar -czf "$archive" -C "$source_dir" \
                jniLibs libplugin.geometry2d.so metadata.lua
            ;;
    esac
    tar -tzf "$archive" >/dev/null
}

prepare_destination() {
    local platform="$1"
    local target_dir="$local_server_plugins/$platform/plugin.geometry2d"

    reject_symlink "$local_server_plugins/$platform"
    reject_symlink "$target_dir"
    mkdir -p "$target_dir"
    ensure_within "$target_dir" "$local_server_plugins"
    [[ -w "$target_dir" ]] || fail "local server directory is not writable: $target_dir"
}

install_archive() {
    local platform="$1"
    local archive_name="$plugin_build-$platform.tgz"
    local source_archive="$staging_root/$archive_name"
    local target_dir="$local_server_plugins/$platform/plugin.geometry2d"

    pending_archive="$(mktemp "$target_dir/.${archive_name}.XXXXXX")"
    cp -f "$source_archive" "$pending_archive"
    tar -tzf "$pending_archive" >/dev/null
    mv -f "$pending_archive" "$target_dir/$archive_name"
    pending_archive=""
    echo "  $platform -> $target_dir/$archive_name"
}

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --force|-f)
            force=1
            ;;
        --config)
            [[ "$#" -ge 2 ]] || fail 'Missing --config value'
            dev_config_args=(--config "$2")
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        --)
            shift
            while [[ "$#" -gt 0 ]]; do
                requested_platforms+=("$1")
                shift
            done
            break
            ;;
        -*)
            fail "unknown option '$1'"
            ;;
        *)
            requested_platforms+=("$1")
            ;;
    esac
    shift
done

if [[ "${#requested_platforms[@]}" -eq 0 ]]; then
    platforms=(mac-sim iphone iphone-sim appletvos android)
else
    platforms=("${requested_platforms[@]}")
fi

for platform in "${platforms[@]}"; do
    validate_platform "$platform"
done
local_server_root="$(python3 "$root_dir/scripts/dev_config.py" ${dev_config_args[@]+"${dev_config_args[@]}"} --get localPluginServerRoot)"
validate_configuration
confirm_sync

build_selected_platforms

for platform in "${platforms[@]}"; do
    preflight_platform "$platform"
done

staging_root="$(mktemp -d "${TMPDIR:-/tmp}/plugin-geometry2d-sync.XXXXXX")"

echo "Packaging directly from $plugin_output_root"
for platform in "${platforms[@]}"; do
    create_archive "$platform"
done

for platform in "${platforms[@]}"; do
    prepare_destination "$platform"
done

echo "Installing plugin.geometry2d archives into the local Solar2D plugin server"
for platform in "${platforms[@]}"; do
    install_archive "$platform"
done
