/*
  Copyright (c) 2012 John-Anthony Owens

  Permission is hereby granted, free of charge, to any person obtaining a
  copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/

/* JSONSAX is a library written in ANSI C which implements a stream-oriented
 * JSON parser that uses callbacks to notify its client of the JSON document
 * structure as the document is parsed.
 *
 * JSONSAX stands for "JSON Streamed Ala eXpat", and is so named because its
 * usage is patterned after the "SAX" style implemented by the venerable
 * Expat XML parser.
 *
 * JSONSAX is designed to be lightweight, portable, robust, fast, and have
 * minimal memory overhead, suitable for memory-constrained environments such
 * as embedded systems. It has no external dependencies other than the
 * standard ANSI C runtime library.
 *
 * Callback-based parsers are significantly more difficult to use than those
 * that simply build and return DOM representations of the input, but they are
 * useful in situations where the client wants to build a custom DOM
 * representation of the input without incurring the overhead of a generic
 * "intermediate" DOM representation built by the parser, or where the client
 * wants to perform processing that doesn't require creating any kind of DOM
 * at all.
 *
 * Because the JSONSAX parser is stream-oriented, clients have absolute
 * flexibility to provide input asynchronously as it is available to them,
 * in whatever size chunks are convenient.
 *
 * The parser adheres to [RFC 4627](http://www.ietf.org/rfc/rfc4627.txt),
 * with the following caveats:
 *
 * 1. Any JSON value (null, true, false, string, number, object, or array)
 *    is accepted as a valid top-level entity in the parsed text; this
 *    deviates from RFC 4627, which requires the top-level entity to be an
 *    an object or an array. This deviation is consistent with the behavior
 *    of the JSON.parse() function present in ECMAScript 5 and other common
 *    JSON parsers.
 *
 * 2. Detection of duplicate object members is not enabled by default (to
 *    avoid memory overhead) but can be enabled if desired. Clients can also
 *    choose to implement duplicate detection themselves.
 *
 * The parser can parse input encoded in UTF-8, UTF-16 (LE or BE), and
 * UTF-32 (LE or BE). By default it automatically detects the input encoding;
 * clients can also explicitly specify the input encoding on a parser-by-
 * parser basis.
 *
 * The encoding of string values passed by the parser to the client can be
 * UTF-8, UTF-16 (LE or BE), or UTF-32 (LE or BE). Clients can specify the
 * string encoding on a parser-by-parser basis.
 *
 * By default, the parser is strict when decoding the input stream, and will
 * fail if it encounters an encoding sequence that is not valid for the input
 * encoding. Note especially that this includes (but is not limited to) the
 * following:
 *
 *  - Overlong encoding sequences in UTF-8.
 *  - Surrogate codepoints encoded in UTF-8 or UTF-32.
 *  - Unpaired or improperly-paired surrogates in UTF-16.
 *  - Codepoints outside the Unicode range encoded in UTF-8 or UTF-32.
 *
 * Clients also have the option, on a parser-by-parser basis, of replacing
 * invalid encoding sequences in the input stream with the Unicode replacement
 * character (U+FFFD) rather than triggering an error. The replacement follows
 * the rules and recommendations described in section 3.9 of version 5.2.0 of
 * [the Unicode Standard](http://www.unicode.org/versions/Unicode5.2.0/).
 *
 * The parser also supports several optional extensions to RFC 4627, each
 * of which can be enabled on a parser-by-parser basis. These include:
 *
 * - Allowing the JSON text to begin with a Unicode byte-order-mark (BOM).
 * - Allowing the JSON text to contain Javascript-style comments.
 * - Allowing the "special" number literals NaN, Infinity, and -Infinity.
 * - Allowing Javascript-style hexadecimal numbers.
 *
 * The JSONSAX library also includes a JSON writer that provides a fast and
 * reliable way for clients to create JSON documents that are guaranteed to
 * be well-formed and properly encoded.
 *
 * The writer can encode its output in UTF-8, UTF-16 (LE or BE), or UTF-32
 * (LE or BE). String values can be passed to the writer in any of these
 * encodings, and will be decoded, have escape sequences substituted as
 * appropriate, and encoded in the desired output encoding before being sent
 * to the writer's output handler.
 *
 * The JSONSAX library is licensed under the MIT License. The full license is
 * contained in the accompanying LICENSE file.
 */

#ifndef JSONSAX_H_INCLUDED
#define JSONSAX_H_INCLUDED

#include <stddef.h> /* for size_t and NULL */

