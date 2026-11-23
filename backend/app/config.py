import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

MQTT_HOST = os.getenv("MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USERNAME = os.getenv("MQTT_USERNAME", "")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "")
MQTT_MAX_PAYLOAD_BYTES = int(os.getenv("MQTT_MAX_PAYLOAD_BYTES", "16384"))

db_env = os.getenv("DATABASE_PATH", "./data/potato.sqlite3")
db_path = Path(db_env)
DATABASE_PATH = db_path if db_path.is_absolute() else (ROOT / db_path).resolve()

MQTT_CLIENT_ID = os.getenv("MQTT_CLIENT_ID", "potato-local-backend")
LOCAL_UTC_OFFSET_HOURS = int(os.getenv("LOCAL_UTC_OFFSET_HOURS", "7"))

IMAGE_UPLOAD_TOKEN = os.getenv("IMAGE_UPLOAD_TOKEN", "").strip()
MAX_IMAGE_BYTES = int(os.getenv("MAX_IMAGE_BYTES", "2000000"))
img_env = os.getenv("IMAGE_DIR")
if img_env:
    img_path = Path(img_env)
    IMAGE_DIR = img_path if img_path.is_absolute() else (ROOT / img_path).resolve()
else:
    IMAGE_DIR = (DATABASE_PATH.parent / "images").resolve()
