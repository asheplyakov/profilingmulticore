#!/bin/bash
set -e
MYDIR="${0%/*}"
cd "$MYDIR"
tool="${1:-falsesharing}"
repetitions=${2:-10000000}

if [ -e /proc/self/cpuset ]; then
	if [ "$(cat /proc/self/cpuset)" != '/' ]; then
		/bin/echo $$ > /dev/cpuset/tasks
		taskset -p 0xffffffff $$ >/dev/null
	fi
fi

tCount=1
tmax=`nproc`

outfile="${tool}_timing.csv"
echo '#tCount;repetitions;elapsed;user;system' > "${outfile}"
while [ "$tCount" -le $tmax ]; do
	/usr/bin/time -f "$tCount;${repetitions};%e;%U;%S" -a -o "$outfile" -- "./bin/${tool}" -t $tCount -r ${repetitions}
	tCount=$((tCount*2))
done

./scripts/mkplot.py -t "${repetitions} fetch_add timing" -o "${tool}_timing.png" "${outfile}"

if [ -n "$DISPLAY" ]; then
	display "${tool}_timing.png"
fi
