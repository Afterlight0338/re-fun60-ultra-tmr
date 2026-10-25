#!/usr/bin/env python3
"""Render docs/src/*.md (joined in name order) into README.md.

Backticked repository paths become links when the path exists and git doesn't ignore it.
"""
import pathlib, re, subprocess

root = pathlib.Path(__file__).resolve().parent.parent
src = "\n".join(p.read_text() for p in sorted((root / "docs/src").glob("*.md")))


def link_paths(line):
    """`path` -> [`path`](path) when the path exists in this repository."""
    def sub(m):
        path = m.group(1)
        published = ("/" in path and not path.startswith("/") and (root / path).exists()
                     and subprocess.run(["git", "-C", root, "check-ignore", "-q", path]).returncode)
        return f"[`{path}`]({path})" if published else m.group(0)
    return re.sub(r"(?<!\[)`([A-Za-z0-9_./-]+)`(?!\])", sub, line)


out, fenced = [], False
for line in src.splitlines():
    if line.strip().startswith("```"):
        fenced = not fenced
        out.append(line)
        continue
    out.append(line if fenced else link_paths(line))
(root / "README.md").write_text("\n".join(out) + "\n")
print("README.md written")
