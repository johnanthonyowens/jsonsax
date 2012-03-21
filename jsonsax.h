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
 * JSON parser that uses callbacks to notify its client of the structure
 * represented by JSON text as it is parsed.
 *
 * JSONSAX stands for "JSON Streamed Ala eXpat", and is so named because its
 * usage  is patterned after the "SAX" style implemented by the venerable
 * Expat XML parser.
 *
 * JSONSAX is designed to be lightweight, portable, robust, fast, and have
 * minimal memory overhead, suitable for memory-constrained environments such
 * as embedded systems. It has no external dependencies other than the
 * standard ANSI C runtime library.
 *
 * Callback-based parsers are significantly more difficult to use than
 * those that simply build and returns DOM representations of the input,
 * but they are useful in situations where the client wants to build a custom
 * DOM representation of the input without incurring the overhead of a
 * generic "intermediate" DOM representation built by the parser, or where the
 * client wants to perform processing that doesn't require creating any kind
 * of DOM at all.
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
 * 2. Number values are limited to 63 characters in length (imposing limits
 *    on the length of numbers is permitted by RFC 4637).
 *
 * 3. Detection of duplicate object members is not enabled by default (to
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
 * output encoding on a parser-by-parser basis.
 *
 * The parser is strict when decoding the input stream, and will fail if it
 * encounters an encoding sequence that is not valid for the input encoding.
 * Note especially that this includes (but is not limited to) the following:
 *
 *  - Overlong encoding sequences in UTF-8.
 *  - Surrogate codepoints encoded in UTF-8 or UTF-32.
 *  - Unpaired or improperly-paired surrogates in UTF-16.
 *  - Codepoints outside the Unicode range encoded in UTF-8 or UTF-32.
 *
 * The JSONSAX library is licensed under the MIT License.
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
 * includes jsax.h.
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

/* Values returned by parse handlers to indicate whether parsing should
 * continue or be aborted.
 *
 * Note that JSON_TreatAsDuplicateObjectMember should only be returned by
 * object member handlers. Refer to JSON_SetObjectMemberHandler() for details.
 */
typedef enum tag_JSON_HandlerResult
{
    JSON_ContinueParsing              = 0,
    JSON_AbortParsing                 = 1,
    JSON_TreatAsDuplicateObjectMember = 2
} JSON_HandlerResult;

/* Error codes. */
typedef enum tag_JSON_Error
{
    JSON_Error_None                            = 0,
    JSON_Error_OutOfMemory                     = 1,
    JSON_Error_AbortedByHandler                = 2,
    JSON_Error_BOMNotAllowed                   = 3,
    JSON_Error_IncompleteInput                 = 4,
    JSON_Error_InvalidEncodingSequence         = 5,
    JSON_Error_UnknownToken                    = 6,
    JSON_Error_UnexpectedToken                 = 7,
    JSON_Error_UnescapedControlCharacter       = 8,
    JSON_Error_InvalidEscapeSequence           = 9,
    JSON_Error_UnpairedSurrogateEscapeSequence = 10,
    JSON_Error_TooLongString                   = 11,
    JSON_Error_InvalidNumber                   = 12,
    JSON_Error_TooLongNumber                   = 13,
    JSON_Error_DuplicateObjectMember           = 14
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
    JSON_ContainsNonBMPCharacter   = 1 << 3  /* U+10000 - U+10FFFF */
} JSON_StringAttribute;
typedef int JSON_StringAttributes;

/* Types of number value. */
typedef enum tag_JSON_NumberType
{
    JSON_NormalNumber     = 0,
    JSON_NaN              = 1,
    JSON_Infinity         = 2,
    JSON_NegativeInfinity = 3
} JSON_NumberType;

/* Information identifying a location in a parser instance's input stream.
 * All parse handlers receive the location of the token that triggered the
 * handler.
 */
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
} JSON_Location;

/* Parser instance. */
struct JSON_ParserData; /* opaque data */
typedef struct JSON_ParserData* JSON_Parser;

