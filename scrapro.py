from __future__ import annotations
# 슈퍼 하이퍼 마스터리 앱솔루션하게 평범하고 평범한 사람좌가 만든 슈퍼 우ㅠㄹ트라 마제스티 충무공 얼티메이트한 퀠리티를 자랑하지 않는 그저 평범하게 실할 수 있는 앱
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
# GitHub raw 경로에서 정적 리소스를 내려받아 로컬 서버로 제공한다.
# 인생은 끝이없고 난 왜 이딴것만 하는건짐 모르겠다..
RAW_BASE_URL = "https://raw.githubusercontent.com/moonkyu12/Random_Page/main"
REQUEST_TIMEOUT_SECONDS = 4
LIVE_CACHE_DIRNAME = "live_repo_cache"

# 로컬 HTTP 서버에서 처리할 정적 파일 라우팅 목록이다. ㅇㅉㄹㄲ
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
    # PyInstaller 사용하는거
    if getattr(sys, "frozen", False):
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).resolve().parent))
    return Path(__file__).resolve().parent # runtime_dir 뭐 반환하는ㅇ거고 일다ㅏㄴ은


def read_local_payload(filename: str) -> bytes | None:
    # 번들 또는 현재 폴더에 포함된 파일을 우선 읽기
    target = runtime_dir() / filename
    if not target.exists():
        return None
    try:
        return target.read_bytes()
    except OSError:
        return None


# function - 0.1 = fuck
def live_cache_dir() -> Path:
    # 사용자별 캐시 폴더를 정해 디스크 캐시 위치를 만들기 이거 안했다가 10분 처먹음
    local_app_data = os.getenv("LOCALAPPDATA")
    if local_app_data:
        return Path(local_app_data) / APP_TITLE / LIVE_CACHE_DIRNAME
    return Path.home() / ".cache" / "school-random-program" / LIVE_CACHE_DIRNAME


def cache_file_path(filename: str) -> Path: # 파일별 디스크 캐시 경로를 계산
    return live_cache_dir() / filename


def fetch_remote_payload(filename: str) -> bytes:
    # GitHub raw에서 최신 정적 파일 내용을 받아오기 뭔가 이상하다 했다 
    remote_url = f"{RAW_BASE_URL}/{filename}"
    with urllib.request.urlopen(remote_url, timeout=REQUEST_TIMEOUT_SECONDS) as response:
        return response.read()

# 아니 야비쉬 PyQt6시부꺼 작명센스 개구리네 하지만 이거있어야 시작 실패 나춤 개같튼거
def import_qt_modules():
    from PyQt6.QtCore import QUrl
    from PyQt6.QtGui import QIcon, QKeySequence, QShortcut
    from PyQt6.QtWebEngineCore import QWebEngineProfile
    from PyQt6.QtWebEngineWidgets import QWebEngineView
    from PyQt6.QtWidgets import QApplication # QApplication은 어휴....인생....
    return QUrl, QIcon, QKeySequence, QShortcut, QWebEngineProfile, QWebEngineView, QApplication #에헤헤 리턴뒤에 이건 몰라?


def install_missing_requirements() -> bool:
    if getattr(sys, "frozen", False): # 개발 환경에서만 requirements.txt 기준 자동 설치를 시도 잠깐만..내가 이거 왜만들었지?
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


