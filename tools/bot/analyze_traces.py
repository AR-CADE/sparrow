#!/usr/bin/env python3
"""
Sparrow - Unified Trace Analyzer (Perfetto C++ & Flutter Dart VM)
Parses and interprets both:
  1. Perfetto binary Protobuf traces (.pftrace) from the Sparrow Wayland Compositor.
  2. Flutter Timeline Chrome-trace JSON (.json) from the Dart VM Service.
Computes slice durations, detects bottlenecks, correlates threads, and presents
actionable latency breakdowns.
"""

import sys
import os
import json
import argparse
from typing import Dict, List, Any, Tuple, Optional

def read_varint(data: bytes, offset: int) -> Tuple[int, int]:
    res = 0
    shift = 0
    while offset < len(data):
        b = data[offset]
        offset += 1
        res |= (b & 0x7f) << shift
        if not (b & 0x80):
            break
        shift += 7
    return res, offset

def parse_proto_fields(data: bytes) -> List[Tuple[int, int, Any]]:
    offset = 0
    fields = []
    while offset < len(data):
        try:
            tag, offset = read_varint(data, offset)
        except Exception:
            break
        field_num = tag >> 3
        wire_type = tag & 0x7
        if wire_type == 0: # Varint
            val, offset = read_varint(data, offset)
            fields.append((field_num, wire_type, val))
        elif wire_type == 2: # Length-delimited
            length, offset = read_varint(data, offset)
            val = data[offset:offset+length]
            offset += length
            fields.append((field_num, wire_type, val))
        elif wire_type == 1: # 64-bit
            val = data[offset:offset+8]
            offset += 8
            fields.append((field_num, wire_type, val))
        elif wire_type == 5: # 32-bit
            val = data[offset:offset+4]
            offset += 4
            fields.append((field_num, wire_type, val))
        else:
            break
    return fields

def analyze_perfetto_trace(filepath: str) -> Dict[str, Any]:
    if not os.path.isfile(filepath):
        return {"error": f"File not found: {filepath}"}

    with open(filepath, "rb") as f:
        buf = f.read()

    offset = 0
    packets = []
    while offset < len(buf):
        try:
            tag, offset = read_varint(buf, offset)
        except Exception:
            break
        field_num = tag >> 3
        wire_type = tag & 0x7
        if wire_type == 2:
            length, offset = read_varint(buf, offset)
            if field_num == 1: # TracePacket
                packets.append(buf[offset:offset+length])
            offset += length
        elif wire_type == 0:
            val, offset = read_varint(buf, offset)
        else:
            break

    interned_names = {}
    interned_cats = {}
    events = [] # (ts_ns, track_uuid, type, name)

    last_ts_ns = 0

    for p in packets:
        fields = parse_proto_fields(p)
        packet_ts = None
        track_event_bytes = None
        for fnum, wtype, val in fields:
            if fnum == 8: # timestamp (ns)
                packet_ts = val
                last_ts_ns = packet_ts
            elif fnum == 11: # TrackEvent
                track_event_bytes = val
            elif fnum == 12: # InternedData
                for sfnum, swtype, sval in parse_proto_fields(val):
                    if sfnum == 2: # event_names
                        iid = None
                        name = None
                        for efnum, _, evalue in parse_proto_fields(sval):
                            if efnum == 1: iid = evalue
                            elif efnum == 2: name = evalue.decode("utf-8", "ignore")
                        if iid is not None and name is not None:
                            interned_names[iid] = name
                    elif sfnum == 1: # event_categories
                        iid = None
                        cat = None
                        for efnum, _, evalue in parse_proto_fields(sval):
                            if efnum == 1: iid = evalue
                            elif efnum == 2: cat = evalue.decode("utf-8", "ignore")
                        if iid is not None and cat is not None:
                            interned_cats[iid] = cat

        if track_event_bytes is not None:
            te_fields = parse_proto_fields(track_event_bytes)
            te_type = 1 # default slice_begin
            name = None
            track_uuid = 0
            ts_delta_us = 0
            ts_abs_us = None

            for tfnum, twtype, tval in te_fields:
                if tfnum == 1: # timestamp_delta_us
                    ts_delta_us = tval
                elif tfnum == 16: # timestamp_absolute_us
                    ts_abs_us = tval
                elif tfnum == 9: # type (1=BEGIN, 2=END, 3=INSTANT)
                    te_type = tval
                elif tfnum == 10: # name_iid
                    name = interned_names.get(tval, f"iid_{tval}")
                elif tfnum == 23: # name (string)
                    name = tval.decode("utf-8", "ignore")
                elif tfnum == 11: # track_uuid
                    track_uuid = tval

            if ts_abs_us is not None:
                cur_ts_ns = ts_abs_us * 1000
            elif packet_ts is not None:
                cur_ts_ns = packet_ts + (ts_delta_us * 1000)
            else:
                cur_ts_ns = last_ts_ns + (ts_delta_us * 1000)
                last_ts_ns = cur_ts_ns

            events.append((cur_ts_ns, track_uuid, te_type, name))

    slice_stack = {}
    durations = {}

    for ts, track, te_type, name in events:
        if te_type == 1: # BEGIN
            if track not in slice_stack:
                slice_stack[track] = []
            slice_stack[track].append((name, ts))
        elif te_type == 2: # END
            if track in slice_stack and slice_stack[track]:
                s_name, s_ts = slice_stack[track].pop()
                if ts >= s_ts:
                    dur_ms = (ts - s_ts) / 1000000.0
                    if s_name not in durations:
                        durations[s_name] = []
                    durations[s_name].append(dur_ms)

    stats = {}
    for name, times in durations.items():
        if times:
            stats[name] = {
                "count": len(times),
                "total_ms": round(sum(times), 2),
                "avg_ms": round(sum(times) / len(times), 3),
                "min_ms": round(min(times), 3),
                "max_ms": round(max(times), 3),
            }

    return {
        "file": filepath,
        "packets_count": len(packets),
        "events_count": len(events),
        "slices": stats
    }

