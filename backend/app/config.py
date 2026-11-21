import os
from pathlib import Path


MQTT_HOST = os.getenv("MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USERNAME = os.getenv("MQTT_USERNAME", "")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "")
DATABASE_PATH = Path(os.getenv("DATABASE_PATH", "./data/potato.sqlite3"))
MQTT_CLIENT_ID = os.getenv("MQTT_CLIENT_ID", "potato-local-backend")
# Timestamps are stored in UTC; daily statistics are grouped by local day (Vietnam = UTC+7).
LOCAL_UTC_OFFSET_HOURS = int(os.getenv("LOCAL_UTC_OFFSET_HOURS", "7"))
