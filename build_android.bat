@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Windows-to-Android cross compilation. No WSL or Visual Studio required.
set "SOURCE_DIR=%~dp0"
set "NDK_VERSION=25.1.8937393"
set "TASK_NDK="
set "TASK_ABI=arm64-v8a"
set "TASK_CONFIG=Release"
set "TASK_API=21"
set "TASK_JOBS=4"
set "TASK_BUILD="
set "TASK_INSTALL="
set "TASK_CMAKE="
set "TASK_NINJA="
set "TASK_SDK=%ANDROID_HOME%"
if not defined TASK_SDK set "TASK_SDK=%ANDROID_SDK_ROOT%"
if not defined TASK_SDK set "TASK_SDK=%LOCALAPPDATA%\Android\Sdk"

:parse
if "%~1"=="" goto configure_tools
if /i "%~1"=="--help" goto usage
if /i "%~1"=="/?" goto usage
if "%~2"=="" (echo ERROR: Missing value for %~1.& goto failed)
if /i "%~1"=="--ndk-path" (set "TASK_NDK=%~2"& goto next_arg)
if /i "%~1"=="--abi" (set "TASK_ABI=%~2"& goto next_arg)
if /i "%~1"=="--build-type" (set "TASK_CONFIG=%~2"& goto next_arg)
if /i "%~1"=="--api-level" (set "TASK_API=%~2"& goto next_arg)
if /i "%~1"=="--jobs" (set "TASK_JOBS=%~2"& goto next_arg)
if /i "%~1"=="--build-dir" (set "TASK_BUILD=%~2"& goto next_arg)
if /i "%~1"=="--install-dir" (set "TASK_INSTALL=%~2"& goto next_arg)
if /i "%~1"=="--cmake-path" (set "TASK_CMAKE=%~2"& goto next_arg)
if /i "%~1"=="--ninja-path" (set "TASK_NINJA=%~2"& goto next_arg)
echo ERROR: Unknown option: %~1
goto failed
:next_arg
shift
shift
goto parse

:configure_tools
if not defined TASK_NDK set "TASK_NDK=%TASK_SDK%\ndk\%NDK_VERSION%"
if not exist "%TASK_NDK%\source.properties" (
    echo ERROR: NDK r25b not found. Set ANDROID_HOME or pass --ndk-path.
    goto failed
)
for %%I in ("%TASK_NDK%") do set "TASK_NDK=%%~fI"
for %%I in ("%TASK_NDK%") do set "TASK_NDK_TAG=%%~nxI"
set "TASK_TOOLCHAIN=%TASK_NDK%\build\cmake\android.toolchain.cmake"
set "TASK_NDK_BIN=%TASK_NDK%\toolchains\llvm\prebuilt\windows-x86_64\bin"
for %%I in ("%TASK_TOOLCHAIN%" "%TASK_NDK_BIN%\clang++.exe" "%TASK_NDK_BIN%\llvm-nm.exe") do (
    if not exist "%%~I" (echo ERROR: Missing NDK file: %%~I& goto failed)
)
if not defined TASK_CMAKE for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined TASK_CMAKE set "TASK_CMAKE=%%I"
if not defined TASK_NINJA for /f "delims=" %%I in ('where ninja.exe 2^>nul') do if not defined TASK_NINJA set "TASK_NINJA=%%I"
rem Android SDK Manager's CMake package also supplies Ninja.
for /f "delims=" %%I in ('dir /b /ad /o-n "%TASK_SDK%\cmake" 2^>nul') do (
    if not defined TASK_CMAKE if exist "%TASK_SDK%\cmake\%%I\bin\cmake.exe" set "TASK_CMAKE=%TASK_SDK%\cmake\%%I\bin\cmake.exe"
    if not defined TASK_NINJA if exist "%TASK_SDK%\cmake\%%I\bin\ninja.exe" set "TASK_NINJA=%TASK_SDK%\cmake\%%I\bin\ninja.exe"
)
if not exist "%TASK_CMAKE%" (echo ERROR: cmake.exe not found. Use --cmake-path.& goto failed)
if not exist "%TASK_NINJA%" (echo ERROR: ninja.exe not found. Use --ninja-path.& goto failed)
set "TASK_VALID="
for %%I in (arm64-v8a armeabi-v7a x86_64 x86) do if "%TASK_ABI%"=="%%I" set "TASK_VALID=1"
if not defined TASK_VALID (echo ERROR: Unsupported ABI: %TASK_ABI%& goto failed)
set "TASK_VALID="
for %%I in (Release Debug RelWithDebInfo MinSizeRel) do if "%TASK_CONFIG%"=="%%I" set "TASK_VALID=1"
if not defined TASK_VALID (echo ERROR: Unsupported build type: %TASK_CONFIG%& goto failed)
for %%I in (libavutil.a libswresample.a libswscale.a libavcodec.a libavformat.a) do (
    if not exist "%SOURCE_DIR%libs\ffmpeg\lib\Android\%TASK_ABI%\%%I" (
        echo ERROR: Missing FFmpeg dependency for %TASK_ABI%: %%I
        goto failed
    )
)
rem Keep Windows caches separate from WSL, NDK versions, ABIs and configs.
rem Always configure incrementally. Never delete existing build directories.
if not defined TASK_BUILD set "TASK_BUILD=%SOURCE_DIR%build\android-windows\%TASK_NDK_TAG%\%TASK_ABI%\%TASK_CONFIG%"
if not defined TASK_INSTALL set "TASK_INSTALL=%SOURCE_DIR%install"
for %%I in ("%TASK_BUILD%") do set "TASK_BUILD=%%~fI"
for %%I in ("%TASK_INSTALL%") do set "TASK_INSTALL=%%~fI"
echo NDK: %TASK_NDK%
findstr /b /c:"Pkg.Revision" "%TASK_NDK%\source.properties"
echo Target: %TASK_ABI% / android-%TASK_API% / %TASK_CONFIG% / c++_static
echo Build: %TASK_BUILD%
echo Install: %TASK_INSTALL%

