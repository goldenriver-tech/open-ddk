#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# SPDX-License-Identifier: NebulaProprietary
# Author: Kai Peng, Yuan-L Liu
# Version:
#       v1.0: 初始化版本
# 检查虚拟机配置文件IO地址是否有重叠，dts的status属性与虚拟机配置的vmem是否一致。
# 用法：python3 iocheck.py --vm-config <lua or json文件>
#       只检查IO地址是否重叠，如果有重叠，则打印相关地址信息，并以错误码1退出。

import argparse
import logging
import sys
import re
import json
import logging
from difflib import SequenceMatcher

class VmemNode():
    def __init__(self, name: str, start: str, size: str):
        logging.debug(f'VmemNode: {name}, {start}, {size}')
        self.name = name
        self.start = int(start, 16)
        self.size = int(size, 16)
        self.end = self.start + self.size

    def __str__(self):
        return f'{self.name},0x{self.start:08x},{hex(self.size)}'

class Vmem():
    lua_pattern = re.compile(r'\s*uos_config:setVmem\("([^"]+)",\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*(\d+),\s*(\d+)\)')
    def __init__(self, config: str):
        self.mem_node = []
        self.filename = config
        logging.debug(config)
        if config.endswith('.lua'):
            self._parse_lua(config)
        elif config.endswith('.json'):
            self._parse_json(config)
        else:
            raise Exception(f'invalid config file {config}, only support lua or json')

        if (len(self.mem_node)):
            self.mem_node.sort(key= lambda x: x.start)

    def _parse_lua(self, config: str):
        logging.debug(f'parse lua config: {config}')
        with open(config, 'r') as f:
            block_comment = False
            for line in f.read().splitlines():
                logging.debug(f'parse line: {line}')
                line = line.strip()
                if line == "--]]":
                    block_comment = False
                    continue
                if line == "--[[":
                    block_comment = True
                    continue
                if block_comment:
                    logging.debug(f"skip comment line: {line}")
                    continue
                if line.startswith('--'):
                    logging.debug(f"skip comment line: {line}")
                    continue
                mem_info = self.lua_pattern.match(line)
                if mem_info:
                    self.mem_node.append(VmemNode(mem_info.group(1), mem_info.group(2), mem_info.group(3)))

    def _parse_json(self, config: str):
        logging.debug(f'parse json config: {config}')
        with open(config, 'r') as f:
            data = json.load(f)
        if 'vmem' not in data:
            raise Exception(f'vmem not in {config}')

        for _node in data['vmem']:
            node = _node.split(',')
            self.mem_node.append(VmemNode(node[0], node[1], node[3]))

    def __str__ (self):
        return '\n'.join(str(node) for node in self.mem_node)

    def _merge_node(self, merge_list):
        size = 0
        for node in merge_list:
            size = size + node.size
        start = merge_list[0]
        if self.filename.endswith('.lua'):
            return f'uos_config:setVmem("{start.name}",0x{start.start:08x},0x{size:x},1,0)'
        else:
            return f'"{start.name},0x{start.start:08x},0x{start.start:08x},0x{size:x},1,0",'

    def check_overlap(self):
        is_overlap = False
        for i in range(1, len(self.mem_node)):
            prev = self.mem_node[i-1]
            curr = self.mem_node[i]

            if curr.start < prev.end:
                logging.error( f"Overlap: {prev} and {curr}")
                is_overlap = True

        if not is_overlap:
            logging.info("No overlap found")
        return is_overlap

    def check_merge(self):
        merge_list = []
        for i in range(1, len(self.mem_node)):
            prev = self.mem_node[i-1]
            curr = self.mem_node[i]
            similarity = SequenceMatcher(None, prev.name, curr.name).ratio()
            logging.debug(f"{prev.name} and {curr.name} similarity: {similarity}")

            if prev.end == curr.start and similarity >= 0.8:
                if not merge_list:
                    merge_list.append(prev)
                    merge_list.append(curr)
                else:
                    merge_list.append(curr)
            else:
                if merge_list:
                    logging.warning(f"{[str(node) for node in merge_list]} are very similar, please merge to: {self._merge_node(merge_list)}")
                    merge_list = []

    def check_align(self):
        is_align = True
        for node in self.mem_node:
            if node.start % 0x1000 != 0:
                logging.error(f"{node} start address is not align with 0x1000")
                is_align = False
            if node.size % 0x1000 != 0:
                logging.error(f"{node} size is not align with 0x1000")
                is_align = False
        if not is_align:
            return 1

    def check_name(self):
        nodes = {}
        check_fail = False
        for node in self.mem_node:
            if node.name not in nodes:
                nodes[node.name] = node
                continue

            check_fail = True
            logging.error(f"{node.name} appears multiple times, please rename")
        return check_fail

    def find_vmem(self, addr: int):
        for node in self.mem_node:
            if addr >= node.start and addr < node.end:
                return node
        return None

def findio(vmem):
    while True:
        try:
            address = input("please input address: ").strip()
            if address == "exit":
                break
            address = int(address, 16)
        except ValueError:
            print("Invalid input")
            continue

        nodes = vmem.find_vmem(address)
        if nodes:
            print(nodes)
        else:
            print(f"0x{address:08x} not found")

def main():
    parser = argparse.ArgumentParser(description="check vm io")
    parser.add_argument('--vm-config', type=str, required=True, help='vm config file, lua or json')
    parser.add_argument('--find', action='store_true', help='find mode')
    args = parser.parse_args()

    logging.info(f"vm_config: {args.vm_config}")
    mem = Vmem(args.vm_config)

    if args.find:
        findio(mem)
        return 0

    if mem.check_overlap():
        logging.warning("vm config have overlap vmem, please check vm config")
        return 1

    if mem.check_align():
        logging.error("align check fail")
        return 1

    mem.check_merge()

    if mem.check_name():
        logging.error("name check fail")
        return 1

    return 0

if __name__ == '__main__':
    FORMAT = '%(levelname)s(%(lineno)03d): %(message)s'
    logging.basicConfig(level=logging.INFO, format=FORMAT)
    sys.exit(main())
