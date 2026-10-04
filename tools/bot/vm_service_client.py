#!/usr/bin/env python3
"""
Sparrow - Dart VM Service Client & Test Health Inspector
Connects to the Flutter / Dart VM Service (http://127.0.0.1:8181/) via JSON-RPC 2.0
to monitor isolate health, detect unhandled Dart exceptions, measure memory usage,
and inspect Flutter frame rendering statistics during automated tests.
"""

import sys
import json
import time
import urllib.request
import urllib.error
import argparse
from typing import Dict, Any, Optional, List

DEFAULT_VM_SERVICE_URL = "http://127.0.0.1:8181"

class VMServiceClient:
    def __init__(self, base_url: str = DEFAULT_VM_SERVICE_URL):
        self.base_url = base_url.rstrip("/")
        self._req_id = 0

    def call_rpc(self, method: str, params: Optional[Dict[str, Any]] = None, timeout: float = 3.0) -> Dict[str, Any]:
        """Sends a JSON-RPC 2.0 request to the Dart VM Service via HTTP POST."""
        self._req_id += 1
        payload = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
            "id": self._req_id
        }
        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            self.base_url,
            data=data,
            headers={"Content-Type": "application/json"}
        )
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                result = json.loads(resp.read().decode("utf-8"))
                if "error" in result:
                    return {"success": False, "error": result["error"]}
                return {"success": True, "result": result.get("result", {})}
        except Exception as e:
            return {"success": False, "error": str(e)}

    def wait_ready(self, timeout: float = 10.0) -> bool:
        """Polls until the VM Service is responsive or timeout expires."""
        start = time.time()
        while time.time() - start < timeout:
            res = self.call_rpc("getVersion", timeout=1.0)
            if res.get("success"):
                return True
            time.sleep(0.2)
        return False

    def get_version(self) -> Optional[Dict[str, Any]]:
        res = self.call_rpc("getVersion")
        return res.get("result") if res.get("success") else None

    def get_vm(self) -> Optional[Dict[str, Any]]:
        res = self.call_rpc("getVM")
        return res.get("result") if res.get("success") else None

    def get_isolates(self) -> List[Dict[str, Any]]:
        vm = self.get_vm()
        if not vm or "isolates" not in vm:
            return []
        return vm["isolates"]

    def get_isolate(self, isolate_id: str) -> Optional[Dict[str, Any]]:
        res = self.call_rpc("getIsolate", {"isolateId": isolate_id})
        return res.get("result") if res.get("success") else None

    def get_memory_usage(self, isolate_id: Optional[str] = None) -> Dict[str, Any]:
        """Fetches heap and external memory usage for all isolates or a specific one."""
        if isolate_id:
            res = self.call_rpc("getMemoryUsage", {"isolateId": isolate_id})
            return res.get("result", {}) if res.get("success") else {}

        isolates = self.get_isolates()
        total_heap_used = 0
        total_heap_capacity = 0
        total_external = 0
        isolate_details = []

        for iso in isolates:
            iso_id = iso.get("id")
            res = self.call_rpc("getMemoryUsage", {"isolateId": iso_id})
            if res.get("success"):
                mem = res["result"]
                heap_used = mem.get("heapUsage", 0)
                heap_cap = mem.get("heapCapacity", 0)
                external = mem.get("externalUsage", 0)
                total_heap_used += heap_used
                total_heap_capacity += heap_cap
                total_external += external
                isolate_details.append({
                    "id": iso_id,
                    "name": iso.get("name"),
                    "heapUsed": heap_used,
                    "heapCapacity": heap_cap,
                    "external": external,
                })

        return {
            "totalHeapUsed": total_heap_used,
            "totalHeapCapacity": total_heap_capacity,
            "totalExternal": total_external,
            "totalMB": round((total_heap_used + total_external) / (1024 * 1024), 2),
            "isolates": isolate_details
        }

    def check_health(self) -> Dict[str, Any]:
        """
        Verifies that all running Dart isolates are healthy (not paused on exception,
        not terminated abnormally). Returns a health summary.
        """
        vm = self.get_vm()
        if not vm:
            return {"healthy": False, "reason": "Cannot connect to Dart VM"}

        isolates = vm.get("isolates", [])
        if not isolates:
            return {"healthy": False, "reason": "No active isolates found in Dart VM"}

        errors = []
        for iso_ref in isolates:
            iso_id = iso_ref.get("id")
            iso = self.get_isolate(iso_id)
            if not iso:
                errors.append(f"Failed to inspect isolate {iso_id}")
                continue

            pause_event = iso.get("pauseEvent", {})
            kind = pause_event.get("kind", "")
            if kind == "PauseException":
                exc = pause_event.get("exception", {}).get("valueAsString", "Unknown Exception")
                errors.append(f"Isolate '{iso.get('name')}' crashed with uncaught exception: {exc}")
            elif kind == "PauseExit":
                errors.append(f"Isolate '{iso.get('name')}' exited unexpectedly")

        if errors:
            return {"healthy": False, "reason": "; ".join(errors)}

        return {
            "healthy": True,
            "vmName": vm.get("name", "Dart VM"),
            "targetCPU": vm.get("targetCPU", "unknown"),
            "isolateCount": len(isolates),
        }

    def get_flutter_views(self) -> List[Dict[str, Any]]:
        """Queries Flutter views registered in the engine."""
        isolates = self.get_isolates()
        views = []
        for iso in isolates:
            res = self.call_rpc("_flutter.listViews", {"isolateId": iso["id"]})
            if res.get("success") and "views" in res["result"]:
                for v in res["result"]["views"]:
                    views.append({
                        "isolateId": iso["id"],
                        "viewId": v.get("id"),
                        "type": v.get("type"),
                    })
        return views


