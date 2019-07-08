#!/bin/bash
set -e
MYDIR="${0%/*}"
cd "$MYDIR"
tool="${1:-falsesharing}"
repetitions=${2:-10000000}

count_upto() {
	local i=1
	local to=$1
	while [ $i -lt $to ]; do
		echo $i
		i=$((i*2))
		if [ $i -ge $to ]; then
			echo $to
		fi
	done
}

if [ -e /proc/self/cpuset ]; then
	if [ "$(cat /proc/self/cpuset)" != '/' ]; then
		/bin/echo $$ > /dev/cpuset/tasks
		taskset -p 0xffffffff $$ >/dev/null
	fi
fi

run_benchmark() {
	local tool="$1"
	local outfile="$2"
	echo '#tCount;repetitions;elapsed;user;system' > "${outfile}"
	for tCount in $(count_upto `nproc`); do
		/usr/bin/time -f "$tCount;${repetitions};%e;%U;%S" -a -o "$outfile" -- "./bin/${tool}" -t $tCount -r ${repetitions}
	done
}

tool='falsesharing'
outfile="${tool}_timing.csv"
outfiles="${outfile}"
run_benchmark "$tool" "$outfile"

if [ -n "$SHOW_FIX" ]; then
	tool='nomorefalsesharing'
	outfile="${tool}_timing.csv"
	outfiles="${outfiles} ${outfile}"
	run_benchmark "$tool" "$outfile"
fi

./scripts/mkplot.py -t "${repetitions} fetch_add timing" -o "${tool}_timing.png" ${outfiles}

if [ -n "$SHOW_NOW" -a -n "$DISPLAY" ]; then
	display "${tool}_timing.png"
fi
