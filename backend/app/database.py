import sqlite3
from contextlib import contextmanager
from datetime import datetime, timezone
from typing import Any

from . import config


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


def _connect() -> sqlite3.Connection:
    config.DATABASE_PATH.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(config.DATABASE_PATH, timeout=10)
    connection.row_factory = sqlite3.Row
    connection.execute("PRAGMA journal_mode=WAL")
    return connection


@contextmanager
def _session():
    connection = _connect()
    try:
        yield connection
        connection.commit()
    except Exception:
        connection.rollback()
        raise
    finally:
        connection.close()


def initialize() -> None:
    with _session() as connection:
        connection.executescript(
            """
            CREATE TABLE IF NOT EXISTS classifications (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                device_id TEXT NOT NULL,
                quality TEXT NOT NULL CHECK (quality IN ('good', 'not_good', 'uncertain')),
                confidence REAL NOT NULL CHECK (confidence >= 0 AND confidence <= 1),
                sequence INTEGER,
                uptime_ms INTEGER,
                event_time TEXT NOT NULL,
                received_at TEXT NOT NULL,
                raw_payload TEXT NOT NULL,
                UNIQUE(device_id, sequence, uptime_ms)
            );
            CREATE INDEX IF NOT EXISTS idx_classifications_received
                ON classifications(received_at DESC);
            CREATE INDEX IF NOT EXISTS idx_classifications_quality
                ON classifications(quality);

            CREATE TABLE IF NOT EXISTS devices (
                device_id TEXT PRIMARY KEY,
                status TEXT NOT NULL,
                last_seen TEXT NOT NULL
            );
            """
        )


def insert_classification(record: dict[str, Any]) -> bool:
    with _session() as connection:
        cursor = connection.execute(
            """INSERT OR IGNORE INTO classifications
               (device_id, quality, confidence, sequence, uptime_ms, event_time, received_at, raw_payload)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?)""",
            (
                record["device_id"],
                record["quality"],
                record["confidence"],
                record.get("sequence"),
                record.get("uptime_ms"),
                record["event_time"],
                record["received_at"],
                record["raw_payload"],
            ),
        )
        return cursor.rowcount > 0


def update_device_status(device_id: str, status: str, seen_at: str | None = None) -> None:
    with _session() as connection:
        connection.execute(
            """INSERT INTO devices(device_id, status, last_seen) VALUES (?, ?, ?)
               ON CONFLICT(device_id) DO UPDATE SET status=excluded.status, last_seen=excluded.last_seen""",
            (device_id, status, seen_at or utc_now()),
        )


def fetch_summary(days: int = 7) -> dict[str, Any]:
    with _session() as connection:
        total = connection.execute("SELECT COUNT(*) FROM classifications").fetchone()[0]
        quality_rows = connection.execute(
            "SELECT quality, COUNT(*) AS count FROM classifications GROUP BY quality"
        ).fetchall()
        counts = {"good": 0, "not_good": 0, "uncertain": 0}
        counts.update({row["quality"]: row["count"] for row in quality_rows})
        # Group by LOCAL calendar day (received_at is UTC), e.g. "+7 hours" for Vietnam.
        offset = f"{config.LOCAL_UTC_OFFSET_HOURS:+d} hours"
        daily_rows = connection.execute(
            """SELECT date(received_at, ?) AS day, COUNT(*) AS total,
                      SUM(quality='good') AS good,
                      SUM(quality='not_good') AS not_good,
                      SUM(quality='uncertain') AS uncertain
               FROM classifications
               WHERE date(received_at, ?) >= date('now', ?, ?)
               GROUP BY day ORDER BY day""",
            (offset, offset, offset, f"-{days - 1} days"),
        ).fetchall()
        devices = connection.execute(
            "SELECT device_id, status, last_seen FROM devices ORDER BY device_id"
        ).fetchall()
        latest = connection.execute(
            """SELECT device_id, quality, confidence, event_time, received_at
               FROM classifications ORDER BY id DESC LIMIT 1"""
        ).fetchone()

    daily = [dict(row) for row in daily_rows]
    device_list = [dict(row) for row in devices]
    return {
        "total": total,
        "by_quality": counts,
        "daily": daily,
        "devices": device_list,
        "online_devices": sum(1 for device in device_list if device["status"] == "online"),
        "latest": dict(latest) if latest else None,
    }


def fetch_events(limit: int = 100, offset: int = 0, quality: str | None = None) -> list[dict[str, Any]]:
    limit = min(max(limit, 1), 500)
    offset = max(offset, 0)
    with _session() as connection:
        if quality:
            rows = connection.execute(
                """SELECT id, device_id, quality, confidence, sequence, uptime_ms, event_time, received_at
                   FROM classifications WHERE quality=? ORDER BY id DESC LIMIT ? OFFSET ?""",
                (quality, limit, offset),
            ).fetchall()
        else:
            rows = connection.execute(
                """SELECT id, device_id, quality, confidence, sequence, uptime_ms, event_time, received_at
                   FROM classifications ORDER BY id DESC LIMIT ? OFFSET ?""",
                (limit, offset),
            ).fetchall()
    return [dict(row) for row in rows]


def fetch_devices() -> list[dict[str, Any]]:
    with _session() as connection:
        rows = connection.execute(
            "SELECT device_id, status, last_seen FROM devices ORDER BY device_id"
        ).fetchall()
    return [dict(row) for row in rows]