def main():
    parser = argparse.ArgumentParser(description="Sparrow Dart VM Service Client")
    parser.add_argument("--url", default=DEFAULT_VM_SERVICE_URL, help="VM Service base URL (default: http://127.0.0.1:8181)")
    parser.add_argument("--wait", action="store_true", help="Wait for VM Service to become ready")
    parser.add_argument("--timeout", type=float, default=10.0, help="Wait timeout in seconds")
    parser.add_argument("--check-health", action="store_true", help="Verify Isolate health and check for uncaught exceptions")
    parser.add_argument("--memory", action="store_true", help="Print Dart memory and heap usage")
    parser.add_argument("--json", action="store_true", help="Output raw JSON results")

    args = parser.parse_args()
    client = VMServiceClient(args.url)

    if args.wait:
        ok = client.wait_ready(args.timeout)
        if not ok:
            if args.json:
                print(json.dumps({"ready": False, "error": f"VM Service did not respond within {args.timeout}s"}))
            else:
                print(f"[FAIL] Dart VM Service at {args.url} did not respond within {args.timeout}s", file=sys.stderr)
            sys.exit(1)
        if not args.check_health and not args.memory:
            if args.json:
                print(json.dumps({"ready": True, "url": args.url}))
            else:
                print(f"[OK] Dart VM Service ready at {args.url}")
            sys.exit(0)

    if args.check_health:
        health = client.check_health()
        if args.json:
            print(json.dumps(health, indent=2))
        else:
            if health.get("healthy"):
                print(f"[OK] Dart VM Healthy: {health.get('isolateCount')} isolate(s) running cleanly")
            else:
                print(f"[FAIL] Dart VM Health Error: {health.get('reason')}", file=sys.stderr)
                sys.exit(1)

    if args.memory:
        mem = client.get_memory_usage()
        if args.json:
            print(json.dumps(mem, indent=2))
        else:
            print(f"[MEMORY] Dart Heap Used: {round(mem.get('totalHeapUsed', 0)/(1024*1024), 2)} MB | Total: {mem.get('totalMB')} MB")


if __name__ == "__main__":
    main()
