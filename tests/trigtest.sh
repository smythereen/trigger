#!/bin/bash
#
#	Copyright (c) 2026 John R. Smythe Jr.
#
#	Trigger Unit Tests
#
#	Usage:	tests/trigtest.sh	(run from the trigger unit directory)
#
#	$Revision$
#
#	$Id$
#

U=$( CDPATH= cd $( dirname $0 )/.. && pwd )
CC=${CC:-gcc}
TRIGGER=${TRIGGER:-$U/trigger}
TRIGMEM=${TRIGMEM:-$U/trigmem}
T=$( mktemp -d /tmp/trigtest.XXXXXX )
M=$T/test.trigmem
LOG=$T/run.log
PASS=0
FAIL=0
SPID=

cleanup( ) {
	[ -n "$SPID" ] && kill $SPID 2>/dev/null
	wait 2>/dev/null
	[ "$FAIL" = 0 ] && rm -rf $T || echo "	kept $T"
}
trap cleanup EXIT

ok( ) {
	PASS=$(( PASS + 1 ))
	echo "	PASS	$*"
}
no( ) {
	FAIL=$(( FAIL + 1 ))
	echo "	FAIL	$*"
}
check( ) {			# check "name" command...
	local n="$1"; shift
	if "$@" ; then ok "$n" ; else no "$n" ; fi
}
waitfor( ) {			# waitfor file pattern [ seconds ]
	local i
	for (( i = 0; i < ${3:-5}0; i++ )) ; do
		grep -q -- "$2" "$1" 2>/dev/null && return 0
		sleep 0.1
	done
	return 1
}
waitprint( ) {			# waitprint pattern [ seconds ]: poll trigmem print
	local i
	for (( i = 0; i < ${2:-5}0; i++ )) ; do
		$TRIGMEM print $M 2>/dev/null | grep -q -- "$1" && return 0
		sleep 0.1
	done
	return 1
}
count( ) {
	grep -c -- "$2" "$1" 2>/dev/null || true
}
settle( ) {
	sleep ${1:-0.5}
}

echo "Trigger unit tests in $T"
mkdir -p $T/w $T/tree $T/b $T/gone
cat > $T/logger.sh <<EOS
#!/bin/sh
echo "\$TRIG_ENTRY \$TRIG_EVENTS \$1" >> $LOG
exit 0
EOS
chmod +x $T/logger.sh

#
#	Library: Event Names
#

$CC -g -I$HOME/cmn -I$U -o $T/trigcb $U/tests/trigcb.c $U/../lib/libtrigger.a $HOME/lib/libtrigger.a $HOME/lib/libsys.a 2>$T/cc.err ||
$CC -g -I$HOME/cmn -I$U -o $T/trigcb $U/tests/trigcb.c $HOME/lib/libtrigger.a $HOME/lib/libsys.a 2>$T/cc.err
check "compile library test program" test -x $T/trigcb

#
#	.trigmem File Build
#

$TRIGMEM create $M 4 50 > /dev/null
check "trigmem create" test -s $M
$TRIGMEM add $M $T/w changes s 0 "$T/logger.sh \"\$1\"" > /dev/null
$TRIGMEM add $M $T/tree changes,attrib rs 0 "$T/logger.sh \"\$1\"" > /dev/null
cat > $T/entries.txt <<EOS
# path		events	flags	debounce	program
$T/b		write	s	400		$T/logger.sh "\$1"
$T/gone		create	-			$T/logger.sh
EOS
$TRIGMEM load $M $T/entries.txt > /dev/null
check "trigmem load text file" test "$( $TRIGMEM print $M | grep -c '^\[' )" = 4
check "header TraceLevel field present" grep -q TraceLevel= <( $TRIGMEM print $M )
$TRIGMEM print $M | grep -q 'TraceLevel=0x00000000' || no "initial TraceLevel 0"

#
#	Server Start
#

cd $T
$TRIGGER $M $T/trigger.trc &
SPID=$!
$TRIGMEM wait $M running 10
check "server running, pid in header" grep -q "pid=$SPID" <( $TRIGMEM print $M )
check "Trace(0) startup line" waitfor $T/trigger.trc "serving $M"

#
#	Create / Write / Move / Delete
#

echo hello > $T/w/a.txt
check "create+write event runs program with path" waitfor $LOG "0 .*create.*$T/w/a.txt"
mv $T/w/a.txt $T/w/b.txt
check "move event (moved_to) path" waitfor $LOG "0 .*moved_to.* $T/w/b.txt"
check "move event (moved_from) path" waitfor $LOG "0 .*moved_from.* $T/w/a.txt"
rm $T/w/b.txt
check "delete event path" waitfor $LOG "0 .*delete $T/w/b.txt"
check "child exit status recorded" waitprint "status=exit 0"

#
#	Recursive Tree: New Subdirectories Are Watched
#

mkdir -p $T/tree/x/y
settle 0.3
echo deep > $T/tree/x/y/deep.c
check "recursive: new subdir create event" waitfor $LOG "1 create,isdir $T/tree/x"
check "recursive: file in new sub-subdir" waitfor $LOG "1 .*write.* $T/tree/x/y/deep.c"
mkdir -p $T/tree/p/q/r && echo fast > $T/tree/p/q/r/fast.txt
check "recursive: file created with its dirs (race scan)" waitfor $LOG "1 .* $T/tree/p/q/r/fast.txt"
mv $T/tree/x $T/x.out
settle
echo after > $T/x.out/y/after.c
settle
check "recursive: moved-out subtree no longer watched" test "$( count $LOG x.out )" = 0
mv $T/x.out $T/tree/z
settle 0.3
echo back > $T/tree/z/y/back.c
check "recursive: moved-in subtree watched" waitfor $LOG "1 .* $T/tree/z/y/back.c"
touch -d '2001-01-01' $T/tree/p/q/r/fast.txt
check "recursive: attrib event" waitfor $LOG "1 .*attrib $T/tree/p/q/r/fast.txt"

