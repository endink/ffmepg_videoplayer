@echo on
chcp 65001

setlocal EnableDelayedExpansion

@rem Build configuration
set "WINDOWS_SDK_VERSION=10.0.22621.0"
set "MSVC_TOOLSET_FULL_VERSION=14.38.33130"
set "VS_INSTANCE_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "BUILD_PLATFORM=x64"
set "BUILD_CONFIG=Release"

@rem Generate debug symbols
set "width_pdb=1"

pushd "%~dp0"
set "source_path=%cd%"
set "build_directory=%source_path%\build"
set "install_dir=%source_path%\install"
set "build_or_not=y"

@rem Use only the explicitly configured VS installation. No auto-discovery.
set "vcvarsall_bat=%VS_INSTANCE_PATH%\VC\Auxiliary\Build\vcvarsall.bat"

@rem Initialize the Visual C++ environment
echo Setting up VC toolchain...
IF EXIST "%vcvarsall_bat%" (
  call "%vcvarsall_bat%" amd64 %WINDOWS_SDK_VERSION% -vcvars_ver=%MSVC_TOOLSET_FULL_VERSION%
  if !errorlevel! neq 0 (
    echo Failed to set up VC environment!
    pause
    exit /b 1
  )
) ELSE (
  echo Error: vcvarsall.bat not found at "%vcvarsall_bat%"
  pause
  exit /b 1
)

@rem Prepare the build directory
if EXIST "%build_directory%" (
    set /p BB="build folder existed, rebuild? y/n[n]"
    if /i "!BB!"=="y" (
        echo rebuild ...
        @rem Only remove this project's real build directory, never a junction.
        for %%I in ("%source_path%\build") do set "expected_build=%%~fI"
        if /i not "!expected_build!"=="%build_directory%" (popd& exit /b 1)
        for %%I in ("%build_directory%") do set "build_attrs=%%~aI"
        if not "!build_attrs:l=!"=="!build_attrs!" (
            echo Refusing to remove a linked build directory.
            pause
            popd
            exit /b 1
        )
        rd /s /q "%build_directory%"
        if exist "%build_directory%" (echo Could not remove build directory.& popd& exit /b 1)
    ) else (
        echo skip make
        set build_or_not=n
    )
)

@rem Always configure, including incremental builds, to enforce version choices.
@rem If an old cache has a different generator/toolset, rerun and choose y.
(
    mkdir "%build_directory%" >nul 2>&1
    
    echo CMake generating ...
    
    cmake -S "%source_path%" -B "%build_directory%" ^
    -G "Visual Studio 17 2022" ^
    -A %BUILD_PLATFORM% ^
    -T "v143,version=%MSVC_TOOLSET_FULL_VERSION%" ^
    "-DCMAKE_GENERATOR_INSTANCE=%VS_INSTANCE_PATH%" ^
    -DCMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=%WINDOWS_SDK_VERSION% ^
    -DCMAKE_SYSTEM_VERSION=%WINDOWS_SDK_VERSION% ^
    -DWITH_PDB=%width_pdb% ^
    -DAPP=1 ^
    -DCMAKE_INSTALL_PREFIX="%build_directory%/install"
    
    if !errorlevel! neq 0 (
        echo CMake generation failed!
        pause
        exit /b 1
    )
)

@rem Build and install
echo Starting build...

cmake --build "%build_directory%" --config %BUILD_CONFIG%
if !errorlevel! neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

cmake --install "%build_directory%" --config %BUILD_CONFIG% --prefix "%install_dir%"
if !errorlevel! neq 0 (
    echo Install failed!
    pause
    exit /b 1
)

echo Build completed successfully!
popd
pause
exit /b 0
