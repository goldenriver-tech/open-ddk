#!/usr/bin/env python3
# SPDX-License-Identifier: NebulaProprietary

import argparse
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


SOS_CONFIG = "sos_mt8668.lua"
RPROC_CONFIG = "rproc.pb.txt"


@dataclass(frozen=True)
class Source:
    path: Path
    line: int
    label: str

    def __str__(self):
        return f"{self.path}:{self.line}: {self.label}"


class CheckError(Exception):
    pass


@dataclass(frozen=True)
class RprocRecord:
    kind: str
    first: int
    last: int
    source: Source


@dataclass(frozen=True)
class LuaIrqRecord:
    function: str
    irq: int
    source: Source


@dataclass(frozen=True)
class LuaTable:
    name: str
    values: list
    source: Source


NORMAL_VGIC_FUNCTIONS = {
    "setVgicIrq",
    "setVgicIrqs",
}


def strip_lua_line_comment(line):
    in_string = False
    escaped = False
    i = 0
    while i < len(line):
        ch = line[i]
        if in_string:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                in_string = False
            i += 1
            continue
        if ch == '"':
            in_string = True
            i += 1
            continue
        if ch == "-" and i + 1 < len(line) and line[i + 1] == "-":
            return line[:i]
        i += 1
    return line


def read_lua_code_lines(path):
    lines = []
    in_block_comment = False
    for lineno, raw in enumerate(path.read_text().splitlines(), start=1):
        line = raw
        if in_block_comment:
            end = line.find("--]]")
            if end < 0:
                lines.append((lineno, ""))
                continue
            line = line[end + len("--]]") :]
            in_block_comment = False

        while True:
            start = line.find("--[[")
            if start < 0:
                break
            end = line.find("--]]", start + len("--[["))
            if end < 0:
                line = line[:start]
                in_block_comment = True
                break
            line = line[:start] + line[end + len("--]]") :]

        lines.append((lineno, strip_lua_line_comment(line)))
    return lines


def parse_irq_token(token, source):
    if re.fullmatch(r"\d+", token):
        return [int(token)]

    match = re.fullmatch(r"(\d+)-(\d+)", token)
    if not match:
        raise CheckError(f"{source}: invalid IRQ token {token!r}")

    first = int(match.group(1))
    last = int(match.group(2))
    if first > last:
        raise CheckError(f"{source}: invalid IRQ range {token!r}")
    return list(range(first, last + 1))


def parse_lua_table_values(text):
    values = []
    for token in re.findall(r'"([^"]*)"', text):
        token = token.strip()
        source = Source(Path("<lua-table>"), 0, f'"{token}"')
        try:
            values.extend(parse_irq_token(token, source))
        except CheckError:
            continue

    unquoted = re.sub(r'"[^"]*"', "", text)
    values.extend(parse_number_list(unquoted))
    return values


def expand_pb_irq_range(start, length):
    return range(start, start + length)


def parse_rproc_irqs(path):
    irq_sources = defaultdict(list)
    errors = []
    records = []
    current = None

    for lineno, raw in enumerate(path.read_text().splitlines(), start=1):
        line = raw.strip()
        match = re.search(r"\bctrl_irq\s*:\s*(\d+)", line)
        if match:
            irq = int(match.group(1))
            source = Source(path, lineno, f"ctrl_irq {irq}")
            irq_sources[irq].append(source)
            records.append(RprocRecord("ctrl_irq", irq, irq, source))

        range_match = re.search(r"\b(local_irqs|remote_irqs)\s*:\s*\{", line)
        if range_match:
            current = {
                "kind": range_match.group(1),
                "line": lineno,
                "start": None,
                "length": 1,
            }

        if current is None:
            continue

        start_match = re.search(r"\bstart\s*:\s*(\d+)", line)
        if start_match:
            current["start"] = int(start_match.group(1))
        length_match = re.search(r"\blength\s*:\s*(\d+)", line)
        if length_match:
            current["length"] = int(length_match.group(1))

        if "}" not in line:
            continue

        if current["start"] is None:
            errors.append(f"{path}:{current['line']}: {current['kind']} has no start")
        else:
            first = current["start"]
            length = current["length"]
            last = first + length - 1
            label = f"{current['kind']} {first}-{last}"
            source = Source(path, current["line"], label)
            records.append(RprocRecord(current["kind"], first, last, source))
            for irq in expand_pb_irq_range(first, length):
                irq_sources[irq].append(source)
        current = None

    return irq_sources, errors, records


