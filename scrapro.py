from __future__ import annotations

from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import os
import socket
import subprocess
import sys
import threading
from urllib.parse import urlsplit
import urllib.error
import urllib.request
APP_TITLE = "School Random Program"
WINDOW_WIDTH = 1440
WINDOW_HEIGHT = 900
APP_ICON_FILE = "app_icon.ico"
# 인생은 끝이없고 난 왜 이딴것만 하는건짐 모르겠다..
RAW_BASE_URL = "https://raw.githubusercontent.com/moonkyu12/School-Random-Program/main"
REQUEST_TIMEOUT_SECONDS = 4
LIVE_CACHE_DIRNAME = "live_repo_cache"

ROUTES = {
    "/": "index.html",
    "/index.html": "index.html",
    "/style.css": "style.css",
    "/script.js": "script.js",
}
# 에헤헿.....뜌땨 우땨땨
CONTENT_TYPES = {
    "index.html": "text/html; charset=utf-8",
    "style.css": "text/css; charset=utf-8",
    "script.js": "application/javascript; charset=utf-8",
}


def runtime_dir() -> Path:
    if getattr(sys, "frozen", False):
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).resolve().parent))
    return Path(__file__).resolve().parent # runtime_dir 뭐 반환하는ㅇ거고 일다ㅏㄴ은


def read_local_payload(filename: str) -> bytes | None:
    target = runtime_dir() / filename
    if not target.exists():
        return None
    try:
        return target.read_bytes()
    except OSError:
        return None


# function - 0.1 = fuck
def live_cache_dir() -> Path:
    local_app_data = os.getenv("LOCALAPPDATA")
    if local_app_data:
        return Path(local_app_data) / APP_TITLE / LIVE_CACHE_DIRNAME
    return Path.home() / ".cache" / "school-random-program" / LIVE_CACHE_DIRNAME


def cache_file_path(filename: str) -> Path:
    return live_cache_dir() / filename


def fetch_remote_payload(filename: str) -> bytes:
    remote_url = f"{RAW_BASE_URL}/{filename}"
    with urllib.request.urlopen(remote_url, timeout=REQUEST_TIMEOUT_SECONDS) as response:
        return response.read()

# 아니 야비쉬 PyQt6시부꺼 작명센스 개구리네
def import_qt_modules():
    from PyQt6.QtCore import QUrl
    from PyQt6.QtGui import QIcon, QKeySequence, QShortcut
    from PyQt6.QtWebEngineCore import QWebEngineProfile
    from PyQt6.QtWebEngineWidgets import QWebEngineView
    from PyQt6.QtWidgets import QApplication # QApplication은 어휴....인생....
    return QUrl, QIcon, QKeySequence, QShortcut, QWebEngineProfile, QWebEngineView, QApplication #에헤헤 리턴뒤에 이건 몰라?


def install_missing_requirements() -> bool:
    if getattr(sys, "frozen", False):
        return False

    req_file = runtime_dir() / "requirements.txt"
    if not req_file.exists():
        print(f"requirements.txt 파일이 없습니다: {req_file}") # 사실 있다 멍청아
        return False

    print("필수 패키지가 없어 requirements.txt"" 자동 설치를 시작합니다...")# print requirements를 걍 깔게 할껄 그랬나 아니다...
    try:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "-r", str(req_file)])
        return True
    except subprocess.CalledProcessError as err:
        print(f"자동 설치에 실패했습니다: {err}")
        return False


class LiveRepoHandler(BaseHTTPRequestHandler):
    cache: dict[str, bytes] = {}
    cache_lock = threading.Lock()
    refresh_in_flight: set[str] = set()

    def log_message(self, fmt: str, *args: object) -> None:
        return

    def _send_payload(self, filename: str, payload: bytes, source: str) -> None:
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", CONTENT_TYPES[filename])
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Source", source) # 아...약탈자 쉐리프 제발~~~뜨게해주세요
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    @classmethod
    def _read_disk_cache(cls, filename: str) -> bytes | None:
        target = cache_file_path(filename)
        if not target.exists():
            return None
        try:
            return target.read_bytes()
        except OSError:
            return None

    @classmethod
    def _write_disk_cache(cls, filename: str, payload: bytes) -> None:
        target = cache_file_path(filename)
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(payload)
        except OSError:
            return

    @classmethod
    def get_cached_payload(cls, filename: str) -> bytes | None:
        with cls.cache_lock:# 나는 말한다 그로 존재한다..하지만 존재하지 않는다...왜냐고? 몰라 내가 알면 신이지
            payload = cls.cache.get(filename)
        if payload is not None:
            return payload

        payload = cls._read_disk_cache(filename)
        if payload is None:
            return None

        with cls.cache_lock:
            cls.cache[filename] = payload
        return payload

    @classmethod
    def store_payload(cls, filename: str, payload: bytes) -> None:
        with cls.cache_lock:
            cls.cache[filename] = payload
        cls._write_disk_cache(filename, payload)

    @classmethod
    def _background_refresh(cls, filename: str) -> None:
        try:
            payload = fetch_remote_payload(filename)
            cls.store_payload(filename, payload)
        except (urllib.error.URLError, TimeoutError):
            pass
        finally:
            with cls.cache_lock:
                cls.refresh_in_flight.discard(filename)

    @classmethod
    def schedule_background_refresh(cls, filename: str) -> None:
        with cls.cache_lock:
            if filename in cls.refresh_in_flight:
                return
            cls.refresh_in_flight.add(filename)
        thread = threading.Thread(target=cls._background_refresh, args=(filename,), daemon=True)
        thread.start()

    @classmethod
    def warm_cache_from_disk(cls) -> None:
        for filename in sorted(set(ROUTES.values())):
            cls.get_cached_payload(filename)

    def do_GET(self) -> None:
        path = urlsplit(self.path).path
        filename = ROUTES.get(path)
        if filename is None:
            self.send_error(HTTPStatus.NOT_FOUND, "Not Found")
            return # 아~ 리턴 그는 신이야~

        payload: bytes
        try:
            payload = fetch_remote_payload(filename)
        except (urllib.error.URLError, TimeoutError):
            cached = type(self).get_cached_payload(filename)
            if cached is not None:
                self._send_payload(filename, cached, "cache")
                return
            self.send_error(HTTPStatus.BAD_GATEWAY, "Cannot load source from GitHub right now.") # 굳이 도커를 써야할까? (어 써야되) 응 싫어~
            return

        type(self).store_payload(filename, payload)
        self._send_payload(filename, payload, "live")


