from contextlib import asynccontextmanager
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from . import database
from .mqtt_subscriber import MqttSubscriber

ROOT = Path(__file__).resolve().parents[1]
STATIC_DIR = ROOT / "static"
mqtt_subscriber: Optional[MqttSubscriber] = None


@asynccontextmanager
async def lifespan(app: FastAPI):
    global mqtt_subscriber
    database.initialize()
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