/* The library API is C and should not be subjected to C++ name mangling. */
#ifdef __cplusplus
extern "C" {
#endif

/* JSON_EXPORT controls the library's public API import/export linkage
 * specifiers. By default, the library will be compiled to support dynamic
 * linkage. In order to build the library for static linkage, the JSON_STATIC
 * macro must be defined when the library itself is built AND when the client
 * includes jsonsax.h.
 */
#if defined(JSON_STATIC)
#define JSON_EXPORT /* nothing */
#else
#if defined(_MSC_VER)
#if defined(JSON_BUILDING)
#define JSON_EXPORT __declspec(dllexport)
#else
#define JSON_EXPORT __declspec(dllimport)
#endif
#else
#if defined(JSON_BUILDING)
#define JSON_EXPORT __attribute__ ((visibility("default")))
#else
#define JSON_EXPORT /* nothing */
#endif
#endif
#endif

/* JSON_CALL controls the library's public API calling-convention. Clients'
 * handler functions should be declared with JSON_CALL in order to ensure
 * that the calling convention matches.
 */
#ifndef JSON_CALL
#if defined(_MSC_VER)
#define JSON_CALL __cdecl
#elif defined(__GNUC__) && defined(__i386) && !defined(__INTEL_COMPILER)
#define JSON_CALL __attribute__((cdecl))
#else
#define JSON_CALL /* nothing */
#endif
#endif

#define JSON_API(t) JSON_EXPORT t JSON_CALL

/* Boolean values used by the library. */
typedef enum tag_JSON_Boolean
{
    JSON_False = 0,
    JSON_True  = 1
} JSON_Boolean;

/* Values returned by library APIs to indicate success or failure. */
typedef enum tag_JSON_Status
{
    JSON_Failure = 0,
    JSON_Success = 1
} JSON_Status;

/* Error codes. */
typedef enum tag_JSON_Error
{
    JSON_Error_None                            = 0,
    JSON_Error_OutOfMemory                     = 1,
    JSON_Error_AbortedByHandler                = 2,
    JSON_Error_BOMNotAllowed                   = 3,
    JSON_Error_InvalidEncodingSequence         = 4,
    JSON_Error_UnknownToken                    = 5,
    JSON_Error_UnexpectedToken                 = 6,
    JSON_Error_IncompleteToken                 = 7,
    JSON_Error_ExpectedMoreTokens              = 8,
    JSON_Error_UnescapedControlCharacter       = 9,
    JSON_Error_InvalidEscapeSequence           = 10,
    JSON_Error_UnpairedSurrogateEscapeSequence = 11,
    JSON_Error_TooLongString                   = 12,
    JSON_Error_InvalidNumber                   = 13,
    JSON_Error_TooLongNumber                   = 14,
    JSON_Error_DuplicateObjectMember           = 15
} JSON_Error;

/* Text encodings. */
typedef enum tag_JSON_Encoding
{
    JSON_UnknownEncoding = 0,
    JSON_UTF8            = 1,
    JSON_UTF16LE         = 2,
    JSON_UTF16BE         = 3,
    JSON_UTF32LE         = 4,
    JSON_UTF32BE         = 5
} JSON_Encoding;

/* Attributes of a string value. */
typedef enum tag_JSON_StringAttribute
{
    JSON_SimpleString              = 0,
    JSON_ContainsNullCharacter     = 1 << 0, /* U+0000 */
    JSON_ContainsControlCharacter  = 1 << 1, /* U+0000 - U+001F */
    JSON_ContainsNonASCIICharacter = 1 << 2, /* U+0080 - U+10FFFF */
    JSON_ContainsNonBMPCharacter   = 1 << 3, /* U+10000 - U+10FFFF */
    JSON_ContainsReplacedCharacter = 1 << 4  /* an invalid encoding sequence was replaced by U+FFFD */
} JSON_StringAttribute;
typedef unsigned int JSON_StringAttributes;

/* Attributes of a number value. */
typedef enum tag_JSON_NumberAttribute
{
    JSON_SimpleNumber             = 0,
    JSON_IsNegative               = 1 << 0,
    JSON_IsHex                    = 1 << 1,
    JSON_ContainsDecimalPoint     = 1 << 2,
    JSON_ContainsExponent         = 1 << 3,
    JSON_ContainsNegativeExponent = 1 << 4
} JSON_NumberAttribute;
typedef unsigned int JSON_NumberAttributes;

/* Types of "special" number. */
typedef enum tag_JSON_SpecialNumber
{
    JSON_NaN              = 0,
    JSON_Infinity         = 1,
    JSON_NegativeInfinity = 2
} JSON_SpecialNumber;

/* Information identifying a location in a parser instance's input stream. */
typedef struct tag_JSON_Location
{
    /* The zero-based index of the byte in the input stream. Note that this
     * is the only value that unambiguously identifies the location, since
     * line and column refer to characters (which may be encoded in the input
     * as multi-byte sequences) rather than bytes.
     */
    size_t byte;

    /* The zero-based line number of the character in the input stream. Note
     * that the parser treats each of the following character sequences as a
     * single line break for purposes of computing line numbers:
     *
     *   U+000A        (LINE FEED)
     *   U+000D        (CARRIAGE RETURN)
     *   U+000D U+000A (CARRIAGE RETURN, LINE FEED)
     *
     */
    size_t line;

    /* The zero-based column number of the character in the input stream. */
    size_t column;

    /* The zero-based depth in the JSON document structure at the location. */
    size_t depth;
} JSON_Location;

/* Custom memory management handlers.
 *
 * The semantics of these handlers correspond exactly to those of standard
 * realloc(), and free(). The handlers also receive the value of the memory
 * suite's user data parameter, which clients can use to implement memory
 * pools or impose custom allocation limits, if desired.
 */
typedef void* (JSON_CALL * JSON_ReallocHandler)(void* userData, void* ptr, size_t size);
typedef void (JSON_CALL * JSON_FreeHandler)(void* userData, void* ptr);

/* A suite of custom memory management functions. */
typedef struct tag_JSON_MemorySuite
{
    void*               userData;
    JSON_ReallocHandler realloc;
    JSON_FreeHandler    free;
} JSON_MemorySuite;

/* Parser instance. */
struct JSON_Parser_Data; /* opaque data */
typedef struct JSON_Parser_Data* JSON_Parser;

/* Create a parser instance.
 *
 * If pMemorySuite is null, the library will use the C runtime realloc() and
 * free() as the parser's memory management suite. Otherwise, all the
 * handlers in the memory suite must be non-null or the call will fail and
 * return null.
 */
JSON_API(JSON_Parser) JSON_Parser_Create(const JSON_MemorySuite* pMemorySuite);

/* Free a parser instance.
 *
 * Every successful call to JSON_Parser_Create() must eventually be paired
 * with a call to JSON_Parser_Free() in order to avoid leaking memory.
 *
 * This function returns failure if the parser parameter is null or if the
 * function was called reentrantly from inside a handler.
 */
JSON_API(JSON_Status) JSON_Parser_Free(JSON_Parser parser);

/* Reset a parser instance so that it can be used to parse a new input stream.
 *
 * This function returns failure if the parser parameter is null or if the
 * function was called reentrantly from inside a handler.
 *
 * After a parser is reset, its state is indistinguishable from its state
 * when it was returned by JSON_Parser_Create(). The parser's custom memory
 * suite, if any, is preserved; all other settings, state, and handlers are
 * restored to their default values.
 */
JSON_API(JSON_Status) JSON_Parser_Reset(JSON_Parser parser);

/* Get and set the user data value associated with a parser instance.
 *
 * This setting allows clients to associate additional data with a
 * parser instance. The parser itself does not use the value.
 *
 * The default value of this setting is null.
 *
 * This setting can be changed at any time, even inside handlers.
 */
JSON_API(void*) JSON_Parser_GetUserData(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetUserData(JSON_Parser parser, void* userData);

/* Get and set the input encoding for a parser instance.
 *
 * If the client does not explicitly set the input encoding before calling
 * JSON_Parser_Parse() on the parser instance, the parser will use the first
 * 4 bytes of input to detect the input encoding automatically. Once the
 * parser has detected the encoding, calls to JSON_Parser_GetInputEncoding()
 * will return the detected value.
 *
 * The default value of this setting is JSON_UnknownEncoding.
 *
 * This setting cannot be changed once the parser has started parsing.
 */
JSON_API(JSON_Encoding) JSON_Parser_GetInputEncoding(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetInputEncoding(JSON_Parser parser, JSON_Encoding encoding);

/* Get and set the string encoding for a parser instance.
 *
 * This setting controls the encoding of the string values that are
 * passed to the string and object member handlers.
 *
 * The default value of this setting is JSON_UTF8.
 *
 * This setting cannot be set to JSON_UnknownEncoding.
 *
 * This setting cannot be changed once the parser has started parsing.
 */
JSON_API(JSON_Encoding) JSON_Parser_GetStringEncoding(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetStringEncoding(JSON_Parser parser, JSON_Encoding encoding);

/* Get and set the maximum length of strings that a parser instance allows.
 *
 * This setting controls the maximum length, in bytes (NOT characters), of
 * the encoded strings that are passed to the string and object member
 * handlers. If the parser encounters a string that, when encoded in the
 * string encoding, is longer than the maximum string length, it triggers
 * the JSON_TooLongString error.
 *
 * The default value of this setting is SIZE_MAX.
 *
 * This setting cannot be changed once the parser has started parsing.
 */
JSON_API(size_t) JSON_Parser_GetMaxStringLength(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetMaxStringLength(JSON_Parser parser, size_t maxLength);

/* Get and set the maximum length of numbers that a parser instance allows.
 *
 * This setting controls the maximum length, in bytes, of the numbers that
 * are passed to the number handler. If the parser encounters a number that,
 * when encoded in ASCII, is longer than the maximum number length, it
 * triggers the JSON_TooLongNumber error.
 *
 * The default value of this setting is SIZE_MAX.
 *
 * This setting cannot be changed once the parser has started parsing.
 */
JSON_API(size_t) JSON_Parser_GetMaxNumberLength(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetMaxNumberLength(JSON_Parser parser, size_t maxLength);

/* Get and set whether a parser instance allows the input to begin with a
 * byte-order-mark (BOM).
 *
 * RFC 4627 does not allow JSON text to begin with a BOM, but some clients
 * may find it convenient to be lenient in this regard; for example, if the
 * JSON text is being read from a file that has a BOM.
 *
 * The default value of this setting is JSON_False.
 *
 * This setting cannot be changed once the parser has started parsing.
 */
JSON_API(JSON_Boolean) JSON_Parser_GetAllowBOM(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetAllowBOM(JSON_Parser parser, JSON_Boolean allowBOM);

/* Get and set whether a parser instance allows Javascript-style comments to
 * appear in the JSON text.
 *
 * RFC 4627 does not allow JSON text to contain comments, but some clients
 * may find it useful to allow them.
 *
 * Both types of comment described by ECMA-262 (multi-line and single-line)
 * are supported.
 *
 * The default value of this setting is JSON_False.
 *
 * This setting cannot be changed once the parser has started parsing.
 */
JSON_API(JSON_Boolean) JSON_Parser_GetAllowComments(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetAllowComments(JSON_Parser parser, JSON_Boolean allowComments);

/* Get and set whether a parser instance allows the "special" number literals
 * NaN, Infinity, and -Infinity.
 *
 * RFC 4627 does not provide any way to represent NaN, Infinity, or -Infinity,
 * but some clients may find it convenient to recognize these as literals,
 * since they are emitted by many common JSON generators.
 *
 * The default value of this setting is JSON_False.
 *
 * This setting cannot be changed once the parser has started parsing.
 */
JSON_API(JSON_Boolean) JSON_Parser_GetAllowSpecialNumbers(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetAllowSpecialNumbers(JSON_Parser parser, JSON_Boolean allowSpecialNumbers);

/* Get and set whether a parser instance allows hexadecimal notation to be
 * used for specifying number values.
 *
 * RFC 4627 does not allow hexadecimal numbers, but some clients may find it
 * convenient to allow them, in order to represent binary bit patterns more
 * easily.
 *
 * The parser recognizes hexadecimal numbers that conform to the syntax of
 * HexIntegerLiteral, as described in section 7.8.3 of ECMA-262. That is, a
 * valid hexadecimal number must comprise the prefix '0x' or '0X', followed
 * by a sequence of one or more of the following characters: '0' - '9',
 * 'a' - 'f', and 'A' - 'F'.
 *
 * Hexadecimal numbers cannot be prefixed by a minus sign.
 *
 * The default value of this setting is JSON_False.
 *
 * This setting cannot be changed once the parser has started parsing.
 */
JSON_API(JSON_Boolean) JSON_Parser_GetAllowHexNumbers(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetAllowHexNumbers(JSON_Parser parser, JSON_Boolean allowHexNumbers);

/* Get and set whether a parser instance replaces invalid encoding sequences
 * it encounters in the input stream with the Unicode replacement character
 * U+FFFD rather than triggering an error.
 *
 * The default value of this setting is JSON_False.
 *
 * This setting cannot be changed once the parser has started parsing.
 */
JSON_API(JSON_Boolean) JSON_Parser_GetReplaceInvalidEncodingSequences(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetReplaceInvalidEncodingSequences(JSON_Parser parser, JSON_Boolean replaceInvalidEncodingSequences);

/* Get and set whether a parser instance tracks object member names for all
 * open objects and detects duplicate members if any occur in the input.
 *
 * RFC 4627 stipulates that JSON parsers SHOULD check for duplicates, but
 * may opt not to in light of reasonable implementation considerations.
 * Checking for duplicate members necessarily incurs non-trivial memory
 * overhead, and is therefore not enabled by default. Most clients use
 * their parse handlers to build some sort of in-memory DOM representation
 * of the JSON text and therefore already have the means to check for
 * duplicate member names without incurring additional memory overhead; it
 * is recommended that these clients implement duplicate member checking
 * in their object member handler (refer to SetObjectMemberHandler() for
 * details) and leave this setting disabled.
 *
 * The default value of this setting is JSON_False.
 *
 * This setting cannot be changed once the parser has started parsing.
 */
JSON_API(JSON_Boolean) JSON_Parser_GetTrackObjectMembers(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetTrackObjectMembers(JSON_Parser parser, JSON_Boolean trackObjectMembers);

/* Get the type of error, if any, encountered by a parser instance.
 *
 * If the parser encountered an error while parsing input, this function
 * returns the type of the error. Otherwise, this function returns
 * JSON_Error_None.
 */
JSON_API(JSON_Error) JSON_Parser_GetError(JSON_Parser parser);

/* Get the location in the input stream at which a parser instance
 * encountered an error.
 *
 * If the parser encountered an error while parsing input, this function
 * sets the members of the structure pointed to by pLocation to the location
 * in the input stream at which the error occurred and returns success.
 * Otherwise, it leaves the members unchanged and returns failure.
 */
JSON_API(JSON_Status) JSON_Parser_GetErrorLocation(JSON_Parser parser, JSON_Location* pLocation);

/* Get the location in the input stream of the token that is currently
 * being handled by one of a parser instance's parse handlers.
 *
 * If the parser is inside a parse handler, this function sets the members
 * of the structure pointed to by pLocation to the location in the input
 * stream of the token that triggered the handler and returns success.
 * Otherwise, it leaves the members unchanged and returns failure.
 */
JSON_API(JSON_Status) JSON_Parser_GetTokenLocation(JSON_Parser parser, JSON_Location* pLocation);

/* Parse handlers are callbacks that the client provides in order to
 * be notified about the structure of the JSON document as it is being
 * parsed. The following notes apply equally to all parse handlers:
 *
 *   1. Parse handlers are optional. In fact, a parser with no parse
 *      handlers at all can be used to simply validate that the input
 *      is valid JSON.
 *
 *   2. Parse handlers can be set, unset, or changed at any time, even
 *      from inside a parse handler.
 *
 *   3. If a parse handler returns JSON_Parser_Abort, the parser will
 *      abort the parse, set its error to JSON_Error_AbortedByHandler,
 *      set its error location to the start of the token that triggered
 *      the handler, and return JSON_Failure from the outer call to
 *      JSON_Parser_Parse().
 *
 *   4. A parse handler can get the location in the input stream of the
 *      token that triggered the handler by calling
 *      JSON_Parser_GetTokenLocation().
 */

/* Values returned by parse handlers to indicate whether parsing should
 * continue or be aborted.
 *
 * Note that JSON_TreatAsDuplicateObjectMember should only be returned by
 * object member handlers. Refer to JSON_Parser_SetObjectMemberHandler()
 * for details.
 */
typedef enum tag_JSON_Parser_HandlerResult
{
    JSON_Parser_Continue                     = 0,
    JSON_Parser_Abort                        = 1,
    JSON_Parser_TreatAsDuplicateObjectMember = 2
} JSON_Parser_HandlerResult;

/* Get and set the handler that is called when a parser instance detects the
 * input encoding.
 *
 * If the parser instance's input encoding was set to JSON_UnknownEncoding
 * when parsing began, this handler will be called as soon as the actual
 * input encoding has been detected.
 *
 * Note that JSON_Parser_GetTokenLocation() will return failure if called
 * from inside this handler, since there is no token associated with this
 * event.
 */
typedef JSON_Parser_HandlerResult (JSON_CALL * JSON_Parser_EncodingDetectedHandler)(JSON_Parser parser);
JSON_API(JSON_Parser_EncodingDetectedHandler) JSON_Parser_GetEncodingDetectedHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetEncodingDetectedHandler(JSON_Parser parser, JSON_Parser_EncodingDetectedHandler handler);

/* Get and set the handler that is called when a parser instance encounters
 * a JSON null literal value.
 */
typedef JSON_Parser_HandlerResult (JSON_CALL * JSON_Parser_NullHandler)(JSON_Parser parser);
JSON_API(JSON_Parser_NullHandler) JSON_Parser_GetNullHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetNullHandler(JSON_Parser parser, JSON_Parser_NullHandler handler);

/* Get and set the handler that is called when a parser instance encounters
 * a JSON boolean value (true or false).
 */
typedef JSON_Parser_HandlerResult (JSON_CALL * JSON_Parser_BooleanHandler)(JSON_Parser parser, JSON_Boolean value);
JSON_API(JSON_Parser_BooleanHandler) JSON_Parser_GetBooleanHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetBooleanHandler(JSON_Parser parser, JSON_Parser_BooleanHandler handler);

/* Get and set the handler that is called when a parser instance encounters
 * a JSON string value.
 *
 * The pBytes parameter points to a buffer containing the string value,
 * encoded according to the parser instance's string encoding setting. The
 * buffer is null-terminated (the null terminator character is also encoded).
 * Note, however, that JSON strings may contain embedded null characters,
 * which are specifiable using the escape sequence \u0000.
 *
 * The length parameter specifies the number of bytes (NOT characters) in
 * the encoded string, not including the encoded null terminator.
 *
 * The attributes parameter provides information about the characters
 * that comprise the string. If the option to replace invalid encoding
 * sequences is enabled and the string contains any Unicode replacement
 * characters (U+FFFD) that were the result of replacing invalid encoding
 * sequences in the input, the attributes will include the value
 * JSON_ContainsReplacedCharacter. Note that the absence of this attribute
 * does not imply that the string does not contain any U+FFFD characters,
 * since such characters may have been present in the original input, and
 * not inserted by a replacement operation.
 */
typedef JSON_Parser_HandlerResult (JSON_CALL * JSON_Parser_StringHandler)(JSON_Parser parser, const char* pBytes, size_t length, JSON_StringAttributes attributes);
JSON_API(JSON_Parser_StringHandler) JSON_Parser_GetStringHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetStringHandler(JSON_Parser parser, JSON_Parser_StringHandler handler);

/* Get and set the handler that is called when a parser instance encounters
 * a JSON number value.
 *
 * JSON numbers do not have a defined binary representation or precision,
 * and different clients may wish to interpret them differently, for
 * example, as IEEE 754 doubles, 64-bit integers, or arbitrary-precision
 * bignums. For this reason, the parser does not attempt to interpret
 * number values, but leaves this to the client.
 *
 * The pValue parameter points to a null-terminated buffer containing the
 * number value's text as it appeared in the input. The buffer is always
 * encoded as ASCII, regardless of the parser instance's encoding settings.
 * The text is guaranteed to contain only characters allowed in JSON number
 * values, that is: '0' - '9', '+', '-', '.', 'e', and 'E'; if the option
 * to allow hex numbers is enabled, the text may also contain the characters
 * 'x', 'X', 'a' - 'f', and 'A' - 'F'.
 *
 * The length parameter specifies the number of bytes (which is also
 * the number of characters) in the buffer, not including the encoded null
 * terminator.
 *
 * The attributes parameter provides information about the number.
 */
typedef JSON_Parser_HandlerResult (JSON_CALL * JSON_Parser_NumberHandler)(JSON_Parser parser, const char* pValue, size_t length, JSON_NumberAttributes attributes);
JSON_API(JSON_Parser_NumberHandler) JSON_Parser_GetNumberHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetNumberHandler(JSON_Parser parser, JSON_Parser_NumberHandler handler);

/* Get and set the handler that is called when a parser instance encounters
 * one of the "special" number literals NaN, Infinity, and -Inifinity.
 */
typedef JSON_Parser_HandlerResult (JSON_CALL * JSON_Parser_SpecialNumberHandler)(JSON_Parser parser, JSON_SpecialNumber value);
JSON_API(JSON_Parser_SpecialNumberHandler) JSON_Parser_GetSpecialNumberHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetSpecialNumberHandler(JSON_Parser parser, JSON_Parser_SpecialNumberHandler handler);

/* Get and set the handler that is called when a parser instance encounters
 * the left curly brace that starts an object.
 */
typedef JSON_Parser_HandlerResult (JSON_CALL * JSON_Parser_StartObjectHandler)(JSON_Parser parser);
JSON_API(JSON_Parser_StartObjectHandler) JSON_Parser_GetStartObjectHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetStartObjectHandler(JSON_Parser parser, JSON_Parser_StartObjectHandler handler);

/* Get and set the handler that is called when a parser instance encounters
 * the right curly brace that ends an object.
 */
typedef JSON_Parser_HandlerResult (JSON_CALL * JSON_Parser_EndObjectHandler)(JSON_Parser parser);
JSON_API(JSON_Parser_EndObjectHandler) JSON_Parser_GetEndObjectHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetEndObjectHandler(JSON_Parser parser, JSON_Parser_EndObjectHandler handler);

/* Get and set the handler that is called when a parser instance encounters
 * an object member name.
 *
 * The pBytes parameter points to a buffer containing the member name,
 * encoded according to the parser instance's string encoding setting. The
 * buffer is null-terminated (the null terminator character is also encoded).
 * Note, however, that JSON strings may contain embedded null characters,
 * which are specifiable using the escape sequence \u0000.
 *
 * The length parameter specifies the number of bytes (NOT characters) in
 * the encoded string, not including the encoded null terminator.
 *
 * The attributes parameter provides information about the characters
 * that comprise the string. If the option to replace invalid encoding
 * sequences is enabled and the string contains any Unicode replacement
 * characters (U+FFFD) that were the result of replacing invalid encoding
 * sequences in the input, the attributes will include the value
 * JSON_ContainsReplacedCharacter. Note that the absence of this attribute
 * does not imply that the string does not contain any U+FFFD characters,
 * since such characters may have been present in the original input, and
 * not inserted by a replacement operation.
 *
 * The handler can return JSON_Parser_TreatAsDuplicateObjectMember to
 * indicate that the current object already contains a member with the
 * specified name. This allows clients to implement duplicate member
 * checking without incurring the additional memory overhead associated
 * with enabling the TrackObjectMembers setting.
 */
typedef JSON_Parser_HandlerResult (JSON_CALL * JSON_Parser_ObjectMemberHandler)(JSON_Parser parser, const char* pBytes, size_t length, JSON_StringAttributes attributes);
JSON_API(JSON_Parser_ObjectMemberHandler) JSON_Parser_GetObjectMemberHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetObjectMemberHandler(JSON_Parser parser, JSON_Parser_ObjectMemberHandler handler);

/* Get and set the handler that is called when a parser instance encounters
 * the left square brace that starts an array.
 */
typedef JSON_Parser_HandlerResult (JSON_CALL * JSON_Parser_StartArrayHandler)(JSON_Parser parser);
JSON_API(JSON_Parser_StartArrayHandler) JSON_Parser_GetStartArrayHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetStartArrayHandler(JSON_Parser parser, JSON_Parser_StartArrayHandler handler);

/* Get and set the handler that is called when a parser instance encounters
 * the right square brace that ends an array.
 */
typedef JSON_Parser_HandlerResult (JSON_CALL * JSON_Parser_EndArrayHandler)(JSON_Parser parser);
JSON_API(JSON_Parser_EndArrayHandler) JSON_Parser_GetEndArrayHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetEndArrayHandler(JSON_Parser parser, JSON_Parser_EndArrayHandler handler);

/* Get and set the handler that is called when a parser instance encounters
 * an array item.
 *
 * This event is always immediately followed by a null, boolean, string,
 * number, special number, start object, or start array event.
 */
typedef JSON_Parser_HandlerResult (JSON_CALL * JSON_Parser_ArrayItemHandler)(JSON_Parser parser);
JSON_API(JSON_Parser_ArrayItemHandler) JSON_Parser_GetArrayItemHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_Parser_SetArrayItemHandler(JSON_Parser parser, JSON_Parser_ArrayItemHandler handler);

/* Push zero or more bytes of input to a parser instance.
 *
 * The length parameter specifies the number of bytes (NOT characters)
 * pointed to by pBytes. pBytes may be NULL if and only if length is 0.
 *
 * The isFinal parameter indicates whether any more input is forthcoming.
 *
 * This function returns failure if the parser parameter is null, if the
 * function was called reentrantly from inside a handler, or if the
 * parser instance has already finished parsing.
 */
JSON_API(JSON_Status) JSON_Parser_Parse(JSON_Parser parser, const char* pBytes, size_t length, JSON_Boolean isFinal);

/* Writer instance. */
struct JSON_Writer_Data; /* opaque data */
typedef struct JSON_Writer_Data* JSON_Writer;

/* Create a writer instance.
 *
 * If pMemorySuite is null, the library will use the C runtime realloc() and
 * free() as the writer's memory management suite. Otherwise, all the
 * handlers in the memory suite must be non-null or the call will fail and
 * return null.
 */
JSON_API(JSON_Writer) JSON_Writer_Create(const JSON_MemorySuite* pMemorySuite);

/* Free a writer instance.
 *
 * Every successful call to JSON_Writer_Create() must eventually be paired
 * with a call to JSON_Writer_Free() in order to avoid leaking memory.
 *
 * This function returns failure if the writer parameter is null or if the
 * function was called reentrantly from inside a handler.
 */
JSON_API(JSON_Status) JSON_Writer_Free(JSON_Writer writer);

/* Reset a writer instance so that it can be used to write a new output
 * stream.
 *
 * This function returns failure if the writer parameter is null or if the
 * function was called reentrantly from inside a handler.
 *
 * After a writer is reset, its state is indistinguishable from its state
 * when it was returned by JSON_Writer_Create(). The writer's custom memory
 * suite, if any, is preserved; all other settings, state, and handlers are
 * restored to their default values.
 */
JSON_API(JSON_Status) JSON_Writer_Reset(JSON_Writer writer);

/* Get and set the user data value associated with a writer instance.
 *
 * This setting allows clients to associate additional data with a
 * writer instance. The writer itself does not use the value.
 *
 * The default value of this setting is NULL.
 *
 * This setting can be changed at any time, even inside handlers.
 */
JSON_API(void*) JSON_Writer_GetUserData(JSON_Writer writer);
JSON_API(JSON_Status) JSON_Writer_SetUserData(JSON_Writer writer, void* userData);

/* Get and set the output encoding for a writer instance.
 *
 * The default value of this setting is JSON_UTF8.
 *
 * This setting cannot be set to JSON_UnknownEncoding.
 *
 * This setting cannot be changed once the writer has started writing.
 */
JSON_API(JSON_Encoding) JSON_Writer_GetOutputEncoding(JSON_Writer writer);
JSON_API(JSON_Status) JSON_Writer_SetOutputEncoding(JSON_Writer writer, JSON_Encoding encoding);

/* Get and set whether a writer instance uses CARRIAGE RETURN, LINE FEED
 * (CRLF) as the new line sequence generated by JSON_Writer_WriteNewLine().
 *
 * The default value of this setting is JSON_False.
 *
 * This setting cannot be changed once the writer has started writing.
 */
JSON_API(JSON_Boolean) JSON_Writer_GetUseCRLF(JSON_Writer writer);
JSON_API(JSON_Status) JSON_Writer_SetUseCRLF(JSON_Writer writer, JSON_Boolean useCRLF);

/* Get and set whether a writer instance replaces invalid encoding sequences
 * it encounters in string tokens with the Unicode replacement character
 * U+FFFD rather than triggering an error.
 *
 * The default value of this setting is JSON_False.
 *
 * This setting cannot be changed once the writer has started writing.
 */
JSON_API(JSON_Boolean) JSON_Writer_GetReplaceInvalidEncodingSequences(JSON_Writer writer);
JSON_API(JSON_Status) JSON_Writer_SetReplaceInvalidEncodingSequences(JSON_Writer writer, JSON_Boolean replaceInvalidEncodingSequences);

/* Get the type of error, if any, encountered by a writer instance.
 *
 * If the writer encountered an error while writing input, this function
 * returns the type of the error. Otherwise, this function returns
 * JSON_Error_None.
 */
JSON_API(JSON_Error) JSON_Writer_GetError(JSON_Writer writer);

/* The JSON_Writer_WriteXXX() family of functions cause JSON text to be
 * sent to a writer instance's output handler. The following notes apply
 * equally to all these functions:
 *
 *   1. The output handler is optional, and can be set, unset, or changed
 *      at any time, even from inside the output handler.
 *
 *   2. A single call to JSON_Writer_WriteXXX() may trigger multiple calls
 *      to the output handler.
 *
 *   3. All output generated by a call to JSON_Writer_WriteXXX() is sent
 *      to the output handler before the call returns; that is, the writer
 *      does not aggregate output from multiple writes before sending it to
 *      the output handler.
 *
 *   4. A call to JSON_Writer_WriteXXX() will fail if the writer has
 *      already encountered an error.
 *
 *   5. A call to JSON_Writer_WriteXXX() will fail if the call was made
 *      reentrantly from inside a handler.
 *
 *   6. A call to JSON_Writer_WriteXXX() will fail if it would cause the
 *      writer to output grammatically-incorrect JSON text.
 *
 *   7. If an output handler returns JSON_Writer_Abort, the writer will
 *      abort the write, set its error to JSON_Error_AbortedByHandler,
 *      set its error location to the location in the output stream prior
 *      to the call to the handler, and return JSON_Failure from the outer
 *      call to JSON_Writer_WriteXXX().
 */

/* Values returned by write handlers to indicate whether writing should
 * continue or be aborted.
 */
typedef enum tag_JSON_Writer_HandlerResult
{
    JSON_Writer_Continue = 0,
    JSON_Writer_Abort    = 1
} JSON_Writer_HandlerResult;

/* Get and set the handler that is called when a writer instance has output
 * ready to be written.
 *
 * The pBytes parameter points to a buffer containing the bytes to be written,
 * encoded according to the writer instance's output encoding setting. The
 * buffer is NOT null-terminated.
 *
 * The length parameter specifies the number of bytes (NOT characters) in
 * the encoded output.
 */
typedef JSON_Writer_HandlerResult (JSON_CALL * JSON_Writer_OutputHandler)(JSON_Writer writer, const char* pBytes, size_t length);
JSON_API(JSON_Writer_OutputHandler) JSON_Writer_GetOutputHandler(JSON_Writer writer);
JSON_API(JSON_Status) JSON_Writer_SetOutputHandler(JSON_Writer writer, JSON_Writer_OutputHandler handler);

/* Write the JSON null literal to the output. */
JSON_API(JSON_Status) JSON_Writer_WriteNull(JSON_Writer writer);

/* Write a JSON boolean value to the output. */
JSON_API(JSON_Status) JSON_Writer_WriteBoolean(JSON_Writer writer, JSON_Boolean value);

/* Write a JSON string value to the output.
 *
 * The pBytes parameter points to a buffer containing the string to be
 * written. The buffer does NOT need to be null-terminated. This
 * parameter can be null if and only if the length parameter is zero.
 *
 * The length parameter specifies the number of bytes (NOT characters)
 * in the buffer. If the buffer is null-terminated, the length should
 * NOT include the null terminator.
 *
 * The encoding parameter specifies the encoding of the text pointed
 * to by pBytes. This parameter cannot be JSON_UnknownEncoding.
 *
 * If the string contains invalid encoding sequences and the option to
 * replace invalid encoding sequences with the Unicode replacement
 * character (U+FFFD) is not enabled for the writer instance, the writer
 * sets its error to JSON_Error_InvalidEncodingSequence and returns
 * failure.
 *
 * The writer escapes the following codepoints:
 *
 * - BACKSPACE (U+0008)       => \b
 * - TAB (U+0009)             => \t
 * - LINE FEED (U+000A)       => \n
 * - FORM FEED (U+000C)       => \f
 * - CARRIAGE RETURN (U+000D) => \r
 * - QUOTATION MARK (U+0022)  => \"
 * - SOLIDUS (U+002F)         => \/
 * - REVERSE SOLIDUS (U+005C) => \\
 *
 * The writer also escapes the following codepoints using hex-style escape
 * sequences:
 *
 * - All control characters (U+0000 - U+001F) except those covered by the
 *   list above.
 * - DELETE (U+007F)
 * - LINE SEPARATOR (U+2028)
 * - PARAGRAPH SEPARATOR (U+2029)
 * - All 34 Unicode "noncharacter" codepoints whose values end in FE or FF.
 * - All 32 Unicode "noncharacter" codepoints in the range U+FDD0 - U+FDEF.
 * - REPLACEMENT CHARACTER (U+FFFD), if and only if it did not appear in the
 *   original string provided by the client; in other words, if the writer
 *   introduced it in the output as a replacement for an invalid encoding
 *   sequence in the original string.
 */
JSON_API(JSON_Status) JSON_Writer_WriteString(JSON_Writer writer, const char* pBytes, size_t length, JSON_Encoding encoding);

/* Write a JSON number value to the output.
 *
 * The pValue parameter points to a buffer containing the number's JSON
 * text representation, encoded as ASCII. The buffer does NOT need to be
 * null-terminated. This parameter cannot be null.
 *
 * The length parameter specifies the number of bytes (which is also the
 * number of characters) in the buffer. If the buffer is null-terminated,
 * the length should NOT include the null terminator. This parameter
 * cannot be zero.
 *
 * The number must be a valid JSON number as described by RFC 4627, or a
 * hexadecimal number conforming to the syntax of HexIntegerLiteral, as
 * described in section 7.8.3 of ECMA-262. Otherwise, the writer sets its
 * error to JSON_Error_InvalidNumber and returns failure.
 */
JSON_API(JSON_Status) JSON_Writer_WriteNumber(JSON_Writer writer, const char* pValue, size_t length);

/* Write a JSON "special" number literal to the output. */
JSON_API(JSON_Status) JSON_Writer_WriteSpecialNumber(JSON_Writer writer, JSON_SpecialNumber value);

/* Write a left curly-brace character to the output. */
JSON_API(JSON_Status) JSON_Writer_WriteStartObject(JSON_Writer writer);

/* Write a right curly-brace character to the output. */
JSON_API(JSON_Status) JSON_Writer_WriteEndObject(JSON_Writer writer);

/* Write a left square-brace character to the output. */
JSON_API(JSON_Status) JSON_Writer_WriteStartArray(JSON_Writer writer);

/* Write a right square-brace character to the output. */
JSON_API(JSON_Status) JSON_Writer_WriteEndArray(JSON_Writer writer);

/* Write a colon character to the output. */
JSON_API(JSON_Status) JSON_Writer_WriteColon(JSON_Writer writer);

/* Write a comma character to the output. */
JSON_API(JSON_Status) JSON_Writer_WriteComma(JSON_Writer writer);

/* Write space characters to the output. */
JSON_API(JSON_Status) JSON_Writer_WriteSpace(JSON_Writer writer, size_t numberOfSpaces);

/* Write a newline sequence to the output. */
JSON_API(JSON_Status) JSON_Writer_WriteNewLine(JSON_Writer writer);

/* Get a constant, null-terminated, ASCII string describing an error code. */
JSON_API(const char*) JSON_ErrorString(JSON_Error error);

#ifdef __cplusplus
}
#endif

#endif /* JSONSAX_H_INCLUDED */
