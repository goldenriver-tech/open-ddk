#!/usr/bin/env python3
# Copyright 2026 GoldenRiver Technology. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Take open-ddk/trim.txt's packages out of a package root (garnet/packages
# with a product overlay over it): every package file's "packages" loses
# them, and an app list a resource installs (data/grt_loader/apps.config)
# loses their binaries, written as a copy under OUT that the resource then
# names. The package files are edited in place; GRT's sources are not.
#
#   trim-packages.py PKGROOT OUT [BASE]
#
# BASE is where resource paths are relative to (default: the nebula root;
# the buildroot for its own copy of the packages).
import json
import os
import sys

root, out = sys.argv[1:3]
here = os.path.dirname(os.path.abspath(__file__))
nebula = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else os.path.dirname(here)
drop = [l.strip() for l in open(os.path.join(here, 'trim.txt')) if l.strip() and not l.startswith('#')]
os.makedirs(out, exist_ok=True)
n = 0
for d, _, files in os.walk(root):
    for f in files:
        p = os.path.join(d, f)
        try:
            data = json.load(open(p))
        except (ValueError, UnicodeDecodeError):
            continue
        if not isinstance(data, dict):
            continue
        changed = False
        pk = data.get('packages')
        if isinstance(pk, dict):
            for k in drop:
                if k in pk:
                    del pk[k]
                    changed = True
        for r in data.get('resources', []) if isinstance(data.get('resources'), list) else []:
            src = os.path.join(nebula, r.get('file', ''))
            if not r.get('file', '').endswith('.config') or not os.path.isfile(src):
                continue
            apps = json.load(open(src))
            if not isinstance(apps.get('apps'), list):
                continue
            kept = [a for a in apps['apps'] if not any(a.endswith('/' + k) for k in drop)]
            if kept != apps['apps']:
                new = os.path.join(out, r['file'].replace('/', '_'))
                json.dump(dict(apps, apps=kept), open(new, 'w'), indent=2)
                r['file'] = os.path.relpath(new, nebula)
                changed = True
        if changed:
            json.dump(data, open(p, 'w'), indent=4)
            n += 1
print('trim-packages: %s out of %d package file(s)' % (', '.join(drop), n))
