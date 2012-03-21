JSONSAX Library
======================================================================

Copyright (c) 2012 John-Anthony Owens

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

----------------------------------------------------------------------

JSONSAX is a library written in ANSI C which implements a stream-oriented JSON parser that uses callbacks to notify its client of the structure represented by JSON text as it is parsed.

JSONSAX stands for "JSON Streamed Ala eXpat", and is so named because its usage parser is patterned after the "SAX" style implemented by the venerable Expat XML parser.

JSONSAX is designed to be lightweight, portable, robust, fast, and have minimal memory overhead, suitable for memory-constrained environments such as embedded systems. It has no external dependencies other than the standard ANSI C runtime library.

Callback-based parsers are significantly more difficult to use than those that simply build and returns DOM representations of the input, but they are useful in situations where the client wants to build a custom DOM representation of the input without incurring the overhead of a generic "intermediate" DOM representation built by the parser, or where the client wants to perform processing that doesn't require creating any kind of DOM at all.

Because the JSONSAX parser is stream-oriented, clients have absolute flexibility to provide input asynchronously as it is available to them, in whatever size chunks are convenient.

The parser adheres to [RFC 4627](http://www.ietf.org/rfc/rfc4627.txt), with the following caveats:

1. Any JSON value (null, true, false, string, number, object, or array) is accepted as a valid top-level entity in the parsed text; this deviates from RFC 4627, which requires the top-level entity to be an an object or an array. This deviation is consistent with the behavior of the JSON.parse() function present in ECMAScript 5 and other common JSON parsers.

2. Number values are limited to 63 characters in length (imposing limits on the length of numbers is permitted by RFC 4637).

3. Detection of duplicate object members is not enabled by default (to avoid memory overhead) but can be enabled if desired. Clients can also choose to implement duplicate detection themselves.

The parser can parse input encoded in UTF-8, UTF-16 (LE or BE), and UTF-32 (LE or BE). By default it automatically detects the input encoding; clients can also explicitly specify the input encoding on a parser-by-parser basis.

The encoding of string values passed by the parser to the client can be UTF-8, UTF-16 (LE or BE), or UTF-32 (LE or BE). Clients can specify the output encoding on a parser-by-parser basis.

The parser is strict when decoding the input stream, and will fail if it encounters an encoding sequence that is not valid for the input encoding. Note especially that this includes (but is not limited to) the following:

 - Overlong encoding sequences in UTF-8.
 - Surrogate codepoints encoded in UTF-8 or UTF-32.
 - Unpaired or improperly-paired surrogates in UTF-16.
 - Codepoints outside the Unicode range encoded in UTF-8 or UTF-32.

The JSONSAX library is licensed under the MIT License.

Full documentation of the JSONSAX API is provided in the jsonsax.h header.
