import json
import logging
import os
from concurrent.futures import ThreadPoolExecutor
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

logger = logging.getLogger("potato.telegram")


class TelegramNotifier:
    def __init__(self) -> None:
        self.token = os.getenv("TELEGRAM_BOT_TOKEN", "").strip()
        self.chat_id = os.getenv("TELEGRAM_CHAT_ID", "").strip()
        self.executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="telegram-alert")

    @property
    def enabled(self) -> bool:
        return bool(self.token and self.chat_id)

    def notify_rejected_potato(self, device_id: str, confidence: float, event_time: str) -> None:
        if not self.enabled:
            return
        text = (
            "⚠️ Phát hiện khoai tây không đạt\n"
            f"Thiết bị: {device_id}\n"
            f"Độ tin cậy: {confidence * 100:.1f}%\n"
            f"Thời gian: {event_time}"
        )
        future = self.executor.submit(self._send, text)
        future.add_done_callback(self._log_result)

    def _send(self, text: str) -> None:
        url = f"https://api.telegram.org/bot{self.token}/sendMessage"
        body = json.dumps({"chat_id": self.chat_id, "text": text}).encode("utf-8")
        request = Request(url, data=body, headers={"Content-Type": "application/json"}, method="POST")
        with urlopen(request, timeout=5) as response:
            if response.status != 200:
                raise RuntimeError(f"Telegram returned HTTP {response.status}")

    @staticmethod
    def _log_result(future) -> None:
        try:
            future.result()
            logger.info("Telegram rejection alert sent")
        except (HTTPError, URLError, TimeoutError, RuntimeError) as exc:
            logger.warning("Telegram alert could not be sent: %s", exc)

    def close(self) -> None:
        self.executor.shutdown(wait=False, cancel_futures=True)