def parse_number_list(text):
    return [int(value, 0) for value in re.findall(r"\b(?:0x[0-9a-fA-F]+|\d+)\b", text)]


def collect_table_assignment(path, lines, start_index):
    lineno, line = lines[start_index]
    match = re.search(r"\b(?:local\s+)?([A-Za-z_]\w*)\s*=\s*\{", line)
    if not match:
        return None

    name = match.group(1)
    body = line.split("{", 1)[1]
    index = start_index
    while "}" not in body and index + 1 < len(lines):
        index += 1
        _next_lineno, next_line = lines[index]
        body += "\n" + next_line

    body = body.split("}", 1)[0]
    table = LuaTable(
        name,
        parse_lua_table_values(body),
        Source(path, lineno, f"{name} = {{...}}"),
    )
    return table, index


def collect_loop_body(lines, start_index):
    body = []
    depth = 1
    index = start_index + 1
    while index < len(lines):
        lineno, line = lines[index]
        if re.search(r"\b(for|if)\b", line):
            depth += 1
        if re.search(r"\bend\b", line):
            depth -= 1
            if depth == 0:
                return body, index
        body.append((lineno, line))
        index += 1
    return body, index


def get_table_values(name, tables, table_sets=None):
    if table_sets is not None and name in table_sets:
        return sorted(table_sets[name])
    table = tables.get(name)
    if table is None:
        return []
    return list(table.values)


def expand_loop_values(line, tables, table_sets=None):
    range_match = re.search(r"\bfor\s+(\w+)\s*=\s*(\d+)\s*,\s*(\d+)\s+do", line)
    if range_match:
        var = range_match.group(1)
        values = list(range(int(range_match.group(2)), int(range_match.group(3)) + 1))
        return var, None, values

    table_match = re.search(r"\bfor\s+(\w+)\s*=\s*1\s*,\s*#(\w+)\s+do", line)
    if table_match:
        var = table_match.group(1)
        table_name = table_match.group(2)
        values = list(range(1, len(get_table_values(table_name, tables, table_sets)) + 1))
        return var, table_name, values

    ipairs_match = re.search(r"\bfor\s+_,\s*(\w+)\s+in\s+ipairs\((\w+)\)\s+do", line)
    if ipairs_match:
        var = ipairs_match.group(1)
        table_name = ipairs_match.group(2)
        values = get_table_values(table_name, tables, table_sets)
        return var, table_name, values

    return None


def resolve_lua_values(expr, tables, loop_vars):
    values = []
    for token in re.findall(r'"([^"]*)"', expr):
        token = token.strip()
        source = Source(Path("<lua-expr>"), 0, f'"{token}"')
        try:
            values.extend(parse_irq_token(token, source))
        except CheckError:
            continue

    expr_without_strings = re.sub(r'"[^"]*"', "", expr)
    values.extend(parse_number_list(expr_without_strings))

    table_access_matched = False
    for table_name, index_name in re.findall(
        r"\b([A-Za-z_]\w*)\s*\[\s*([A-Za-z_]\w*)\s*\]", expr_without_strings
    ):
        if table_name not in tables or index_name not in loop_vars:
            continue
        table_access_matched = True
        table_values = tables[table_name].values
        for index in loop_vars[index_name]:
            if 1 <= index <= len(table_values):
                values.append(table_values[index - 1])

    if not table_access_matched:
        for name, loop_values in loop_vars.items():
            if re.search(rf"\b{name}\b", expr_without_strings):
                values.extend(loop_values)

    return sorted(set(values))


