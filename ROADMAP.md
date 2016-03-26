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


[tsdp]: https://github.com/bolo/rfc/blob/master/draft-hunt-tsdp-00.txt
[bolo-30]: https://github.com/bolo/bolo/issues/30
