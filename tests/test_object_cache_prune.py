"""(issue 1287) After a build the object cache keeps the objects that build used (named by sa/build/obj/*.sha1) and lets the rest go,
so an update compiles only what changed; a build that never reached the compile (no stamps) leaves the cache alone.

    python -I -B tests/test_object_cache_prune.py
"""
import ast
import os
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
tree = ast.parse((ROOT / "nx2" / "make_game.py").read_text(encoding="utf-8"))
fn = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "prune_object_cache")
ns = {"os": os}
exec(compile(ast.Module([fn], []), "make_game.py", "exec"), ns)
prune = ns["prune_object_cache"]

with tempfile.TemporaryDirectory() as w:
    cache, obj = Path(w, "objcache"), Path(w, "sa", "build", "obj")
    assert prune(w) == 0   # (no cache yet)
    cache.mkdir(parents=True)
    for k in ("a" * 40, "b" * 40, "c" * 40):
        (cache / (k + ".o")).write_bytes(b"x" * 10)
    assert prune(w) == 30 and len(list(cache.iterdir())) == 3   # (no stamps: nothing goes)
    obj.mkdir(parents=True)
    (obj / "code_000.o.sha1").write_text("a" * 40)
    (obj / "host_x.o.sha1").write_text("c" * 40 + "\n")
    assert prune(w) == 20, prune(w)
    assert sorted(p.name for p in cache.iterdir()) == ["a" * 40 + ".o", "c" * 40 + ".o"]
print("ok: the object cache keeps this build's objects, and only when the build compiled")
