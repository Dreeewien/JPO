@echo off
cd /d %~dp0

start /B main.exe server

timeout /t 2 /nobreak > nul

start http://localhost:8080/index.html