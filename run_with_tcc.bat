@echo off

ECHO:
ECHO ERROR: The TCC build path is deprecated and unsupported.
ECHO It omits recursive randomizer sources and cannot compile the C++ settings UI.
ECHO Build Zelda3.sln with Visual Studio/MSBuild instead; see README.md.
ECHO:
PAUSE
EXIT /B 1
