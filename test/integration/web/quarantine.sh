#!/bin/sh
# Checks that shader_error.mjs and transitions.mjs fail at once when the Chrome app has the
# macOS quarantine attribute: builds a fake Foo.app with the attribute and a fake bin dir, runs
# both scripts with --chrome pointing into it and expects exit status 2 with the message within
# a few seconds. macOS only.
set -u
[ "$(uname)" = Darwin ] || { echo "SKIP: not macOS"; exit 0; }
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/Foo.app/Contents/MacOS" "$tmp/bin"
printf '#!/bin/sh\nsleep 60\n' > "$tmp/Foo.app/Contents/MacOS/foo"
chmod +x "$tmp/Foo.app/Contents/MacOS/foo"
: > "$tmp/bin/boxrun.html"
xattr -w com.apple.quarantine "0081;00000000;test;" "$tmp/Foo.app"
status=0
for script in shader_error.mjs transitions.mjs; do
  start=$(date +%s)
  out=$(node "$here/$script" --chrome "$tmp/Foo.app/Contents/MacOS/foo" "$tmp/bin" 2>&1)
  code=$?
  secs=$(( $(date +%s) - start ))
  if [ "$code" -eq 2 ] && [ "$secs" -lt 5 ] &&
     printf '%s' "$out" | grep -q "Foo.app has the com.apple.quarantine attribute" &&
     printf '%s' "$out" | grep -q "xattr -dr com.apple.quarantine"; then
    echo "PASS $script (${secs}s)"
  else
    echo "FAIL $script: status $code after ${secs}s"
    printf '%s\n' "$out"
    status=1
  fi
done
exit $status