#
#	Debounce: 10 Quick Writes = 1 Run
#

for i in 1 2 3 4 5 6 7 8 9 10 ; do echo $i >> $T/b/burst.txt ; sleep 0.02 ; done
settle 1
check "debounce: 10 writes -> 1 run" test "$( count $LOG burst.txt )" = 1

#
#	Live Trace Level Via The File
#

echo one > $T/w/t1
waitfor $LOG "$T/w/t1" > /dev/null
check "trace off: no event trace" test "$( count $T/trigger.trc 'event.*t1' )" = 0
$TRIGMEM trace $M 0x0000000F > /dev/null
echo two > $T/w/t2
waitfor $LOG "$T/w/t2" > /dev/null
check "trace on (live write): event traced" waitfor $T/trigger.trc "Entry 0: event .*$T/w/t2"
check "trace on: spawn traced" waitfor $T/trigger.trc "Entry 0: spawned pid="
check "trace on: child exit traced" waitfor $T/trigger.trc "Child pid=.* exit=0"
check "lib trace off (TRIG_TRC_LVL clear)" test "$( count $T/trigger.trc 'Trig: watch wd' )" = 0
$TRIGMEM trace $M 0x2000001F > /dev/null
echo three > $T/w/t3
check "lib trace on (0x20000000): library traced" waitfor $T/trigger.trc "Trig: fire tag=0 .*$T/w/t3"
$TRIGMEM trace $M 0x10 > /dev/null

#
#	Control Word: Pause / Resume / Reload
#

$TRIGMEM ctl $M pause
settle
echo p > $T/w/paused.txt
settle 1
check "pause: no program run" test "$( count $LOG paused.txt )" = 0
check "pause: state paused" waitprint "state=paused"
$TRIGMEM ctl $M resume
settle
echo r > $T/w/resumed.txt
check "resume: program runs" waitfor $LOG "resumed.txt"

mkdir -p $T/new
GEN=$( $TRIGMEM print $M | sed -n 's/.*generation=\([0-9]*\).*/\1/p' )
printf '#!/bin/sh\necho direct "$@" >> %s\n' $LOG > $T/args.sh ; chmod +x $T/args.sh
$TRIGMEM add $M $T/new create - 0 $T/args.sh A B > /dev/null
check "trigmem add auto-reload" waitfor $T/trigger.trc "Loaded generation $(( GEN + 1 ))"
echo n > $T/new/n.txt
check "reloaded entry (direct exec, args + path)" waitfor $LOG "direct A B $T/new/n.txt"
$TRIGMEM add $M $T/new delete s 0 "exit 3" > /dev/null
settle
rm $T/new/n.txt
check "grown file: entry 5 past max 4" waitprint "max=8"
check "child exit status 3 recorded" waitprint "status=exit 3"
$TRIGMEM del $M 5 > /dev/null
check "trigmem del" test "$( $TRIGMEM print $M | grep -c '^\[' )" = 5

#
#	Multiple Entries On One Event
#

$TRIGMEM add $M $T/w create s 0 "echo second \$1 >> $LOG" > /dev/null
settle
echo m > $T/w/multi.txt
check "multiple entries: first" waitfor $LOG "0 .*$T/w/multi.txt"
check "multiple entries: second" waitfor $LOG "second $T/w/multi.txt"

#
#	Watched Directory Deleted, Then Recreated
#

rmdir $T/gone
check "watched dir deleted: reported" waitfor $T/trigger.trc "watched root $T/gone deleted"
check "watched dir deleted: GONE flag" waitprint "GONE"
mkdir $T/gone
settle 1.5
echo g > $T/gone/g.txt
check "watched dir recreated: watched again" waitfor $LOG "3 create $T/gone/g.txt"

#
#	Library Function Callbacks
#

mkdir -p $T/lib/sub
$T/trigcb fork $T/lib $T/cb.log 3 > $T/cb.out &
CB=$!
settle 0.5
echo f > $T/lib/sub/forked.txt
mkdir $T/lib/new
settle 0.3
echo f > $T/lib/new/inner.txt
wait $CB
check "Trig_Fork_Watch: callback with path" grep -q "cb tag=0 .* $T/lib/sub/forked.txt" $T/cb.log
check "Trig_Fork_Watch: recursive new dir" grep -q "cb tag=0 .* $T/lib/new/inner.txt" $T/cb.log
check "Trig_Stop_Watch: clean stop" grep -q "stopped status=0" $T/cb.out
$T/trigcb poll $T/lib $T/poll.log 2 > /dev/null &
CB=$!
settle 0.5
rm $T/lib/sub/forked.txt
wait $CB
check "Trig_Poll: callback with tag" grep -q "cb tag=7 delete $T/lib/sub/forked.txt" $T/poll.log

#
#	Stop Via Control Word
#

$TRIGMEM ctl $M stop
$TRIGMEM wait $M stopped 10
check "stop: server exited" bash -c "! kill -0 $SPID 2>/dev/null"
wait $SPID
check "stop: exit code 0" test $? = 0
SPID=
check "stop: header state stopped, pid 0" waitprint "state=stopped pid=0"
check "stop: trace line" grep -q "stopping" $T/trigger.trc
$TRIGGER $T/nope.trigmem 2>/dev/null
check "bad file rejected" test $? = 2

echo
echo "Trigger tests: $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