def analyze_flutter_trace(filepath: str) -> Dict[str, Any]:
    if not os.path.isfile(filepath):
        return {"error": f"File not found: {filepath}"}

    with open(filepath, "r", encoding="utf-8") as f:
        data = json.load(f)

    events = data.get("traceEvents", [])
    threads = {}
    for e in events:
        if e.get("name") == "thread_name":
            threads[(e.get("pid"), e.get("tid"))] = e.get("args", {}).get("name", "unknown")

    stacks = {}
    durations = {}

    for e in events:
        key = (e.get("pid"), e.get("tid"))
        ph = e.get("ph")
        name = e.get("name")
        ts = e.get("ts", 0)

        if ph == "B":
            if key not in stacks:
                stacks[key] = []
            stacks[key].append((name, ts))
        elif ph == "E":
            if key in stacks and stacks[key]:
                s_name, s_ts = stacks[key].pop()
                dur_ms = (ts - s_ts) / 1000.0
                if s_name not in durations:
                    durations[s_name] = []
                durations[s_name].append(dur_ms)

    stats = {}
    for name, times in durations.items():
        if times:
            stats[name] = {
                "count": len(times),
                "total_ms": round(sum(times), 2),
                "avg_ms": round(sum(times) / len(times), 3),
                "min_ms": round(min(times), 3),
                "max_ms": round(max(times), 3),
            }

    return {
        "file": filepath,
        "events_count": len(events),
        "threads": threads,
        "slices": stats
    }

def main():
    parser = argparse.ArgumentParser(description="Sparrow Unified Trace Analyzer")
    parser.add_argument("traces", nargs="+", help="Trace files to analyze (.pftrace or .json)")
    args = parser.parse_args()

    for path in args.traces:
        print("\n" + "="*70)
        print(f" ANALYZING TRACE: {path}")
        print("="*70)
        if path.endswith(".pftrace"):
            res = analyze_perfetto_trace(path)
            if "error" in res:
                print(f"[ERROR] {res['error']}")
                continue
            print(f"Protobuf Packets : {res['packets_count']}")
            print(f"Track Events     : {res['events_count']}")
            print("\nC++ Compositor & Renderer Scopes:")
            print(f"  {'Scope Name':35s} | {'Count':6s} | {'Total (ms)':11s} | {'Avg (ms)':9s} | {'Min (ms)':9s} | {'Max (ms)':9s}")
            print("  " + "-"*85)
            for name, s in sorted(res['slices'].items(), key=lambda x: x[1]['total_ms'], reverse=True):
                print(f"  {name:35s} | {s['count']:6d} | {s['total_ms']:10.2f}ms | {s['avg_ms']:8.3f}ms | {s['min_ms']:8.3f}ms | {s['max_ms']:8.3f}ms")

        elif path.endswith(".json"):
            res = analyze_flutter_trace(path)
            if "error" in res:
                print(f"[ERROR] {res['error']}")
                continue
            print(f"Timeline Events  : {res['events_count']}")
            print(f"Registered Threads ({len(res['threads'])}):")
            for (pid, tid), tname in res['threads'].items():
                print(f"  - PID {pid}, TID {tid}: {tname}")

            print("\nFlutter UI & Impeller Raster Scopes:")
            print(f"  {'Slice Name':35s} | {'Count':6s} | {'Total (ms)':11s} | {'Avg (ms)':9s} | {'Min (ms)':9s} | {'Max (ms)':9s}")
            print("  " + "-"*85)
            for name, s in sorted(res['slices'].items(), key=lambda x: x[1]['total_ms'], reverse=True)[:25]:
                print(f"  {name:35s} | {s['count']:6d} | {s['total_ms']:10.2f}ms | {s['avg_ms']:8.3f}ms | {s['min_ms']:8.3f}ms | {s['max_ms']:8.3f}ms")

if __name__ == "__main__":
    main()

