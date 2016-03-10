#!/bin/bash

BEACON="tcp://127.0.0.1:2996"
BROADCAST="tcp://127.0.0.1:2997"
CONTROLLER="tcp://127.0.0.1:2998"
LISTENER="tcp://127.0.0.1:2999"

REDIS="127.0.0.1:6379"

case "${1}" in
(bolo)
  echo "RUNNING BOLO"
  workdir=$PWD/$(mktemp -d .bolo.spin.XXXXXXX)
  trap "rm -rf ${workdir}" EXIT QUIT INT TERM

  mkdir ${workdir}/etc \
        ${workdir}/var
  cat <<EOF | tee ${workdir}/etc/bolo.conf
log info console

listener   ${LISTENER}
controller ${CONTROLLER}
broadcast  ${BROADCAST}
beacon     ${BEACON}

savefile  ${workdir}/var/save.db
keysfile  ${workdir}/var/keys.db

sweep 60

window @minutely 60
use @minutely
sample  m/./
counter m/./
rate    m/./
EOF
  echo ; echo ; echo "STARTING BOLO"
  ./bolo -Fc ${workdir}/etc/bolo.conf

  echo
  echo "BOLO EXITED.  Press enter to close this session."
  read JUNK
  ;;

(bolo2log)
  echo "RUNNING BOLO2LOG"
  ./bolo2log -Fv -e ${BROADCAST}

  echo
  echo "BOLO2LOG EXITED.  Press enter to close this session."
  read JUNK
  ;;

(dbolo)
  echo "RUNNING DBOLO"
  workdir=$PWD/$(mktemp -d .bolo.spin.XXXXXXX)
  trap "rm -rf ${workdir}" EXIT QUIT INT TERM

  mkdir ${workdir}/etc
  cat <<EOF | tee ${workdir}/etc/dbolo.conf
@5s ${workdir}/bin/dummy
EOF

  mkdir ${workdir}/bin
  cat >${workdir}/bin/dummy <<'EOF'
#!/bin/bash
d=$(date +%s)
echo "fact name=hostname $HOSTNAME"
echo "sample $d t=sample,mode=$MODE $RANDOM"
echo "tally $d t=tally,mode=$MODE 2"
echo "event $d subsys=sudo,user=root,host=box sudo: root logged in (maybe...)"
EOF
  chmod 0755 ${workdir}/bin/dummy

  echo ; echo ; echo "STARTING DBOLO"
  MODE=dev \
  ./dbolo -Fv -c ${workdir}/etc/dbolo.conf -e ${LISTENER}

  echo
  echo "DBOLO EXITED.  Press enter to close this session."
  read JUNK
  ;;

("")
  tmux new-session -s bolo-spin \; \
       new-window -n aggr  './spin bolo'       \; \
       new-window -n log   'sleep 4 && ./spin bolo2log'   \; \
       new-window -n agent 'sleep 4 && ./spin dbolo'      \; \
       attach
  ;;

(*)
  echo >&2 "USAGE: $0 [ACTION]"
  echo >&2 ""
  echo >&2 "Run components of a test/dev bolo setup, on 127.0.0.1"
  echo >&2 ""
  echo >&2 "Actions:"
  echo >&2 "  bolo        Run bolo core aggregator"
  echo >&2 "  bolo2log    Run a bolo2log subscriber"
  echo >&2 "  dbolo       Run the dbolo collector"
  exit 1
esac
