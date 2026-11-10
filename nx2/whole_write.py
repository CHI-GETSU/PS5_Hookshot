"""(issue 1197) A lifted C file rewritten in place (the patches, the chunk split) is written beside it and moved over it: a run
stopped mid-write (the builder closed, a second build stopping the first, a full disk) leaves the old file whole, never the first
megabytes of the new one, which the compile then stopped on at every try ("g_module_b" where g_module_base was)."""
import os
import time


def write_whole(path, text, newline="\n"):
    tmp = path + ".part"
    with open(tmp, "w", encoding="utf-8", newline=newline) as f:
        f.write(text)
    for attempt in range(20):   # (an antivirus reading the file holds it a moment: Windows refuses the move until it lets go)
        try:
            os.replace(tmp, path)
            return
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.25)
