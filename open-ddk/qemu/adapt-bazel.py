#!/usr/bin/env python3
# Copyright 2026 GoldenRiver Technology. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# The REE bazel workspace as open-ddk builds it, offline and with linux 6.6
# only. bazel/ree/ is a farm of symlinks into build/bazel/ree/ (build/
# bazel_symlink.sh); this replaces two of its links with edited copies and
# leaves build/ as it is:
#   WORKSPACE    rules_nbl from open-ddk/qemu/rules_nbl/, not GRT's Gerrit;
#                no linux 4.9, 4.19 or 5.4 tree; gRPC's extra dependencies
#                from open-ddk/qemu/bazel/, which names the Go SDK by its
#                checksum (rules_go would otherwise ask golang.org for its
#                list of releases every time, which no cache can answer)
#   BUILD.bazel  gen_init_cpio from linux 6.6; no images of the old kernels;
#                the root file system is open-ddk's (qemu/linux-rootfs.genrule):
#                busybox, rprocd, nblruncmd and gzfs_d, dropbear, socat, iperf3 -- not
#                GRT's TEE-era daemons, tests and test keys
# Each edit must find what it edits: a tag that changes these files stops
# here instead of building something else.
#   adapt-bazel.py <nebula root>
import os
import re
import sys

root = os.path.abspath(sys.argv[1])
ree = os.path.join(root, 'bazel', 'ree')


def edit(name, edits):
    src = os.path.join(root, 'build', 'bazel', 'ree', name)
    s = open(src).read()
    for what, pattern, repl in edits:
        s, n = re.subn(pattern, repl, s, flags=re.M | re.S)
        if not n:
            sys.exit('adapt-bazel: %s: no %s' % (name, what))
    dst = os.path.join(ree, name)
    if os.path.lexists(dst):
        os.remove(dst)
    open(dst, 'w').write(s)


def drop_repo(name):
    return ('the %s repository' % name,
            r'^new_local_repository\(\n    name = "%s",\n.*?^\)\n\n?' % re.escape(name), '')


edit('WORKSPACE', [
    ('rules_nbl from Gerrit',
     r'^git_repository\(\n    name = "io_bazel_rules_nbl",\n.*?^\)\n',
     'local_repository(\n    name = "io_bazel_rules_nbl",\n'
     '    path = "../../open-ddk/qemu/rules_nbl",\n)\n'),
    drop_repo('linux_4.9'), drop_repo('linux_4.19'), drop_repo('linux_5.4'),
    ('gRPC\'s extra dependencies',
     r'^load\("@com_github_grpc_grpc//bazel:grpc_extra_deps\.bzl", "grpc_extra_deps"\)\n+grpc_extra_deps\(\)\n',
     'local_repository(\n    name = "open_ddk_qemu",\n    path = "../../open-ddk/qemu/bazel",\n)\n\n'
     'load("@open_ddk_qemu//:grpc_extra_deps.bzl", "grpc_extra_deps_offline")\n\n'
     'grpc_extra_deps_offline()\n'),
])
ROOTFS = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'linux-rootfs.genrule')).read()
edit('BUILD.bazel', [
    ('gen_init_cpio from linux 4.9', r'"@linux_4\.9//:gen_init_cpio\.c"', '"@linux_6.6//:gen_init_cpio.c"'),
    ('the old kernel images', r'^build_linux_image\("(4\.9|4\.19|5\.4)"\)\n', ''),
    ('the root file system', r'^genrule\(\n    name = "linux-rootfs",\n.*?^\)\n', lambda m: ROOTFS),
])
print('bazel/ree: WORKSPACE and BUILD.bazel adapted')
