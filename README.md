# School Random Program

Windows용 `School Random Program` 실행 파일을 빌드하는 프로젝트입니다.

## 요구 사항

- Windows
- PowerShell
- .NET SDK 8.0 이상
- C/C++ 컴파일러 중 하나
  - 권장: Visual Studio 2022 Build Tools + `Desktop development with C++`
  - 또는 LLVM `clang.exe`
  - 또는 MinGW-w64 `gcc.exe`

Visual Studio Build Tools가 없다면 PowerShell에서 아래 명령으로 설치할 수 있습니다.

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools -e
```

설치 화면에서 `Desktop development with C++` 워크로드를 선택한 뒤 PowerShell을 새로 열어 주세요.

## 빌드 방법

프로젝트 폴더에서 아래 명령을 실행합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

성공하면 프로젝트 루트에 다음 파일이 생성됩니다.

```text
SchoolRandomProgram.exe
```

다른 이름으로 출력하려면 `-Output` 옵션을 사용합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Output MyProgram.exe
```

## 빌드 방식

`build.ps1`은 다음 순서로 빌드합니다.

1. `school_random_program_webview.cpp`가 있으면 WebView2 버전으로 빌드합니다.
2. WebView2 SDK 패키지가 없으면 `dotnet restore`로 `packages` 폴더에 복원합니다.
3. 사용 가능한 컴파일러를 자동으로 찾습니다.
   - `cl.exe`
   - Visual Studio `VsDevCmd.bat` 경유 `cl.exe`
   - `clang.exe`
   - `gcc.exe`
4. 실행 파일을 루트 폴더에 생성합니다.

`school_random_program_webview.cpp`가 없으면 `school_random_program.c`를 대신 빌드합니다.

## 실행

빌드 후 PowerShell 또는 탐색기에서 실행합니다.

```powershell
.\SchoolRandomProgram.exe
```

프로그램은 실행 중 GitHub 원격 파일을 가져와 로컬 캐시에 사용합니다. 인터넷 연결이 필요할 수 있습니다.

## 주의

- `webview2_restore.csproj`는 WebView2 NuGet 패키지 복원용 파일입니다.
- 일반 빌드는 `dotnet build`가 아니라 반드시 `build.ps1`로 실행하세요.
- `packages`, `bin`, `obj`, `*.obj`, `*.exe`는 빌드 산출물입니다.

## 문제 해결

### `No C compiler was found.`

C/C++ 컴파일러가 설치되어 있지 않거나 PATH에서 찾을 수 없는 상태입니다.

Visual Studio Build Tools를 설치하고 `Desktop development with C++` 워크로드를 선택한 뒤 PowerShell을 새로 열어 다시 빌드하세요.

### `dotnet` 명령을 찾을 수 없음

.NET SDK 8.0 이상을 설치한 뒤 PowerShell을 새로 열어 다시 실행하세요.

### WebView2 관련 헤더나 라이브러리를 찾을 수 없음

아래 명령으로 패키지를 다시 복원한 뒤 빌드하세요.

```powershell
dotnet restore .\webview2_restore.csproj --packages .\packages
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

### PowerShell 실행 정책 오류

아래처럼 `-ExecutionPolicy Bypass`를 붙여 실행합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```