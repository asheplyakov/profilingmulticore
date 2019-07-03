#!/bin/bash
set -e
MYDIR="${0%/*}"
cd "$MYDIR"
tool="${1:-falsesharing}"

if [ "$(cat /proc/self/cpuset)" != '/' ]; then
	/bin/echo $$ > /dec/cpuset/tasks
fi

tCount=1
tmax=`nproc`

while [ "$tCount" -le $tmax ]; do
	/usr/bin/time -f "$tCount;%e;%U;%S" -- "./bin/${tool}" -t $tCount
	tCount=$((tCount*2))
done

