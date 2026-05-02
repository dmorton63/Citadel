@echo off
REM ============================================================================
REM BUILD_STUBS.bat - Build and test QCSQL service stubs on Windows
REM For Citadel OS porting - Option A, Step 1
REM ============================================================================

echo.
echo ╔══════════════════════════════════════════════════╗
echo ║  QCSQL Service Stub Builder                     ║
echo ║  Option A: Quick Testing - Step 1               ║
echo ╚══════════════════════════════════════════════════╝
echo.

REM Check for Visual Studio compiler
where cl.exe >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Visual Studio compiler (cl.exe) not found!
    echo Please run this from Visual Studio Developer Command Prompt
    echo or Developer PowerShell for VS 2026.
    echo.
    pause
    exit /b 1
)

echo [INFO] Found Visual Studio compiler
echo.

REM Create build directory
if not exist "build_stubs" mkdir build_stubs
cd build_stubs

echo ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
echo  Step 1: Compiling QCSQLService.cpp
echo ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
cl /c /std:c++17 /EHsc /W3 /nologo /I.. ..\QCSQLService.cpp
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to compile QCSQLService.cpp
    cd ..
    pause
    exit /b 1
)
echo [SUCCESS] QCSQLService.cpp compiled
echo.

echo ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
echo  Step 2: Compiling QCSQLServiceTest.cpp
echo ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
cl /c /std:c++17 /EHsc /W3 /nologo /I.. ..\QCSQLServiceTest.cpp
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to compile QCSQLServiceTest.cpp
    cd ..
    pause
    exit /b 1
)
echo [SUCCESS] QCSQLServiceTest.cpp compiled
echo.

echo ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
echo  Step 3: Linking test executable
echo ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
cl /Fe:qcsql_test.exe /nologo QCSQLService.obj QCSQLServiceTest.obj
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to link executable
    cd ..
    pause
    exit /b 1
)
echo [SUCCESS] Linked qcsql_test.exe
echo.

echo ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
echo  Step 4: Running tests
echo ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
qcsql_test.exe
if %ERRORLEVEL% NEQ 0 (
    echo [WARNING] Tests completed with errors
) else (
    echo [SUCCESS] All tests completed
)
echo.

cd ..

echo ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
echo  Build Summary
echo ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
echo  Compiled: QCSQLService.cpp, QCSQLServiceTest.cpp
echo  Linked:   build_stubs\qcsql_test.exe
echo  Tests:    Executed
echo.
echo  Note: All functionality is STUB mode
echo        Actual CQL engine not connected yet
echo ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
echo.

echo [INFO] Build artifacts in build_stubs\ directory
echo [INFO] Next step: Port Windows CQL engine (Phase 2)
echo.

pause
