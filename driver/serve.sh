#!/bin/sh
# Serve FUN60 Studio at http://127.0.0.1:8060 . WebHID only works on localhost or HTTPS, and
# only in Chromium-based browsers.   nix-shell -p python3 --run driver/serve.sh
# `driver/serve.sh deploy` publishes the same page to https://driver.vivlos.dev instead
# (Cloudflare Worker, static assets only, see wrangler.jsonc; needs `npx wrangler login` once).
cd "$(dirname "$0")" || exit 1
mkdir -p site
{ printf '<!doctype html>\n<html lang="en"><meta charset="utf-8">\n<meta name="viewport" content="width=device-width,initial-scale=1">\n'
  cat app.html; } > site/index.html
[ "$1" = deploy ] && exec npx -y wrangler@4 deploy
echo "FUN60 Studio: http://127.0.0.1:8060"
exec python3 -m http.server 8060 --bind 127.0.0.1 -d site
