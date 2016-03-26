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



Little Things
-------------

- Switch to SQLite for data / keys storage.  I think this will
  obviate the need for [#30][bolo-30], and provide us better
  forwards- and backwards-compatibility avenues.  We should run
  benchmarks of the mmap'd binfmt databases with the SQLite impl.


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




[tsdp]: https://github.com/bolo/rfc/blob/master/draft-hunt-tsdp-00.txt
[bolo-30]: https://github.com/bolo/bolo/issues/30