/* Custom memory management handlers.
 *
 * The semantics of these handlers correspond exactly to those of standard
 * malloc(), realloc(), and free(). The handlers also receive a parameter
 * specifying the parser instance that is making the call, which clients can
 * use to impose per-parser allocation limits, if desired.
 */
typedef void* (JSON_CALL * JSON_MallocHandler)(JSON_Parser parser, size_t size);
typedef void* (JSON_CALL * JSON_ReallocHandler)(JSON_Parser parser, void* ptr, size_t size);
typedef void (JSON_CALL * JSON_FreeHandler)(JSON_Parser parser, void* ptr);

/* A suite of custom memory management functions. */
typedef struct tag_JSON_MemorySuite
{
    JSON_MallocHandler  malloc;
    JSON_ReallocHandler realloc;
    JSON_FreeHandler    free;
} JSON_MemorySuite;

/* Create a parser instance.
 *
 * If pMemorySuite is null, the library will use malloc(), realloc(), and
 * free() as the parser's memory management suite. Otherwise, all the
 * handlers in the memory suite must be non-null or the call will fail and
 * return null.
 *
 * Note that if a custom memory suite is specified, that suite's malloc()
 * will be invoked during the call to JSON_CreateParser() in order to
 * allocate the state data for the parser itself; the value of the
 * parser parameter passed to that invocation will be null.
 */
JSON_API(JSON_Parser) JSON_CreateParser(const JSON_MemorySuite* pMemorySuite);

/* Free a parser instance.
 *
 * Every successful call to JSON_CreateParser() must eventually be paired
 * with a call to JSON_FreeParser() in order to avoid leaking memory.
 *
 * This function returns failure if the parser parameter is null or if the
 * function was called reentrantly from inside a handler.
 *
 * Note that if the parser uses a custom memory suite, that suite's free()
 * will be invoked during the call to JSON_FreeParser() in order to free the
 * state data for the parser; the value of the parser parameter passed to
 * that invocation will be null.
 */
JSON_API(JSON_Status) JSON_FreeParser(JSON_Parser parser);

/* Reset a parser instance so that it can be used to parse a new input stream.
 *
 * This function returns failure if the parser parameter is null or if the
 * function was called reentrantly from inside a handler.
 *
 * After a parser is reset, its state is indistinguishable from its state
 * when it was returned by JSON_CreateParser(). The parser's custom memory
 * suite, if any, is preserved; all other settings, state, and handlers are
 * restored to their default values.
 */
JSON_API(JSON_Status) JSON_ResetParser(JSON_Parser parser);

/* Get the parse error, if any, encountered by a parser instance.
 *
 * If the parser encountered an error while parsing input, this function
 * will return the type of the error. Otherwise, this function returns
 * JSON_Error_None.
 */
JSON_API(JSON_Error) JSON_GetError(JSON_Parser parser);

/* Get the location in the input stream at which a parser instance
 * encountered an error.
 *
 * If the parser encountered an error while parsing input, this function
 * will set the members of the structure pointed to by pLocation to the
 * location in the input stream at which the error occurred. Otherwise,
 * it will set the members to 0.
 */
JSON_API(void) JSON_GetErrorLocation(JSON_Parser parser, JSON_Location* pLocation);

/* Get the index of the byte in the input stream at which a parser instance
 * encountered an error.
 *
 * If the parser encountered an error while parsing input, this function
 * will return the zero-based index of the byte in the input stream that
 * corresponds to the error. Otherwise, it will return 0.
 */
JSON_API(size_t) JSON_GetErrorLocationByte(JSON_Parser parser);

/* Get the line number in the input stream at which a parser instance
 * encountered an error.
 *
 * If the parser encountered an error while parsing input, this function
 * will return the zero-based line number that corresponds to the error.
 * Otherwise, it will return 0.
 */
JSON_API(size_t) JSON_GetErrorLocationLine(JSON_Parser parser);

/* Get the column number in the input stream at which a parser instance
 * encountered an error.
 *
 * If the parser encountered an error while parsing input, this function
 * will return the zero-based column number that corresponds to the error.
 * Otherwise, it will return 0.
 */
JSON_API(size_t) JSON_GetErrorLocationColumn(JSON_Parser parser);

