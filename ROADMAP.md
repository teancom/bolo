ROADMAP
=======

This file documents the proposed changes to be made to Bolo, to
help track their evolution without creating too many extra
tickets.

Big Things
----------

- [TSDP][tsdp] compliance.  Rework the communication patterns to
  fit the TSDP model, and implement binary message packing
  routines.  This will also require changes to other components
  like dbolo, the send\_bolo CLI, etc.
- Better Modular Decomposition.  Right now, the code is organized
  as "all things bolo, plus a file for each extra binary".  This
  makes it difficult to hide information / interfaces that are
  only of interest to the bolo aggregator from the subscribers.
- Subscribers for all major dashboard / visualization systems,
  as well as all major time series database formats.
- Library for bolo primitives, which other projects can link to
  and use.  Need to be cery careful on the public symbols for this
  one, to avoid library churn.



Little Things
-------------

- Switch to SQLite for data / keys storage.  I think this will
  obviate the need for [#30][bolo-30], and provide us better
  forwards- and backwards-compatibility avenues.  We should run
  benchmarks of the mmap'd binfmt databases with the SQLite impl.


Wanted Subscribers
------------------

- **bolo2influxdb** - Push metrics into InfluxDB.  _May_ require
  the TSDP qualified names implementation.
- **bolo2otsdb** - Push metrics into OpenTSDB (using TSD).  Also
  probably requires the TSDP qualified names implementation.


Modular Decomposition
---------------------

Public header files go in `include/`.  A header file is public if
it is applicable to someone building a something that interacts
with bolo, but is not bolo itself.  For example, parts of the TSDP
implementation should go in public header files, as do the
structure definitions and function declarations for the subscriber
architecture.

All `*.c` files live under `src/`.

Common internal will live in appropriate files directly underneath
the `src/` directory.  For example, the qualified names
implementation lives at `src/qnames.c`.  C translation units at
this level should be small and singular in purpose -- they will be
combined into larger binaries by the compilation process.

Each utility will have it's own eponymous sub-directory under
`src/`.  For example, the `bolo` daemon source code will live in
`src/bolo/`, but the code specific to the `dbolo` agent will live
in `src.dbolo`.


libbolo Library
---------------

`libbolo` will provide the primitives that are useful for other
components outside the purview of this codebase.  That includes
clients for submitting metrics (in case someone wants to embed
bolo support in their application proper) and the subscriber
architecture.  Other bits, like configuration parsing, aggregation
code, etc., must remain outside this library and be compiled
directly into the binaries that need that functionality.  (Due to
the way libtool / autotools works, this will probably end up being
a `noinst` libboloimpl library or something).

All exported symbols **must** be prefixed with `bolo_`, to be a
good namespace citizen with respect to other libraries.

Subscriber datatypes:

- `typedef struct { ... } bolo_subscriber_t`
- `typedef (int)(*bolo_subscriber_fn(pdu_t*, void*))`

Subscriber functions:

- `int bolo_subscriber_config(bolo_subscriber_t*, int, char**)`
- `int bolo_subscriber_run(bolo_subscriber_t*, bolo_subscriber_fn, void*)`
- `int bolo_subscriber_wait(bolo_subscriber_t*)`
- `void bolo_subscriber_track(const char *, ...)`

Here is a first draft of the `bolo_subscriber_t` structure:

```
typedef struct {
  const char  *prefix;         /* prefix for submitted metrics */
  const char  *submit_to;      /* Bolo Aggregator endpoint to
                                  submit metrics to */
  const char  *subscribe_to;   /* Bolo Aggregator endpoint to
                                  subscribe to for broadcasts */
  const char **metrics;        /* Metrics (health metadata) to
                                  submit, i.e. "COUNT x.y" or
                                  "SAMPLE y.z"; NULL-terminated. */
} bolo_subscriber_t;
```

Submission functions (PDU creators):

- `pdu_t* bolo_FORGET(unsigned int, const char*, int)`
- `pdu_t* bolo_STATE(const char*, int, const char*)`
- `pdu_t* bolo_COUNTER(const char*, unsigned int)`
- `pdu_t* bolo_SAMPLE(const char*, int, ...)`
- `pdu_t* bolo_RATE(const char*, unsigned long)`
- `pdu_t* bolo_SETKEYS(int n, ...)`
- `pdu_t* bolo_EVENT(const char*, const char*)`

Submission functions (create / send / free):

- `int bolo_send_FORGET(unsigned int, const char*, int)`
- `int bolo_send_STATE(const char*, int, const char*)`
- `int bolo_send_COUNTER(const char*, unsigned int)`
- `int bolo_send_SAMPLE(const char*, int, ...)`
- `int bolo_send_RATE(const char*, unsigned long)`
- `int bolo_send_SETKEYS(int n, ...)`
- `int bolo_send_EVENT(const char*, const char*)`

Qualified Name structures:

- `typedef struct { ... } bolo_name_t`

(note: I would like to hide the implementation of the components
 inside the `bolo_name_t` structure, to avoid polluting namespace
 with anything the user won't be using directly.)

Qualified Name functions:

- `bolo_name_t* bolo_name_parse(const char*)`
- `char* bolo_name_string(bolo_name_t*)`
- `int bolo_name_match(bolo_name_t*, bolo_name_t*)`
- `bolo_name_t* bolo_name_copy(bolo_name_t*)`
- `int bolo_name_add(bolo_name_t*, const char*, const char*)`
- `int bolo_name_concat(bolo_name_t*, bolo_name_t*)`



[tsdp]: https://github.com/bolo/rfc/blob/master/draft-hunt-tsdp-00.txt
[bolo-30]: https://github.com/bolo/bolo/issues/30
