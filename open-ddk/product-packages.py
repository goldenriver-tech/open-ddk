#!/usr/bin/env python2
# Copyright 2026 GoldenRiver Technology. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Resolve a product's package set and write it as one flat GN package file.
#
#   product-packages.py --root PKGROOT --package garnet/packages/venus-hee --out FILE
#
# PKGROOT is garnet/packages with the product overlay copied over it, which is
# how GRT's release flow sees packages (gr-android.py copies the overlay
# through buildroot_packages/garnet/packages). The output has the resolved
# packages and the resources the product's config packages put in /system,
# so building it builds what the product image is made of and nothing else.
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'build', 'gn'))
import paths  # noqa: E402
from package_imports_resolver import PackageImportsResolver  # noqa: E402
from process_build_packages import Amalgamation  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True)
    ap.add_argument('--package', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    am = Amalgamation(None)
    if not PackageImportsResolver(am).resolve_imports([a.package], a.root):
        return 1
    resources = []
    for f in am.system.files:
        r = {'file': os.path.relpath(f['file'], paths.FUCHSIA_ROOT),
             'bootfs_path': f['bootfs_path']}
        if f['default']:
            r['default'] = True
        resources.append(r)
    doc = {'packages': dict(zip(am.packages, am.deps))}
    if resources:
        doc['resources'] = resources
    with open(a.out, 'w') as out:
        json.dump(doc, out, indent=2, sort_keys=True)
    sys.stdout.write('%s: %d packages, %d resources\n'
                     % (a.package, len(am.packages), len(resources)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
