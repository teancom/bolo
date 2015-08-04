#!/bin/bash

BOLO_DAEMON_PID=
BOLO_CLIENT_PID=
TMPROOT=
trap "teardown" INT TERM QUIT EXIT


teardown() {
	[ -d $TMPROOT ] || return

	echo "tearing down..."
	if [ -n $BOLO_DAEMON_PID ]; then
		echo "terminating bolo daemon (pid $BOLO_DAEMON_PID)"
		kill -INT $BOLO_DAEMON_PID;
		sleep 1
	fi

	if [ -n $BOLO_CLIENT_PID ]; then
		echo "terminating bolo client (pid $BOLO_CLIENT_PID)"
		kill -INT $BOLO_CLIENT_PID;
		sleep 1
	fi

	echo
	ls -l $TMPROOT
	echo

	echo "====[ stat_bolo : stdout ]=========================================="
	cat $TMPROOT/stat_bolo.out
	echo "====[ stat_bolo : stderr ]=========================================="
	cat $TMPROOT/stat_bolo.err

	echo "====[ bolo : stdout ]==============================================="
	cat $TMPROOT/bolo.out | sed -e "s/\\[$BOLO_DAEMON_PID\\]/[-pid-]/"
	echo "====[ bolo : stderr ]==============================================="
	cat $TMPROOT/bolo.err | sed -e "s/\\[$BOLO_DAEMON_PID\\]/[-pid-]/"

	echo "====[ save.db (hex) ]==============================================="
	xxd $TMPROOT/save.db
	echo "===================================================================="

	rm -rf ${TMPROOT:?TMPROOT not set!}
	exit
}

setup() {
	[ -z $TMPROOT] && TMPROOT=$(mktemp -d)

	cat >$TMPROOT/bolo.cfg <<EOF
listener   tcp://*:9990
controller tcp://127.0.0.1:9991
broadcast  tcp://*:9992

log info console
savefile $TMPROOT/save.db

type :check {
  freshness 10
  critical "no results in 10s"
}

use :check
state state1.test

counter 5 counter-5s
sample  5 sample-5s
EOF
}

bolo_daemon() {
	./bolo -F -c $TMPROOT/bolo.cfg >$TMPROOT/bolo.out 2>$TMPROOT/bolo.err &
	BOLO_DAEMON_PID=${!:?No daemon PID detected!}
	sleep 1
	echo "bolo daemon running (pid $BOLO_DAEMON_PID)"
}

bolo_client() {
	./stat_bolo -v -l -e tcp://127.0.0.1:9992 >$TMPROOT/stat_bolo.out 2>$TMPROOT/stat_bolo.err &
	BOLO_CLIENT_PID=${!:?No client PID detected!}
	sleep 1
	echo "bolo client running (pid $BOLO_CLIENT_PID)"
}

setup
bolo_client
bolo_daemon

for X in $(seq 1 5); do
	echo "sending round $X of updates"
	./send_bolo -e tcp://127.0.0.1:9990 -t state state1.test 0 everything is awesome
	./send_bolo -e tcp://127.0.0.1:9990 -t counter counter-5s
	./send_bolo -e tcp://127.0.0.1:9990 -t sample sample-5s 10.4 11.2 18.7 10.8 10.9 10.7 14.2
	sleep 1
done
