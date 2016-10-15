Bolo `savedb` Binary Format Specification
=========================================

This document provides a technical specification for the Bolo
`savedb` binary file format.

Purpose of the `savedb` File
----------------------------

The `savedb` is used to persist state between bolo aggregator
daemon restarts.  It is regularly updated during normal operation
of the bolo aggregator, usually being written to once every 15
seconds.  For this reason, the binary format was designed to be
fast to update.

File Format Overview
--------------------

The `savedb` file consists of a HEADER, providing metadata about
the rest of the file, zero or more RECORD entries, and a TRAILER.

The HEADER contains important metadata about the file, including:

  - File format version in force
  - Flags indicating optional features in force
  - When the savedb was written
  - How many records are in the file

Data Types
----------

For brevity, we define here named data types, and explain their
bit pattern encoding, lengths, and signedness (where applicable).

### TIMESTAMP

A TIMESTAMP is a 4-octet, 32-bit unsigned integer stored in
network byte order (most significant byte first).  It signifies a
single point in time, represented as the number of seconds that
have elapsed since January 1st, 1970 00:00:00 UTC, the UNIX epoch.

TIMESTAMP values cannot represent dates beyond the year 2038.  A
future version of this specification may fix that limitation.

### BYTE

A BYTE is a single octet, 8-bit unsigned integer.  Since it
consists of a single octet, there are no ordering concerns.

### WORD

A WORD is a 2-octet, 16-bit unsigned integer stored in network
byte order (most significant byte first).

### DWORD

A DWORD (double WORD) is a 4-octet, 32-bit unsigned integer stored
in network byte order (most significant byte first).

### QWORD

A QWORD (quadruple WORD) is an 8-octet, 64-bit unsigned integer
stored in network byte order (most significant byte first).

### FLOAT

A FLOAT is a double-precision, 64-bit IEEE-754 floating point
number, stored according to the rules of the IEEE-754 standard.

### STRING

A STRING is a vector of BYTEs, usually representing ASCII-encoded
(or UTF-8) textual data, terminated by a NULL BYTE (00h).  All
STRING values are at least 1-octet long -- the empty string
consisting of a single NULL BYTE.

HEADER Segment Specification
----------------------------

The HEADER consists of the following fields:

    0       8      16      24       32
    +-------+-------+-------+-------+
    |          MAGIC NUMBER         |
    +-------+-------+-------+-------+
    |    VERSION    |     FLAGS     |
    +-------+-------+-------+-------+
    |           WRITTEN AT          |
    +-------+-------+-------+-------+
    |          RECORD COUNT         |
    +-------+-------+-------+-------+

MAGIC NUMBER is a 4-octet bit pattern that serves to identify this
file as a savedb.  It must contain the ASCII character codes for
the 4-character string "BOLO".

VERSION is a WORD indicating the file format version in force. The
current version of this specification is version 1, so the only
acceptable bit pattern for VERSION is `0001h`.

FLAGS is a WORD reserved for future use.  There are no flags
defined by this version of the binary file format.  The only
acceptable bit pattern for FLAGS is `0000h`.

WRITTEN AT is a TIMESTAMP indicating when the savedb file was
written to disk.  This value can and should be used to determine
if a savedb file is too old to be processed.

COUNT is a DWORD indicating how many RECORD segments are contained
in the file.

RECORD Segment Specification
----------------------------

Each metric or state has a RECORD segment, which consists of at
least 4 octets that identify the type of RECORD and its length:

    0       8      16      24       32
    +-------+-------+-------+-------+
    |    LENGTH     |     FLAGS     |
    +-------+-------+-------+-------+
    |        <variable data>        |
    |              ...              |
    +-------+-------+-------+-------+

LENGTH is a WORD indicating the total length of this RECORD, in
octets, including the combined length of the LENGTH and FLAGS
field (i.e. 4 octets).

