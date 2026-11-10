"""(issue 1197) make_game's check that every lifted file ends as a function does, before the compile: a file cut short (a run
stopped mid-write, a second build in the same folder, a full disk) is named, so the game is translated again instead of the compile
stopping on it ("g_module_b") at every try. And the rewrites of lifted files (nx2/whole_write.py) leave the old file whole when they
do not finish.

    python -I -B tests/test_lift_cut_short.py
"""
import ast
import os
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "nx2"))
import whole_write  # noqa: E402

tree = ast.parse((ROOT / "nx2" / "make_game.py").read_text(encoding="utf-8"))
fn = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "lifted_cut_short")
ns = {"os": os}
exec(compile(ast.Module([fn], []), "make_game.py", "exec"), ns)

with tempfile.TemporaryDirectory() as d:
    files = {"code_000.c": b"void f_80000000(Ctx *c) {\n}\n", "code_001.c": b"void f(Ctx *c) {\n    c->x[0]=g_module_b",
             "code_hot_1.c": b"", "code_006_1.c": b"x\n}\n", "a64_table.c": b"cut", "code_002.c.whole": b"cut"}
    for name, data in files.items():
        Path(d, name).write_bytes(data)
    got = ns["lifted_cut_short"](d)
    assert got == ["code_001.c", "code_hot_1.c"], got
    assert ns["lifted_cut_short"](os.path.join(d, "missing")) == []

    # a rewrite that fails before its move leaves the old file as it was
    target = Path(d, "code_000.c")
    real_replace = os.replace
    os.replace = lambda a, b: (_ for _ in ()).throw(OSError("stopped"))
    try:
        whole_write.write_whole(str(target), "partial")
    except OSError:
        pass
    finally:
        os.replace = real_replace
    assert target.read_bytes() == files["code_000.c"]
    whole_write.write_whole(str(target), "void g(Ctx *c) {\n}\n")
    assert target.read_text() == "void g(Ctx *c) {\n}\n" and not Path(d, "code_000.c.part").exists()
print("ok: cut-short lifted files are found, and a rewrite that stops leaves the old file whole")
