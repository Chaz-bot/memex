@ECHO OFF
REM Test batch for bare MS-DOS 6.22 / LFN-disabled DOSBox-X.
REM Run memex.exe --smoke-test and --persistence-test.
REM Requires the FAT build of memex.exe (make -f Makefile.dj fat).
REM
REM Usage from DOS prompt with repo on C: and scratch dirs on S: and P:
REM   RUNTEST
REM
REM DOSBox-X dos622-test.conf calls this automatically if you prefer
REM to drive testing from the Linux host.

ECHO ========================================
ECHO memex.exe FAT smoke test
ECHO ========================================
memex.exe --smoke-test S:\

ECHO ========================================
ECHO memex.exe FAT persistence test
ECHO ========================================
memex.exe --persistence-test P:\
