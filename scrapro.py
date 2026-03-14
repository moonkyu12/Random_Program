from __future__ import annotations

from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import socket
import subprocess
import sys
import threading
from urllib.parse import urlsplit
import urllib.error
import urllib.request
# 캬~ 역시 AI가 이런 정렬은 잘해 스파게티 만들뻔,,,,
APP_TITLE = "School Random Program"
WINDOW_WIDTH = 1920
WINDOW_HEIGHT = 1360
APP_ICON_FILE = "app_icon.ico"

RAW_BASE_URL = "https://raw.githubusercontent.com/moonkyu12/School-Random-Program/main"
REQUEST_TIMEOUT_SECONDS = 10

ROUTES = {
    "/": "index.html",
    "/index.html": "index.html",
    "/style.css": "style.css",
    "/script.js": "script.js",
}

CONTENT_TYPES = {
    "index.html": "text/html; charset=utf-8",
    "style.css": "text/css; charset=utf-8",
    "script.js": "application/javascript; charset=utf-8",
}


def runtime_dir() -> Path:
    if getattr(sys, "frozen", False):
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).resolve().parent))
    return Path(__file__).resolve().parent


def import_qt_modules():
    from PyQt6.QtCore import QUrl
    from PyQt6.QtGui import QIcon
    from PyQt6.QtWebEngineCore import QWebEngineProfile
    from PyQt6.QtWebEngineWidgets import QWebEngineView
    from PyQt6.QtWidgets import QApplication
    return QUrl, QIcon, QWebEngineProfile, QWebEngineView, QApplication


def install_missing_requirements() -> bool:
    if getattr(sys, "frozen", False):
        # EXE(onefile)에서는 런타임 패키지 설치를 시도하지 않습니다.
        return False

    req_file = runtime_dir() / "requirements.txt"
    if not req_file.exists():
        print(f"requirements.txt 파일이 없습니다: {req_file}")
        return False

    print("필수 패키지가 없어 requirements.txt 자동 설치를 시작합니다...")
    try:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "-r", str(req_file)])
        return True
    except subprocess.CalledProcessError as err:
        print(f"자동 설치에 실패했습니다: {err}")
        return False


class LiveRepoHandler(BaseHTTPRequestHandler):
    # Network error 시 마지막 정상 응답본을 잠시 사용하고싶었다
    # 크~ 뭔 애런가 했더니 오브젝트가 걍 없던거였어 개같은거
    cache: dict[str, bytes] = {}

    def log_message(self, fmt: str, *args: object) -> None:
        return
    # do
    def do_GET(self) -> None: #왜들 그리 다운돼있어. 뭐가 문제야 say something 분위기가 검나 싸해 캬 좋당
        path = urlsplit(self.path).path
        filename = ROUTES.get(path)
        if filename is None:
            self.send_error(HTTPStatus.NOT_FOUND, "Not Found")
            return

        remote_url = f"{RAW_BASE_URL}/{filename}"
        payload: bytes
        source = "live" # 라~~~~~~이.....뭘까요~?

        try:
            with urllib.request.urlopen(remote_url, timeout=REQUEST_TIMEOUT_SECONDS) as response:
                payload = response.read()
            self.cache[filename] = payload # 비와이 가라사대 깝치지 말지어다 넌 나를 위로 볼 지어다 역사로 살 지어다 증인의 삶이 될 지어다 여기를 밝힐 지어다 여길 다 삼킬 지어다 구와 신의 기준이 나일 지어다 그게 나일 지어다 비와이 가라사대 리더는 날 따를 지어다 난 선구자일 지어다 돈은 날 찾을 지어다 내 이름의 시대를 만들 지어다 한글은 팔릴 지어다 잔들을 따를 지어다 나는 되고 싶은 내가 될 지어다 내가 될 지어다 비와이 가라사대 보기나 해 bish 가라사대 모든 것 위 가라사대 여호와 밑 가라사대 이게 내 위치 가라사대 내 어젠 이제 가라사대 전설이 돼 가라사대 열매를 맺어 가라사대 비와이 가라사대 비와이 가라사대 비와이 가라사대 비와이 가라사대 그게 나일 지어다 삶으로 나를 뱉어대 역사들은 새겨 댈걸 짭들은 베껴 대 진짜들은 말했어 내 건 최고 최초 내 날들은 매일매일 또 매번 배꼽 잡어 배고파도 내걸 만들어 새 걸 창조 계속하고 패권 잡어 배부른 날로 랩으로 바꿔 래퍼는 닥쳐 현재의 고난도 애써 참을 어제로 남아 여태껏 하던 대로 중심은 나일 지어다 신의 형상일 지어다 세상은 내 손 안일 지어다 영광의 면류관이 날 가질 지어다 기준을 제시할 지어다 미래를 계시할 지어다 가짜는 후회에 살 지어다 나는 그 위에 살 지어다 비와이 가라사대 보기나 해 bish 가라사대 모든 것 위 가라사대 여호와 밑 가라사대 이게 내 위치 가라사대 내 어젠 이제 가라사대 전설이 돼 가라사대 열매를 맺어 가라사대 비와이 가라사대 비와이 가라사대 비와이 가라사대 비와이 가라사대 그게 나일 지어다 비와이 가라사대
        except (urllib.error.URLError, TimeoutError):
            cached = self.cache.get(filename)
            if cached is None:
                self.send_error(HTTPStatus.BAD_GATEWAY, "Cannot load source from GitHub right now.")
                return
            payload = cached
            source = "cache"

        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", CONTENT_TYPES[filename])
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Source", source)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def pick_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def start_live_server() -> tuple[ThreadingHTTPServer, int]:
    port = pick_free_port()
    server = ThreadingHTTPServer(("127.0.0.1", port), LiveRepoHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, port

# PyQt6가 진짜 뭔임? ㅈㄴ모르것다....
def main() -> int:
    try:
        QUrl, QIcon, QWebEngineProfile, QWebEngineView, QApplication = import_qt_modules()
    except ImportError:
        if not install_missing_requirements():
            print("PyQt6 또는 PyQt6-WebEngine이 설치되지 않았습니다.")
            print("다음 명령으로 설치 후 다시 실행하세요: pip install -r requirements.txt")
            return 1
        try:
            QUrl, QIcon, QWebEngineProfile, QWebEngineView, QApplication = import_qt_modules()
        except ImportError:
            print("자동 설치 후에도 PyQt6 로드에 실패했습니다.")
            return 1


    server, port = start_live_server()
    app_url = f"http://127.0.0.1:{port}/"

# Q는 진짜 왜 붙는데...걍 Application으로 하면 뭐 죽냐? 참....
    try:
        app = QApplication(sys.argv)
        icon_path = runtime_dir() / APP_ICON_FILE
        app_icon = QIcon(str(icon_path)) if icon_path.exists() else QIcon()
        profile = QWebEngineProfile.defaultProfile()
        profile.setHttpCacheType(QWebEngineProfile.HttpCacheType.NoCache)
        profile.setPersistentCookiesPolicy(QWebEngineProfile.PersistentCookiesPolicy.NoPersistentCookies)

        window = QWebEngineView()
        if not app_icon.isNull():
            app.setWindowIcon(app_icon)
            window.setWindowIcon(app_icon)
        window.setWindowTitle(APP_TITLE)
        window.resize(WINDOW_WIDTH, WINDOW_HEIGHT)
        window.load(QUrl(app_url))
        window.show()

        return app.exec()
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    raise SystemExit(main())
# 끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝
