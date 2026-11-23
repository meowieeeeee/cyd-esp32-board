@echo off
rem Starts pc_monitor.py in the background (no console window).
rem Change --host to the board's IP if clawd.local does not resolve on your PC.
rem To start with Windows: Win+R -> shell:startup -> put a shortcut to this file there.
cd /d "%~dp0"
start "" pythonw pc_monitor.py --host 192.168.1.33