def pick_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def start_live_server() -> tuple[ThreadingHTTPServer, int]:
    LiveRepoHandler.warm_cache_from_disk()
    port = pick_free_port()
    server = ThreadingHTTPServer(("127.0.0.1", port), LiveRepoHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, port


def default_download_dir() -> Path:
    downloads = Path.home() / "Downloads"
    if downloads.exists():
        return downloads
    return Path.home()


def pick_unique_download_target(directory: Path, filename: str) -> Path:
    safe_name = Path(filename).name.strip() or "download.bin"
    candidate = directory / safe_name
    if not candidate.exists():
        return candidate

    stem = candidate.stem or "download"
    suffix = candidate.suffix
    idx = 1
    while True:
        renamed = directory / f"{stem} ({idx}){suffix}"
        if not renamed.exists():
            return renamed
        idx += 1

def main() -> int:
    try:
        QUrl, QIcon, QKeySequence, QShortcut, QWebEngineProfile, QWebEngineView, QApplication = import_qt_modules()
    except ImportError:
        if not install_missing_requirements():
            print("PyQt6 또는 PyQt6-WebEngine이 설치되지 않았습니다.")# 에러문 어쩌피 없을꺼 같은데 이귀찮은걸 내가 왜하는거지...
            print("다음 명령으로 설치 후 다시 실행하세요: pip install -r requirements.txt")
            return 1
        try:
            QUrl, QIcon, QKeySequence, QShortcut, QWebEngineProfile, QWebEngineView, QApplication = import_qt_modules()
        except ImportError:
            print("자동 설치 후에도 PyQt6 로드에 실패했습니다.")
            return 1


    server, port = start_live_server()
    app_url = f"http://127.0.0.1:{port}/"
    try:# 곧 끝지 오는데 왜 에러가 나는거냐?
        app = QApplication(sys.argv)
        icon_path = runtime_dir() / APP_ICON_FILE
        app_icon = QIcon(str(icon_path)) if icon_path.exists() else QIcon()
        profile = QWebEngineProfile.defaultProfile()
        profile.setHttpCacheType(QWebEngineProfile.HttpCacheType.MemoryHttpCache)
        profile.setPersistentCookiesPolicy(QWebEngineProfile.PersistentCookiesPolicy.NoPersistentCookies)

        def handle_download(download) -> None:
            suggested = download.downloadFileName() or Path(download.url().path()).name or "download.bin"
            target = pick_unique_download_target(default_download_dir(), suggested)
            try:
                target.parent.mkdir(parents=True, exist_ok=True)
                download.setDownloadDirectory(str(target.parent))
                download.setDownloadFileName(target.name)
                download.accept()
                print(f"다운로드 시작: {target}")
            except Exception as err:
                print(f"다운로드 처리 실패: {err}")
                download.cancel()

        profile.downloadRequested.connect(handle_download)

        window = QWebEngineView()
        window._download_handler = handle_download
        if not app_icon.isNull():
            app.setWindowIcon(app_icon)
            window.setWindowIcon(app_icon)# 얘떄문이였어 얘를 안둔거였어 이!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        window.setWindowTitle(APP_TITLE)# EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE
        window.resize(WINDOW_WIDTH, WINDOW_HEIGHT)
        window.load(QUrl(app_url))
        def toggle_fullscreen() -> None:
            if window.isFullScreen():
                window.showNormal()
            else:
                window.showFullScreen()

        fullscreen_shortcut = QShortcut(QKeySequence("F11"), window)
        fullscreen_shortcut.activated.connect(toggle_fullscreen)
        window._fullscreen_shortcut = fullscreen_shortcut
        window.show()

        return app.exec()
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    raise SystemExit(main())
# 끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝 끝이다!!!!!!!!
