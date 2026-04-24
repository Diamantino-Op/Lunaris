#!/bin/bash
cd /mnt/d/Lunaris/LunarisOS || exit 1
make all > /tmp/lunaris_build.log 2>&1
status=$?
if [ "$status" -eq 0 ]; then
  echo BUILD_SUCCESS
else
  echo BUILD_FAILED
fi
grep -n -m 1 -E 'error:|undefined reference|ld:|collect2:|fatal error' /tmp/lunaris_build.log
exit $status