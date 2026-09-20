#!/usr/bin/env python3
import json
from pathlib import Path
import shlex
import sys

commands = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
entry = next((x for x in commands if x.get("file", "").endswith("stub.c")), None)
if entry is None:
    raise SystemExit("stub.c compile command not found")

parts = shlex.split(entry.get("command") or " ".join(entry.get("arguments", [])))
selected = []
i = 0
while i < len(parts):
    token = parts[i]
    if token.startswith("-I") and len(token) > 2:
        selected.append(token)
    elif token == "-I" and i + 1 < len(parts):
        selected.extend([token, parts[i + 1]])
        i += 1
    elif token == "-isystem" and i + 1 < len(parts):
        selected.extend([token, parts[i + 1]])
        i += 1
    i += 1

selected.append("-DESP_PLATFORM")
print(" ".join(shlex.quote(x) for x in selected))
