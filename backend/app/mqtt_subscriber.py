import json
import logging
import math
import re
import threading
from datetime import datetime, timezone

import paho.mqtt.client as mqtt

from . import config, database
from .telegram import TelegramNotifier

logger = logging.getLogger("potato.mqtt")
TOPIC_PATTERN = re.compile(r"^agri/([^/]+)/(result|status)$")
VALID_QUALITY = {"good", "not_good", "uncertain"}


def _event_time(value: object, received_at: str) -> str:
    if not isinstance(value, str) or not value:
        return received_at
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=timezone.utc)
        return parsed.astimezone(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")
    except ValueError:
        return received_at


class MqttSubscriber:
    def __init__(self) -> None:
        self._connected = threading.Event()
        self.telegram = TelegramNotifier()
        if self.telegram.enabled:
            logger.info("Telegram alerts enabled for not_good results")
        self.client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=config.MQTT_CLIENT_ID,
            protocol=mqtt.MQTTv311,
        )
        if config.MQTT_USERNAME:
            self.client.username_pw_set(config.MQTT_USERNAME, config.MQTT_PASSWORD)
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_message = self._on_message
        self.client.reconnect_delay_set(min_delay=1, max_delay=30)

    @property
    def connected(self) -> bool:
        return self._connected.is_set()

    def start(self) -> None:
        self.client.connect_async(config.MQTT_HOST, config.MQTT_PORT, keepalive=30)
        self.client.loop_start()
        logger.info("MQTT subscriber starting for %s:%s", config.MQTT_HOST, config.MQTT_PORT)

    def stop(self) -> None:
        try:
            self.client.disconnect()
        finally:
            self.client.loop_stop()
            self._connected.clear()
            self.telegram.close()

    def _on_connect(self, client, userdata, connect_flags, reason_code, properties) -> None:
        if reason_code.is_failure:
            self._connected.clear()
            logger.error("MQTT connection rejected: %s", reason_code)
            return
        self._connected.set()
        client.subscribe("agri/+/result", qos=1)
        client.subscribe("agri/+/status", qos=1)
        logger.info("MQTT connected; subscribed to agri/+/result and agri/+/status")

    def _on_disconnect(self, client, userdata, disconnect_flags, reason_code, properties) -> None:
        self._connected.clear()
        if reason_code.is_failure:
            logger.warning("MQTT disconnected: %s", reason_code)

    def _on_message(self, client, userdata, message) -> None:
        match = TOPIC_PATTERN.fullmatch(message.topic)
        if not match:
            return
        device_id, message_type = match.groups()
        received_at = database.utc_now()
        try:
            payload = message.payload.decode("utf-8")
            if message_type == "status":
                status = payload.strip().strip('"').lower()
                if status not in {"online", "offline"}:
                    logger.warning("Ignoring unknown status for %s: %s", device_id, status)
                    return
                database.update_device_status(device_id, status, received_at)
                logger.info("Device %s is %s", device_id, status)
                return

            data = json.loads(payload)
            if not isinstance(data, dict):
                raise ValueError("result must be a JSON object")
            if data.get("device", device_id) != device_id:
                raise ValueError("device ID in payload does not match MQTT topic")
            quality = str(data.get("quality", "")).lower()
            confidence = data.get("confidence")
            if quality not in VALID_QUALITY:
                raise ValueError(f"unsupported quality: {quality}")
            if isinstance(confidence, bool) or not isinstance(confidence, (float, int)):
                raise ValueError("confidence must be numeric")
            confidence = float(confidence)
            if not math.isfinite(confidence) or not 0 <= confidence <= 1:
                raise ValueError("confidence must be between 0 and 1")

            sequence = data.get("seq")
            uptime_ms = data.get("uptime_ms")
            if sequence is not None:
                sequence = int(sequence)
            if uptime_ms is not None:
                uptime_ms = int(uptime_ms)
            created = database.insert_classification(
                {
                    "device_id": device_id,
                    "quality": quality,
                    "confidence": confidence,
                    "sequence": sequence,
                    "uptime_ms": uptime_ms,
                    "event_time": _event_time(data.get("ts"), received_at),
                    "received_at": received_at,
                    "raw_payload": payload,
                }
            )
            if created:
                database.update_device_status(device_id, "online", received_at)
                logger.info("Stored %s result from %s", quality, device_id)
                # Alert only for NEW not_good results (duplicates from re-sends are ignored above).
                if quality == "not_good":
                    self.telegram.notify_rejected_potato(
                        device_id, confidence, _event_time(data.get("ts"), received_at)
                    )
            else:
                logger.debug("Ignored duplicate result from %s", device_id)
        except (UnicodeDecodeError, json.JSONDecodeError, TypeError, ValueError, OverflowError) as exc:
            logger.warning("Ignoring invalid MQTT message on %s: %s", message.topic, exc)