def apply_table_mutations(path, tables, table_sets, lines, loop_vars):
    condition_stack = []

    for lineno, line in lines:
        condition_match = re.search(
            r"\bif\s+([A-Za-z_]\w*)\s*\[\s*([A-Za-z_]\w*)\s*\]\s+then\b", line
        )
        if condition_match:
            table_name = condition_match.group(1)
            var_name = condition_match.group(2)
            condition_stack.append((var_name, set(table_sets.get(table_name, set()))))
            continue

        if re.search(r"\bend\b", line):
            if condition_stack:
                condition_stack.pop()
            continue

        effective_loop_vars = {
            var: list(values)
            for var, values in loop_vars.items()
        }
        for condition_var, allowed_values in condition_stack:
            if condition_var in effective_loop_vars:
                effective_loop_vars[condition_var] = [
                    value
                    for value in effective_loop_vars[condition_var]
                    if value in allowed_values
                ]

        assign_match = re.search(
            r"\b([A-Za-z_]\w*)\s*\[\s*([^\]]+)\s*\]\s*=\s*(true|nil)\b", line
        )
        if assign_match:
            table_name = assign_match.group(1)
            values = resolve_lua_values(assign_match.group(2), tables, effective_loop_vars)
            if table_name not in table_sets:
                table_sets[table_name] = set()
            for value in values:
                if assign_match.group(3) == "true":
                    table_sets[table_name].add(value)
                else:
                    table_sets[table_name].discard(value)
            continue

        insert_match = re.search(
            r"\btable\.insert\(\s*([A-Za-z_]\w*)\s*,\s*([^)]*)\)", line
        )
        if insert_match:
            table_name = insert_match.group(1)
            values = resolve_lua_values(insert_match.group(2), tables, effective_loop_vars)
            if table_name not in table_sets:
                table_sets[table_name] = set()
            for value in values:
                table_sets[table_name].add(value)


def filter_loop_vars_by_conditions(lines, table_sets, loop_vars):
    result = []
    condition_stack = []

    for lineno, line in lines:
        condition_match = re.search(
            r"\bif\s+([A-Za-z_]\w*)\s*\[\s*([A-Za-z_]\w*)\s*\]\s+then\b", line
        )
        if condition_match:
            table_name = condition_match.group(1)
            var_name = condition_match.group(2)
            condition_stack.append((var_name, set(table_sets.get(table_name, set()))))
            result.append((lineno, line, {}))
            continue

        effective_loop_vars = {var: list(values) for var, values in loop_vars.items()}
        for condition_var, allowed_values in condition_stack:
            if condition_var in effective_loop_vars:
                effective_loop_vars[condition_var] = [
                    value
                    for value in effective_loop_vars[condition_var]
                    if value in allowed_values
                ]
        result.append((lineno, line, effective_loop_vars))

        if re.search(r"\bend\b", line) and condition_stack:
            condition_stack.pop()

    return result


def collect_irq_call_records(path, lines, tables, table_sets, loop_vars, function_pattern):
    records = []
    for lineno, line, effective_loop_vars in filter_loop_vars_by_conditions(
        lines, table_sets, loop_vars
    ):
        for match in re.finditer(function_pattern, line):
            function = match.group(1)
            values = resolve_lua_values(match.group(2), tables, effective_loop_vars)
            for irq in values:
                records.append(
                    LuaIrqRecord(function, irq, Source(path, lineno, line.strip()))
                )
    return records


def simulate_sos_normal_vgic(path):
    lines = read_lua_code_lines(path)
    tables = {}
    table_sets = {}
    normal_sources = defaultdict(list)
    warnings = []
    skip_lines = set()
    index = 0

    while index < len(lines):
        lineno, line = lines[index]
        table = collect_table_assignment(path, lines, index)
        if table:
            lua_table, end_index = table
            tables[lua_table.name] = lua_table
            table_sets[lua_table.name] = set(lua_table.values)
            index = end_index + 1
            continue

        loop = expand_loop_values(line, tables, table_sets)
        if loop:
            var, _table_name, loop_values = loop
            body, end_index = collect_loop_body(lines, index)
            for body_lineno, _body_line in body:
                skip_lines.add(body_lineno)
            apply_table_mutations(path, tables, table_sets, body, {var: loop_values})
            for record in collect_irq_call_records(
                path,
                body,
                tables,
                table_sets,
                {var: loop_values},
                r":(setVgicIrqs?)\(([^)]*)\)",
            ):
                normal_sources[record.irq].append(record.source)
            index = end_index + 1
            continue

        apply_table_mutations(path, tables, table_sets, [(lineno, line)], {})
        index += 1

    for lineno, line in lines:
        if lineno in skip_lines:
            continue
        for record in collect_irq_call_records(
            path,
            [(lineno, line)],
            tables,
            table_sets,
            {},
            r":(setVgicIrqs?)\(([^)]*)\)",
        ):
            normal_sources[record.irq].append(record.source)

    if not normal_sources:
        warnings.append(f"{path}: no SOS normal VGIC IRQ output was found")

    return normal_sources, warnings


