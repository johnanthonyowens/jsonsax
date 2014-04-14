JSONSAX Library
===============

JSONSAX is a library written in ANSI C which implements a stream-oriented JSON
parser that uses callbacks to notify its client of the JSON document structure
as the document is parsed.

JSONSAX stands for "JSON Streamed Ala eXpat", and is so named because its usage
is patterned after the "SAX" style implemented by the venerable Expat XML
parser.

JSONSAX is designed to be lightweight, portable, robust, fast, and have minimal
memory overhead, suitable for memory-constrained environments such as embedded
systems. It has no external dependencies other than the standard ANSI C runtime
library.

Callback-based parsers are significantly more difficult to use than those that
simply build and return DOM representations of the input, but they are useful
in situations where the client wants to build a custom DOM representation of the
input without incurring the overhead of a generic "intermediate" DOM built by
the parser -- for example, adding JSON serialization support to a higher-level
programming language -- or where the client wants to perform processing that
doesn't require creating any kind of DOM at all, like pretty printing.

Because the JSONSAX parser is stream-oriented, clients have absolute flexibility
to provide input asynchronously as it is available to them, in whatever size
chunks are convenient.

The parser adheres to [RFC 4627](http://www.ietf.org/rfc/rfc4627.txt), with the
following caveats:

1. Any JSON value (null, true, false, string, number, object, or array) is
   accepted as a valid top-level entity in the parsed text; this deviates from
   RFC 4627, which requires the top-level entity to be an an object or an
   array. This deviation is consistent with the behavior of the JSON.parse()
   function present in ECMAScript 5 and other common JSON parsers.

2. Detection of duplicate object members is not enabled by default (to avoid
   memory overhead) but can be enabled if desired. Clients can also choose to
   implement duplicate detection themselves.

The parser can parse input encoded in UTF-8, UTF-16 (LE or BE), and UTF-32 (LE
or BE). By default it automatically detects the input encoding; clients can also
explicitly specify the input encoding on a parser-by-parser basis.

Clients can control, on a parser-by-parser basis, whether the string values
that are passed to them by the parser are encoded as UTF-8, UTF-16 (LE or BE),
or UTF-32 (LE or BE).

For maximum flexibility and portability, number values are passed by the parser
to the client as strings. Clients can control, on a parser-by-parser basis,
whether the number values that are passed to them by the parser are encoded as
UTF-8, UTF-16 (LE or BE), or UTF-32 (LE or BE).

By default, the parser is strict when decoding the input stream, and will fail
if it encounters an encoding sequence that is not valid for the input
encoding. Note especially that this includes (but is not limited to) the
following:

- Overlong encoding sequences in UTF-8.
- Surrogate codepoints encoded in UTF-8 or UTF-32.
- Unpaired or improperly-paired surrogates in UTF-16.
- Codepoints outside the Unicode range encoded in UTF-8 or UTF-32.

Clients also have the option, on a parser-by-parser basis, of replacing invalid
encoding sequences in the input stream with the Unicode replacement character
(U+FFFD) rather than triggering an error. The replacement follows the rules and
recommendations described in section 3.9 of version 5.2.0 of [the Unicode
Standard](http://www.unicode.org/versions/Unicode5.2.0/).

The parser also supports several optional extensions to RFC 4627, each
of which can be enabled on a parser-by-parser basis. These include:

- Allowing the JSON text to begin with a Unicode byte-order-mark (BOM).
- Allowing the JSON text to contain Javascript-style comments.
- Allowing the "special" number literals NaN, Infinity, and -Infinity.
- Allowing Javascript-style hexadecimal numbers.
- Allowing unescaped control characters (U+0000 - U+001F) in strings.

The JSONSAX library also includes a JSON writer that provides a fast and
reliable way for clients to create JSON documents that are guaranteed to be
well-formed and properly encoded.

The writer can encode its output in UTF-8, UTF-16 (LE or BE), or UTF-32
(LE or BE). String values can be passed to the writer in any of these
encodings, and will be decoded, have escape sequences substituted as
appropriate, and encoded in the desired output encoding before being sent to
the writer's output handler. Number values can also be passed to the writer
in any of these encodings, and will be decoded, checked for well-formedness,
and encoded in the desired output encoding before being output.

The JSONSAX library is licensed under the MIT License. The full license is
contained in the accompanying LICENSE file.

Full documentation of the JSONSAX API is provided in the jsonsax.h header.