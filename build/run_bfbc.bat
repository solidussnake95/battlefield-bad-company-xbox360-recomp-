@echo off
cd /d "%~dp0"
bfbc.exe --game_data_root=..\game --gpu_plugin=xenos --allow_game_relative_writes
pause
