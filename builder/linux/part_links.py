"""Safe handling of the rooted links in Linux sysroot update parts."""
import os


SYSROOT_PART = "usr/lib/hookshot/sysroot"


def is_absolute_link(target):
    return target.startswith("/") or os.path.isabs(target)


def relative_sysroot_link(root, link, target):
    """Rebase a sysroot-absolute link onto an existing target within the same sysroot."""
    if not target.startswith("/") or target.startswith("//"):
        raise ValueError("not a rooted Linux sysroot link: %s -> %s" % (link, target))
    root = os.path.abspath(root)
    destination = os.path.abspath(os.path.join(root, target.lstrip("/")))
    if os.path.commonpath((root, destination)) != root:
        raise ValueError("a sysroot link leaves its root: %s -> %s" % (link, target))
    real_root = os.path.realpath(root)
    real_destination = os.path.realpath(destination)
    if os.path.commonpath((real_root, real_destination)) != real_root:
        raise ValueError("a sysroot link resolves outside its root: %s -> %s" % (link, target))
    if not os.path.exists(destination):
        raise FileNotFoundError("a sysroot link target is missing: %s -> %s" % (link, target))
    return os.path.relpath(destination, os.path.dirname(link))