"%TASK_CMAKE%" -S "%SOURCE_DIR%." -B "%TASK_BUILD%" -G Ninja ^
    "-DCMAKE_MAKE_PROGRAM=%TASK_NINJA%" ^
    "-DCMAKE_TOOLCHAIN_FILE=%TASK_TOOLCHAIN%" ^
    "-DANDROID_ABI=%TASK_ABI%" "-DANDROID_PLATFORM=android-%TASK_API%" ^
    "-DCMAKE_BUILD_TYPE=%TASK_CONFIG%" "-DCMAKE_INSTALL_PREFIX=%TASK_INSTALL%" ^
    -DANDROID_STL=c++_static "-DANDROID_CPP_FEATURES=rtti exceptions"
if errorlevel 1 goto failed
"%TASK_CMAKE%" --build "%TASK_BUILD%" --parallel "%TASK_JOBS%"
if errorlevel 1 goto failed

rem Compare the dynamic exports with the C API allowlist before installing.
"%TASK_NDK_BIN%\llvm-nm.exe" -D --defined-only -j "%TASK_BUILD%\libvideoplayer.so" > "%TASK_BUILD%\exports.actual.txt"
if errorlevel 1 goto failed
type nul > "%TASK_BUILD%\exports.expected.txt"
for /f "tokens=1 delims=; " %%I in ('findstr /r /c:"^[ ][ ]*[A-Za-z_][A-Za-z_0-9]*;" "%SOURCE_DIR%videoplayer.exports.map"') do echo %%I>> "%TASK_BUILD%\exports.expected.txt"
for %%I in ("%TASK_BUILD%\exports.expected.txt") do if %%~zI==0 (echo ERROR: Empty C API export list.& goto failed)
sort "%TASK_BUILD%\exports.actual.txt" /o "%TASK_BUILD%\exports.actual.sorted.txt"
if errorlevel 1 goto failed
sort "%TASK_BUILD%\exports.expected.txt" /o "%TASK_BUILD%\exports.expected.sorted.txt"
if errorlevel 1 goto failed
fc /b "%TASK_BUILD%\exports.actual.sorted.txt" "%TASK_BUILD%\exports.expected.sorted.txt" >nul
if errorlevel 1 (
    echo ERROR: Unexpected or missing exports. See exports.*.txt in the build directory.
    goto failed
)
echo Verified: only the C API allowlist is exported.
"%TASK_CMAKE%" --install "%TASK_BUILD%" --config "%TASK_CONFIG%" --prefix "%TASK_INSTALL%"
if errorlevel 1 goto failed
fc /b "%TASK_BUILD%\libvideoplayer.so" "%TASK_INSTALL%\lib\Android\%TASK_ABI%\libvideoplayer.so" >nul
if errorlevel 1 (echo ERROR: Installed library differs from build output.& goto failed)
echo SUCCESS: %TASK_INSTALL%\lib\Android\%TASK_ABI%\libvideoplayer.so
goto succeeded

:usage
echo Usage: build_android.bat [options]
echo Defaults: NDK r25b, arm64-v8a, API 21, Release, static libc++.
echo   --ndk-path PATH       Windows NDK directory
echo   --abi ABI             Requires matching FFmpeg libraries
echo   --build-type TYPE     Release, Debug, RelWithDebInfo, MinSizeRel
echo   --api-level NUMBER    Android API level, default 21
echo   --jobs NUMBER         Parallel jobs, default 4
echo   --build-dir PATH      Separate directory from existing WSL builds
echo   --install-dir PATH    Default: project install directory
echo   --cmake-path PATH     Full path to cmake.exe
echo   --ninja-path PATH     Full path to ninja.exe
echo   --help                Show this help
goto succeeded

:failed
set "TASK_EXIT_CODE=1"
goto finish

:succeeded
set "TASK_EXIT_CODE=0"

:finish
rem Pause only for interactive local use; preserve the build result after PAUSE.
if /i "%GITHUB_ACTIONS%"=="true" goto return_result
if defined CODEX_THREAD_ID goto return_result
if defined CODEX_SESSION_ID goto return_result
if /i "%CODEX_CI%"=="1" goto return_result
if /i "%CODEX_CI%"=="true" goto return_result
pause
:return_result
exit /b %TASK_EXIT_CODE%