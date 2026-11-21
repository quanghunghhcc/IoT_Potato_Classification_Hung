"""Simulate an ESP32 potato classifier over MQTT and check that the backend stored everything correctly.

It publishes exactly what the firmware (ESP32_S3_CAM/.../network_reporter.cpp) publishes:
  agri/<device>/status  "online" (retained) + Last Will "offline" (retained)
  agri/<device>/result  {"device","quality","confidence","seq","uptime_ms","ts"?, "p_good", ...}  QoS 1

Then it checks, through the HTTP API, that:
  * valid results are stored, the duplicate (same seq + uptime_ms) is ignored, the invalid one is rejected,
  * the device is "online", and after an abrupt disconnect the broker's Last Will turns it "offline".

Run inside the backend container (credentials come from .env):
    docker compose exec backend python -m app.simulate_device
"""

import argparse
import json
import os
import sys
import time
import urllib.request
from datetime import datetime, timezone

import paho.mqtt.client as mqtt


def api(base, path):
    with urllib.request.urlopen(base + path, timeout=5) as r:
        return json.loads(r.read().decode("utf-8"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=os.getenv("MQTT_HOST", "mosquitto"))
    ap.add_argument("--port", type=int, default=int(os.getenv("MQTT_PORT", "1883")))
    ap.add_argument("--user", default=os.getenv("MQTT_USERNAME", ""))
    ap.add_argument("--password", default=os.getenv("MQTT_PASSWORD", ""))
    ap.add_argument("--device", default=f"sim-{int(time.time()) % 100000}")
    ap.add_argument("--api", default="http://127.0.0.1:8000")
    a = ap.parse_args()

    status_topic, result_topic = f"agri/{a.device}/status", f"agri/{a.device}/result"
    c = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2, client_id=a.device,
                    protocol=mqtt.MQTTv311)
    if a.user:
        c.username_pw_set(a.user, a.password)
    c.will_set(status_topic, "offline", qos=1, retain=True)
    c.connect(a.host, a.port, keepalive=5)
    c.loop_start()
    time.sleep(0.5)
    c.publish(status_topic, "online", qos=0, retain=True)
    print(f"[SIM] connected as {a.device} to {a.host}:{a.port}")

    before = api(a.api, "/api/summary")["by_quality"]
    ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    results = [
        {"quality": "good", "confidence": 0.918, "raw": [107, -107]},
        {"quality": "not_good", "confidence": 0.996, "raw": [-127, 127]},
        {"quality": "uncertain", "confidence": 0.55, "raw": [12, -12]},
    ]
    payloads = []
    for seq, r in enumerate(results, start=1):
        p = {"device": a.device, "quality": r["quality"], "confidence": r["confidence"], "seq": seq,
             "uptime_ms": 100000 + seq * 1000, "ts": ts, "raw": r["raw"], "inf_ms": 120.4, "fw": "sim"}
        payloads.append(p)
        c.publish(result_topic, json.dumps(p), qos=1).wait_for_publish(5)
    c.publish(result_topic, json.dumps(payloads[0]), qos=1).wait_for_publish(5)          # duplicate
    bad = dict(payloads[0], seq=99, confidence=1.5)                                          # invalid
    c.publish(result_topic, json.dumps(bad), qos=1).wait_for_publish(5)
    time.sleep(1.5)

    ok = True

    def check(name, cond, detail=""):
        nonlocal ok
        ok &= bool(cond)
        print(f"[{'PASS' if cond else 'FAIL'}] {name} {detail}")

    after = api(a.api, "/api/summary")["by_quality"]
    delta = {k: after[k] - before[k] for k in after}
    check("3 valid results stored, duplicate + invalid ignored", delta == {"good": 1, "not_good": 1, "uncertain": 1},
          f"delta={delta}")
    events = [e for e in api(a.api, "/api/events?limit=20") if e["device_id"] == a.device]
    check("events visible in /api/events", len(events) == 3, f"({len(events)} rows)")
    dev = {d["device_id"]: d for d in api(a.api, "/api/devices")}
    check("device online", dev.get(a.device, {}).get("status") == "online")

    # Abrupt disconnect (no MQTT DISCONNECT) -> broker publishes the Last Will after ~1.5 x keepalive.
    c.loop_stop()
    c.socket().close()
    print("[SIM] socket closed abruptly; waiting for Last Will (~8-10 s)...")
    for _ in range(20):
        time.sleep(1)
        if {d["device_id"]: d for d in api(a.api, "/api/devices")}.get(a.device, {}).get("status") == "offline":
            break
    check("Last Will -> device offline",
          {d["device_id"]: d for d in api(a.api, "/api/devices")}.get(a.device, {}).get("status") == "offline")
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