def parse_lua_irq_records(path):
    lines = read_lua_code_lines(path)
    tables = {}
    records = []
    skip_lines = set()
    index = 0

    while index < len(lines):
        lineno, line = lines[index]
        table = collect_table_assignment(path, lines, index)
        if table:
            lua_table, end_index = table
            tables[lua_table.name] = lua_table
            index = end_index + 1
            continue

        loop = expand_loop_values(line, tables)
        if loop:
            body, end_index = collect_loop_body(lines, index)
            for body_lineno, body_line in body:
                skip_lines.add(body_lineno)

            var, table_name, loop_values = loop

            for body_lineno, body_line in body:
                for match in re.finditer(r":(set\w*Irq\w*)\(([^)]*)\)", body_line):
                    function = match.group(1)
                    args = match.group(2)
                    values = []
                    if table_name and re.search(rf"\b{table_name}\s*\[\s*{var}\s*\]", args):
                        values.extend(tables.get(table_name, LuaTable(table_name, [], None)).values)
                    elif re.search(rf"\b{var}\b", args):
                        values.extend(loop_values)
                    values.extend(resolve_lua_values(args, tables, {var: loop_values}))

                    for irq in sorted(set(values)):
                        records.append(
                            LuaIrqRecord(
                                function,
                                irq,
                                Source(path, body_lineno, body_line.strip()),
                            )
                        )

            index = end_index + 1
            continue

        index += 1

    for lineno, line in lines:
        if lineno in skip_lines:
            continue
        for match in re.finditer(r":(set\w*Irq\w*)\(([^)]*)\)", line):
            function = match.group(1)
            for irq in resolve_lua_values(match.group(2), tables, {}):
                records.append(LuaIrqRecord(function, irq, Source(path, lineno, line.strip())))

    return records


def records_by_irq(records):
    by_irq = defaultdict(list)
    for record in records:
        by_irq[record.irq].append(record.source)
    return by_irq


def format_sources(sources, limit=4):
    shown = list(sources[:limit])
    text = "; ".join(str(source) for source in shown)
    if len(sources) > limit:
        text += f"; ... +{len(sources) - limit} more"
    return text


def format_irq_span(first, last):
    if first == last:
        return str(first)
    return f"{first}-{last}"


def format_irq_spans(spans):
    return ", ".join(format_irq_span(first, last) for first, last in spans)


def compact_irq_values(values):
    numbers = sorted(set(values))
    if not numbers:
        return []
    spans = []
    first = last = numbers[0]
    for value in numbers[1:]:
        if value == last + 1:
            last = value
            continue
        spans.append((first, last))
        first = last = value
    spans.append((first, last))
    return spans


def format_irq_values(values):
    return format_irq_spans(compact_irq_values(values))


def print_rproc_records(records):
    print("rproc IRQ 展开:")
    by_kind = defaultdict(list)
    for record in records:
        by_kind[record.kind].append((record.first, record.last))

    for kind in ("ctrl_irq", "local_irqs", "remote_irqs"):
        spans = by_kind.get(kind, [])
        value = format_irq_spans(spans) if spans else "(none)"
        print(f"  {kind}: {value}")


def print_lua_irq_summary(title, records):
    print(title)
    if not records:
        print("  (none)")
        return

    grouped = defaultdict(lambda: defaultdict(list))
    for record in records:
        grouped[record.source.path.name][record.function].append(record.irq)

    for file_name in sorted(grouped):
        print(f"  {file_name}:")
        for function in sorted(grouped[file_name]):
            print(f"    {function}: {format_irq_values(grouped[file_name][function])}")


def print_check_line(passed, pass_text, fail_text=None):
    status = "PASS" if passed else "FAIL"
    text = pass_text if passed else (fail_text or pass_text)
    print(f"  [{status}] {text}")


def print_message_list(title, messages):
    if not messages:
        return
    print()
    print(title)
    for message in messages:
        print(f"  - {message}")


