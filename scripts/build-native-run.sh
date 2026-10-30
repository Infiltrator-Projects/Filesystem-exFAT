#!/usr/bin/env bash
# Build the self-extracting, source-embedded native exFAT installer.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
dest=${1:?Expected destination for native .run}
case "$dest" in /*) ;; *) dest="$PWD/$dest" ;; esac
revision=$(git -C "$root" rev-parse HEAD)
[[ $revision =~ ^[0-9a-f]{40}$ ]] || exit 1
version=$(<"$root/VERSION")
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || exit 1
mkdir -p -- "$(dirname -- "$dest")"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
git -C "$root" archive --format=tar HEAD | gzip -n > "$work/source.tar.gz"
sha=$(sha256sum "$work/source.tar.gz" | cut -d' ' -f1)
cat > "$work/header" <<'HEADER'
#!/usr/bin/env bash
set -euo pipefail
if [[ ${1:-} == --help ]]; then
  echo 'Usage: filesystem-exfat-native-installer.run [--profile aggressive] [--system-package-mode] [--build-only --output DIRECTORY]'
  exit 0
fi
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
line=$(awk '/^__EXFAT_NATIVE_SOURCE_BELOW__$/ {print NR+1; exit}' "$0")
[[ $line =~ ^[0-9]+$ ]] || exit 1
tail -n +"$line" "$0" > "$work/source.tar.gz"
printf '%s  %s\n' '@ARCHIVE_SHA256@' "$work/source.tar.gz" | sha256sum -c - >/dev/null
mkdir -- "$work/source"
tar -xzf "$work/source.tar.gz" -C "$work/source" --no-same-owner --no-same-permissions
EXFAT_SOURCE_REVISION=@SOURCE_REVISION@ bash "$work/source/scripts/native-package.sh" "$@"
exit $?
__EXFAT_NATIVE_SOURCE_BELOW__
HEADER
sed -e "s/@ARCHIVE_SHA256@/$sha/g" -e "s/@SOURCE_REVISION@/$revision/g" "$work/header" > "$dest"
cat "$work/source.tar.gz" >> "$dest"
chmod 0755 "$dest"
printf 'Embedded native exFAT source from %s into %s\n' "$revision" "$dest"
