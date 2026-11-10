# Sysroot library aliases in Linux update parts

The AppImage's selected system libraries are copied by value into its sysroot.
Some host distributions make a development alias such as libmvec.so an absolute
symlink. Keeping that target would either depend on the player's host filesystem
or be rejected by a strict updater. Dereferencing the
source alias while copying preserves its bytes and attributes inside the part.

The internal relative lib -> usr/lib sysroot layout remains a link. Existing
parts carrying absolute links are not retroactively rewritten. For legacy
sysroot parts only, the updater rebases absolute links to relative links when
their targets exist and resolve inside that sysroot. Absolute links in other
parts, missing or circular targets, and paths that escape the sysroot remain
errors.

Tests execute the production copy loop with synthetic library inputs, then
package and unpack the resulting sysroot, alongside legacy-link and
hostile-archive regression cases.
