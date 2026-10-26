// driver_smoke.mjs - FUN60 Studio against the real keyboard in headless Brave, read only.
//   sh driver/serve.sh &          (http://127.0.0.1:8060)
//   node scripts/hwtest/driver_smoke.mjs [url]
// Headless Brave can't click the WebHID device picker, so the browser must already be allowed
// to use the keyboard: a Chromium policy file, e.g. /etc/brave/policies/managed/fun60-webhid.json:
//   {"WebHidAllowDevicesForUrls":[{"devices":[{"vendor_id":12625,"product_id":20528},
//    {"vendor_id":12625,"product_id":20522}],"urls":["http://127.0.0.1:8060"]}]}
// Checks: it connects, every page renders without an exception, the Travel page reads the
// keyboard's own numbers, and a second tab takes the keyboard over cleanly (and Connect takes it back).
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const URL = process.argv[2] || "http://127.0.0.1:8060/", PORT = 9339;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const profile = mkdtempSync(join(tmpdir(), "fun60-smoke-"));   // fresh: no restored tabs
const br = spawn("brave", ["--headless=new", `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`,
  "--no-first-run", "--disable-gpu", "--window-size=1400,1000", "about:blank"], { stdio: "ignore" });
let targets;
for (let i = 0; i < 50 && !targets; i++) { await sleep(200); try { targets = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json(); } catch {} }
const ws = new WebSocket(targets.find(t => t.type === "page").webSocketDebuggerUrl);
await new Promise(r => ws.onopen = r);
let id = 0, exceptions = 0, fails = 0;
const pend = new Map();
ws.onmessage = m => { const d = JSON.parse(m.data);
  if (d.id && pend.has(d.id)) { pend.get(d.id)(d); pend.delete(d.id); }
  else if (d.method === "Runtime.exceptionThrown") { exceptions++; console.log("  exception:", d.params.exceptionDetails.exception?.description); } };
const cmd = (method, params = {}) => new Promise(r => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
const ev = async expr => (await cmd("Runtime.evaluate", { expression: `(async()=>{${expr}})()`, awaitPromise: true, returnByValue: true })).result?.result?.value;
const check = (ok, msg) => { console.log((ok ? "  ok    " : "  FAIL  ") + msg); fails += !ok; };

try {
  await cmd("Runtime.enable");
  await cmd("Page.navigate", { url: URL }); await sleep(3000);
  const s = await ev(`return { demo: !st.kb || st.kb.demo, info: st.info, err: st.error }`);
  check(!s.demo && s.info && s.info.devId === 2307 && !s.err, `connected: firmware ${s.info && s.info.version}, protocol ${s.info && s.info.proto} ${s.err || ""}`);
  const pages = await ev(`return [...document.querySelectorAll('.nav button')].map(b => b.dataset.page)`);
  for (const p of pages) { await ev(`document.querySelector('[data-page=${p}]').click()`); await sleep(400); }
  check(!exceptions && !(await ev(`return st.error`)), `${pages.length} pages render without an error`);
  await ev(`document.querySelector('[data-page=travel]').click()`); await sleep(1500);
  const t = await ev(`return { raw: st.raw && st.raw[0], nums: $('tv-nums') && $('tv-nums').textContent }`);
  check(t.raw > 0 && /rest \d+/.test(t.nums || ""), `travel page live: ${t.nums}`);
  await cmd("Target.createTarget", { url: URL, background: true }); await sleep(3000);
  const handed = await ev(`return { demo: st.kb.demo, err: st.error }`);
  check(handed.demo && /another tab/.test(handed.err), "a second tab takes the keyboard, this one says so and stops talking to it");
  await ev(`await connect()`); await sleep(1500);
  const back = await ev(`try { const a = await st.kb.keymap(), b = await st.kb.keymap(); return { demo: st.kb.demo, same: a.every((l, i) => l.every((c, j) => c === b[i][j])), err: st.error }; } catch (e) { return { same: false, err: e.message }; }`);
  check(!back.demo && back.same && !back.err, `Connect takes it back, replies right ${back.err || ""}`);
} finally {
  ws.close(); br.kill(); spawn("pkill", ["-f", `user-data-dir=${profile}`]); await sleep(800); rmSync(profile, { recursive: true, force: true });
}
console.log(fails ? `${fails} FAILED` : "ALL OK");
process.exit(fails ? 1 : 0);