def check_config(config_dir, verbose=False):
    sos_path = config_dir / SOS_CONFIG
    rproc_path = config_dir / RPROC_CONFIG
    if not sos_path.exists():
        raise CheckError(f"missing {sos_path}")
    if not rproc_path.exists():
        raise CheckError(f"missing {rproc_path}")

    errors = []
    warnings = []

    rproc_sources, rproc_errors, rproc_records = parse_rproc_irqs(rproc_path)
    rproc_irqs = set(rproc_sources)
    sos_normal_sources, sos_normal_warnings = simulate_sos_normal_vgic(sos_path)
    sos_normal_irqs = set(sos_normal_sources)
    warnings.extend(sos_normal_warnings)

    sos_records = parse_lua_irq_records(sos_path)
    uos_paths = sorted(config_dir.glob("uos_*.lua"))
    uos_records = []
    for uos_path in uos_paths:
        uos_records.extend(parse_lua_irq_records(uos_path))
    uos_normal_records = [
        record for record in uos_records if record.function in NORMAL_VGIC_FUNCTIONS
    ]
    uos_info_records = [
        record for record in uos_records if record.function not in NORMAL_VGIC_FUNCTIONS
    ]
    sos_movable_records = [
        record for record in sos_records if record.function == "setVgicIrqsMovable"
    ]
    sos_movable_sources = records_by_irq(sos_movable_records)
    sos_movable_irqs = set(sos_movable_sources)
    uos_normal_sources = records_by_irq(uos_normal_records)
    uos_normal_irqs = set(uos_normal_sources)
    uos_sources = records_by_irq(uos_records)
    uos_irqs = set(uos_sources)

    rproc_duplicate_errors = []
    for irq, sources in sorted(rproc_sources.items()):
        if len(sources) > 1:
            rproc_duplicate_errors.append(
                f"rproc IRQ {irq} is described more than once: "
                f"{format_sources(sources)}"
            )

    rproc_sos_conflicts = []
    for irq in sorted(rproc_irqs & sos_normal_irqs):
        rproc_sos_conflicts.append(
            f"rproc IRQ {irq} is still configured by SOS normal VGIC; "
            f"rproc: {format_sources(rproc_sources[irq])}; "
            f"SOS: {format_sources(sos_normal_sources[irq])}"
        )

    sos_movable_errors = []
    conflicting_sos_movable = sorted(sos_movable_irqs & sos_normal_irqs)
    for irq in conflicting_sos_movable:
        sos_movable_errors.append(
            f"SOS movable IRQ {irq} is also configured by SOS normal VGIC; "
            f"movable: {format_sources(sos_movable_sources[irq])}; "
            f"normal VGIC: {format_sources(sos_normal_sources[irq])}"
        )

    uos_sos_conflicts = []
    for irq in sorted(uos_normal_irqs & sos_normal_irqs):
        uos_sos_conflicts.append(
            f"UOS normal VGIC IRQ {irq} is also configured by SOS normal VGIC; "
            f"UOS: {format_sources(uos_normal_sources[irq])}; "
            f"SOS: {format_sources(sos_normal_sources[irq])}"
        )

    uos_duplicate_warnings = []
    for irq, sources in sorted(uos_normal_sources.items()):
        paths = {source.path.name for source in sources}
        if len(sources) > 1 and len(paths) > 1:
            uos_duplicate_warnings.append(
                f"UOS IRQ {irq} is configured by multiple UOS Lua files: "
                f"{format_sources(sources)}"
            )

    uos_rproc_conflicts = []
    for irq in sorted(uos_irqs & rproc_irqs):
        uos_rproc_conflicts.append(
            f"IRQ {irq} is configured by both UOS Lua and rproc.pb.txt; "
            f"UOS: {format_sources(uos_sources[irq])}; "
            f"rproc: {format_sources(rproc_sources[irq])}"
        )

    errors.extend(rproc_errors)
    errors.extend(rproc_duplicate_errors)
    errors.extend(rproc_sos_conflicts)
    errors.extend(sos_movable_errors)
    errors.extend(uos_sos_conflicts)
    errors.extend(uos_rproc_conflicts)

    print(f"检查目录: {config_dir}")
    print("检查文件:")
    print(f"  SOS lua: {sos_path.name}")
    print(f"  rproc IRQ: {rproc_path.name}")
    if uos_paths:
        print("  UOS lua:")
        for uos_path in uos_paths:
            print(f"    {uos_path.name}")
    else:
        print("  UOS lua: (none)")
    print()
    print_rproc_records(rproc_records)
    print()
    print(f"SOS 普通 VGIC 最终输出: {len(sos_normal_irqs)} 个 IRQ")
    print(f"  {format_irq_values(sos_normal_irqs)}")
    print()
    print_lua_irq_summary("SOS movable IRQ 展开（不能同时留在 SOS 普通 VGIC）:", sos_movable_records)
    print()
    print_lua_irq_summary("UOS 普通 VGIC IRQ 展开（不能与 SOS 普通 VGIC 重叠）:", uos_normal_records)
    print()
    print_lua_irq_summary("UOS 特殊/共享 IRQ 展开（仅做 rproc 重叠检查）:", uos_info_records)
    print()
    print("检查结果:")
    rproc_error_count = len(rproc_errors) + len(rproc_duplicate_errors)
    print_check_line(
        not rproc_errors and not rproc_duplicate_errors,
        f"rproc 描述重复检查通过，展开后 {len(rproc_irqs)} 个 IRQ",
        f"rproc 描述重复检查失败，发现 {rproc_error_count} 个问题，展开后 {len(rproc_irqs)} 个 IRQ",
    )
    rproc_sos_conflict_text = format_irq_values(sorted(rproc_irqs & sos_normal_irqs))
    print_check_line(
        not rproc_sos_conflicts,
        f"rproc 与 SOS 普通 VGIC 无重叠，检查 {len(rproc_irqs)} 个 rproc IRQ",
        f"rproc 与 SOS 普通 VGIC 存在 {len(rproc_sos_conflicts)} 个重叠: "
        f"{rproc_sos_conflict_text}",
    )
    conflicting_sos_movable_text = format_irq_values(conflicting_sos_movable)
    sos_movable_text = (
        f"SOS movable 与 SOS 普通 VGIC 无重叠，检查 {len(sos_movable_irqs)} 个 movable IRQ"
        if not sos_movable_errors
        else f"SOS movable 与 SOS 普通 VGIC 存在 {len(conflicting_sos_movable)} 个重叠: "
        f"{conflicting_sos_movable_text}"
    )
    print_check_line(not sos_movable_errors, sos_movable_text)
    uos_sos_conflict_text = format_irq_values(sorted(uos_normal_irqs & sos_normal_irqs))
    uos_text = (
        f"UOS 普通 VGIC 与 SOS 普通 VGIC 无重叠，检查 {len(uos_normal_irqs)} 个 UOS IRQ"
        if not uos_sos_conflicts
        else f"UOS 普通 VGIC 与 SOS 普通 VGIC 存在 {len(uos_sos_conflicts)} 个重叠: "
        f"{uos_sos_conflict_text}"
    )
    print_check_line(not uos_sos_conflicts, uos_text)
    print_check_line(
        not uos_rproc_conflicts,
        "UOS 与 rproc IRQ 无重叠",
        f"UOS 与 rproc IRQ 存在 {len(uos_rproc_conflicts)} 个重叠: "
        f"{format_irq_values(sorted(uos_irqs & rproc_irqs))}",
    )

    if verbose:
        print()
        print("rproc -> SOS 普通 VGIC 明细:")
        for irq in sorted(rproc_irqs):
            status = "冲突" if irq in sos_normal_irqs else "无重叠"
            print(f"  {irq}: {status}; {format_sources(rproc_sources[irq])}")
        print()
        print("SOS movable -> SOS 普通 VGIC 明细:")
        for irq in sorted(sos_movable_irqs):
            status = "冲突" if irq in sos_normal_irqs else "无重叠"
            print(f"  {irq}: {status}; {format_sources(sos_movable_sources[irq])}")
        print()
        print("UOS 普通 VGIC -> SOS 普通 VGIC 明细:")
        for irq in sorted(uos_normal_irqs):
            status = "冲突" if irq in sos_normal_irqs else "无重叠"
            print(f"  {irq}: {status}; {format_sources(uos_normal_sources[irq])}")

    print_message_list("静态求值警告:", warnings)
    print_message_list("警告明细:", uos_duplicate_warnings)
    print_message_list("错误明细:", errors)

    if errors:
        print()
        print("结论: FAIL")
        return 1

    print()
    print("结论: PASS")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Check IRQ ownership for the given config directory"
    )
    parser.add_argument(
        "config_dir",
        help="config directory to check",
    )
    parser.add_argument("--verbose", action="store_true", help="print expanded details")
    args = parser.parse_args()

    config_dir = Path(args.config_dir)
    if not config_dir.is_absolute():
        config_dir = Path.cwd() / config_dir

    try:
        return check_config(config_dir, args.verbose)
    except CheckError as err:
        print(f"ERROR: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
