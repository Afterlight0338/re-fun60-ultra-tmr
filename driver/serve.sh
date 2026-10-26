#!/bin/sh
# Serve FUN60 Studio at http://127.0.0.1:8060 . WebHID only works on localhost or HTTPS, and
# only in Chromium-based browsers.   sh driver/serve.sh   (`sh driver/serve.sh build` only builds site/)
# `sh driver/serve.sh deploy` publishes the same page to https://driver.vivlos.dev instead
# (Cloudflare Worker, static assets only, see wrangler.jsonc; needs `npx wrangler login` once).
#
# Every firmware in firmware.json is built from its git tag into site/fw/ for the page's flasher,
# so a version always ships exactly what its tag holds (fw/vendor from `make -C fw vendor` is reused).
set -e
cd "$(dirname "$0")"
mkdir -p site/fw
{ printf '<!doctype html>\n<html lang="en"><meta charset="utf-8">\n<meta name="viewport" content="width=device-width,initial-scale=1">\n'
  cat app.html; } > site/index.html
cp firmware.json site/fw/

[ -d ../fw/vendor/at32 ] || { echo "fw/vendor missing: run make -C fw vendor first"; exit 1; }
make_fw() { if command -v arm-none-eabi-gcc >/dev/null; then make -s "$@"; else nix-shell -p gnumake gcc-arm-embedded --run "make -s $*"; fi; }
tmp=$(mktemp -d)
trap 'git -C .. worktree prune; rm -rf "$tmp"' EXIT
sed -n 's/.*"version": *"\([^"]*\)".*"ref": *"\([^"]*\)".*/\1 \2/p' firmware.json | while read -r ver ref; do
  git -C .. worktree add -q --detach "$tmp/$ver" "$ref"
  ln -s "$(cd ../fw/vendor && pwd)" "$tmp/$ver/fw/vendor"
  make_fw -C "$tmp/$ver/fw" >"$tmp/log" 2>&1 || { cat "$tmp/log"; exit 1; }   # newlib stub warnings otherwise
  cp "$tmp/$ver/fw/build/fun60_dfu.bin" "site/fw/fun60-v$ver.bin"
  git -C .. worktree remove --force "$tmp/$ver"
  echo "built firmware v$ver from $ref"
done

[ "$1" = build ] && exit 0
[ "$1" = deploy ] && exec npx -y wrangler@4 deploy
echo "FUN60 Studio: http://127.0.0.1:8060"
exec python3 -m http.server 8060 --bind 127.0.0.1 -d site
