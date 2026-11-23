from contextlib import asynccontextmanager
import hmac
from pathlib import Path
import re
from typing import Optional

from fastapi import FastAPI, Header, HTTPException, Query, Request
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from . import config, database
from .mqtt_subscriber import MqttSubscriber

ROOT = Path(__file__).resolve().parents[1]
STATIC_DIR = ROOT / "static"
mqtt_subscriber: Optional[MqttSubscriber] = None
DEVICE_ID_PATTERN = re.compile(r"^[A-Za-z0-9_-]{1,64}$")


@asynccontextmanager
async def lifespan(app: FastAPI):
    global mqtt_subscriber
    database.initialize()
    config.IMAGE_DIR.mkdir(parents=True, exist_ok=True)
    mqtt_subscriber = MqttSubscriber()
    mqtt_subscriber.start()
    yield
    if mqtt_subscriber:
        mqtt_subscriber.stop()
        mqtt_subscriber = None


app = FastAPI(title="Potato Quality Dashboard", version="1.0.0", lifespan=lifespan)
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")


@app.get("/", include_in_schema=False)
def dashboard():
    return FileResponse(STATIC_DIR / "index.html")


@app.get("/api/health")
def health():
    return {
        "ok": True,
        "mqtt_connected": bool(mqtt_subscriber and mqtt_subscriber.connected),
    }


@app.get("/api/summary")
def summary(days: int = Query(7, ge=1, le=31)):
    return database.fetch_summary(days)


@app.get("/api/events")
def events(
    limit: int = Query(100, ge=1, le=500),
    offset: int = Query(0, ge=0),
    quality: str | None = Query(None, pattern="^(good|not_good|uncertain)$"),
):
    return database.fetch_events(limit, offset, quality)


@app.get("/api/devices")
def devices():
    return database.fetch_devices()


@app.post("/api/images", status_code=201)
async def upload_image(
    request: Request,
    x_upload_token: str | None = Header(None),
    x_device_id: str = Header(...),
    x_sequence: int = Header(...),
    x_uptime_ms: int = Header(...),
):
    if not config.IMAGE_UPLOAD_TOKEN:
        raise HTTPException(status_code=503, detail="Image upload is not configured")
    if not x_upload_token or not hmac.compare_digest(x_upload_token, config.IMAGE_UPLOAD_TOKEN):
        raise HTTPException(status_code=401, detail="Invalid upload token")
    if not DEVICE_ID_PATTERN.fullmatch(x_device_id):
        raise HTTPException(status_code=400, detail="Invalid device ID")
    if x_sequence < 0 or x_uptime_ms < 0:
        raise HTTPException(status_code=400, detail="Sequence and uptime must be non-negative")
    content_type = request.headers.get("content-type", "").split(";", 1)[0].strip().lower()
    if content_type != "image/jpeg":
        raise HTTPException(status_code=415, detail="Content-Type must be image/jpeg")
    declared_size = request.headers.get("content-length")
    if declared_size:
        try:
            if int(declared_size) > config.MAX_IMAGE_BYTES:
                raise HTTPException(status_code=413, detail="Image is too large")
        except ValueError as exc:
            raise HTTPException(status_code=400, detail="Invalid content length") from exc
    body = bytearray()
    async for chunk in request.stream():
        body.extend(chunk)
        if len(body) > config.MAX_IMAGE_BYTES:
            raise HTTPException(status_code=413, detail="Image is too large")
    image = bytes(body)
    if len(image) < 4 or not image.startswith(b"\xff\xd8") or not image.endswith(b"\xff\xd9"):
        raise HTTPException(status_code=415, detail="Expected a JPEG image")

    config.IMAGE_DIR.mkdir(parents=True, exist_ok=True)
    filename = f"{x_device_id}_{x_sequence}_{x_uptime_ms}.jpg"
    destination = config.IMAGE_DIR / filename
    temporary = destination.with_suffix(".jpg.tmp")
    temporary.write_bytes(image)
    temporary.replace(destination)
    database.store_image(x_device_id, x_sequence, x_uptime_ms, str(destination))
    return {"ok": True, "image_url": f"/api/images/{x_device_id}/{x_sequence}/{x_uptime_ms}"}


@app.get("/api/images/{device_id}/{sequence}/{uptime_ms}")
def get_image(device_id: str, sequence: int, uptime_ms: int):
    if not DEVICE_ID_PATTERN.fullmatch(device_id):
        raise HTTPException(status_code=404, detail="Image not found")
    image_path = database.fetch_image_path(device_id, sequence, uptime_ms)
    if not image_path or not Path(image_path).is_file():
        raise HTTPException(status_code=404, detail="Image not found")
    return FileResponse(image_path, media_type="image/jpeg")
