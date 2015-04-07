bolo - A Versatile Monitoring Toolkit
=====================================

bolo is a toolkit for building distributed, scalable monitoring
systems.  It provides a set of small, versatile components that
you can plug together in new and exciting ways, to gather and
aggregate metrics, mine data and track system states.

Getting Started
---------------

To compile the software, use the standard incantation:

    $ ./bootstrap
    $ ./configure
    $ make
    $ make check           # optional, but recommended
    $ sudo make install

This will compile and install all of the bolo components in
standard systems places.  bolo requires libzmq3, pthreads and
[libvigor][libvigor] to run.  It uses [ctap][ctap] to run
automated tests (which are, again, highly recommended).

A Minimal System
----------------

The core of every bolo deployment is the `bolo` daemon itself,
which requires a configuration file, usually **/etc/bolo.conf**.

    # /etc/bolo.conf
    #
    listener   tcp://*:2999
    controller tcp://127.0.0.1:2998
    broadcast  tcp://*:2997

    log info daemon
    savefile /var/lib/bolo/save.db
    keysfile /var/lib/bolo/keys.db

    dumpfiles /var/tmp/bolo.%s

    type :generic {
      freshness 60
      critical "no result from monitored thing"
    }
    state :generic m/./;

    window @minutely 60
    use @minutely;
    sample  m/./
    counter m/./
    rate    m/./

This is a **catch-all** configuration, that excepts all submitted
metrics to be collected at at least a 1-minute resolution, and
aggregate at the per-minute mark.  Check **bolo.conf(5)** for
details and more advanced usage.

With this configuration in place, start up the bolo daemon:

    $ sudo bolo

By default, bolo starts up daemonized, and reads from
/etc/bolo.conf.

Submitting Data
---------------

The **send_bolo** utility can be used to submit metrics and state
data to bolo, via the _listener_ port (TCP/2999 in our case):

    $ send_bolo -e tcp://localhost:2999 -t sample life 42
    $ send_bolo -e tcp://localhost:2999 -t sample universe 42
    $ send_bolo -e tcp://localhost:2999 -t sample everything 42

At this point, you will probably want to go get
[bolo-collectors][collectors] installed to give you some real
metric collectors, for real things like memory usage, swap rates,
CPU time allocation, etc.  The output from these collectors can be
piped directly into **send_bolo**:

    $ /path/to/collector | send_bolo -e tcp://localhost:2999

You'll probably want to run your collectors out of cron, or use
something like [tinybolo][tinybolo] or [bmad][bmad] to schedule
and run them regularly.

Using the Data
--------------

As the bolo daemon receives metrics and state data from monitored
hosts, it aggregates the data and regularly broadcasts these
summaries out to connected subscribers.  One such subscriber is
**bolo2rrd**, which listens for samples, counters and rate data
and creates / updates RRD files on disk.

    $ mkdir /srv/rrd
    $ sudo bolo2rrd -r /srv/rrd -e tcp://localhost:2997

bolo2rrd daemonizes into the background.

The **stat_bolo** utility also has a _listen_ mode wherein it
connects to the broadcast port and prints out the data as it is
received.  This can be useful for subscriber prototyping in
languages like Perl or Ruby, without having to deal directly with
0MQ and the bolo message protocol.

    $ stat_bolo -e tcp://localhost:2997 -l | grep 'universe'

Writing subscribers isn't that difficult, and provides an
interesting way of customizing the behavior of your monitoring
system without needing to modify bolo itself.

Next Steps
----------

Congratulations!  You have a bolo-based monitoring system up.

So where do you go from here?

You'll probably want to build a user-interface to display graphs
rendered from the RRD files, even if that's just to render the
images via cron and serve them up statically.



[ctap]:       https://github.com/filefrog/ctap
[libvigor]:   https://github.com/filefrog/libvigor
[collectors]: https://github.com/filefrog/bolo-collectors
[tinybolo]:   https://github.com/filefrog/tinybolo
[bmad]:       https://github.com/geofffranks/bmad