FLAGS is a WORD indicating what type of RECORD segment this is.
Current valid bit patterns are (in hexadecimal):

    00 01      RECORD is a STATE RECORD
    00 02      RECORD is a COUNTER RECORD
    00 03      RECORD is a SAMPLE RECORD
    00 04      RECORD is an EVENT RECORD
    00 05      RECORD is a RATE RECORD

All other bit patterns are invalid.

### STATE RECORD Variant

A STATE RECORD Variant is a type of RECORD that encodes
information about a state.

    0       8      16      24       32
    +-------+-------+-------+-------+
    |    LENGTH     |     FLAGS     |
    +-------+-------+-------+-------+
    |           LAST SEEN           |
    +-------+-------+-------+-------+
    | CODE  | STALE | IGNOR | ...   |
    +-------+-------+-------+-------+
    |              ...              |
    +-------+-------+-------+-------+

LENGTH and FLAGS retain their meaning, as specified in the RECORD
Segment Specification section.

LAST SEEN is a TIMESTAMP indicating when the Bolo aggregator last
saw a value for this state.  This is used to ensure that freshness
calculations are accurate after a restart.

CODE is a BYTE indicating the status code of the state.  The
following values (in hexadecimal) have meaning:

    00   OK - The state is healthy
    01   WARNING - The state is in pre-failure
    02   CRITICAL - The state is unhealthy
    03   UNKNOWN - Health of the state is unknown

STALE is a BYTE indicating whether the state information is fresh
(`00h`) or not (`01h`).

IGNOR is a BYTE indicating whether the state should be ignored
(`01h`) or not (`00h`) -- this is an optimization to avoid
rewriting savedb files when states are explicitly forgotten (via a
FORGET operation)

The remainder of the data is a series of at least two octets,
to be interpreted as two STRING values.

The first STRING value is the name of the state.

The second STRING value is the summary message given by the last
check of this state.

### COUNTER RECORD Variant

A COUNTER RECORD Variant is a type of RECORD that encodes
information about a counter metric.

    0       8      16      24       32
    +-------+-------+-------+-------+
    |    LENGTH     |     FLAGS     |
    +-------+-------+-------+-------+
    |           LAST SEEN           |
    +-------+-------+-------+-------+
    |             VALUE             |
    |                               |
    +-------+-------+-------+-------+
    | IGNOR | ...                   |
    +-------+-------+-------+-------+

LENGTH and FLAGS retain their meaning, as specified in the RECORD
Segment Specification section.

LAST SEEN is a TIMESTAMP indicating when the Bolo aggregator last
saw a value for this metric.  This is used to ensure that freshness
calculations are accurate after a restart.

VALUE is a QWORD indicating the current value of the counter.

IGNOR is a BYTE indicating whether the metric should be ignored
(`01h`) or not (`00h`) -- this is an optimization to avoid
rewriting savedb files when metrics are explicitly forgotten (via a
FORGET operation)

The remainder of the data is a single STRING value, containing the
name of the counter metric.

### SAMPLE RECORD Variant

A SAMPLE RECORD Variant is a type of RECORD that encodes
information about a sample metric.

    0       8      16      24       32
    +-------+-------+-------+-------+
    |    LENGTH     |     FLAGS     |
    +-------+-------+-------+-------+
    |           LAST SEEN           |
    +-------+-------+-------+-------+
    |               N               |
    |                               |
    +-------+-------+-------+-------+
    |              MIN              |
    |                               |
    +-------+-------+-------+-------+
    |              MAX              |
    |                               |
    +-------+-------+-------+-------+
    |              SUM              |
    |                               |
    +-------+-------+-------+-------+
    |             MEAN              |
    |                               |
    +-------+-------+-------+-------+
    |           MEAN PRIME          |
    |                               |
    +-------+-------+-------+-------+
    |            VARIANCE           |
    |                               |
    +-------+-------+-------+-------+
    |         VARIANCE PRIME        |
    |                               |
    +-------+-------+-------+-------+
    | IGNOR | ...                   |
    +-------+-------+-------+-------+

