#!/usr/bin/env python3
"""Read a file's post-image out of a unified diff.

The self-test runs against the sources the kernel build actually applies,
which for CI is xnu/patches/0084-sagit-lse-emulation.patch -- there is no
full xnu tree in the checkout.  Reconstructing the file from the patch keeps
the test wired to the real code instead of a copy that can drift.
"""


def added_lines(patch_path, target):
    """Lines of ``target`` after patching, in order (context + additions)."""
    with open(patch_path, encoding='utf-8', errors='replace') as f:
        lines = f.read().splitlines()

    out = []
    inside = False
    for ln in lines:
        if ln.startswith('diff --git '):
            inside = ln.rstrip().endswith('b/' + target)
            continue
        if not inside:
            continue
        if ln.startswith(('+++', '---', 'index ', '@@')):
            continue
        if ln.startswith('+'):
            out.append(ln[1:])
        elif ln.startswith(' '):
            out.append(ln[1:])
        # '-' lines are the pre-image; drop them.
    if not out:
        raise SystemExit('%s: no post-image for %s' % (patch_path, target))
    return out


def load(path, target):
    """Return the file's lines, from a plain file or from a patch."""
    if path.endswith('.patch') or path.endswith('.diff'):
        return added_lines(path, target)
    if target not in path.replace('\\', '/'):
        raise SystemExit('%s does not look like %s (use a patch instead)'
                         % (path, target))
    with open(path, encoding='utf-8') as f:
        return f.read().splitlines()
