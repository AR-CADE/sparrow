#!/usr/bin/env python3
"""
Sparrow PGO & Automated Workload Runner
Reads a declarative JSON application list (pgo_apps.json) and drives
real Wayland client execution and automated synthetic interactions (inputs,
motions, hotkeys) via wlrctl for a specified duration per application.
"""

import os
import sys
import json
import time
import math
import shlex
import shutil
import argparse
import subprocess
from pathlib import Path

# ANSI colors
GREEN = '\033[0;32m'
RED = '\033[0;31m'
YELLOW = '\033[1;33m'
CYAN = '\033[0;36m'
MAGENTA = '\033[0;35m'
BOLD = '\033[1m'
NC = '\033[0m'

def log_info(msg):
    print(f"{CYAN}[INFO]{NC} {msg}")

def log_ok(msg):
    print(f"{GREEN}[OK]{NC} {msg}")

def log_warn(msg):
    print(f"{YELLOW}[WARN]{NC} {msg}")

def log_err(msg):
    print(f"{RED}[ERR]{NC} {msg}")

def ensure_helper_binaries(repo_root):
    repro_dir = repo_root / "tools/bot/repro_clients"
    sparrow_key = repro_dir / "sparrow_key"
    sparrow_touch = repro_dir / "sparrow_touch"
    targets = []
    if not sparrow_key.is_file():
        targets.append("sparrow_key")
    if not sparrow_touch.is_file():
        targets.append("sparrow_touch")
    if targets and (repro_dir / "Makefile").is_file():
        subprocess.run(["make", "-C", str(repro_dir)] + targets,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)

def find_wlrctl():
    root_dir = Path(__file__).resolve().parent.parent.parent
    out_bin = root_dir / "out" / "wlrctl"
    if out_bin.is_file() and os.access(out_bin, os.X_OK):
        return str(out_bin)
    build_bin = root_dir / "build" / "subprojects" / "wlrctl" / "wlrctl"
    if build_bin.is_file() and os.access(build_bin, os.X_OK):
        return str(build_bin)
    system_bin = shutil.which("wlrctl")
    if system_bin:
        return system_bin
    return str(out_bin)

WLRCTL_BIN = find_wlrctl()

def run_wlrctl(args, verbose=False):
    """Execute a wlrctl command silently or with verbose output."""
    try:
        cmd = [WLRCTL_BIN] + args
        subprocess.run(
            cmd,
            check=False,
            stdout=subprocess.DEVNULL if not verbose else None,
            stderr=subprocess.DEVNULL if not verbose else None,
        )
    except FileNotFoundError:
        log_err(f"'{WLRCTL_BIN}' utility is missing!")

def perform_mouse_motion(pattern, steps=6, radius=100, interval=0.04, screen_w=1280, screen_h=720):
    center_x, center_y = screen_w // 2, screen_h // 2
    if pattern == "cross_screen":
        points = [
            (100, 100), (300, 200), (600, 400),
            (900, 500), (1100, 250), (640, 360)
        ]
        for x, y in points:
            run_wlrctl(["pointer", "move", str(x), str(y)])
            time.sleep(interval)
    elif pattern == "circle":
        num_points = max(8, steps * 2)
        for i in range(num_points):
            angle = (2 * math.pi * i) / num_points
            x = int(center_x + radius * math.cos(angle))
            y = int(center_y + radius * math.sin(angle))
            run_wlrctl(["pointer", "move", str(x), str(y)])
            time.sleep(interval)
    elif pattern == "zigzag":
        y = 200
        for x in range(100, 1000, 150):
            run_wlrctl(["pointer", "move", str(x), str(y)])
            time.sleep(interval)
            y = 500 if y == 200 else 200

