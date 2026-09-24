@echo off
REM ============================================================================
REM DEPRECATED as primary path: the vcpkg route needs Visual Studio on this
REM machine ("Could not locate a complete Visual Studio instance") and vcpkg also
REM requires the proxy value WITHOUT a scheme. We now build the arm64 deps with
REM the NDK directly - see build_deps_arm64.ps1 (writes _deps_arm64.log).
REM This wrapper is kept for convenience.
REM ============================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_deps_arm64.ps1"