LENGTH and FLAGS retain their meaning, as specified in the RECORD
Segment Specification section.

LAST SEEN is a TIMESTAMP indicating when the Bolo aggregator last
saw a value for this metric.  This is used to ensure that freshness
calculations are accurate after a restart.

N is a QWORD indicating the number of sample measurements seen.

MIN is a FLOAT indicating the minimum value of sample
measurements.

MAX is a FLOAT indicating the maximum value of sample
measurements.

SUM is a FLOAT indicating the summation of all sample
measurements.

MEAN is a FLOAT indicating the arithmetic mean of all sample
measurements.

MEAN PRIME is a FLOAT containing an intermediate value of the
arithmetic mean, for use in calculating the actual arithmetic mean
without keeping track of all sample measurements.

VARIANCE is a FLOAT indicating the statistical variance of the
sample set.

VARIANCE PRIME is a FLOAT containing an intermediate value of
statistical variance, for use in calcualting the actual variance
without keeping track of all sample measurements.

IGNOR is a BYTE indicating whether the metric should be ignored
(`01h`) or not (`00h`) -- this is an optimization to avoid
rewriting savedb files when metrics are explicitly forgotten (via a
FORGET operation)

The remainder of the data is a single STRING value, containing the
name of the sample metric.

### EVENT RECORD Variant

AN EVENT RECORD Variant is a type of RECORD that encodes
information about an event.

    0       8      16      24       32
    +-------+-------+-------+-------+
    |    LENGTH     |     FLAGS     |
    +-------+-------+-------+-------+
    |             WHEN              |
    +-------+-------+-------+-------+
    | ...                           |
    +-------+-------+-------+-------+

LENGTH and FLAGS retain their meaning, as specified in the RECORD
Segment Specification section.

WHEN is a TIMESTAMP indicating when the event occurred.

The remainder of the data is a series of at least two octets,
to be interpreted as two STRING values.

The first STRING value is the name of the event.

The second STRING value is the additional description for this
event.


### RATE RECORD Variant

A RATE RECORD Variant is a type of RECORD that encodes
information about a rate metric.

    0       8      16      24       32
    +-------+-------+-------+-------+
    |    LENGTH     |     FLAGS     |
    +-------+-------+-------+-------+
    |          FIRST SEEN           |
    +-------+-------+-------+-------+
    |           LAST SEEN           |
    +-------+-------+-------+-------+
    |          FIRST VALUE          |
    |                               |
    +-------+-------+-------+-------+
    |           LAST VALUE          |
    |                               |
    +-------+-------+-------+-------+
    | IGNOR | ...                   |
    +-------+-------+-------+-------+

LENGTH and FLAGS retain their meaning, as specified in the RECORD
Segment Specification section.

FIRST SEEN is a TIMESTAMP indicating when the Bolo aggregator
first saw a value for this metric.  This is used to ensure
calculate the actual window rate, via extrapolation.

LAST SEEN is a TIMESTAMP indicating when the Bolo aggregator
last saw a value for this metric.  This is used to ensure
calculate the actual window rate, via extrapolation.

FIRST VALUE is a QWORD indicating the first value seen for this
rate.

LAST VALUE is a QWORD indicating the last value seen for this
rate.

IGNOR is a BYTE indicating whether the metric should be ignored
(`01h`) or not (`00h`) -- this is an optimization to avoid
rewriting savedb files when metrics are explicitly forgotten (via a
FORGET operation)

The remainder of the data is a single STRING value, containing the
name of the event metric.


TRAILER Segment Specification
-----------------------------

The TRAILER is two (2) octets in length.  Both octets must be set
to the binary value `00000000b`.  This is designed to help
interative parsers that ignore or miscalculate the number of
records in the savedb.
