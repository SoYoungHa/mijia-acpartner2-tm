@echo off
REM 使用 PowerShell 执行编译脚本
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0compile.ps1"
pause
