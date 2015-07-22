#!/bin/bash

ts=$(date +%s)
echo "SAMPLE $ts box01.lab.example.com:memory:total 8249303040"
echo "SAMPLE $ts box01.lab.example.com:memory:used 712564736"
echo "SAMPLE $ts box01.lab.example.com:memory:free 6682619904"
echo "SAMPLE $ts box01.lab.example.com:memory:buffers 155226112"
echo "SAMPLE $ts box01.lab.example.com:memory:cached 590557184"
echo "SAMPLE $ts box01.lab.example.com:memory:slab 108335104"
echo "SAMPLE $ts box01.lab.example.com:swap:total 3221221376"
echo "SAMPLE $ts box01.lab.example.com:swap:cached 12238848"
echo "SAMPLE $ts box01.lab.example.com:swap:used 44666880"
echo "SAMPLE $ts box01.lab.example.com:swap:free 3164315648"
echo "SAMPLE $ts box01.lab.example.com:load:1min 0.22"
echo "SAMPLE $ts box01.lab.example.com:load:5min 0.23"
echo "SAMPLE $ts box01.lab.example.com:load:15min 0.16"
echo "SAMPLE $ts box01.lab.example.com:load:runnable 0"
echo "SAMPLE $ts box01.lab.example.com:load:schedulable 182"
echo "RATE $ts box01.lab.example.com:cpu:user 26877096"
echo "RATE $ts box01.lab.example.com:cpu:nice 17208"
echo "RATE $ts box01.lab.example.com:cpu:system 10878683"
echo "RATE $ts box01.lab.example.com:cpu:idle 1075974441"
echo "RATE $ts box01.lab.example.com:cpu:iowait 3503984"
echo "RATE $ts box01.lab.example.com:cpu:irq 131"
echo "RATE $ts box01.lab.example.com:cpu:softirq 239272"
echo "RATE $ts box01.lab.example.com:cpu:steal 5916793"
echo "RATE $ts box01.lab.example.com:cpu:guest 0"
echo "RATE $ts box01.lab.example.com:cpu:guest-nice "
echo "RATE $ts box01.lab.example.com:ctxt:cswch-s 2759837386"
echo "RATE $ts box01.lab.example.com:ctxt:forks-s 34125655"
echo "SAMPLE $ts box01.lab.example.com:load:cpus 2"
echo "SAMPLE $ts box01.lab.example.com:procs:running 1"
echo "SAMPLE $ts box01.lab.example.com:procs:sleeping 137"
echo "SAMPLE $ts box01.lab.example.com:procs:blocked 0"
echo "SAMPLE $ts box01.lab.example.com:procs:zombies 0"
echo "SAMPLE $ts box01.lab.example.com:procs:stopped 0"
echo "SAMPLE $ts box01.lab.example.com:procs:paging 0"
echo "SAMPLE $ts box01.lab.example.com:procs:unknown 0"
echo "SAMPLE $ts box01.lab.example.com:openfiles:used 1504"
echo "SAMPLE $ts box01.lab.example.com:openfiles:free 0"
echo "SAMPLE $ts box01.lab.example.com:openfiles:max 797776"
echo "KEY box01.lab.example.com:fs:/ rootfs"
echo "KEY box01.lab.example.com:dev:rootfs /"
echo "SAMPLE $ts box01.lab.example.com:df:/:inodes.total 1766016"
echo "SAMPLE $ts box01.lab.example.com:df:/:inodes.free 1677536"
echo "SAMPLE $ts box01.lab.example.com:df:/:inodes.rfree 0"
echo "SAMPLE $ts box01.lab.example.com:df:/:bytes.total 28296900608"
echo "SAMPLE $ts box01.lab.example.com:df:/:bytes.free 22760669184"
echo "SAMPLE $ts box01.lab.example.com:df:/:bytes.rfree 1444253696"
echo "KEY box01.lab.example.com:fs:/boot /dev/vda1"
echo "KEY box01.lab.example.com:dev:/dev/vda1 /boot"
echo "SAMPLE $ts box01.lab.example.com:df:/boot:inodes.total 25688"
echo "SAMPLE $ts box01.lab.example.com:df:/boot:inodes.free 25644"
echo "SAMPLE $ts box01.lab.example.com:df:/boot:inodes.rfree 0"
echo "SAMPLE $ts box01.lab.example.com:df:/boot:bytes.total 97335296"
echo "SAMPLE $ts box01.lab.example.com:df:/boot:bytes.free 37415936"
echo "SAMPLE $ts box01.lab.example.com:df:/boot:bytes.rfree 5242880"
echo "RATE $ts box01.lab.example.com:vm:pgpgin 39270944"
echo "RATE $ts box01.lab.example.com:vm:pgpgout 273149128"
echo "RATE $ts box01.lab.example.com:vm:pswpin 17546"
echo "RATE $ts box01.lab.example.com:vm:pswpout 249844"
echo "RATE $ts box01.lab.example.com:vm:pgfree 7250566841"
echo "RATE $ts box01.lab.example.com:vm:pgfault 20045393491"
echo "RATE $ts box01.lab.example.com:vm:pgmajfault 222180"
echo "RATE $ts box01.lab.example.com:vm:pgsteal 10866153"
echo "RATE $ts box01.lab.example.com:vm:pgscan.kswapd 69670138"
echo "RATE $ts box01.lab.example.com:vm:pgscan.direct 6361184"
echo "RATE $ts box01.lab.example.com:diskio:sr0:rd-iops 0"
echo "RATE $ts box01.lab.example.com:diskio:sr0:rd-miops 0"
echo "RATE $ts box01.lab.example.com:diskio:sr0:rd-msec 0"
echo "RATE $ts box01.lab.example.com:diskio:sr0:rd-bytes 0"
echo "RATE $ts box01.lab.example.com:diskio:sr0:wr-iops 0"
echo "RATE $ts box01.lab.example.com:diskio:sr0:wr-miops 0"
echo "RATE $ts box01.lab.example.com:diskio:sr0:wr-msec 0"
echo "RATE $ts box01.lab.example.com:diskio:sr0:wr-bytes 0"
echo "RATE $ts box01.lab.example.com:diskio:sr1:rd-iops 9"
echo "RATE $ts box01.lab.example.com:diskio:sr1:rd-miops 9"
echo "RATE $ts box01.lab.example.com:diskio:sr1:rd-msec 72"
echo "RATE $ts box01.lab.example.com:diskio:sr1:rd-bytes 512"
echo "RATE $ts box01.lab.example.com:diskio:sr1:wr-iops 0"
echo "RATE $ts box01.lab.example.com:diskio:sr1:wr-miops 0"
echo "RATE $ts box01.lab.example.com:diskio:sr1:wr-msec 0"
echo "RATE $ts box01.lab.example.com:diskio:sr1:wr-bytes 0"
echo "RATE $ts box01.lab.example.com:diskio:vda:rd-iops 1273712"
echo "RATE $ts box01.lab.example.com:diskio:vda:rd-miops 118357"
echo "RATE $ts box01.lab.example.com:diskio:vda:rd-msec 74942456"
echo "RATE $ts box01.lab.example.com:diskio:vda:rd-bytes 3726522368"
echo "RATE $ts box01.lab.example.com:diskio:vda:wr-iops 25720489"
echo "RATE $ts box01.lab.example.com:diskio:vda:wr-miops 46672131"
echo "RATE $ts box01.lab.example.com:diskio:vda:wr-msec 546298256"
echo "RATE $ts box01.lab.example.com:diskio:vda:wr-bytes 814268269056"
echo "RATE $ts box01.lab.example.com:diskio:vda1:rd-iops 28822"
echo "RATE $ts box01.lab.example.com:diskio:vda1:rd-miops 237"
echo "RATE $ts box01.lab.example.com:diskio:vda1:rd-msec 79350"
echo "RATE $ts box01.lab.example.com:diskio:vda1:rd-bytes 34689024"
echo "RATE $ts box01.lab.example.com:diskio:vda1:wr-iops 467"
echo "RATE $ts box01.lab.example.com:diskio:vda1:wr-miops 199"
echo "RATE $ts box01.lab.example.com:diskio:vda1:wr-msec 1344"
echo "RATE $ts box01.lab.example.com:diskio:vda1:wr-bytes 6567424"
echo "RATE $ts box01.lab.example.com:diskio:vda2:rd-iops 31821"
echo "RATE $ts box01.lab.example.com:diskio:vda2:rd-miops 14233"
echo "RATE $ts box01.lab.example.com:diskio:vda2:rd-msec 368448"
echo "RATE $ts box01.lab.example.com:diskio:vda2:rd-bytes 45841920"
echo "RATE $ts box01.lab.example.com:diskio:vda2:wr-iops 7139"
echo "RATE $ts box01.lab.example.com:diskio:vda2:wr-miops 241535"
echo "RATE $ts box01.lab.example.com:diskio:vda2:wr-msec 1998752"
echo "RATE $ts box01.lab.example.com:diskio:vda2:wr-bytes 823791104"
echo "RATE $ts box01.lab.example.com:diskio:vda3:rd-iops 1208235"
echo "RATE $ts box01.lab.example.com:diskio:vda3:rd-miops 103887"
echo "RATE $ts box01.lab.example.com:diskio:vda3:rd-msec 74455986"
echo "RATE $ts box01.lab.example.com:diskio:vda3:rd-bytes 3635085312"
echo "RATE $ts box01.lab.example.com:diskio:vda3:wr-iops 21606189"
echo "RATE $ts box01.lab.example.com:diskio:vda3:wr-miops 46430397"
echo "RATE $ts box01.lab.example.com:diskio:vda3:wr-msec 544298160"
echo "RATE $ts box01.lab.example.com:diskio:vda3:wr-bytes 812892827136"
echo "RATE $ts box01.lab.example.com:net:lo:rx.bytes 42765109206"
echo "RATE $ts box01.lab.example.com:net:lo:rx.packets 212439264"
echo "RATE $ts box01.lab.example.com:net:lo:rx.errors 0"
echo "RATE $ts box01.lab.example.com:net:lo:rx.drops 0"
echo "RATE $ts box01.lab.example.com:net:lo:rx.overruns 0"
echo "RATE $ts box01.lab.example.com:net:lo:rx.compressed 0"
echo "RATE $ts box01.lab.example.com:net:lo:rx.frames 0"
echo "RATE $ts box01.lab.example.com:net:lo:rx.multicast 0"
echo "RATE $ts box01.lab.example.com:net:lo:tx.bytes 42765109206"
echo "RATE $ts box01.lab.example.com:net:lo:tx.packets 212439264"
echo "RATE $ts box01.lab.example.com:net:lo:tx.errors 0"
echo "RATE $ts box01.lab.example.com:net:lo:tx.drops 0"
echo "RATE $ts box01.lab.example.com:net:lo:tx.overruns 0"
echo "RATE $ts box01.lab.example.com:net:lo:tx.compressed 0"
echo "RATE $ts box01.lab.example.com:net:lo:tx.collisions 0"
echo "RATE $ts box01.lab.example.com:net:lo:tx.carrier 0"
echo "RATE $ts box01.lab.example.com:net:eth0:rx.bytes 13837512522"
echo "RATE $ts box01.lab.example.com:net:eth0:rx.packets 21349454"
echo "RATE $ts box01.lab.example.com:net:eth0:rx.errors 0"
echo "RATE $ts box01.lab.example.com:net:eth0:rx.drops 0"
echo "RATE $ts box01.lab.example.com:net:eth0:rx.overruns 0"
echo "RATE $ts box01.lab.example.com:net:eth0:rx.compressed 0"
echo "RATE $ts box01.lab.example.com:net:eth0:rx.frames 0"
echo "RATE $ts box01.lab.example.com:net:eth0:rx.multicast 0"
echo "RATE $ts box01.lab.example.com:net:eth0:tx.bytes 3915900474"
echo "RATE $ts box01.lab.example.com:net:eth0:tx.packets 13061057"
echo "RATE $ts box01.lab.example.com:net:eth0:tx.errors 0"
echo "RATE $ts box01.lab.example.com:net:eth0:tx.drops 0"
echo "RATE $ts box01.lab.example.com:net:eth0:tx.overruns 0"
echo "RATE $ts box01.lab.example.com:net:eth0:tx.compressed 0"
echo "RATE $ts box01.lab.example.com:net:eth0:tx.collisions 0"
echo "RATE $ts box01.lab.example.com:net:eth0:tx.carrier 0"
echo "RATE $ts box01.lab.example.com:net:eth1:rx.bytes 10087271670"
echo "RATE $ts box01.lab.example.com:net:eth1:rx.packets 29856127"
echo "RATE $ts box01.lab.example.com:net:eth1:rx.errors 0"
echo "RATE $ts box01.lab.example.com:net:eth1:rx.drops 0"
echo "RATE $ts box01.lab.example.com:net:eth1:rx.overruns 0"
echo "RATE $ts box01.lab.example.com:net:eth1:rx.compressed 0"
echo "RATE $ts box01.lab.example.com:net:eth1:rx.frames 0"
echo "RATE $ts box01.lab.example.com:net:eth1:rx.multicast 0"
echo "RATE $ts box01.lab.example.com:net:eth1:tx.bytes 15718034346"
echo "RATE $ts box01.lab.example.com:net:eth1:tx.packets 22968537"
echo "RATE $ts box01.lab.example.com:net:eth1:tx.errors 0"
echo "RATE $ts box01.lab.example.com:net:eth1:tx.drops 0"
echo "RATE $ts box01.lab.example.com:net:eth1:tx.overruns 0"
echo "RATE $ts box01.lab.example.com:net:eth1:tx.compressed 0"
echo "RATE $ts box01.lab.example.com:net:eth1:tx.collisions 0"
echo "RATE $ts box01.lab.example.com:net:eth1:tx.carrier 0"