class LiveRepoHandler(BaseHTTPRequestHandler):     # 메모리 캐시, 디스크 캐시, 백그라운드 갱신 상태를 함께 관리하고 이딴건 모르니깐 검색
    cache: dict[str, bytes] = {}
    cache_lock = threading.Lock()
    refresh_in_flight: set[str] = set()
    # refresh_lock = threading.Lock()

    def log_message(self, fmt: str, *args: object) -> None:
        return

    def _send_payload(self, filename: str, payload: bytes, source: str) -> None:
        # 브라우저가 즉시 새 파일을 보도록 캐시를 막고 응답 본문을 전달
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", CONTENT_TYPES[filename])
        self.send_header("Cache-Control", "no-store")
       # self.send_header("Pragma", "no-cache")
       # self.send_header("Expires", "0")
        self.send_header("X-Source", source) # 아...약탈자 쉐리프 제발~~~뜨게해주세요
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    @classmethod
    def _read_disk_cache(cls, filename: str) -> bytes | None:
        #TODO - 이전 실행에서 저장한 디스크 캐시를 읽어오기
        target = cache_file_path(filename)
        if not target.exists():
            return None
        try:
            return target.read_bytes()
        except OSError:
            return None

    @classmethod
    def _write_disk_cache(cls, filename: str, payload: bytes) -> None:
        # 네트워크 실패 시 재사용할 수 있도록 디스크에도 캐시를 남기기 유지보구 떄문임 사실 별로 핋요없쓰
        #!SECTION - file 캐시 받기
        target = cache_file_path(filename)
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(payload)
        except OSError:
            return

    @classmethod
    def get_cached_payload(cls, filename: str) -> bytes | None:
        # 메모리 캐시를 먼저 보고, 없으면 디스크 캐시를 불러와 메모리에 올린다.
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
        # 최신 응답을 메모리와 디스크 양쪽에 저장한다.
        with cls.cache_lock:
            cls.cache[filename] = payload
        cls._write_disk_cache(filename, payload)

    @classmethod
    def _background_refresh(cls, filename: str) -> None:
        # 백그라운드 새로고침이 필요할 때 조용히 최신 파일로 교체한다.
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
        # 같은 파일에 대한 중복 새로고침 스레드 생성을 막는다.
        with cls.cache_lock:
            if filename in cls.refresh_in_flight:
                return
            cls.refresh_in_flight.add(filename)
        thread = threading.Thread(target=cls._background_refresh, args=(filename,), daemon=True)
        thread.start()

    @classmethod
    def warm_cache_from_disk(cls) -> None:
        # 서버 시작 전에 디스크 캐시를 메모리로 미리 올린다.
        for filename in sorted(set(ROUTES.values())):
            cls.get_cached_payload(filename)

    def do_GET(self) -> None:
        # 요청 경로를 파일명으로 바꾼 뒤 원격 fetch, 실패 시 캐시 fallback 순서로 처리한다.
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
    # OS가 비어 있다고 판단한 localhost 포트를 하나 고른다.
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def start_live_server() -> tuple[ThreadingHTTPServer, int]:
    # 로컬 웹 서버를 백그라운드 스레드로 띄우고 접속 포트를 반환한다.
    LiveRepoHandler.warm_cache_from_disk()
    port = pick_free_port()
    server = ThreadingHTTPServer(("127.0.0.1", port), LiveRepoHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, port


def default_download_dir() -> Path:
    # 기본 다운로드 위치는 사용자 Downloads 폴더를 우선 사용한다.
    downloads = Path.home() / "Downloads"
    if downloads.exists():
        return downloads
    return Path.home()


def pick_unique_download_target(directory: Path, filename: str) -> Path:
    # 같은 이름 파일이 이미 있으면 " (1)" 형식으로 충돌을 피한다. 이정도면 FM식으로 쓴거같다
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
    # PyQt import 실패 시 자동 설치를 시도하고, 성공하면 GUI를 시작
    #TODO - 아이콘 보이기
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
        # 로컬 서버 주소를 QWebEngineView에 로드해서 앱처럼 표시하는데 씨발 안돼는거 죽여버리고싶네
        app = QApplication(sys.argv)
        icon_path = runtime_dir() / APP_ICON_FILE
        app_icon = QIcon(str(icon_path)) if icon_path.exists() else QIcon()
        profile = QWebEngineProfile.defaultProfile()
        # 세션성 캐시/쿠키만 사용해 흔적을 최소화한다.
        profile.setHttpCacheType(QWebEngineProfile.HttpCacheType.MemoryHttpCache)
        profile.setPersistentCookiesPolicy(QWebEngineProfile.PersistentCookiesPolicy.NoPersistentCookies)

        def handle_download(download) -> None:
            # 브라우저 다운로드 요청을 사용자 Downloads 폴더로 연결
            #FIXME - 다운로드 요청안됨 고쳐야함
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
        # 핸들러가 GC로 사라지지 않도록 윈도우 객체에 참조를 유지한다.
        window._download_handler = handle_download
        if not app_icon.isNull():
            app.setWindowIcon(app_icon)
            window.setWindowIcon(app_icon)# 얘떄문이였어 얘를 안둔거였어 이!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        window.setWindowTitle(APP_TITLE)# EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE
        window.resize(WINDOW_WIDTH, WINDOW_HEIGHT)
        window.load(QUrl(app_url))
        def toggle_fullscreen() -> None:
            # F11 입력 시 전체화면과 일반 창 모드를 전환
            #TODO - 노말이랑 F11했을때의 Fullscreen만들기
            if window.isFullScreen():
                window.showNormal()
            else:
                window.showFullScreen()

        fullscreen_shortcut = QShortcut(QKeySequence("F11"), window)
        fullscreen_shortcut.activated.connect(toggle_fullscreen)
        # 단축키 객체도 수명 유지를 위해 윈도우에 붙여 둔다. 맥쓰실분들은 저리가쇼
        window._fullscreen_shortcut = fullscreen_shortcut
        window.show()

        return app.exec()
    finally:
        # 앱 종료 시 서버정리
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    raise SystemExit(main())
# 끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝끝 끝이다!!!!!!!!
