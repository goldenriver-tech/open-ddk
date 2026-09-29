#!/usr/bin/env python3
# Copyright 2026 GoldenRiver Technology. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# open-ddk's projects without jiri: every project of manifest/open-ddk
# (jiri's format) checked out, upstream's code at its commit, a fork of
# upstream at the tip of its branch, or at a release's tag. jiri does the
# same (README.md); this needs only git.
#
#   open-ddk/projects.py [--root DIR] [--tag open-ddk-VERSION] [--forks-beside URL] [--check]
#
# --root          the open-ddk checkout (default: the one this script is in)
# --tag           each fork at this tag, a release's (default: its branch)
# --forks-beside  where the forks are instead of where the manifest says:
#                 a fork is URL/<its name> (a directory of bare repositories
#                 works too)
# --check         check out nothing: say which projects are missing (with
#                 --tag, or upstream's, at another commit), exit 1 if any
#
# A project already checked out (by jiri, or by this script) is left as it
# is; one at another commit than it must be is an error, not overwritten.
import argparse
import os
import re
import subprocess
import sys


def git(*args, cwd=None, ok=False):
    p = subprocess.run(['git', '-c', 'protocol.file.allow=always'] + list(args), cwd=cwd,
                       capture_output=True, text=True)
    if p.returncode and not ok:
        sys.exit('git %s: %s' % (' '.join(args), p.stderr.strip()))
    return p


def projects(root):
    """[(name, path, remote, branch or None, revision)] of manifest/open-ddk,
    but the repository itself"""
    text = open(os.path.join(root, 'manifest', 'open-ddk')).read()
    out = []
    for m in re.finditer(r'<project\s([^>]*?)/>', text, re.S):
        at = dict(re.findall(r'(\w+)="([^"]*)"', m.group(1)))
        if at['path'] != '.':   # (the repository itself, jiri's root)
            out.append((at['name'], at['path'], at['remote'], at.get('remotebranch'), at.get('revision')))
    return sorted(out, key=lambda x: x[1])   # a project before those inside it


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument('--tag')
    ap.add_argument('--forks-beside')
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    bad = 0
    for name, path, remote, branch, rev in projects(root):
        d = os.path.join(root, path)
        if branch and a.forks_beside:
            # its name where it is published, or (a directory of bare
            # repositories, as a release is made) the project's own
            base = a.forks_beside.rstrip('/')
            remote = base + '/' + remote.rstrip('/').split('/')[-1]
            if os.path.isdir(base):
                for c in (remote, remote + '.git', base + '/' + name, base + '/' + name + '.git'):
                    if os.path.isdir(c):
                        remote = c
                        break
        # what it must be at: upstream's commit; a fork's tag, or its branch
        want = rev if not branch else ('refs/tags/' + a.tag if a.tag else 'refs/heads/' + branch)
        if os.path.exists(os.path.join(d, '.git')):
            if not branch or a.tag:
                have = git('rev-parse', 'HEAD', cwd=d, ok=True).stdout.strip()
                if branch:
                    out = git('ls-remote', remote, want + '^{}', want, ok=True).stdout.split()
                    need = out[0] if out else '?'
                else:
                    need = rev
                if have != need:
                    print('%s: at %s, not %s (%s)' % (path, have[:12] or '?', need[:12], want))
                    bad += 1
            continue
        if a.check:
            print('%s: missing (%s, %s)' % (path, remote, want))
            bad += 1
            continue
        if os.path.exists(d) and os.listdir(d):
            sys.exit('%s: not empty and not a checkout' % path)
        os.makedirs(d, exist_ok=True)
        git('init', '-q', d)
        git('remote', 'add', 'origin', remote, cwd=d)
        if branch:
            git('fetch', '-q', '--no-tags', 'origin', '+%s:refs/open-ddk/want' % want, cwd=d)
            git('checkout', '-q', '--detach', 'refs/open-ddk/want', cwd=d)
        else:
            # the commit alone where the server gives it (GitHub, googlesource),
            # else what the remote has
            if git('fetch', '-q', '--depth', '1', 'origin', rev, cwd=d, ok=True).returncode:
                git('fetch', '-q', 'origin', cwd=d)
                git('fetch', '-q', '--tags', 'origin', cwd=d, ok=True)
            git('checkout', '-q', '--detach', rev, cwd=d)
        # the enclosing checkout ignores it, as open-ddk's .gitignore does
        up = os.path.dirname(d)
        ex = git('rev-parse', '--git-path', 'info/exclude', cwd=up, ok=True).stdout.strip()
        if ex:
            ex = os.path.join(up, ex)
            rel = '/' + os.path.relpath(d, git('rev-parse', '--show-toplevel', cwd=up).stdout.strip())
            os.makedirs(os.path.dirname(ex), exist_ok=True)
            have = open(ex).read().split('\n') if os.path.exists(ex) else []
            if rel not in have:
                with open(ex, 'a') as f:
                    f.write(rel + '\n')
        print('%s: %s at %s' % (path, name, rev if not branch else want))
    if bad:
        sys.exit(1)


if __name__ == '__main__':
    main()