/* Get a constant, null-terminated, ASCII string describing an error code. */
JSON_API(const char*) JSON_ErrorString(JSON_Error error);

/* Get and set the user data value associated with a parser instance.
 *
 * The user data value allows clients to associate additional data with a
 * parser instance. The parser itself does not use the value.
 *
 * The default value of this setting is NULL.
 *
 * The user data value can be changed at any time, even inside handlers.
 */
JSON_API(void*) JSON_GetUserData(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetUserData(JSON_Parser parser, void* userData);

/* Get and set the handler that is called when a parser instance parses the
 * JSON value "null".
 *
 * If the handler returns JSON_AbortParsing, the parser will abort the parse,
 * set its error to JSON_Error_AbortedByHandler, and return JSON_Failure from
 * the pending call to JSON_Parse().
 *
 * The handler can be changed at any time, even inside a handler.
 */
typedef JSON_HandlerResult (JSON_CALL * JSON_NullHandler)(JSON_Parser parser, const JSON_Location* pLocation);
JSON_API(JSON_NullHandler) JSON_GetNullHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetNullHandler(JSON_Parser parser, JSON_NullHandler handler);

/* Get and set the handler that is called when a parser instance parses a
 * JSON boolean value.
 *
 * If the handler returns JSON_AbortParsing, the parser will abort the parse,
 * set its error to JSON_Error_AbortedByHandler, and return JSON_Failure from
 * the pending call to JSON_Parse().
 *
 * The handler can be changed at any time, even inside a handler.
 */
typedef JSON_HandlerResult (JSON_CALL * JSON_BooleanHandler)(JSON_Parser parser, const JSON_Location* pLocation, JSON_Boolean value);
JSON_API(JSON_BooleanHandler) JSON_GetBooleanHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetBooleanHandler(JSON_Parser parser, JSON_BooleanHandler handler);

/* Get and set the handler that is called when a parser instance parses a
 * JSON string value.
 *
 * The pBytes parameter points to a buffer containing the string value,
 * encoded according to the parser instance's output encoding setting. The
 * buffer is null-terminated (the null terminator character is also encoded).
 *
 * The length parameter specifies the number of bytes (NOT characters) in
 * the encoded string, not including the encoded null terminator.
 *
 * Note, however, that JSON strings may contain embedded null characters,
 * which are specifiable using the escape sequence \u0000.
 *
 * If the handler returns JSON_AbortParsing, the parser will abort the parse,
 * set its error to JSON_Error_AbortedByHandler, and return JSON_Failure from
 * the pending call to JSON_Parse().
 *
 * The handler can be changed at any time, even inside a handler.
 */
typedef JSON_HandlerResult (JSON_CALL * JSON_StringHandler)(JSON_Parser parser, const JSON_Location* pLocation, const char* pBytes, size_t length, JSON_StringAttributes attributes);
JSON_API(JSON_StringHandler) JSON_GetStringHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetStringHandler(JSON_Parser parser, JSON_StringHandler handler);

/* Get and set the handler that is called when a parser instance parses a
 * JSON number value and has interpreted it as an IEEE 754 double-precision
 * floating-point value.
 *
 * Clients that want to interpret number values differently should set the
 * parser instance's raw number handler instead. See SetRawNumberHandler()
 * for details.
 *
 * For all RFC 4627-compliant number values, the type parameter will be
 * JSON_NormalNumber.
 *
 * If the option to allow NaN and infinity is enabled and the value was
 * represented in the input by the literal "NaN", the value parameter will
 * be 0.0 (NOT one of the IEEE 754 NaN values) and the type parameter will
 * be JSON_NaN.
 *
 * If the option to allow NaN and infinity is enabled and the value was
 * represented in the input by the literal "Infinity", the value parameter
 * will be HUGE_VAL and the type parameter will be JSON_Infinity.
 *
 * If the option to allow NaN and infinity is enabled and the value was
 * represented in the input by the literal "-Infinity", the value parameter
 * will be -HUGE_VAL and the type parameter will be JSON_NegativeInfinity.
 *
 * Note that if the value was represented in the input by a RFC 4627-compliant
 * number value whose magnitude was simply too large to be represented by an
 * IEEE 754 double-precision value, the value parameter will be the
 * corresponding IEEE 754 infinity or negative infinity value, but the type
 * parameter will be JSON_NormalNumber.
 *
 * If the handler returns JSON_AbortParsing, the parser will abort the parse,
 * set its error to JSON_Error_AbortedByHandler, and return JSON_Failure from
 * the pending call to JSON_Parse().
 *
 * The handler can be changed at any time, even inside a handler.
 */
typedef JSON_HandlerResult (JSON_CALL * JSON_NumberHandler)(JSON_Parser parser, const JSON_Location* pLocation, double value, JSON_NumberType type);
JSON_API(JSON_NumberHandler) JSON_GetNumberHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetNumberHandler(JSON_Parser parser, JSON_NumberHandler handler);

/* Get and set the handler that is called when a parser instance parses a
 * JSON number value, before that value's text is converted to an IEEE 754
 * double-precision floating point value.
 *
 * This handler allows clients to interpret number values without being
 * constrained by the limits of IEEE 754 double-precision floating-point
 * values. For example, a client might use this handler to enable precise
 * representation of arbitrarily-large numbers using a bignum library.
 * Another client might use it to enable precise representation of all
 * 64-bit integer values.
 *
 * The pValue parameter points to a null-terminated buffer containing the
 * number value's text as it appeared in the input. The buffer is always
 * encoded as ASCII, regardless of the parser instance's input and output
 * encoding settings. The text is guaranteed to contain only characters
 * allowed in JSON number values, that is: '0' - '9', '+', '-', '.', 'e',
 * and 'E'. If the option to allow NaN and infinity is enabled, the text
 * may also be the string "NaN", "Infinity", or "-Infinity".
 *
 * Note that if this handler is set, the non-raw number handler will not be
 * called.
 *
 * If the handler returns JSON_AbortParsing, the parser will abort the parse,
 * set its error to JSON_Error_AbortedByHandler, and return JSON_Failure from
 * the pending call to JSON_Parse().
 *
 * The handler can be changed at any time, even inside a handler.
 */
typedef JSON_HandlerResult (JSON_CALL * JSON_RawNumberHandler)(JSON_Parser parser, const JSON_Location* pLocation, const char* pValue);
JSON_API(JSON_RawNumberHandler) JSON_GetRawNumberHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetRawNumberHandler(JSON_Parser parser, JSON_RawNumberHandler handler);

/* Get and set the handler that is called when a parser instance parses the
 * left curly brace that begins an object.
 *
 * If the handler returns JSON_AbortParsing, the parser will abort the parse,
 * set its error to JSON_Error_AbortedByHandler, and return JSON_Failure from
 * the pending call to JSON_Parse().
 *
 * The handler can be changed at any time, even inside a handler.
 */
typedef JSON_HandlerResult (JSON_CALL * JSON_StartObjectHandler)(JSON_Parser parser, const JSON_Location* pLocation);
JSON_API(JSON_StartObjectHandler) JSON_GetStartObjectHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetStartObjectHandler(JSON_Parser parser, JSON_StartObjectHandler handler);

/* Get and set the handler that is called when a parser instance parses the
 * right curly brace that ends an object.
 *
 * If the handler returns JSON_AbortParsing, the parser will abort the parse,
 * set its error to JSON_Error_AbortedByHandler, and return JSON_Failure from
 * the pending call to JSON_Parse().
 *
 * The handler can be changed at any time, even inside a handler.
 */
typedef JSON_HandlerResult (JSON_CALL * JSON_EndObjectHandler)(JSON_Parser parser, const JSON_Location* pLocation);
JSON_API(JSON_EndObjectHandler) JSON_GetEndObjectHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetEndObjectHandler(JSON_Parser parser, JSON_EndObjectHandler handler);

/* Get and set the handler that is called when a parser instance starts
 * parsing an object member.
 *
 * The pBytes parameter points to a buffer containing the member name,
 * encoded according to the parser instance's output encoding setting. The
 * buffer is null-terminated (the null terminator character is also encoded).
 *
 * The length parameter specifies the number of bytes (NOT characters) in
 * the encoded string, not including the encoded null terminator.
 *
 * Note, however, that JSON strings may contain embedded null characters,
 * which are specifiable using the escape sequence \u0000.
 *
 * The handler can indicate that the current object already contains a member
 * with the specified name by returning JSON_TreatAsDuplicateObjectMember.
 * This allows clients to implement duplicate member checking without
 * incurring the additional memory overhead associated with enabling the
 * TrackObjectMembers setting.
 *
 * If the handler returns JSON_AbortParsing, the parser will abort the parse,
 * set its error to JSON_Error_AbortedByHandler, and return JSON_Failure from
 * the pending call to JSON_Parse().
 *
 * The handler can be changed at any time, even inside a handler.
 */
typedef JSON_HandlerResult (JSON_CALL * JSON_ObjectMemberHandler)(JSON_Parser parser, const JSON_Location* pLocation, const char* pBytes, size_t length, JSON_StringAttributes attributes);
JSON_API(JSON_ObjectMemberHandler) JSON_GetObjectMemberHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetObjectMemberHandler(JSON_Parser parser, JSON_ObjectMemberHandler handler);

/* Get and set the handler that is called when a parser instance parses the
 * left square brace that begins an array.
 *
 * If the handler returns JSON_AbortParsing, the parser will abort the parse,
 * set its error to JSON_Error_AbortedByHandler, and return JSON_Failure from
 * the pending call to JSON_Parse().
 *
 * The handler can be changed at any time, even inside a handler.
 */
typedef JSON_HandlerResult (JSON_CALL * JSON_StartArrayHandler)(JSON_Parser parser, const JSON_Location* pLocation);
JSON_API(JSON_StartArrayHandler) JSON_GetStartArrayHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetStartArrayHandler(JSON_Parser parser, JSON_StartArrayHandler handler);

/* Get and set the handler that is called when a parser instance parses the
 * right square brace that ends an array.
 *
 * If the handler returns JSON_AbortParsing, the parser will abort the parse,
 * set its error to JSON_Error_AbortedByHandler, and return JSON_Failure from
 * the pending call to JSON_Parse().
 *
 * The handler can be changed at any time, even inside a handler.
 */
typedef JSON_HandlerResult (JSON_CALL * JSON_EndArrayHandler)(JSON_Parser parser, const JSON_Location* pLocation);
JSON_API(JSON_EndArrayHandler) JSON_GetEndArrayHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetEndArrayHandler(JSON_Parser parser, JSON_EndArrayHandler handler);

/* Get and set the handler that is called when a parser instance starts
 * parsing an array item.
 *
 * If the handler returns JSON_AbortParsing, the parser will abort the parse,
 * set its error to JSON_Error_AbortedByHandler, and return JSON_Failure from
 * the pending call to JSON_Parse().
 *
 * The handler can be changed at any time, even inside a handler.
 */
typedef JSON_HandlerResult (JSON_CALL * JSON_ArrayItemHandler)(JSON_Parser parser, const JSON_Location* pLocation);
JSON_API(JSON_ArrayItemHandler) JSON_GetArrayItemHandler(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetArrayItemHandler(JSON_Parser parser, JSON_ArrayItemHandler handler);

/* Get whether JSON_Parse() has been called on a parser instance since that
 * instance was created or reset.
 */
JSON_API(JSON_Boolean) JSON_StartedParsing(JSON_Parser parser);

/* Get whether a parser instance has finished parsing.
 *
 * The parser finishes parsing when JSON_Parse() returns failure, or when the
 * isFinal parameter to JSON_Parse() is specified as JSON_True and the call
 * returns success.
 */
JSON_API(JSON_Boolean) JSON_FinishedParsing(JSON_Parser parser);

/* Get and set the input encoding for a parser instance.
 *
 * The default value of this setting is JSON_UnknownEncoding.
 *
 * If the client does not explicitly set the input encoding before calling
 * JSON_Parse() on the parser instance, the parser will use the first 4 bytes
 * of input to detect the  input encoding automatically. Once the parser has
 * detected the encoding, calls to JSON_GetInputEncoding() will return the
 * detected value.
 *
 * Note that calls to JSON_SetInputEncoding() will return failure if the
 * parser has started parsing.
 */
JSON_API(JSON_Encoding) JSON_GetInputEncoding(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetInputEncoding(JSON_Parser parser, JSON_Encoding encoding);

/* Get and set the output encoding for a parser instance.
 *
 * The output encoding controls the encoding of the string values that are
 * passed to the string and object member handlers.
 *
 * The default value of this setting is JSON_UTF8.
 *
 * The output encoding cannot be set to JSON_UnknownEncoding.
 *
 * Note that calls to JSON_SetOutputEncoding() will return failure if the
 * parser has started parsing.
 */
JSON_API(JSON_Encoding) JSON_GetOutputEncoding(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetOutputEncoding(JSON_Parser parser, JSON_Encoding encoding);

/* Get and set the maximum length of strings that a parser instance outputs.
 *
 * The maximum output string length controls the maximum length, in bytes
 * (NOT characters), of the strings that are passed to the string and object
 * member handlers. If the parser encounters a string that, when encoded in
 * the output encoding, is longer than the maximum output string length, it
 * will trigger the JSON_TooLongString error.
 *
 * The default value of this setting is SIZE_MAX.
 *
 * Note that calls to JSON_SetMaxOutputStringLength() will return failure if
 * the parser has started parsing.
 */
JSON_API(size_t) JSON_GetMaxOutputStringLength(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetMaxOutputStringLength(JSON_Parser parser, size_t maxLength);

/* Get and set whether a parser instance allows the input to begin with a
 * byte-order-mark (BOM).
 *
 * RFC 4627 does not allow JSON text to begin with a BOM, but some clients
 * may find it convenient to be lenient in this regard; for example, if the
 * JSON text is being read from a file that has a BOM.
 *
 * The default value of this setting is JSON_False.
 *
 * Note that calls to JSON_SetAllowBOM() will return failure if the parser has
 * started parsing.
 */
JSON_API(JSON_Boolean) JSON_GetAllowBOM(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetAllowBOM(JSON_Parser parser, JSON_Boolean allowBOM);

/* Get and set whether a parser instance allows a comma to be present after
 * the last member of an object and the last item in an array.
 *
 * RFC 4627 does not allow trailing commas, but some clients may find it
 * convenient to be lenient if the input was generated by a poorly-behaved
 * JSON generator.
 *
 * The default value of this setting is JSON_False.
 *
 * Note that calls to JSON_SetAllowTrailingCommas() will return failure if the
 * parser has started parsing.
 */
JSON_API(JSON_Boolean) JSON_GetAllowTrailingCommas(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetAllowTrailingCommas(JSON_Parser parser, JSON_Boolean allowTrailingCommas);

/* Get and set whether a parser instance allows NaN, Infinity, and -Infinity
 * as number values.
 *
 * RFC 4627 does not provide any way to represent NaN, Infinity, or -Infinity,
 * but some clients may find it convenient to recognize these as literals,
 * since they are emitted by many common JSON generators.
 *
 * The default value of this setting is JSON_False.
 *
 * Note that calls to JSON_SetAllowNaNAndInfinity() will return failure if the
 * parser has started parsing.
 */
JSON_API(JSON_Boolean) JSON_GetAllowNaNAndInfinity(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetAllowNaNAndInfinity(JSON_Parser parser, JSON_Boolean allowNaNAndInfinity);

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
 * Note that calls to JSON_SetTrackObjectMembers() will return failure if the
 * parser has started parsing.
 */
JSON_API(JSON_Boolean) JSON_GetTrackObjectMembers(JSON_Parser parser);
JSON_API(JSON_Status) JSON_SetTrackObjectMembers(JSON_Parser parser, JSON_Boolean trackObjectMembers);

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
JSON_API(JSON_Status) JSON_Parse(JSON_Parser parser, const char* pBytes, size_t length, JSON_Boolean isFinal);

#ifdef __cplusplus
}
#endif

#endif /* JSONSAX_H_INCLUDED */