def get_primary_output():
    try:
        res = subprocess.run(["wlr-randr"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        for line in res.stdout.splitlines():
            parts = line.strip().split()
            if parts:
                return parts[0]
    except Exception:
        pass
    return "HEADLESS-1"

CURRENT_TRANSFORM = "normal"
ROTATION_CYCLE = ["normal", "90", "180", "270"]

def perform_rotate(action, verbose=False):
    global CURRENT_TRANSFORM
    output_name = action.get("output") or get_primary_output()
    raw_transform = str(action.get("transform", "cycle")).lower().strip()

    if raw_transform in ("0", "normal", "none"):
        new_transform = "normal"
    elif raw_transform in ("90", "180", "270"):
        new_transform = raw_transform
    elif raw_transform == "cycle":
        try:
            idx = ROTATION_CYCLE.index(CURRENT_TRANSFORM)
            new_transform = ROTATION_CYCLE[(idx + 1) % len(ROTATION_CYCLE)]
        except ValueError:
            new_transform = "90"
    else:
        new_transform = raw_transform

    CURRENT_TRANSFORM = new_transform
    if verbose:
        log_info(f"Rotating display output {output_name} -> {new_transform}")
    subprocess.run(["wlr-randr", "--output", output_name, "--transform", new_transform],
                   stdout=subprocess.DEVNULL if not verbose else None,
                   stderr=subprocess.DEVNULL if not verbose else None,
                   check=False)
    delay = float(action.get("delay", 0.5))
    if delay > 0:
        time.sleep(delay)

def perform_touch(action, repo_root, verbose=False):
    sparrow_touch = repo_root / "tools/bot/repro_clients/sparrow_touch"
    if not sparrow_touch.is_file() or not os.access(sparrow_touch, os.X_OK):
        # Fallback to wlrctl pointer if sparrow_touch binary is not present
        act_type = action.get("type")
        if act_type in ("tap", "touch_tap"):
            x = action.get("x", 640)
            y = action.get("y", 360)
            run_wlrctl(["pointer", "move", str(x), str(y)])
            run_wlrctl(["pointer", "click", "left"])
        return

    act_type = action.get("type")
    cmd = [str(sparrow_touch)]

    if act_type in ("tap", "touch_tap"):
        x = str(action.get("x", 640))
        y = str(action.get("y", 360))
        hold_ms = str(int(action.get("hold_ms", action.get("duration", 0.03) * 1000)))
        cmd += ["tap", x, y, hold_ms]

    elif act_type in ("double_tap", "touch_double_tap"):
        x = str(action.get("x", 640))
        y = str(action.get("y", 360))
        cmd += ["double-tap", x, y]

    elif act_type in ("long_press", "touch_long_press"):
        x = str(action.get("x", 640))
        y = str(action.get("y", 360))
        hold_ms = str(int(action.get("hold_ms", action.get("duration", 0.6) * 1000)))
        cmd += ["long-press", x, y, hold_ms]

    elif act_type in ("swipe", "touch_swipe"):
        direction = action.get("direction")
        duration_ms = str(int(action.get("duration_ms", action.get("duration", 0.25) * 1000)))
        if direction:
            dir_clean = direction.lower().strip()
            if dir_clean in ("left", "swipe-left"):
                cmd += ["swipe-left", duration_ms]
            elif dir_clean in ("right", "swipe-right"):
                cmd += ["swipe-right", duration_ms]
            elif dir_clean in ("up", "swipe-up"):
                cmd += ["swipe-up", duration_ms]
            elif dir_clean in ("down", "swipe-down"):
                cmd += ["swipe-down", duration_ms]
            else:
                cmd += ["swipe-left", duration_ms]
        elif "start" in action and "end" in action:
            start_x, start_y = str(action["start"][0]), str(action["start"][1])
            end_x, end_y = str(action["end"][0]), str(action["end"][1])
            steps = str(action.get("steps", 16))
            cmd += ["swipe", start_x, start_y, end_x, end_y, duration_ms, steps]
        elif "x1" in action and "y1" in action and "x2" in action and "y2" in action:
            start_x, start_y = str(action["x1"]), str(action["y1"])
            end_x, end_y = str(action["x2"]), str(action["y2"])
            steps = str(action.get("steps", 16))
            cmd += ["swipe", start_x, start_y, end_x, end_y, duration_ms, steps]
        else:
            cmd += ["swipe-left", duration_ms]

    elif act_type in ("touch_drag", "drag"):
        start_x = str(action.get("start", [500, 300])[0]) if "start" in action else str(action.get("x1", 500))
        start_y = str(action.get("start", [500, 300])[1]) if "start" in action else str(action.get("y1", 300))
        end_x = str(action.get("end", [700, 300])[0]) if "end" in action else str(action.get("x2", 700))
        end_y = str(action.get("end", [700, 300])[1]) if "end" in action else str(action.get("y2", 300))
        duration_ms = str(int(action.get("duration_ms", action.get("duration", 0.3) * 1000)))
        steps = str(action.get("steps", 20))
        cmd += ["swipe", start_x, start_y, end_x, end_y, duration_ms, steps]

    elif act_type in ("edge_swipe", "edge_bottom", "touch_edge"):
        duration_ms = str(int(action.get("duration_ms", action.get("duration", 0.2) * 1000)))
        cmd += ["edge-bottom", duration_ms]

    if verbose:
        log_info(f"Touch action executed: {' '.join(cmd)}")

    subprocess.run(cmd,
                   stdout=subprocess.DEVNULL if not verbose else None,
                   stderr=subprocess.DEVNULL if not verbose else None,
                   check=False)

def perform_action(action, repo_root, verbose=False):
    action_type = action.get("type")
    if action_type == "mouse_motion":
        if "x" in action and "y" in action:
            run_wlrctl(["pointer", "move", str(action["x"]), str(action["y"])])
        else:
            perform_mouse_motion(
                pattern=action.get("pattern", "cross_screen"),
                steps=action.get("steps", 6),
                radius=action.get("radius", 100),
                interval=action.get("interval", 0.04),
            )
    elif action_type == "click":
        button = action.get("button", "left")
        run_wlrctl(["pointer", "click", button], verbose=verbose)
    elif action_type in ("tap", "touch_tap", "double_tap", "touch_double_tap",
                         "long_press", "touch_long_press", "swipe", "touch_swipe",
                         "touch_drag", "drag", "edge_swipe", "edge_bottom", "touch_edge"):
        perform_touch(action, repo_root, verbose=verbose)
    elif action_type in ("rotate", "rotation"):
        perform_rotate(action, verbose=verbose)
    elif action_type == "keyboard_type":
        text = action.get("text", "")
        sparrow_key = repo_root / "tools/bot/repro_clients/sparrow_key"
        if sparrow_key.exists():
            subprocess.run([str(sparrow_key), "type", text], check=False)
        else:
            lines = text.split("\n")
            for idx, line in enumerate(lines):
                if line:
                    run_wlrctl(["keyboard", "type", line], verbose=verbose)
                if idx < len(lines) - 1:
                    run_wlrctl(["keyboard", "type", "Return"], verbose=verbose)
    elif action_type == "hotkey":
        key = action.get("key", "")
        if key:
            sparrow_key = repo_root / "tools/bot/repro_clients/sparrow_key"
            if sparrow_key.exists():
                subprocess.run([str(sparrow_key), key], check=False)
            else:
                run_wlrctl(["keyboard", "type", key], verbose=verbose)
    elif action_type == "combo":
        mod = action.get("mod", "")
        key = action.get("key", "")
        if mod and key:
            sparrow_key = repo_root / "tools/bot/repro_clients/sparrow_key"
            if sparrow_key.exists():
                subprocess.run([str(sparrow_key), "combo", mod, key], check=False)
    elif action_type == "sleep":
        dur = float(action.get("duration", 0.5))
        time.sleep(dur)

def resolve_command(cmd_str, repo_root):
    try:
        raw_parts = shlex.split(cmd_str.strip())
    except Exception:
        raw_parts = cmd_str.strip().split()
    if not raw_parts:
        return None, []

    parts = [os.path.expanduser(os.path.expandvars(p)) for p in raw_parts]
    binary = parts[0]
    args = parts[1:]

    # Check if relative path in repo
    candidate_path = (repo_root / binary).resolve()
    if candidate_path.is_file() and os.access(candidate_path, os.X_OK):
        return str(candidate_path), args

    # Check in PATH
    found = shutil.which(binary)
    if found:
        return found, args

    return None, args

def main():
    parser = argparse.ArgumentParser(description="Sparrow PGO & Declarative Workload Runner")
    parser.add_argument("--config", "-c", default="tools/bot/pgo_apps.json",
                        help="Path to JSON workload configuration file")
    parser.add_argument("--scale-duration", "-s", type=float, default=1.0,
                        help="Multiplier for application durations (e.g. 0.5 for fast tests)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Check app availability and print workload schedule without executing")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Print detailed action logs")
    parser.add_argument("--app", type=str, default=None,
                        help="Filter and run only specific application name")

    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent.parent
    config_path = Path(args.config)
    if not config_path.is_absolute():
        config_path = repo_root / config_path

    if not config_path.exists():
        log_err(f"Configuration file not found: {config_path}")
        sys.exit(1)

    with open(config_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    apps = data.get("applications", [])
    default_duration = data.get("default_duration_sec", 5)
    inter_app_delay = data.get("inter_app_delay_sec", 0.5)

    if args.app:
        apps = [a for a in apps if args.app.lower() in a.get("name", "").lower()]

    print(f"\n{CYAN}{BOLD}=== Sparrow Declarative Application Workload ==={NC}")
    print(f"Configuration : {config_path.name}")
    print(f"Total Apps    : {len(apps)}")
    print(f"Scale Factor  : {args.scale_duration}x\n")

    executed_count = 0
    skipped_count = 0

    if not args.dry_run:
        ensure_helper_binaries(repo_root)

    for i, app_entry in enumerate(apps, 1):
        name = app_entry.get("name", f"App #{i}")
        raw_cmd = app_entry.get("command", "")
        duration = max(1.0, float(app_entry.get("duration", default_duration)) * args.scale_duration)
        required = app_entry.get("required", False)
        actions = app_entry.get("actions", [])

        bin_path, bin_args = resolve_command(raw_cmd, repo_root)

        if not bin_path:
            msg = f"Binary for '{name}' not found: '{raw_cmd}'"
            if required:
                log_err(f"CRITICAL: {msg}")
                sys.exit(1)
            else:
                log_warn(f"Skipping '{name}' (not installed/compiled).")
                skipped_count += 1
                continue

        print(f"{MAGENTA}[{i}/{len(apps)}] {BOLD}{name}{NC} ({duration:.1f}s) -> {bin_path} {' '.join(bin_args)}")

        if args.dry_run:
            print(f"       Actions: {len(actions)} defined.")
            executed_count += 1
            continue

        # Launch application process
        cmd_full = [bin_path] + bin_args
        proc = None
        try:
            proc = subprocess.Popen(
                cmd_full,
                stdout=subprocess.DEVNULL if not args.verbose else None,
                stderr=subprocess.DEVNULL if not args.verbose else None,
                env=os.environ,
            )
        except Exception as e:
            log_err(f"Failed to spawn {name}: {e}")
            if required:
                sys.exit(1)
            continue

        executed_count += 1
        start_time = time.time()
        time.sleep(0.5)  # Allow Wayland surface creation & mapping

        # Execute actions loop until duration expires
        action_idx = 0
        while (time.time() - start_time) < duration:
            # Check if process unexpectedly exited
            if proc.poll() is not None:
                if proc.returncode != 0:
                    log_warn(f"Process '{name}' exited prematurely with code {proc.returncode}")
                break

            if actions:
                action = actions[action_idx % len(actions)]
                action_idx += 1
                perform_action(action, repo_root, verbose=args.verbose)
            else:
                # Default generic interaction
                run_wlrctl(["pointer", "move", "500", "400"])
                time.sleep(0.2)

            time.sleep(0.1)

        # Terminate application gracefully
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=1.5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

        log_ok(f"Completed workload for '{name}'")
        time.sleep(inter_app_delay)

    print(f"\n{BOLD}Workload Complete: {executed_count} executed, {skipped_count} skipped.{NC}\n")

if __name__ == "__main__":
    main()
