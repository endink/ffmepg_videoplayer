@echo on
chcp 65001

setlocal EnableDelayedExpansion

@rem 配置参数
set "WIN_SDK_VERSION=10.0.22621.0"
set "VC_VERSION=14.38"
set "BUILD_PLATFORM=x64"
set "BUILD_CONFIG=Release"

@rem 调试符号
set "width_pdb=1"

pushd "%~dp0"
set "vcvarsall_bat=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
set "source_path=%cd%"
set "build_directory=%source_path%\build"
set "install_dir=%source_path%\install"
set "build_or_not=y"

@rem 首先设置 VC 环境
echo Setting up VC toolchain...
IF EXIST "%vcvarsall_bat%" (
  call "%vcvarsall_bat%" amd64 %WIN_SDK_VERSION% -vcvars_ver=%VC_VERSION%
  if !errorlevel! neq 0 (
    echo Failed to set up VC environment!
    pause
    exit /b 1
  )
) ELSE (
  echo Error: vcvarsall.bat not found at %vcvarsall_bat%
  pause
  exit /b 1
)

@rem 处理构建目录
if EXIST "%build_directory%" (
    set /p BB="build folder existed, rebuild? y/n[n]"
    if /i "!BB!"=="y" (
        echo rebuild ...
        rd /s /q "%build_directory%"
    ) else (
        echo skip make
        set build_or_not=n
    )
)

@rem CMake 生成
if "!build_or_not!"=="y" (
    mkdir "%build_directory%" >nul 2>&1
    
    echo CMake generating ...
    
    cmake -S "%source_path%" -B "%build_directory%" ^
    -G "Visual Studio 17 2022" ^
    -A %BUILD_PLATFORM% ^
    -DWITH_PDB=%width_pdb% ^
    -DCMAKE_INSTALL_PREFIX="%build_directory%/install"
    
    if !errorlevel! neq 0 (
        echo CMake generation failed!
        pause
        exit /b 1
    )
)

@rem 构建和安装
echo Starting build...

cmake --build "%build_directory%" --config %BUILD_CONFIG%
if !errorlevel! neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

cmake --install "%build_directory%" --config %BUILD_CONFIG% --prefix %install_dir%
if !errorlevel! neq 0 (
    echo Install failed!
    pause
    exit /b 1
)

echo Build completed successfully!
popd
pause
exit