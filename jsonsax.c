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

#include <stdlib.h>
#include <memory.h>
#include <locale.h>
#include <math.h>

/* Mark APIs for export (as opposed to import) when we build this file. */
#define JSON_BUILDING
#include "jsonsax.h"

/* Parser constants. */
#define MAX_NUMBER_CHARACTERS         63
#define DEFAULT_OUTPUT_BUFFER_LENGTH  (MAX_NUMBER_CHARACTERS + 1)
#define DEFAULT_SYMBOL_STACK_SIZE     32
#define IEEE_DOUBLE_MANTISSA_BITS     53
#define ERROR_LOCATION_IS_TOKEN_START 0xFF

/* 32- and 64-bit unsigned integer types (compiler-dependent). */
#if defined(_MSC_VER)
typedef unsigned __int32 JSON_UInt32;
typedef unsigned __int64 JSON_UInt64;
#else
#include <stdint.h>
typedef uint32_t JSON_UInt32;
typedef uint64_t JSON_UInt64;
#endif

/* Especially-relevant Unicode codepoints. */
#define BACKSPACE_CODEPOINT             0x0008
#define TAB_CODEPOINT                   0x0009
#define LINE_FEED_CODEPOINT             0x000A
#define FORM_FEED_CODEPOINT             0x000C
#define CARRIAGE_RETURN_CODEPOINT       0x000D
#define FIRST_NON_CONTROL_CODEPOINT     0x0020
#define FIRST_NON_ASCII_CODEPOINT       0x0080
#define FIRST_2_BYTE_UTF8_CODEPOINT     0x0080
#define FIRST_3_BYTE_UTF8_CODEPOINT     0x0800
#define BOM_CODEPOINT                   0xFEFF
#define REPLACEMENT_CHARACTER_CODEPOINT 0xFFFD
#define FIRST_NON_BMP_CODEPOINT         0x10000
#define FIRST_4_BYTE_UTF8_CODEPOINT     0x10000
#define MAX_CODEPOINT                   0x10FFFF
#define EOF_CODEPOINT                   0xFFFFFFFF

/* Bit-masking macros. */
#define BOTTOM_BIT(x)       ((x) & 0x1)
#define BOTTOM_2_BITS(x)    ((x) & 0x3)
#define BOTTOM_3_BITS(x)    ((x) & 0x7)
#define BOTTOM_4_BITS(x)    ((x) & 0xF)
#define BOTTOM_5_BITS(x)    ((x) & 0x1F)
#define BOTTOM_6_BITS(x)    ((x) & 0x3F)
#define BOTTOM_7_BITS(x)    ((x) & 0x7F)
#define BOTTOM_N_BITS(x, n) ((x) & (((1 << (n)) - 1)))
#define IS_BIT_SET(x, n)    ((x) & (1 << (n)))

/* UTF-8 byte-related macros. */
#define IS_UTF8_SINGLE_BYTE(b)       (((b) & 0x80) == 0)
#define IS_UTF8_CONTINUATION_BYTE(b) (((b) & 0xC0) == 0x80)
#define IS_UTF8_FIRST_BYTE_OF_2(b)   (((b) & 0xE0) == 0xC0)
#define IS_UTF8_FIRST_BYTE_OF_3(b)   (((b) & 0xF0) == 0xE0)
#define IS_UTF8_FIRST_BYTE_OF_4(b)   (((b) & 0xF8) == 0xF0)

/* Unicode codepoint-related macros. */
#define IS_SURROGATE(c)                  (((c) & 0xFFFFF800) == 0xD800)
#define IS_LEADING_SURROGATE(c)          (((c) & 0xFFFFFC00) == 0xD800)
#define IS_TRAILING_SURROGATE(c)         (((c) & 0xFFFFFC00) == 0xDC00)
#define CODEPOINT_FROM_SURROGATES(hi_lo) ((((hi_lo) >> 16) << 10) + ((hi_lo) & 0xFFFF) + 0xFCA02400)
#define SURROGATES_FROM_CODEPOINT(c)     ((((c) << 6) & 0x3FF0000) + ((c) & 0x3FF) + 0xD7C0DC00)

/* Mutually-exclusive decoder states. */
typedef enum tag_DecoderState
{
    DECODER_RESET            = 0,
    DECODER_PROCESSED_1_OF_2 = 1,
    DECODER_PROCESSED_1_OF_3 = 2,
    DECODER_PROCESSED_2_OF_3 = 3,
    DECODER_PROCESSED_1_OF_4 = 4,
    DECODER_PROCESSED_2_OF_4 = 5,
    DECODER_PROCESSED_3_OF_4 = 6
} DecoderState;

/* Decoder data. */
typedef struct tag_Decoder
{
    unsigned char state;
    JSON_UInt32   bits;
} Decoder;

/* The bits of DecoderOutput are layed out as follows:

     --rr-lll---ccccccccccccccccccccc

     r = result code (2 bits)
     l = sequence length (3 bits)
     c = codepoint (21 bits)
     - = unused (6 bits)
 */
typedef enum tag_DecoderResultCode
{
    SEQUENCE_PENDING           = 0,
    SEQUENCE_COMPLETE          = 1,
    SEQUENCE_INVALID_INCLUSIVE = 2,
    SEQUENCE_INVALID_EXCLUSIVE = 3
} DecoderResultCode;
typedef JSON_UInt32 DecoderOutput;

#define DECODER_OUTPUT(r, l, c)    (((r) << 28) | ((l) << 24) | (c))
#define DECODER_RESULT_CODE(o)     ((o) >> 28)
#define DECODER_SEQUENCE_LENGTH(o) (((o) >> 24) & 0x7)
#define DECODER_CODEPOINT(o)       ((o) & 0x001FFFFF)

/* General-purpose decoder functions. */

static void ResetDecoder(Decoder* pDecoder)
{
    pDecoder->state = DECODER_RESET;
    pDecoder->bits = 0;
}

static DecoderOutput DecodeByte(Decoder* pDecoder, JSON_Encoding encoding, unsigned char b)
{
    DecoderOutput output;
    switch (encoding)
    {
    case JSON_UTF8:
    default:
        /* When the input encoding is UTF-8, the decoded codepoint's bits are
           recorded in the bottom 3 bytes of bits as they are decoded.
           The top byte is not used. */
        switch (pDecoder->state)
        {
        case DECODER_RESET:
            if (IS_UTF8_SINGLE_BYTE(b))
            {
                output = DECODER_OUTPUT(SEQUENCE_COMPLETE, 1, b);
            }
            else if (IS_UTF8_FIRST_BYTE_OF_2(b))
            {
                /* UTF-8 2-byte sequences that are overlong encodings can be
                   detected from just the first byte (C0 or C1). */
                pDecoder->bits = BOTTOM_5_BITS(b) << 6;
                if (pDecoder->bits < FIRST_2_BYTE_UTF8_CODEPOINT)
                {
                    output = DECODER_OUTPUT(SEQUENCE_INVALID_INCLUSIVE, 1, 0);
                }
                else
                {
                    pDecoder->state = DECODER_PROCESSED_1_OF_2;
                    return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);
                }
            }
            else if (IS_UTF8_FIRST_BYTE_OF_3(b))
            {
                pDecoder->bits = BOTTOM_4_BITS(b) << 12;
                pDecoder->state = DECODER_PROCESSED_1_OF_3;
                return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);
            }
            else if (IS_UTF8_FIRST_BYTE_OF_4(b))
            {
                /* Some UTF-8 4-byte sequences that encode out-of-range
                   codepoints can be detected from the first byte (F5 - FF). */
                pDecoder->bits = BOTTOM_3_BITS(b) << 18;
                if (pDecoder->bits > MAX_CODEPOINT)
                {
                    output = DECODER_OUTPUT(SEQUENCE_INVALID_INCLUSIVE, 1, 0);
                }
                else
                {
                    pDecoder->state = DECODER_PROCESSED_1_OF_4;
                    return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);
                }
            }
            else
            {
                /* The byte is of the form 11111xxx or 10xxxxxx, and is not
                   a valid first byte for a UTF-8 sequence. */
                output = DECODER_OUTPUT(SEQUENCE_INVALID_INCLUSIVE, 1, 0);
            }
            break;

        case DECODER_PROCESSED_1_OF_2:
            if (IS_UTF8_CONTINUATION_BYTE(b))
            {
                output = DECODER_OUTPUT(SEQUENCE_COMPLETE, 2, pDecoder->bits |= BOTTOM_6_BITS(b));
            }
            else
            {
                output = DECODER_OUTPUT(SEQUENCE_INVALID_EXCLUSIVE, 1, 0);

            }
            break;

        case DECODER_PROCESSED_1_OF_3:
            if (IS_UTF8_CONTINUATION_BYTE(b))
            {
                /* UTF-8 3-byte sequences that are overlong encodings or encode
                   surrogate codepoints can be detected after 2 bytes. */
                pDecoder->bits |= BOTTOM_6_BITS(b) << 6;
                if ((pDecoder->bits < FIRST_3_BYTE_UTF8_CODEPOINT) || IS_SURROGATE(pDecoder->bits))
                {
                    output = DECODER_OUTPUT(SEQUENCE_INVALID_EXCLUSIVE, 1, 0);
                }
                else
                {
                    pDecoder->state = DECODER_PROCESSED_2_OF_3;
                    return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);
                }
            }
            else
            {
                output = DECODER_OUTPUT(SEQUENCE_INVALID_EXCLUSIVE, 1, 0);
            }
            break;

        case DECODER_PROCESSED_2_OF_3:
            if (IS_UTF8_CONTINUATION_BYTE(b))
            {
                output = DECODER_OUTPUT(SEQUENCE_COMPLETE, 3, pDecoder->bits |= BOTTOM_6_BITS(b));
            }
            else
            {
                output = DECODER_OUTPUT(SEQUENCE_INVALID_EXCLUSIVE, 2, 0);
            }
            break;

        case DECODER_PROCESSED_1_OF_4:
            if (IS_UTF8_CONTINUATION_BYTE(b))
            {
                /* UTF-8 4-byte sequences that are overlong encodings or encode
                   out-of-range codepoints can be detected after 2 bytes. */
                pDecoder->bits |= BOTTOM_6_BITS(b) << 12;
                if ((pDecoder->bits < FIRST_4_BYTE_UTF8_CODEPOINT) || (pDecoder->bits > MAX_CODEPOINT))
                {
                    output = DECODER_OUTPUT(SEQUENCE_INVALID_EXCLUSIVE, 1, 0);
                }
                else
                {
                    pDecoder->state = DECODER_PROCESSED_2_OF_4;
                    return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);
                }
            }
            else
            {
                output = DECODER_OUTPUT(SEQUENCE_INVALID_EXCLUSIVE, 1, 0);
            }
            break;

        case DECODER_PROCESSED_2_OF_4:
            if (IS_UTF8_CONTINUATION_BYTE(b))
            {
                pDecoder->bits |= BOTTOM_6_BITS(b) << 6;
                pDecoder->state = DECODER_PROCESSED_3_OF_4;
                return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);
            }
            else
            {
                output = DECODER_OUTPUT(SEQUENCE_INVALID_EXCLUSIVE, 2, 0);
            }
            break;

        case DECODER_PROCESSED_3_OF_4:
            if (IS_UTF8_CONTINUATION_BYTE(b))
            {
                output = DECODER_OUTPUT(SEQUENCE_COMPLETE, 4, pDecoder->bits |= BOTTOM_6_BITS(b));
            }
            else
            {
                output = DECODER_OUTPUT(SEQUENCE_INVALID_EXCLUSIVE, 3, 0);
            }
            break;
        }
        break;

    case JSON_UTF16LE:
        /* When the input encoding is UTF-16, the decoded codepoint's bits are
           recorded in the bottom 2 bytes of bits as they are decoded.
           If those 2 bytes form a leading surrogate, the decoder treats the
           surrogate pair as a single 4-byte sequence, shifts the leading
           surrogate into the high 2 bytes of bits, and decodes the
           trailing surrogate's bits in the bottom 2 bytes of bits. */
        switch (pDecoder->state)
        {
        case DECODER_RESET:
            pDecoder->bits = b;
            pDecoder->state = DECODER_PROCESSED_1_OF_2;
            return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);

        case DECODER_PROCESSED_1_OF_2:
            pDecoder->bits |= b << 8;
            if (IS_TRAILING_SURROGATE(pDecoder->bits))
            {
                /* A trailing surrogate cannot appear on its own. */
                output = DECODER_OUTPUT(SEQUENCE_INVALID_INCLUSIVE, 2, 0);
            }
            else if (IS_LEADING_SURROGATE(pDecoder->bits))
            {
                /* A leading surrogate implies a 4-byte surrogate pair. */
                pDecoder->bits <<= 16;
                pDecoder->state = DECODER_PROCESSED_2_OF_4;
                return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);
            }
            else
            {
                output = DECODER_OUTPUT(SEQUENCE_COMPLETE, 2, pDecoder->bits);
            }
            break;

        case DECODER_PROCESSED_2_OF_4:
            pDecoder->bits |= b;
            pDecoder->state = DECODER_PROCESSED_3_OF_4;
            return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);

        case DECODER_PROCESSED_3_OF_4:
            pDecoder->bits |= b << 8;
            if (!IS_TRAILING_SURROGATE(pDecoder->bits & 0xFFFF))
            {
                /* A leading surrogate must be followed by a trailing one.
                   Treat the previous 3 bytes as an invalid 2-byte sequence
                   followed by the first byte of a new sequence. */
                pDecoder->bits &= 0xFF;
                pDecoder->state = DECODER_PROCESSED_1_OF_2;
                return DECODER_OUTPUT(SEQUENCE_INVALID_EXCLUSIVE, 2, 0);
            }
            else
            {
                output = DECODER_OUTPUT(SEQUENCE_COMPLETE, 4, CODEPOINT_FROM_SURROGATES(pDecoder->bits));
            }
            break;
        }
        break;

    case JSON_UTF16BE:
        /* When the input encoding is UTF-16, the decoded codepoint's bits are
           recorded in the bottom 2 bytes of bits as they are decoded.
           If those 2 bytes form a leading surrogate, the decoder treats the
           surrogate pair as a single 4-byte sequence, shifts the leading
           surrogate into the high 2 bytes of bits, and decodes the
           trailing surrogate's bits in the bottom 2 bytes of bits. */
        switch (pDecoder->state)
        {
        case DECODER_RESET:
            pDecoder->bits = b << 8;
            pDecoder->state = DECODER_PROCESSED_1_OF_2;
            return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);

        case DECODER_PROCESSED_1_OF_2:
            pDecoder->bits |= b;
            if (IS_TRAILING_SURROGATE(pDecoder->bits))
            {
                /* A trailing surrogate cannot appear on its own. */
                output = DECODER_OUTPUT(SEQUENCE_INVALID_INCLUSIVE, 2, 0);
            }
            else if (IS_LEADING_SURROGATE(pDecoder->bits))
            {
                /* A leading surrogate implies a 4-byte surrogate pair. */
                pDecoder->bits <<= 16;
                pDecoder->state = DECODER_PROCESSED_2_OF_4;
                return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);
            }
            else
            {
                output = DECODER_OUTPUT(SEQUENCE_COMPLETE, 2, pDecoder->bits);
            }
            break;

        case DECODER_PROCESSED_2_OF_4:
            pDecoder->bits |= b << 8;
            pDecoder->state = DECODER_PROCESSED_3_OF_4;
            return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);

        case DECODER_PROCESSED_3_OF_4:
            pDecoder->bits |= b;
            if (!IS_TRAILING_SURROGATE(pDecoder->bits & 0xFFFF))
            {
                /* A leading surrogate must be followed by a trailing one.
                   Treat the previous 3 bytes as an invalid 2-byte sequence
                   followed by the first byte of a new sequence. */
                pDecoder->bits &= 0xFF00;
                pDecoder->state = DECODER_PROCESSED_1_OF_2;
                return DECODER_OUTPUT(SEQUENCE_INVALID_EXCLUSIVE, 2, 0);
            }
            else
            {
                output = DECODER_OUTPUT(SEQUENCE_COMPLETE, 4, CODEPOINT_FROM_SURROGATES(pDecoder->bits));
            }
            break;
        }
        break;

    case JSON_UTF32LE:
        /* When the input encoding is UTF-32, the decoded codepoint's bits are
           recorded in bits as they are decoded. */
        switch (pDecoder->state)
        {
        case DECODER_RESET:
            pDecoder->state = DECODER_PROCESSED_1_OF_4;
            pDecoder->bits = b;
            return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);

        case DECODER_PROCESSED_1_OF_4:
            pDecoder->state = DECODER_PROCESSED_2_OF_4;
            pDecoder->bits |= b << 8;
            return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);

        case DECODER_PROCESSED_2_OF_4:
            pDecoder->state = DECODER_PROCESSED_3_OF_4;
            pDecoder->bits |= b << 16;
            return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);

        case DECODER_PROCESSED_3_OF_4:
            pDecoder->bits |= b << 24;
            output = (IS_SURROGATE(pDecoder->bits) || (pDecoder->bits > MAX_CODEPOINT))
                ? DECODER_OUTPUT(SEQUENCE_INVALID_INCLUSIVE, 4, 0)
                : DECODER_OUTPUT(SEQUENCE_COMPLETE, 4, pDecoder->bits);
            break;
        }
        break;

    case JSON_UTF32BE:
        /* When the input encoding is UTF-32, the decoded codepoint's bits are
           recorded in bits as they are decoded. */
        switch (pDecoder->state)
        {
        case DECODER_RESET:
            pDecoder->state = DECODER_PROCESSED_1_OF_4;
            pDecoder->bits = b << 24;
            return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);

        case DECODER_PROCESSED_1_OF_4:
            pDecoder->state = DECODER_PROCESSED_2_OF_4;
            pDecoder->bits |= b << 16;
            return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);

        case DECODER_PROCESSED_2_OF_4:
            pDecoder->state = DECODER_PROCESSED_3_OF_4;
            pDecoder->bits |= b << 8;
            return DECODER_OUTPUT(SEQUENCE_PENDING, 0, 0);

        case DECODER_PROCESSED_3_OF_4:
            pDecoder->bits |= b;
            output = (IS_SURROGATE(pDecoder->bits) || (pDecoder->bits > MAX_CODEPOINT))
                ? DECODER_OUTPUT(SEQUENCE_INVALID_INCLUSIVE, 4, 0)
                : DECODER_OUTPUT(SEQUENCE_COMPLETE, 4, pDecoder->bits);
            break;
        }
        break;
    }

    /* Reset the decoder for the next sequence. */
    ResetDecoder(pDecoder);
    return output;
}

/* Combinable parser status bits. */
typedef enum tag_ParserStatus
{
    PARSER_RESET                               = 0,
    PARSER_STARTED                             = 1 << 0,
    PARSER_FINISHED                            = 1 << 1,
    PARSER_IN_MEMORY_HANDLER                   = 1 << 2,
    PARSER_IN_PARSE_HANDLER                    = 1 << 3,
    PARSER_AFTER_CARRIAGE_RETURN               = 1 << 4,
    PARSER_ALLOW_BOM                           = 1 << 5,
    PARSER_ALLOW_COMMENTS                      = 1 << 6,
    PARSER_ALLOW_TRAILING_COMMAS               = 1 << 7,
    PARSER_ALLOW_SPECIAL_NUMBERS               = 1 << 8,
    PARSER_ALLOW_HEX_NUMBERS                   = 1 << 9,
    PARSER_REPLACE_INVALID_ENCODING_SEQUENCES  = 1 << 10,
    PARSER_TRACK_OBJECT_MEMBERS                = 1 << 11,
    PARSER_IN_HANDLER                          = PARSER_IN_MEMORY_HANDLER | PARSER_IN_PARSE_HANDLER
} ParserStatus;

/* Mutually-exclusive lexer states. */
typedef enum tag_LexerState
{
    LEXER_IDLE                                              = 0,
    LEXER_IN_LITERAL                                        = 1,
    LEXER_IN_STRING                                         = 2,
    LEXER_IN_STRING_ESCAPE                                  = 3,
    LEXER_IN_STRING_HEX_ESCAPE_BYTE_1                       = 4,
    LEXER_IN_STRING_HEX_ESCAPE_BYTE_2                       = 5,
    LEXER_IN_STRING_HEX_ESCAPE_BYTE_3                       = 6,
    LEXER_IN_STRING_HEX_ESCAPE_BYTE_4                       = 7,
    LEXER_IN_STRING_HEX_ESCAPE_BYTE_5                       = 8,
    LEXER_IN_STRING_HEX_ESCAPE_BYTE_6                       = 9,
    LEXER_IN_STRING_HEX_ESCAPE_BYTE_7                       = 10,
    LEXER_IN_STRING_HEX_ESCAPE_BYTE_8                       = 11,
    LEXER_IN_STRING_TRAILING_SURROGATE_HEX_ESCAPE_BACKSLASH = 12,
    LEXER_IN_STRING_TRAILING_SURROGATE_HEX_ESCAPE_U         = 13,
    LEXER_IN_NUMBER_AFTER_MINUS                             = 14,
    LEXER_IN_NUMBER_AFTER_LEADING_ZERO                      = 15,
    LEXER_IN_NUMBER_AFTER_X                                 = 16,
    LEXER_IN_NUMBER_HEX_DIGITS                              = 17,
    LEXER_IN_NUMBER_DECIMAL_DIGITS                          = 18,
    LEXER_IN_NUMBER_AFTER_DOT                               = 19,
    LEXER_IN_NUMBER_FRACTIONAL_DIGITS                       = 20,
    LEXER_IN_NUMBER_AFTER_E                                 = 21,
    LEXER_IN_NUMBER_AFTER_EXPONENT_SIGN                     = 22,
    LEXER_IN_NUMBER_EXPONENT_DIGITS                         = 23,
    LEXER_IN_COMMENT_AFTER_SLASH                            = 24,
    LEXER_IN_SINGLE_LINE_COMMENT                            = 25,
    LEXER_IN_MULTI_LINE_COMMENT                             = 26,
    LEXER_IN_MULTI_LINE_COMMENT_AFTER_STAR                  = 27
} LexerState;

/* Mutually-exclusive parser tokens and non-terminals. The values are defined
   so that the bottom 4 bits of a value can be used as an index into the
   parser's production rule table, and the 5th bit is 0 for tokens and 1
   for non-terminals. */
typedef enum tag_Symbol
{
    /* Token values are in the form 0x0X. */
    TOKEN_NULL              = 0x00,
    TOKEN_TRUE              = 0x01,
    TOKEN_FALSE             = 0x02,
    TOKEN_STRING            = 0x03,
    TOKEN_NUMBER            = 0x04,
    TOKEN_NAN               = 0x05,
    TOKEN_INFINITY          = 0x06,
    TOKEN_NEGATIVE_INFINITY = 0x07,
    TOKEN_LEFT_CURLY        = 0x08,
    TOKEN_RIGHT_CURLY       = 0x09,
    TOKEN_LEFT_SQUARE       = 0x0A,
    TOKEN_RIGHT_SQUARE      = 0x0B,
    TOKEN_COLON             = 0x0C,
    TOKEN_COMMA             = 0x0D,
    TOKEN_COMMENT           = 0x0E,

    /* Non-terminal values are in the form 0x1X. */
    NT_VALUE                = 0x10,
    NT_MEMBERS              = 0x11,
    NT_MEMBER               = 0x12,
    NT_MORE_MEMBERS         = 0x13,
    NT_MEMBERS_AFTER_COMMA  = 0x14,
    NT_ITEMS                = 0x15,
    NT_ITEM                 = 0x16,
    NT_MORE_ITEMS           = 0x17,
    NT_ITEMS_AFTER_COMMA    = 0x18
} Symbol;

#define IS_NONTERMINAL(s) ((s) & 0x10)
#define IS_TOKEN(s)       !IS_NONTERMINAL(s)

/* An object member name stored in an unordered, singly-linked-list, used for
   detecting duplicate member names. Note that the name string is not null-
   terminated. */
typedef struct tag_MemberName
{
    struct tag_MemberName* pNextName;
    size_t                 length;
    char                   pBytes[1]; /* variable-size buffer */
} MemberName;

/* An object's list of member names, and a pointer to the object's
   nearest ancestor object, if any. This is used as a stack. Because arrays
   do not have named items, they do not need to be recorded in the stack. */
typedef struct tag_MemberNames
{
    struct tag_MemberNames* pAncestor;
    MemberName*             pFirstName;
} MemberNames;

/* A parser instance. */
struct JSON_Parser_Data
{
    JSON_Parser_MemorySuite          memorySuite;
    void*                            userData;
    JSON_Parser_NullHandler          nullHandler;
    JSON_Parser_BooleanHandler       booleanHandler;
    JSON_Parser_StringHandler        stringHandler;
    JSON_Parser_NumberHandler        numberHandler;
    JSON_Parser_RawNumberHandler     rawNumberHandler;
    JSON_Parser_SpecialNumberHandler specialNumberHandler;
    JSON_Parser_StartObjectHandler   startObjectHandler;
    JSON_Parser_EndObjectHandler     endObjectHandler;
    JSON_Parser_ObjectMemberHandler  objectMemberHandler;
    JSON_Parser_StartArrayHandler    startArrayHandler;
    JSON_Parser_EndArrayHandler      endArrayHandler;
    JSON_Parser_ArrayItemHandler     arrayItemHandler;
    Decoder                          decoder;
    ParserStatus                     parserStatus;
    unsigned char                    inputEncoding;     /* real type is JSON_Encoding */
    unsigned char                    outputEncoding;    /* real type is JSON_Encoding */
    unsigned char                    lexerState;        /* real type is LexerState */
    unsigned char                    token;             /* real type is Symbol */
    unsigned char                    previousToken;     /* real type is Symbol */
    unsigned char                    error;             /* real type is JSON_Error */
    unsigned char                    errorOffset;
    unsigned char                    outputAttributes;
    JSON_UInt32                      lexerBits;
    size_t                           codepointLocationByte;
    size_t                           codepointLocationLine;
    size_t                           codepointLocationColumn;
    size_t                           tokenLocationByte;
    size_t                           tokenLocationLine;
    size_t                           tokenLocationColumn;
    size_t                           depth;
    unsigned char*                   pOutputBuffer;     /* initially set to defaultOutputBuffer */
    size_t                           outputBufferLength;
    size_t                           outputBufferUsed;
    size_t                           maxOutputStringLength;
    unsigned char*                   pSymbolStack;      /* initially set to defaultSymbolStack */
    size_t                           symbolStackSize;
    size_t                           symbolStackUsed;
    MemberNames*                     pMemberNames;
    unsigned char                    defaultOutputBuffer[DEFAULT_OUTPUT_BUFFER_LENGTH];
    unsigned char                    defaultSymbolStack[DEFAULT_SYMBOL_STACK_SIZE];
};

static void* JSON_CALL DefaultMallocHandler(JSON_Parser parser, size_t size)
{
    (void)parser; /* unused */
    return malloc(size);
}

static void* JSON_CALL DefaultReallocHandler(JSON_Parser parser, void* ptr, size_t size)
{
    (void)parser; /* unused */
    return realloc(ptr, size);
}

static void JSON_CALL DefaultFreeHandler(JSON_Parser parser, void* ptr)
{
    (void)parser; /* unused */
    free(ptr);
}

static void* JSON_CALL CallMalloc(JSON_Parser parser, size_t size)
{
    void* p;
    parser->parserStatus |= PARSER_IN_MEMORY_HANDLER;
    p = parser->memorySuite.malloc(parser, size);
    parser->parserStatus &= ~PARSER_IN_MEMORY_HANDLER;
    return p;
}

static void* JSON_CALL CallRealloc(JSON_Parser parser, void* ptr, size_t size)
{
    void* p;
    parser->parserStatus |= PARSER_IN_MEMORY_HANDLER;
    p = parser->memorySuite.realloc(parser, ptr, size);
    parser->parserStatus &= ~PARSER_IN_MEMORY_HANDLER;
    return p;
}

static void JSON_CALL CallFree(JSON_Parser parser, void* ptr)
{
    parser->parserStatus |= PARSER_IN_MEMORY_HANDLER;
    parser->memorySuite.free(parser, ptr);
    parser->parserStatus &= ~PARSER_IN_MEMORY_HANDLER;
}

/* Parser functions. */

/* This array must match the order and number of the JSON_Encoding enum. */
static const size_t minEncodingSequenceLengths[] =
{
    /* JSON_UnknownEncoding */ 0,
    /* JSON_UTF8 */            1,
    /* JSON_UTF16LE */         2,
    /* JSON_UTF16BE */         2,
    /* JSON_UTF32LE */         4,
    /* JSON_UTF32BE */         4
};

static void SetErrorAtCodepoint(JSON_Parser parser, JSON_Error error)
{
    parser->error = error;
}

static void SetErrorAtStringEscapeSequenceStart(JSON_Parser parser, JSON_Error error, unsigned char codepointsAgo)
{
    /* Note that backtracking from the current codepoint requires us to make
       three assumptions, which are always valid in the context of a string
       escape sequence:

         1. The input encoding is not JSON_UnknownEncoding.

         2 The codepoints we are backing up across are all in the range
            U+0000 - U+007F, aka ASCII, so we can assume the number of
            bytes comprising them based on the input encoding.

         3. The codepoints we are backing up across do not include any
            line breaks, so we can assume that the line number stays the
            same and the column number can simply be decremented.
    */
    parser->error = error;
    parser->errorOffset = codepointsAgo;
}

static void SetErrorAtToken(JSON_Parser parser, JSON_Error error)
{
    parser->error = error;
    parser->errorOffset = ERROR_LOCATION_IS_TOKEN_START;
}

static JSON_Status PushMemberNameList(JSON_Parser parser)
{
    MemberNames* pNames = (MemberNames*)CallMalloc(parser, sizeof(MemberNames));
    if (!pNames)
    {
        SetErrorAtCodepoint(parser, JSON_Error_OutOfMemory);
        return JSON_Failure;
    }
    pNames->pAncestor = parser->pMemberNames;
    pNames->pFirstName = NULL;
    parser->pMemberNames = pNames;
    return JSON_Success;
}

static void PopMemberNameList(JSON_Parser parser)
{
    MemberNames* pAncestor = parser->pMemberNames->pAncestor;
    while (parser->pMemberNames->pFirstName)
    {
        MemberName* pNextName = parser->pMemberNames->pFirstName->pNextName;
        CallFree(parser, parser->pMemberNames->pFirstName);
        parser->pMemberNames->pFirstName = pNextName;
    }
    CallFree(parser, parser->pMemberNames);
    parser->pMemberNames = pAncestor;
}

static JSON_Status StartContainer(JSON_Parser parser, int isObject)
{
    if (isObject && (parser->parserStatus & PARSER_TRACK_OBJECT_MEMBERS) &&
        !PushMemberNameList(parser))
    {
        return JSON_Failure;
    }
    parser->depth++;
    return JSON_Success;
}

static void EndContainer(JSON_Parser parser, int isObject)
{
    parser->depth--;
    if (isObject && (parser->parserStatus & PARSER_TRACK_OBJECT_MEMBERS))
    {
        PopMemberNameList(parser);
    }
}

static JSON_Status AddMemberNameToList(JSON_Parser parser)
{
    if (parser->parserStatus & PARSER_TRACK_OBJECT_MEMBERS)
    {
        MemberName* pName;
        for (pName = parser->pMemberNames->pFirstName; pName; pName = pName->pNextName)
        {
            if (pName->length == parser->outputBufferUsed && !memcmp(pName->pBytes, parser->pOutputBuffer, pName->length))
            {
                SetErrorAtToken(parser, JSON_Error_DuplicateObjectMember);
                return JSON_Failure;
            }
        }
        pName = (MemberName*)CallMalloc(parser, sizeof(MemberName) + parser->outputBufferUsed - 1);
        if (!pName)
        {
            SetErrorAtCodepoint(parser, JSON_Error_OutOfMemory);
            return JSON_Failure;
        }
        pName->pNextName = parser->pMemberNames->pFirstName;
        pName->length = parser->outputBufferUsed;
        memcpy(pName->pBytes, parser->pOutputBuffer, parser->outputBufferUsed);
        parser->pMemberNames->pFirstName = pName;
    }
    return JSON_Success;
}

static void ResetParserData(JSON_Parser parser, int isInitialized)
{
    parser->userData = NULL;
    parser->nullHandler = NULL;
    parser->booleanHandler = NULL;
    parser->stringHandler = NULL;
    parser->numberHandler = NULL;
    parser->rawNumberHandler = NULL;
    parser->specialNumberHandler = NULL;
    parser->startObjectHandler = NULL;
    parser->endObjectHandler = NULL;
    parser->objectMemberHandler = NULL;
    parser->startArrayHandler = NULL;
    parser->endArrayHandler = NULL;
    parser->arrayItemHandler = NULL;
    ResetDecoder(&parser->decoder);
    parser->inputEncoding = JSON_UnknownEncoding;
    parser->outputEncoding = JSON_UTF8;
    parser->parserStatus = PARSER_RESET;
    parser->lexerState = LEXER_IDLE;
    parser->lexerBits = 0;
    parser->token = TOKEN_NULL;
    parser->previousToken = TOKEN_NULL;
    parser->codepointLocationByte = 0;
    parser->codepointLocationLine = 0;
    parser->codepointLocationColumn = 0;
    parser->tokenLocationByte = 0;
    parser->tokenLocationLine = 0;
    parser->tokenLocationColumn = 0;
    parser->depth = 0;
    parser->error = JSON_Error_None;
    parser->errorOffset = 0;
    parser->outputAttributes = 0;
    if (!isInitialized)
    {
        parser->pOutputBuffer = parser->defaultOutputBuffer;
        parser->outputBufferLength = sizeof(parser->defaultOutputBuffer);
        parser->pSymbolStack = parser->defaultSymbolStack;
        parser->symbolStackSize = sizeof(parser->defaultSymbolStack);
    }
    else
    {
        /* When we reset the parser, we keep the output buffer and the symbol
           stack that have already been allocated, if any. If the client wants
           to reclaim the memory used by the those buffers, he needs to free
           the parser and create a new one. The object member lists do get
           freed, however, since they are not reusable. */
        while (parser->pMemberNames)
        {
            PopMemberNameList(parser);
        }
    }
    parser->outputBufferUsed = 0;
    parser->maxOutputStringLength = (size_t)-1;

    /* The parser always starts with NT_VALUE on the symbol stack. */
    parser->pSymbolStack[0] = NT_VALUE;
    parser->symbolStackUsed = 1;
    parser->pMemberNames = NULL;
}

static JSON_Status OutputNumberCharacter(JSON_Parser parser, unsigned char c)
{
    /* Because DEFAULT_OUTPUT_BUFFER_LENGTH == MAX_NUMBER_CHARACTERS + 1,
       the output buffer never needs to grown when we parse a number. */
    if (parser->outputBufferUsed == MAX_NUMBER_CHARACTERS)
    {
        SetErrorAtToken(parser, JSON_Error_TooLongNumber);
        return JSON_Failure;
    }
    parser->pOutputBuffer[parser->outputBufferUsed] = c;
    parser->outputBufferUsed++;
    return JSON_Success;
}

static void NullTerminateNumberOutput(JSON_Parser parser)
{
    /* Because DEFAULT_OUTPUT_BUFFER_LENGTH == MAX_NUMBER_CHARACTERS + 1,
       there is always room for a null terminator byte at the end of the
       output buffer. */
    parser->pOutputBuffer[parser->outputBufferUsed] = 0;
    parser->outputBufferUsed++;
}

static JSON_Status OutputStringBytes(JSON_Parser parser, const unsigned char* pBytes, size_t length)
{
    /* Make sure the output buffer has enough space for the new bytes. */
    unsigned char* pOutputBuffer = NULL;
    size_t requiredLength = parser->outputBufferUsed + length;
    if (requiredLength < length)
    {
        /* Required length is greater than the maximum allocatable length. */
    }
    else
    {
        if (requiredLength <= parser->outputBufferLength)
        {
            /* The current buffer is big enough. */
            pOutputBuffer = parser->pOutputBuffer;
        }
        else
        {
            /* We're going to need a bigger boat. */
            size_t newLength = parser->outputBufferLength * 2;
            if (newLength < requiredLength)
            {
                newLength = requiredLength;
            }
            if (parser->pOutputBuffer == parser->defaultOutputBuffer)
            {
                pOutputBuffer = (unsigned char*)CallMalloc(parser, newLength);
                if (pOutputBuffer)
                {
                    memcpy(pOutputBuffer, parser->defaultOutputBuffer, parser->outputBufferUsed);
                }
            }
            else
            {
                pOutputBuffer = (unsigned char*)CallRealloc(parser, parser->pOutputBuffer, newLength);
            }
            if (pOutputBuffer)
            {
                parser->pOutputBuffer = pOutputBuffer;
                parser->outputBufferLength = newLength;
            }
        }
    }
    if (!pOutputBuffer)
    {
        SetErrorAtCodepoint(parser, JSON_Error_OutOfMemory);
        return JSON_Failure;
    }
    while (parser->outputBufferUsed < requiredLength)
    {
        pOutputBuffer[parser->outputBufferUsed] = *pBytes;
        parser->outputBufferUsed++;
        pBytes++;
    }
    return JSON_Success;
}

static JSON_Status OutputStringCodepoint(JSON_Parser parser, JSON_UInt32 c)
{
    unsigned char bytes[4];
    size_t length = 0;

    /* Note that only valid codepoints are ever passed to this function, so
       no validation is necessary. */
    switch (parser->outputEncoding)
    {
    case JSON_UTF8:
        if (c < FIRST_2_BYTE_UTF8_CODEPOINT)
        {
            bytes[0] = c;
            length = 1;
        }
        else if (c < FIRST_3_BYTE_UTF8_CODEPOINT)
        {
            bytes[0] = 0xC0 | (c >> 6);
            bytes[1] = 0x80 | BOTTOM_6_BITS(c);
            length = 2;
        }
        else if (c < FIRST_4_BYTE_UTF8_CODEPOINT)
        {
            bytes[0] = 0xE0 | (c >> 12);
            bytes[1] = 0x80 | BOTTOM_6_BITS(c >> 6);
            bytes[2] = 0x80 | BOTTOM_6_BITS(c);
            length = 3;
        }
        else
        {
            bytes[0] = 0xF0 | (c >> 18);
            bytes[1] = 0x80 | BOTTOM_6_BITS(c >> 12);
            bytes[2] = 0x80 | BOTTOM_6_BITS(c >> 6);
            bytes[3] = 0x80 | BOTTOM_6_BITS(c);
            length = 4;
        }
        break;

    case JSON_UTF16LE:
        if (c < FIRST_NON_BMP_CODEPOINT)
        {
            bytes[0] = c;
            bytes[1] = c >> 8;
            length = 2;
        }
        else
        {
            JSON_UInt32 surrogates = SURROGATES_FROM_CODEPOINT(c);

            /* Leading surrogate. */
            bytes[0] = surrogates >> 16;
            bytes[1] = surrogates >> 24;

            /* Trailing surrogate. */
            bytes[2] = surrogates;
            bytes[3] = surrogates >> 8;
            length = 4;
        }
        break;

    case JSON_UTF16BE:
        if (c < FIRST_NON_BMP_CODEPOINT)
        {
            bytes[1] = c;
            bytes[0] = c >> 8;
            length = 2;
        }
        else
        {
            /* The codepoint requires a surrogate pair in UTF-16. */
            JSON_UInt32 surrogates = SURROGATES_FROM_CODEPOINT(c);

            /* Leading surrogate. */
            bytes[1] = surrogates >> 16;
            bytes[0] = surrogates >> 24;

            /* Trailing surrogate. */
            bytes[3] = surrogates;
            bytes[2] = surrogates >> 8;
            length = 4;
        }
        break;

    case JSON_UTF32LE:
        bytes[0] = c;
        bytes[1] = c >> 8;
        bytes[2] = c >> 16;
        bytes[3] = c >> 24;
        length = 4;
        break;

    case JSON_UTF32BE:
        bytes[3] = c;
        bytes[2] = c >> 8;
        bytes[1] = c >> 16;
        bytes[0] = c >> 24;
        length = 4;
        break;
    }

    if (parser->outputBufferUsed + length > parser->maxOutputStringLength)
    {
        SetErrorAtToken(parser, JSON_Error_TooLongString);
        return JSON_Failure;
    }

    if (!c)
    {
        parser->outputAttributes |= JSON_ContainsNullCharacter | JSON_ContainsControlCharacter;
    }
    else if (c < FIRST_NON_CONTROL_CODEPOINT)
    {
        parser->outputAttributes |= JSON_ContainsControlCharacter;
    }
    else if (c >= FIRST_NON_BMP_CODEPOINT)
    {
        parser->outputAttributes |= JSON_ContainsNonASCIICharacter | JSON_ContainsNonBMPCharacter;
    }
    else if (c >= FIRST_NON_ASCII_CODEPOINT)
    {
        parser->outputAttributes |= JSON_ContainsNonASCIICharacter;
    }

    if (!OutputStringBytes(parser, bytes, length))
    {
        return JSON_Failure;
    }

    return JSON_Success;
}

static const unsigned char nullTerminatorBytes[4] = { 0, 0, 0, 0 };
static JSON_Status NullTerminateStringOutput(JSON_Parser parser)
{
    if (!OutputStringBytes(parser, nullTerminatorBytes, minEncodingSequenceLengths[parser->outputEncoding]))
    {
        return JSON_Failure;
    }
    return JSON_Success;
}

static JSON_Status FlushParser(JSON_Parser parser)
{
    /* The symbol stack should be empty when parsing finishes. */
    if (parser->symbolStackUsed)
    {
        SetErrorAtCodepoint(parser, JSON_Error_ExpectedMoreTokens);
        return JSON_Failure;
    }
    return JSON_Success;
}

/* This function assumes that the specified character is a valid
   hex digit character (0-9, a-f, A-F). */
int InterpretHexDigit(char c)
{
    if (c >= 'a')
    {
        return c - 'a' + 10;
    }
    else if (c >= 'A')
    {
        return c - 'A' + 10;
    }
    else
    {
        return c - '0';
    }
}

/* This function makes the following assumptions about its input:

     1. The length argument is greater than or equal to 1.
     2. All characters in pDigits are hex digit characters.
     3. The first character in pDigits is not '0'.
*/
static const int significantBitsInHexDigit[15] = { 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4 };
static double InterpretSignificantHexDigits(const char* pDigits, int length)
{
    double value;
    JSON_UInt64 mantissa;
    int exponent = 0;
    int i;
    int digit;
    int remainingMantissaWidth;

    /* Parse the first hex digit. */
    digit = InterpretHexDigit(pDigits[0]);
    mantissa = digit;
    remainingMantissaWidth = IEEE_DOUBLE_MANTISSA_BITS - significantBitsInHexDigit[digit - 1];

    /* Parse any remaining hex digits. */
    for (i = 1; i < length; i++)
    {
        digit = InterpretHexDigit(pDigits[i]);
        mantissa = (mantissa << 4) | digit;
        remainingMantissaWidth -= 4;

        /* Check whether we ran out of mantissa bits. */
        if (remainingMantissaWidth < 0)
        {
            /* The mantissa has overflowed its bits. Shift it back to
               the right so that it's full but not overflowed. */
            int overflowWidth = -remainingMantissaWidth;
            int overflowBits = BOTTOM_N_BITS(digit, overflowWidth);
            mantissa >>= overflowWidth;

            /* Treat the current hex digit as having been consumed. */
            i++;

            /* The exponent should be the total number of bits that we
               couldn't fit into the mantissa. The overflow we just
               encountered accounts for between 1 and 4 bits, and each
               hex digit we haven't parsed yet accounts for 4 more. */
            exponent = overflowWidth + ((length - i) * 4);

            /* Now that the mantissa's bits are full, the rest of the
               bits are used to determine whether the mantissa should be
               rounded up or not. Logically, we can treat the extra bits
               as being preceded by a decimal point, so that we only need
               to determine if the fraction they represent is less than,
               greater than, or equal to 1/2. If the fraction is exactly
               equal to 1/2, it rounds down if the mantissa is even, and
               up if the mantissa is odd, in accordance with IEEE 754's
               "round-towards-even" policy. */

            /* Check the first extra bit. */
            if (!IS_BIT_SET(overflowBits, overflowWidth - 1))
            {
                /* The first extra bit is 0, so the fraction is less than
                   1/2 and should be rounded down. */
            }
            else
            {
                /* The first extra bit is 1, so the fraction is greater than
                   or equal to 1/2, and may be rounded up or down. */
                int roundUp = 0;
                if (mantissa & 1)
                {
                    /* The mantissa is odd, so even if the fraction is
                       exactly equal to 1/2, it should be rounded up. */
                    roundUp = 1;
                }
                else
                {
                    /* The mantissa is even, so the fraction should be
                       rounded down if and only if all the remaining
                       bits are 0, making the fraction exactly equal
                       to 1/2; otherwise it should be rounded up. */
                    if (BOTTOM_N_BITS(overflowBits, overflowWidth - 1))
                    {
                        /* The bits we already truncated contained
                           at least one 1, so the fraction is greater
                           than 1/2 and should be rounded up. */
                        roundUp = 1;
                    }
                    else
                    {
                        /* Scan the rest of the string for non-zero
                           digits, any of which would indicate that
                           the fraction is greater than 1/2 and should
                           be rounded up. */
                        for (; i < length; i++)
                        {
                            if (pDigits[i] != '0')
                            {
                                roundUp = 1;
                                break;
                            }
                        }
                    }
                }

                /* If the fraction represented by the extra bits rounds
                   up, the mantissa must increase by one. */
                if (roundUp)
                {
                    mantissa++;
                    if (mantissa >> IEEE_DOUBLE_MANTISSA_BITS)
                    {
                        /* Rounding up caused the mantissa to overflow. */
                        mantissa >>= 1;
                        exponent++;
                    }
                }
            }
            break;
        }
    }

    /* Combine the mantissa and exponent. */
    value = (double)mantissa;
    if (exponent)
    {
        value = ldexp(value, exponent);
    }
    return value;
}

static double InterpretHexDigits(const char* pDigits, int length)
{
    int i;
    for (i = 0; i < length; i++)
    {
        if (pDigits[i] != '0')
        {
            return InterpretSignificantHexDigits(pDigits + i, length - i);
        }
    }
    /* The digits were all 0. */
    return 0.0;
}

static double InterpretNumber(JSON_Parser parser)
{
    double value;
    /* We can tell whether the number is hex simply by examining the second
       character. This is safe because even the shortest valid number has
       a null terminator in the second character slot. */
    if (parser->pOutputBuffer[1] == 'x' || parser->pOutputBuffer[1] == 'X')
    {
        /* The number is in hex format. Unfortunately, although strtod()
           will parse hex numbers on most platforms, Microsoft's C runtime
           implementation is a notable exception. Therefore, we have to
           do the parsing ourselves. */
        value = InterpretHexDigits((const char*)parser->pOutputBuffer + 2,
                                   (int)parser->outputBufferUsed - 3);
    }
    else
    {
        /* The C standard does not provide a locale-independent floating-point
           number parser, so we have to change the decimal point in our string
           to the locale-specific character before calling strtod(). We assume
           here that the decimal point is always a single character in every
           locale. To ensure correctness, we cannot cache the character, since
           we must account for the valid albeit extraordinarily unlikely
           scenario wherein the client changes the locale between calls to
           JSON_Parser_Parse() or (even more unlikely) changes the locale from
           inside a handler. */
        if (parser->outputAttributes)
        {
            /* Note that because the first character of a JSON number cannot
               be a decimal point, index 0 indicates that the number does not
               contain a decimal point at all. */
            char localeDecimalPoint = *(localeconv()->decimal_point);
            parser->pOutputBuffer[parser->outputAttributes] = localeDecimalPoint;
        }
        value = strtod((const char*)parser->pOutputBuffer, NULL);
    }
    return value;
}

static JSON_Status CallSimpleHandler(JSON_Parser parser, JSON_Parser_NullHandler handler)
{
    if (handler)
    {
        JSON_Parser_HandlerResult result;
        parser->parserStatus |= PARSER_IN_PARSE_HANDLER;
        result = handler(parser);
        parser->parserStatus &= ~PARSER_IN_PARSE_HANDLER;
        if (result != JSON_ContinueParsing)
        {
            SetErrorAtToken(parser, JSON_Error_AbortedByHandler);
            return JSON_Failure;
        }
    }
    return JSON_Success;
}

static JSON_Status CallBooleanHandler(JSON_Parser parser)
{
    if (parser->booleanHandler)
    {
        JSON_Parser_HandlerResult result;
        parser->parserStatus |= PARSER_IN_PARSE_HANDLER;
        result = parser->booleanHandler(parser, parser->token == TOKEN_TRUE ? JSON_True : JSON_False);
        parser->parserStatus &= ~PARSER_IN_PARSE_HANDLER;
        if (result != JSON_ContinueParsing)
        {
            SetErrorAtToken(parser, JSON_Error_AbortedByHandler);
            return JSON_Failure;
        }
    }
    return JSON_Success;
}

static JSON_Status CallStringHandler(JSON_Parser parser)
{
    if (parser->stringHandler)
    {
        /* Strings should be null-terminated in the appropriate output encoding
           before being passed to parse handlers. Note that the length passed
           to the handler does not include the null terminator, so we need to
           save the length before we null-terminate the string. */
        JSON_Parser_HandlerResult result;
        size_t lengthNotIncludingNullTerminator = parser->outputBufferUsed;
        if (!NullTerminateStringOutput(parser))
        {
            return JSON_Failure;
        }
        parser->parserStatus |= PARSER_IN_PARSE_HANDLER;
        result = parser->stringHandler(parser, (const char*)parser->pOutputBuffer, lengthNotIncludingNullTerminator, parser->outputAttributes);
        parser->parserStatus &= ~PARSER_IN_PARSE_HANDLER;
        if (result != JSON_ContinueParsing)
        {
            SetErrorAtToken(parser, JSON_Error_AbortedByHandler);
            return JSON_Failure;
        }
    }
    return JSON_Success;
}

static JSON_Status CallNumberHandler(JSON_Parser parser)
{
    if (parser->rawNumberHandler || parser->numberHandler)
    {
        /* Numbers should be null-terminated with a single null terminator byte
           before being converted to a double and/or passed to handlers. */
        JSON_Parser_HandlerResult result;
        size_t lengthNotIncludingNullTerminator = parser->outputBufferUsed;
        NullTerminateNumberOutput(parser);
        parser->parserStatus |= PARSER_IN_PARSE_HANDLER;
        if (parser->rawNumberHandler)
        {
            result = parser->rawNumberHandler(parser, (const char*)parser->pOutputBuffer, lengthNotIncludingNullTerminator);
        }
        else
        {
            result = parser->numberHandler(parser, InterpretNumber(parser));
        }
        parser->parserStatus &= ~PARSER_IN_PARSE_HANDLER;
        if (result != JSON_ContinueParsing)
        {
            SetErrorAtToken(parser, JSON_Error_AbortedByHandler);
            return JSON_Failure;
        }
    }
    return JSON_Success;
}

static JSON_Status CallSpecialNumberHandler(JSON_Parser parser)
{
    if (parser->specialNumberHandler)
    {
        JSON_Parser_HandlerResult result;
        parser->parserStatus |= PARSER_IN_PARSE_HANDLER;
        result = parser->specialNumberHandler(parser, parser->token == TOKEN_NAN ? JSON_NaN :
                                              (parser->token == TOKEN_INFINITY ? JSON_Infinity : JSON_NegativeInfinity));
        parser->parserStatus &= ~PARSER_IN_PARSE_HANDLER;
        if (result != JSON_ContinueParsing)
        {
            SetErrorAtToken(parser, JSON_Error_AbortedByHandler);
            return JSON_Failure;
        }
    }
    return JSON_Success;
}

static JSON_Status CallObjectMemberHandler(JSON_Parser parser)
{
    if (parser->objectMemberHandler)
    {
        /* Strings should be null-terminated in the appropriate output encoding
           before being passed to parse handlers. Note that the length passed
           to the handler does not include the null terminator, so we need to
           save the length before we null-terminate the string. */
        JSON_Parser_HandlerResult result;
        size_t lengthNotIncludingNullTerminator = parser->outputBufferUsed;
        if (!NullTerminateStringOutput(parser))
        {
            return JSON_Failure;
        }
        parser->parserStatus |= PARSER_IN_PARSE_HANDLER;
        result = parser->objectMemberHandler(
            parser, (parser->previousToken == TOKEN_LEFT_CURLY) ? JSON_True : JSON_False,
            (const char*)parser->pOutputBuffer, lengthNotIncludingNullTerminator,
            parser->outputAttributes);
        parser->parserStatus &= ~PARSER_IN_PARSE_HANDLER;
        if (result != JSON_ContinueParsing)
        {
            SetErrorAtToken(parser, (result == JSON_TreatAsDuplicateObjectMember)
                            ? JSON_Error_DuplicateObjectMember
                            : JSON_Error_AbortedByHandler);
            return JSON_Failure;
        }
    }
    return JSON_Success;
}

static JSON_Status CallArrayItemHandler(JSON_Parser parser)
{
    if (parser->arrayItemHandler)
    {
        JSON_Parser_HandlerResult result;
        parser->parserStatus |= PARSER_IN_PARSE_HANDLER;
        result = parser->arrayItemHandler(parser, (parser->previousToken == TOKEN_LEFT_SQUARE) ? JSON_True : JSON_False);
        parser->parserStatus &= ~PARSER_IN_PARSE_HANDLER;
        if (result != JSON_ContinueParsing)
        {
            SetErrorAtToken(parser, JSON_Error_AbortedByHandler);
            return JSON_Failure;
        }
    }
    return JSON_Success;
}

static JSON_Status ReplaceTopSymbol(JSON_Parser parser, const Symbol* pSymbolsToPush, size_t numberOfSymbolsToPush)
{
    /* Make sure the symbol stack has enough space for the new symbols. */
    unsigned char* pSymbolStack = NULL;
    size_t requiredSize;
    parser->symbolStackUsed--; /* pop top symbol */
    requiredSize = parser->symbolStackUsed + numberOfSymbolsToPush;
    if (requiredSize < numberOfSymbolsToPush)
    {
        /* Required size is greater than the maximum allocatable size. */
    }
    else
    {
        if (requiredSize <= parser->symbolStackSize)
        {
            /* The current stack is big enough. */
            pSymbolStack = parser->pSymbolStack;
        }
        else
        {
            /* We're going to need a bigger boat. */
            size_t newSize = parser->symbolStackSize * 2;
            if (newSize < requiredSize)
            {
                newSize = requiredSize;
            }
            if (parser->pSymbolStack == parser->defaultSymbolStack)
            {
                pSymbolStack = (unsigned char*)CallMalloc(parser, newSize);
                if (pSymbolStack)
                {
                    memcpy(pSymbolStack, parser->defaultSymbolStack, parser->symbolStackUsed);
                }
            }
            else
            {
                pSymbolStack = (unsigned char*)CallRealloc(parser, parser->pSymbolStack, newSize);
            }
            if (pSymbolStack)
            {
                parser->pSymbolStack = pSymbolStack;
                parser->symbolStackSize = newSize;
            }
        }
    }
    if (!pSymbolStack)
    {
        SetErrorAtCodepoint(parser, JSON_Error_OutOfMemory);
        return JSON_Failure;
    }
    while (parser->symbolStackUsed < requiredSize)
    {
        pSymbolStack[parser->symbolStackUsed] = (unsigned char)*pSymbolsToPush;
        parser->symbolStackUsed++;
        pSymbolsToPush++;
    }
    return JSON_Success;
}

/* The JSON grammar comprises the following productions:

   1.  VALUE => null
   2.  VALUE => true
   3.  VALUE => false
   4.  VALUE => string
   5.  VALUE => number
   6.  VALUE => { MEMBERS }
   7.  VALUE => [ ITEMS ]

   8.  MEMBERS => MEMBER MORE_MEMBERS
   9.  MEMBERS => e

   10. MEMBER => string : VALUE

   11. MORE_MEMBERS => , MEMBERS_AFTER_COMMA
   12. MORE_MEMBERS => e

   13. MEMBERS_AFTER_COMMA => MEMBER MORE_MEMBERS
   14. MEMBERS_AFTER_COMMA => e (only if AllowTrailingCommas is enabled)

   15. ITEMS => ITEM MORE_ITEMS
   16. ITEMS => e

   17. ITEM => VALUE

   18. MORE_ITEMS => , ITEMS_AFTER_COMMA
   19. MORE_ITEMS => e

   20. ITEMS_AFTER_COMMA => ITEM MORE_ITEMS
   21. ITEMS_AFTER_COMMA => e (only if AllowTrailingCommas is enabled)

   We implement a simple LL(1) parser based on this grammar, with parse
   handlers invoked when certain non-terminals are replaced.

   The order and number of the rows and columns in the productions table must
   match the token and non-terminal enum values in the Symbol enum.
*/
static const unsigned char productions[14][9] =
{
/*             V     MS    M     MM   MAC    IS    I     MI   IAC  */
/*  null  */ { 1,    0,    0,    0,    0,    15,   17,   0,    20 },
/*  true  */ { 2,    0,    0,    0,    0,    15,   17,   0,    20 },
/* false  */ { 3,    0,    0,    0,    0,    15,   17,   0,    20 },
/* string */ { 4,    8,    10,   0,    13,   15,   17,   0,    20 },
/* number */ { 5,    0,    0,    0,    0,    15,   17,   0,    20 },
/*  NaN   */ { 5,    0,    0,    0,    0,    15,   17,   0,    20 },
/*  Inf   */ { 5,    0,    0,    0,    0,    15,   17,   0,    20 },
/* -Inf   */ { 5,    0,    0,    0,    0,    15,   17,   0,    20 },
/*   {    */ { 6,    0,    0,    0,    0,    15,   17,   0,    20 },
/*   }    */ { 0,    9,    0,    12,   14,   0,    0,    0,    0  },
/*   [    */ { 7,    0,    0,    0,    0,    15,   17,   0,    20 },
/*   ]    */ { 0,    0,    0,    0,    0,    16,   0,    19,   21 },
/*   :    */ { 0,    0,    0,    0,    0,    0,    0,    0,    0  },
/*   ,    */ { 0,    0,    0,    11,   0,    0,    0,    18,   0  }
};

static JSON_Status ProcessToken(JSON_Parser parser)
{
    /* Comment tokens are simply ignored whenever they are encountered. */
    if (parser->token != TOKEN_COMMENT)
    {
        int processTopSymbol;

        /* If the stack is empty, no more tokens were expected. */
        if (parser->symbolStackUsed == 0)
        {
            SetErrorAtToken(parser, JSON_Error_UnexpectedToken);
            return JSON_Failure;
        }

        do
        {
            Symbol topSymbol = (Symbol)parser->pSymbolStack[parser->symbolStackUsed - 1];
            Symbol symbolsToPush[2];
            size_t numberOfSymbolsToPush = 0;
            processTopSymbol = 0;
            if (IS_TOKEN(topSymbol))
            {
                if (parser->token != topSymbol)
                {
                    SetErrorAtToken(parser, JSON_Error_UnexpectedToken);
                    return JSON_Failure;
                }
            }
            else
            {
                switch (productions[parser->token][BOTTOM_4_BITS(topSymbol)])
                {
                case 1: /* VALUE => null */
                    if (!CallSimpleHandler(parser, parser->nullHandler))
                    {
                        return JSON_Failure;
                    }
                    break;

                case 2: /* VALUE => true */
                case 3: /* VALUE => false */
                    if (!CallBooleanHandler(parser))
                    {
                        return JSON_Failure;
                    }
                    break;

                case 4: /* VALUE => string */
                    if (!CallStringHandler(parser))
                    {
                        return JSON_Failure;
                    }
                    break;

                case 5: /* VALUE => number */
                    if (parser->token == TOKEN_NUMBER)
                    {
                        if (!CallNumberHandler(parser))
                        {
                            return JSON_Failure;
                        }
                    }
                    else
                    {
                        if (!CallSpecialNumberHandler(parser))
                        {
                            return JSON_Failure;
                        }
                    }
                    break;

                case 6: /* VALUE => { MEMBERS } */
                    if (!CallSimpleHandler(parser, parser->startObjectHandler) ||
                        !StartContainer(parser, 1/*isObject*/))
                    {
                        return JSON_Failure;
                    }
                    symbolsToPush[0] = TOKEN_RIGHT_CURLY;
                    symbolsToPush[1] = NT_MEMBERS;
                    numberOfSymbolsToPush = 2;
                    break;

                case 7: /* VALUE => [ ITEMS ] */
                    if (!CallSimpleHandler(parser, parser->startArrayHandler) ||
                        !StartContainer(parser, 0/*isObject*/))
                    {
                        return JSON_Failure;
                    }
                    symbolsToPush[0] = TOKEN_RIGHT_SQUARE;
                    symbolsToPush[1] = NT_ITEMS;
                    numberOfSymbolsToPush = 2;
                    break;

                case 8:  /* MEMBERS => MEMBER MORE_MEMBERS */
                case 13: /* MEMBERS_AFTER_COMMA => MEMBER MORE_MEMBERS */
                    symbolsToPush[0] = NT_MORE_MEMBERS;
                    symbolsToPush[1] = NT_MEMBER;
                    numberOfSymbolsToPush = 2;
                    processTopSymbol = 1;
                    break;

                case 14: /* MEMBERS_AFTER_COMMA => e (only if AllowTrailingCommas is enabled) */
                    if (parser->parserStatus & PARSER_ALLOW_TRAILING_COMMAS)
                    {
                        /* fallthrough */
                    }
                    else
                    {
                        SetErrorAtToken(parser, JSON_Error_UnexpectedToken);
                        return JSON_Failure;
                    }
                case 9:  /* MEMBERS => e */
                case 12: /* MORE_MEMBERS => e */
                    EndContainer(parser, 1/*isObject*/);
                    if (!CallSimpleHandler(parser, parser->endObjectHandler))
                    {
                        return JSON_Failure;
                    }
                    processTopSymbol = 1;
                    break;

                case 10: /* MEMBER => string : VALUE */
                    if (!AddMemberNameToList(parser) || /* will fail if member is duplicate */
                        !CallObjectMemberHandler(parser))
                    {
                        return JSON_Failure;
                    }
                    symbolsToPush[0] = NT_VALUE;
                    symbolsToPush[1] = TOKEN_COLON;
                    numberOfSymbolsToPush = 2;
                    break;

                case 11: /* MORE_MEMBERS => , MEMBERS_AFTER_COMMA */
                    symbolsToPush[0] = NT_MEMBERS_AFTER_COMMA;
                    numberOfSymbolsToPush = 1;
                    break;

                case 15: /* ITEMS => ITEM MORE_ITEMS */
                case 20: /* ITEMS_AFTER_COMMA => ITEM MORE_ITEMS */
                    symbolsToPush[0] = NT_MORE_ITEMS;
                    symbolsToPush[1] = NT_ITEM;
                    numberOfSymbolsToPush = 2;
                    processTopSymbol = 1;
                    break;

                case 21: /* ITEMS_AFTER_COMMA => e (only if AllowTrailingCommas is enabled) */
                    if (parser->parserStatus & PARSER_ALLOW_TRAILING_COMMAS)
                    {
                        /* fallthrough */
                    }
                    else
                    {
                        SetErrorAtToken(parser, JSON_Error_UnexpectedToken);
                        return JSON_Failure;
                    }
                case 16: /* ITEMS => e */
                case 19: /* MORE_ITEMS => e */
                    EndContainer(parser, 0/*isObject*/);
                    if (!CallSimpleHandler(parser, parser->endArrayHandler))
                    {
                        return JSON_Failure;
                    }
                    processTopSymbol = 1;
                    break;

                case 17: /* ITEM => VALUE */
                    if (!CallArrayItemHandler(parser))
                    {
                        return JSON_Failure;
                    }
                    symbolsToPush[0] = NT_VALUE;
                    numberOfSymbolsToPush = 1;
                    processTopSymbol = 1;
                    break;

                case 18: /* MORE_ITEMS => , ITEMS_AFTER_COMMA */
                    symbolsToPush[0] = NT_ITEMS_AFTER_COMMA;
                    numberOfSymbolsToPush = 1;
                    break;

                default: /* no production */
                    SetErrorAtToken(parser, JSON_Error_UnexpectedToken);
                    return JSON_Failure;
                }
            }
            if (!ReplaceTopSymbol(parser, symbolsToPush, numberOfSymbolsToPush))
            {
                return JSON_Failure;
            }
        } while (processTopSymbol);
        parser->previousToken = parser->token;
    }

    /* Reset the lexer to prepare for the next token. */
    parser->lexerState = LEXER_IDLE;
    parser->lexerBits = 0;
    parser->token = TOKEN_NULL;
    parser->outputAttributes = 0;
    parser->outputBufferUsed = 0;
    return JSON_Success;
}

/* Lexer functions. */

static const unsigned char expectedLiteralChars[] = { 'u', 'l', 'l', 0, 'r', 'u', 'e', 0, 'a', 'l', 's', 'e', 0, 'a', 'N', 0, 'n', 'f', 'i', 'n', 'i', 't', 'y', 0  };

#define NULL_LITERAL_EXPECTED_CHARS_START_INDEX     0
#define TRUE_LITERAL_EXPECTED_CHARS_START_INDEX     4
#define FALSE_LITERAL_EXPECTED_CHARS_START_INDEX    8
#define NAN_LITERAL_EXPECTED_CHARS_START_INDEX      13
#define INFINITY_LITERAL_EXPECTED_CHARS_START_INDEX 16

static void StartToken(JSON_Parser parser, Symbol token)
{
    parser->token = token;
    parser->tokenLocationByte = parser->codepointLocationByte;
    parser->tokenLocationLine = parser->codepointLocationLine;
    parser->tokenLocationColumn = parser->codepointLocationColumn;
}

static JSON_Status ProcessCodepoint(JSON_Parser parser, JSON_UInt32 c, size_t encodedLength)
{
    JSON_UInt32 codepointToOutput = EOF_CODEPOINT;
    int tokenFinished = 0;

    /* If the previous codepoint was U+000D (CARRIAGE RETURN), and the current
       codepoint is U+000A (LINE FEED), then treat the 2 codepoints as a single
       line break. */
    if (parser->parserStatus & PARSER_AFTER_CARRIAGE_RETURN)
    {
        if (c == LINE_FEED_CODEPOINT)
        {
            parser->codepointLocationLine--;
        }
        parser->parserStatus &= ~PARSER_AFTER_CARRIAGE_RETURN;
    }

reprocess:

    switch (parser->lexerState)
    {
    case LEXER_IDLE:
        if (c == '{')
        {
            StartToken(parser, TOKEN_LEFT_CURLY);
            tokenFinished = 1;
        }
        else if (c == '}')
        {
            StartToken(parser, TOKEN_RIGHT_CURLY);
            tokenFinished = 1;
        }
        else if (c == '[')
        {
            StartToken(parser, TOKEN_LEFT_SQUARE);
            tokenFinished = 1;
        }
        else if (c == ']')
        {
            StartToken(parser, TOKEN_RIGHT_SQUARE);
            tokenFinished = 1;
        }
        else if (c == ':')
        {
            StartToken(parser, TOKEN_COLON);
            tokenFinished = 1;
        }
        else if (c == ',')
        {
            StartToken(parser, TOKEN_COMMA);
            tokenFinished = 1;
        }
        else if (c == 'n')
        {
            StartToken(parser, TOKEN_NULL);
            parser->lexerBits = NULL_LITERAL_EXPECTED_CHARS_START_INDEX;
            parser->lexerState = LEXER_IN_LITERAL;
        }
        else if (c == 't')
        {
            StartToken(parser, TOKEN_TRUE);
            parser->lexerBits = TRUE_LITERAL_EXPECTED_CHARS_START_INDEX;
            parser->lexerState = LEXER_IN_LITERAL;
        }
        else if (c == 'f')
        {
            StartToken(parser, TOKEN_FALSE);
            parser->lexerBits = FALSE_LITERAL_EXPECTED_CHARS_START_INDEX;
            parser->lexerState = LEXER_IN_LITERAL;
        }
        else if (c == '"')
        {
            StartToken(parser, TOKEN_STRING);
            parser->lexerState = LEXER_IN_STRING;
        }
        else if (c == '-')
        {
            StartToken(parser, TOKEN_NUMBER);
            codepointToOutput = '-';
            parser->lexerState = LEXER_IN_NUMBER_AFTER_MINUS;
        }
        else if (c == '0')
        {
            StartToken(parser, TOKEN_NUMBER);
            codepointToOutput = '0';
            parser->lexerState = LEXER_IN_NUMBER_AFTER_LEADING_ZERO;
        }
        else if (c >= '1' && c <= '9')
        {
            StartToken(parser, TOKEN_NUMBER);
            codepointToOutput = c;
            parser->lexerState = LEXER_IN_NUMBER_DECIMAL_DIGITS;
        }
        else if (c == ' ' || c == TAB_CODEPOINT || c == LINE_FEED_CODEPOINT ||
                 c == CARRIAGE_RETURN_CODEPOINT || c == EOF_CODEPOINT)
        {
            /* Ignore whitespace between tokens. */
        }
        else if (c == BOM_CODEPOINT && parser->codepointLocationByte == 0)
        {
            if (parser->parserStatus & PARSER_ALLOW_BOM)
            {
                /* OK, we'll allow the BOM. */
            }
            else
            {
                SetErrorAtCodepoint(parser, JSON_Error_BOMNotAllowed);
                return JSON_Failure;
            }
        }
        else if (c == '/' && (parser->parserStatus & PARSER_ALLOW_COMMENTS))
        {
            StartToken(parser, TOKEN_COMMENT);
            parser->lexerState = LEXER_IN_COMMENT_AFTER_SLASH;
        }
        else if (c == 'N' && (parser->parserStatus & PARSER_ALLOW_SPECIAL_NUMBERS))
        {
            StartToken(parser, TOKEN_NAN);
            parser->lexerBits = NAN_LITERAL_EXPECTED_CHARS_START_INDEX;
            parser->lexerState = LEXER_IN_LITERAL;
        }
        else if (c == 'I' && (parser->parserStatus & PARSER_ALLOW_SPECIAL_NUMBERS))
        {
            StartToken(parser, TOKEN_INFINITY);
            parser->lexerBits = INFINITY_LITERAL_EXPECTED_CHARS_START_INDEX;
            parser->lexerState = LEXER_IN_LITERAL;
        }
        else
        {
            SetErrorAtCodepoint(parser, JSON_Error_UnknownToken);
            return JSON_Failure;
        }
        break;

    case LEXER_IN_LITERAL:
        /* While lexing a literal we store an index into expectedLiteralChars
           in lexerBits. */
        if (expectedLiteralChars[parser->lexerBits])
        {
            /* The codepoint should match the next character in the literal. */
            if (c != expectedLiteralChars[parser->lexerBits])
            {
                SetErrorAtToken(parser, JSON_Error_UnknownToken);
                return JSON_Failure;
            }
            parser->lexerBits++;
        }
        else
        {
            /* The literal should be finished, so the codepoint should not be
               a plausible JSON literal character, but rather EOF, whitespace,
               or the first character of the next token. */
            if ((c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                (c == '_'))
            {
                SetErrorAtToken(parser, JSON_Error_UnknownToken);
                return JSON_Failure;
            }
            if (!ProcessToken(parser))
            {
                return JSON_Failure;
            }
            goto reprocess;
        }
        break;

    case LEXER_IN_STRING:
        if (c == EOF_CODEPOINT)
        {
            /* FlushLexer() will trigger the appropriate error. */
        }
        else if (c == '"')
        {
            tokenFinished = 1;
        }
        else if (c == '\\')
        {
            parser->lexerState = LEXER_IN_STRING_ESCAPE;
        }
        else if (c < 0x20)
        {
            /* ASCII control characters (U+0000 - U+001F) are not allowed to
               appear unescaped in string values. */
            SetErrorAtCodepoint(parser, JSON_Error_UnescapedControlCharacter);
            return JSON_Failure;
        }
        else
        {
            codepointToOutput = c;
        }
        break;

    case LEXER_IN_STRING_ESCAPE:
        if (c == EOF_CODEPOINT)
        {
            /* FlushLexer() will trigger the appropriate error. */
        }
        else
        {
            if (c == 'u')
            {
                parser->lexerState = LEXER_IN_STRING_HEX_ESCAPE_BYTE_1;
            }
            else
            {
                if (c == '"' || c == '\\' || c == '/')
                {
                    codepointToOutput = c;
                }
                else if (c == 'b')
                {
                    codepointToOutput = BACKSPACE_CODEPOINT;
                }
                else if (c == 't')
                {
                    codepointToOutput = TAB_CODEPOINT;
                }
                else if (c == 'n')
                {
                    codepointToOutput = LINE_FEED_CODEPOINT;
                }
                else if (c == 'f')
                {
                    codepointToOutput = FORM_FEED_CODEPOINT;
                }
                else if (c == 'r')
                {
                    codepointToOutput = CARRIAGE_RETURN_CODEPOINT;
                }
                else
                {
                    /* The current codepoint location is the first character after
                       the backslash that started the escape sequence. The error
                       location should be the beginning of the escape sequence, 1
                       character earlier. */
                    SetErrorAtStringEscapeSequenceStart(parser, JSON_Error_InvalidEscapeSequence, 1);
                    return JSON_Failure;
                }
                parser->lexerState = LEXER_IN_STRING;
            }
        }
        break;

    case LEXER_IN_STRING_HEX_ESCAPE_BYTE_1:
    case LEXER_IN_STRING_HEX_ESCAPE_BYTE_2:
    case LEXER_IN_STRING_HEX_ESCAPE_BYTE_3:
    case LEXER_IN_STRING_HEX_ESCAPE_BYTE_4:
    case LEXER_IN_STRING_HEX_ESCAPE_BYTE_5:
    case LEXER_IN_STRING_HEX_ESCAPE_BYTE_6:
    case LEXER_IN_STRING_HEX_ESCAPE_BYTE_7:
    case LEXER_IN_STRING_HEX_ESCAPE_BYTE_8:
        if (c == EOF_CODEPOINT)
        {
            /* FlushLexer() will trigger the appropriate error. */
        }
        else
        {
            /* While lexing a string hex escape sequence we store the bytes
               of the escaped codepoint in the low 2 bytes of lexerBits. If
               the escape sequence represents a leading surrogate, we shift
               the leading surrogate into the high 2 bytes and lex a second
               hex escape sequence (which should be a trailing surrogate). */
            int byteNumber = (parser->lexerState - LEXER_IN_STRING_HEX_ESCAPE_BYTE_1) & 0x3;
            JSON_UInt32 nibble;
            if (c >= '0' && c <= '9')
            {
                nibble = c - '0';
            }
            else if (c >= 'A' && c <= 'F')
            {
                nibble = c - 'A' + 10;
            }
            else if (c >= 'a' && c <= 'f')
            {
                nibble = c - 'a' + 10;
            }
            else
            {
                /* The current codepoint location is one of the 4 hex digit
                   character slots in the hex escape sequence. The error
                   location should be the beginning of the hex escape
                   sequence, between 2 and 5 bytes earlier. */
                unsigned char codepointsAgo = 2 /* for "\u" */ + (unsigned char)byteNumber;
                SetErrorAtStringEscapeSequenceStart(parser, JSON_Error_InvalidEscapeSequence, codepointsAgo);
                return JSON_Failure;
            }
            /* Store the hex digit's bits in the appropriate byte of lexerBits. */
            nibble <<= (3 - byteNumber) * 4 /* shift left by 12, 8, 4, 0 */ ;
            parser->lexerBits |= nibble;
            if (parser->lexerState == LEXER_IN_STRING_HEX_ESCAPE_BYTE_4)
            {
                /* The escape sequence is complete. We need to check whether
                   it represents a leading surrogate (which implies that it
                   will be immediately followed by a hex-escaped trailing
                   surrogate), a trailing surrogate (which is invalid), or a
                   valid codepoint (which should simply be appended to the
                   string token value). */
                if (IS_LEADING_SURROGATE(parser->lexerBits))
                {
                    /* Shift the leading surrogate into the high 2 bytes of
                       lexerBits so that the trailing surrogate can be stored
                       in the low 2 bytes. */
                    parser->lexerBits <<= 16;
                    parser->lexerState = LEXER_IN_STRING_TRAILING_SURROGATE_HEX_ESCAPE_BACKSLASH;
                }
                else if (IS_TRAILING_SURROGATE(parser->lexerBits))
                {
                    /* The current codepoint location is the last hex digit
                       of the hex escape sequence. The error location should
                       be the beginning of the hex escape sequence, 5
                       characters earlier. */
                    SetErrorAtStringEscapeSequenceStart(parser, JSON_Error_UnpairedSurrogateEscapeSequence, 5);
                    return JSON_Failure;
                }
                else
                {
                    /* The escape sequence represents a BMP codepoint. */
                    codepointToOutput = parser->lexerBits;
                    parser->lexerBits = 0;
                    parser->lexerState = LEXER_IN_STRING;
                }
            }
            else if (parser->lexerState == LEXER_IN_STRING_HEX_ESCAPE_BYTE_8)
            {
                /* The second hex escape sequence is complete. We need to
                   check whether it represents a trailing surrogate as
                   expected. If so, the surrogate pair represents a single
                   non-BMP codepoint. */
                if (!IS_TRAILING_SURROGATE(parser->lexerBits & 0xFFFF))
                {
                    /* The current codepoint location is the last hex digit of
                       the second hex escape sequence. The error location
                       should be the beginning of the leading surrogate
                       hex escape sequence, 11 characters earlier. */
                    SetErrorAtStringEscapeSequenceStart(parser, JSON_Error_UnpairedSurrogateEscapeSequence, 11);
                    return JSON_Failure;
                }
                /* The escape sequence represents a non-BMP codepoint. */
                codepointToOutput = CODEPOINT_FROM_SURROGATES(parser->lexerBits);
                parser->lexerBits = 0;
                parser->lexerState = LEXER_IN_STRING;
            }
            else
            {
                parser->lexerState++;
            }
        }
        break;

    case LEXER_IN_STRING_TRAILING_SURROGATE_HEX_ESCAPE_BACKSLASH:
        if (c == EOF_CODEPOINT)
        {
            /* FlushLexer() will trigger the appropriate error. */
        }
        else
        {
            if (c != '\\')
            {
                /* The current codepoint location is the first character after
                   the leading surrogate hex escape sequence. The error
                   location should be the beginning of the leading surrogate
                   hex escape sequence, 6 characters earlier. */
                SetErrorAtStringEscapeSequenceStart(parser, JSON_Error_UnpairedSurrogateEscapeSequence, 6);
                return JSON_Failure;
            }
            parser->lexerState = LEXER_IN_STRING_TRAILING_SURROGATE_HEX_ESCAPE_U;
        }
        break;

    case LEXER_IN_STRING_TRAILING_SURROGATE_HEX_ESCAPE_U:
        if (c == EOF_CODEPOINT)
        {
            /* FlushLexer() will trigger the appropriate error. */
        }
        else
        {
            if (c != 'u')
            {
                /* Distinguish between a totally bogus escape sequence
                   and a valid one that just isn't the hex escape kind
                   that we require for a trailing surrogate. The current
                   codepoint location is the first character after the
                   backslash that should have introduced the trailing
                   surrogate hex escape sequence. */
                if (c == '"' || c == '\\' || c == '/' || c == 'b' ||
                    c == 't' || c == 'n' || c == 'f' || c == 'r')
                {
                    /* The error location should be at that beginning of the
                       leading surrogate's hex escape sequence, 7 characters
                       earlier. */
                    SetErrorAtStringEscapeSequenceStart(parser, JSON_Error_UnpairedSurrogateEscapeSequence, 7);
                }
                else
                {
                    /* The error location should be at that backslash, 1
                       character earlier. */
                    SetErrorAtStringEscapeSequenceStart(parser, JSON_Error_InvalidEscapeSequence, 1);
                }
                return JSON_Failure;
            }
            parser->lexerState = LEXER_IN_STRING_HEX_ESCAPE_BYTE_5;
        }
        break;

    case LEXER_IN_NUMBER_AFTER_MINUS:
        if (c == EOF_CODEPOINT)
        {
            /* FlushLexer() will trigger the appropriate error. */
        }
        else if (c == 'I' && (parser->parserStatus & PARSER_ALLOW_SPECIAL_NUMBERS))
        {
            parser->token = TOKEN_NEGATIVE_INFINITY; /* changing horses mid-stream, so to speak */
            parser->lexerBits = INFINITY_LITERAL_EXPECTED_CHARS_START_INDEX;
            parser->lexerState = LEXER_IN_LITERAL;
        }
        else
        {
            if (c == '0')
            {
                codepointToOutput = '0';
                parser->lexerState = LEXER_IN_NUMBER_AFTER_LEADING_ZERO;
            }
            else if (c >= '1' && c <= '9')
            {
                codepointToOutput = c;
                parser->lexerState = LEXER_IN_NUMBER_DECIMAL_DIGITS;
            }
            else
            {
                /* We trigger an unknown token error rather than an invalid number
                   error so that "Foo" and "-Foo" trigger the same error. */
                SetErrorAtToken(parser, JSON_Error_UnknownToken);
                return JSON_Failure;
            }
        }
        break;

    case LEXER_IN_NUMBER_AFTER_LEADING_ZERO:
        if (c == '.')
        {
            /* We save the index of the decimal point character in the token
               in outputAttributes. */
            codepointToOutput = '.';
            parser->outputAttributes = (unsigned char)parser->outputBufferUsed;
            parser->lexerState = LEXER_IN_NUMBER_AFTER_DOT;
        }
        else if (c == 'e' || c == 'E')
        {
            codepointToOutput = c;
            parser->lexerState = LEXER_IN_NUMBER_AFTER_E;
        }
        else if (c >= '0' && c <= '9')
        {
            SetErrorAtToken(parser, JSON_Error_InvalidNumber);
            return JSON_Failure;
        }
        else if ((c == 'x' || c == 'X') && (parser->pOutputBuffer[0] != '-') &&
                 (parser->parserStatus & PARSER_ALLOW_HEX_NUMBERS))
        {
            codepointToOutput = c;
            parser->lexerState = LEXER_IN_NUMBER_AFTER_X;
        }
        else
        {
            /* The number is finished. */
            if (!ProcessToken(parser))
            {
                return JSON_Failure;
            }
            goto reprocess;
        }
        break;

    case LEXER_IN_NUMBER_AFTER_X:
        if (c == EOF_CODEPOINT)
        {
            /* FlushLexer() will trigger the appropriate error. */
        }
        else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
        {
            codepointToOutput = c;
            parser->lexerState = LEXER_IN_NUMBER_HEX_DIGITS;
        }
        else
        {
            SetErrorAtToken(parser, JSON_Error_InvalidNumber);
            return JSON_Failure;
        }
        break;

    case LEXER_IN_NUMBER_HEX_DIGITS:
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
        {
            codepointToOutput = c;
        }
        else
        {
            /* The number is finished. */
            if (!ProcessToken(parser))
            {
                return JSON_Failure;
            }
            goto reprocess;
        }
        break;

    case LEXER_IN_NUMBER_DECIMAL_DIGITS:
        if (c >= '0' && c <= '9')
        {
            codepointToOutput = c;
        }
        else if (c == '.')
        {
            /* We save the index of the decimal point character in the token
               in outputAttributes. */
            codepointToOutput = '.';
            parser->outputAttributes = (unsigned char)parser->outputBufferUsed;
            parser->lexerState = LEXER_IN_NUMBER_AFTER_DOT;
        }
        else if (c == 'e' || c == 'E')
        {
            codepointToOutput = c;
            parser->lexerState = LEXER_IN_NUMBER_AFTER_E;
        }
        else
        {
            /* The number is finished. */
            if (!ProcessToken(parser))
            {
                return JSON_Failure;
            }
            goto reprocess;
        }
        break;

    case LEXER_IN_NUMBER_AFTER_DOT:
        if (c == EOF_CODEPOINT)
        {
            /* FlushLexer() will trigger the appropriate error. */
        }
        else if (c >= '0' && c <= '9')
        {
            codepointToOutput = c;
            parser->lexerState = LEXER_IN_NUMBER_FRACTIONAL_DIGITS;
        }
        else
        {
            SetErrorAtToken(parser, JSON_Error_InvalidNumber);
            return JSON_Failure;
        }
        break;

    case LEXER_IN_NUMBER_FRACTIONAL_DIGITS:
        if (c >= '0' && c <= '9')
        {
            codepointToOutput = c;
        }
        else if (c == 'e' || c == 'E')
        {
            codepointToOutput = c;
            parser->lexerState = LEXER_IN_NUMBER_AFTER_E;
        }
        else
        {
            /* The number is finished. */
            if (!ProcessToken(parser))
            {
                return JSON_Failure;
            }
            goto reprocess;
        }
        break;

    case LEXER_IN_NUMBER_AFTER_E:
        if (c == EOF_CODEPOINT)
        {
            /* FlushLexer() will trigger the appropriate error. */
        }
        else if (c == '+')
        {
            codepointToOutput = '+';
            parser->lexerState = LEXER_IN_NUMBER_AFTER_EXPONENT_SIGN;
        }
        else if (c == '-')
        {
            codepointToOutput = '-';
            parser->lexerState = LEXER_IN_NUMBER_AFTER_EXPONENT_SIGN;
        }
        else if (c >= '0' && c <= '9')
        {
            codepointToOutput = c;
            parser->lexerState = LEXER_IN_NUMBER_EXPONENT_DIGITS;
        }
        else
        {
            SetErrorAtToken(parser, JSON_Error_InvalidNumber);
            return JSON_Failure;
        }
        break;

    case LEXER_IN_NUMBER_AFTER_EXPONENT_SIGN:
        if (c == EOF_CODEPOINT)
        {
            /* FlushLexer() will trigger the appropriate error. */
        }
        else if (c >= '0' && c <= '9')
        {
            codepointToOutput = c;
            parser->lexerState = LEXER_IN_NUMBER_EXPONENT_DIGITS;
        }
        else
        {
            SetErrorAtToken(parser, JSON_Error_InvalidNumber);
            return JSON_Failure;
        }
        break;

    case LEXER_IN_NUMBER_EXPONENT_DIGITS:
        if (c >= '0' && c <= '9')
        {
            codepointToOutput = c;
        }
        else
        {
            /* The number is finished. */
            if (!ProcessToken(parser))
            {
                return JSON_Failure;
            }
            goto reprocess;
        }
        break;

    case LEXER_IN_COMMENT_AFTER_SLASH:
        if (c == '/')
        {
            parser->lexerState = LEXER_IN_SINGLE_LINE_COMMENT;
        }
        else if (c == '*')
        {
            parser->lexerState = LEXER_IN_MULTI_LINE_COMMENT;
        }
        else
        {
            SetErrorAtToken(parser, JSON_Error_UnknownToken);
            return JSON_Failure;
        }
        break;

    case LEXER_IN_SINGLE_LINE_COMMENT:
        if (c == CARRIAGE_RETURN_CODEPOINT || c == LINE_FEED_CODEPOINT || c == EOF_CODEPOINT)
        {
            tokenFinished = 1;
        }
        break;

    case LEXER_IN_MULTI_LINE_COMMENT:
        if (c == '*')
        {
            parser->lexerState = LEXER_IN_MULTI_LINE_COMMENT_AFTER_STAR;
        }
        break;

    case LEXER_IN_MULTI_LINE_COMMENT_AFTER_STAR:
        if (c == '/')
        {
            tokenFinished = 1;
        }
        else if (c != '*')
        {
            parser->lexerState = LEXER_IN_MULTI_LINE_COMMENT;
        }
        break;
    }

    if (codepointToOutput != EOF_CODEPOINT)
    {
        /* String tokens use the output encoding. Number tokens are always
           encoded in ASCII. */
        if (parser->token == TOKEN_NUMBER)
        {
            if (!OutputNumberCharacter(parser, (unsigned char)codepointToOutput))
            {
                return JSON_Failure;
            }
        }
        else if (!OutputStringCodepoint(parser, codepointToOutput))
        {
            return JSON_Failure;
        }
    }

    if (tokenFinished && !ProcessToken(parser))
    {
        return JSON_Failure;
    }

    /* The current codepoint has been accepted, so advance the codepoint
       location counters accordingly. Note that the one time we don't
       do this is when the codepoint is EOF, which doesn't actually
       appear in the input stream. */
    if (c == CARRIAGE_RETURN_CODEPOINT)
    {
        parser->parserStatus |= PARSER_AFTER_CARRIAGE_RETURN;
    }
    if (c != EOF_CODEPOINT)
    {
        parser->codepointLocationByte += encodedLength;
        if (c == CARRIAGE_RETURN_CODEPOINT || c == LINE_FEED_CODEPOINT)
        {
            /* The next character will begin a new line. */
            parser->codepointLocationLine++;
            parser->codepointLocationColumn = 0;
        }
        else
        {
            /* The next character will be on the same line. */
            parser->codepointLocationColumn++;
        }
    }

    return JSON_Success;
}

static JSON_Status FlushLexer(JSON_Parser parser)
{
    /* Push the EOF codepoint to the lexer so that it can finish the pending
       token, if any. The EOF codepoint is never emitted by the decoder
       itself, since it is outside the Unicode range and therefore cannot
       be encoded in any of the possible input encodings. */
    if (!ProcessCodepoint(parser, EOF_CODEPOINT, 0))
    {
        return JSON_Failure;
    }

    /* The lexer should be idle when parsing finishes. */
    if (parser->lexerState != LEXER_IDLE)
    {
        SetErrorAtToken(parser, JSON_Error_IncompleteToken);
        return JSON_Failure;
    }
    return JSON_Success;
}

/* Parser's decoder functions. */

/* Forward declaration. */
static JSON_Status ProcessInputBytes(JSON_Parser parser, const unsigned char* pBytes, size_t length);

static JSON_Status ProcessUnknownByte(JSON_Parser parser, unsigned char b)
{
    /* When the input encoding is unknown, the first 4 bytes of input are
       recorded in decoder.bits. */

    unsigned char bytes[4];
    switch (parser->decoder.state)
    {
    case DECODER_RESET:
        parser->decoder.state = DECODER_PROCESSED_1_OF_4;
        parser->decoder.bits = b << 24;
        break;

    case DECODER_PROCESSED_1_OF_4:
        parser->decoder.state = DECODER_PROCESSED_2_OF_4;
        parser->decoder.bits |= b << 16;
        break;

    case DECODER_PROCESSED_2_OF_4:
        parser->decoder.state = DECODER_PROCESSED_3_OF_4;
        parser->decoder.bits |= b << 8;
        break;

    case DECODER_PROCESSED_3_OF_4:
        bytes[0] = parser->decoder.bits >> 24;
        bytes[1] = parser->decoder.bits >> 16;
        bytes[2] = parser->decoder.bits >> 8;
        bytes[3] = b;

        /* We try to match the following patterns in order, where .. is any
           byte value and nz is any non-zero byte value:
              EF BB BF .. => UTF-8 with BOM
              FF FE 00 00 => UTF-32LE with BOM
              FF FE nz 00 => UTF-16LE with BOM
              00 00 FE FF -> UTF-32BE with BOM
              FE FF .. .. => UTF-16BE with BOM
              nz nz .. .. => UTF-8
              nz 00 nz .. => UTF-16LE
              nz 00 00 00 => UTF-32LE
              00 nz .. .. => UTF-16BE
              00 00 00 nz => UTF-32BE
              .. .. .. .. => unknown encoding */
        if (bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        {
            /* EF BB BF .. */
            parser->inputEncoding = JSON_UTF8;
        }
        else if (bytes[0] == 0xFF && bytes[1] == 0xFE && bytes[3] == 0x00)
        {
            /* FF FE 00 00 or
               FF FE nz 00 */
            parser->inputEncoding = (bytes[2] == 0x00) ? JSON_UTF32LE : JSON_UTF16LE;
        }
        else if (bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0xFE && bytes[3] == 0xFF)
        {
            /* 00 00 FE FF */
            parser->inputEncoding = JSON_UTF32BE;
        }
        else if (bytes[0] == 0xFE && bytes[1] == 0xFF)
        {
            /* FE FF .. .. */
            parser->inputEncoding = JSON_UTF16BE;
        }
        else if (bytes[0] != 0x00)
        {
            /* nz .. .. .. */
            if (bytes[1] != 0x00)
            {
                /* nz nz .. .. */
                parser->inputEncoding = JSON_UTF8;
            }
            else if (bytes[2] != 0x00)
            {
                /* nz 00 nz .. */
                parser->inputEncoding = JSON_UTF16LE;
            }
            else if (bytes[3] == 0x00)
            {
                /* nz 00 00 00 */
                parser->inputEncoding = JSON_UTF32LE;
            }
            else
            {
                /* nz 00 00 nz => error */
            }
        }
        else if (bytes[1] != 0x00)
        {
            /* 00 nz .. .. */
            parser->inputEncoding = JSON_UTF16BE;
        }
        else if (bytes[2] == 0x00 && bytes[3] != 0x00)
        {
            /* 00 00 00 nz */
            parser->inputEncoding = JSON_UTF32BE;
        }
        else
        {
            /* 00 00 nz .. or
               00 00 00 00 => error */
        }

        if (parser->inputEncoding == JSON_UnknownEncoding)
        {
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }

        /* Reset the decoder before reprocessing the bytes. */
        ResetDecoder(&parser->decoder);
        return ProcessInputBytes(parser, bytes, 4);
    }

    /* We don't have 4 bytes yet. */
    return JSON_Success;
}

JSON_Status ProcessInputBytes(JSON_Parser parser, const unsigned char* pBytes, size_t length)
{
    /* Note that if length is 0, pBytes is allowed to be NULL. */
    size_t i = 0;
    while (parser->inputEncoding == JSON_UnknownEncoding && i < length)
    {
        if (!ProcessUnknownByte(parser, pBytes[i]))
        {
            return JSON_Failure;
        }
        i++;
    }
    while (i < length)
    {
        DecoderOutput output = DecodeByte(&parser->decoder, parser->inputEncoding, pBytes[i]);
        DecoderResultCode result = DECODER_RESULT_CODE(output);
        switch (result)
        {
        case SEQUENCE_PENDING:
            break;

        case SEQUENCE_COMPLETE:
            if (!ProcessCodepoint(parser, DECODER_CODEPOINT(output), DECODER_SEQUENCE_LENGTH(output)))
            {
                return JSON_Failure;
            }
            break;

        case SEQUENCE_INVALID_INCLUSIVE:
        case SEQUENCE_INVALID_EXCLUSIVE:
            if (parser->parserStatus & PARSER_REPLACE_INVALID_ENCODING_SEQUENCES)
            {
                if (parser->lexerState == LEXER_IN_STRING)
                {
                    /* Note that we set JSON_ContainsReplacedCharacter in the output
                       attributes only when the encoding sequence represents a single
                       codepoint inside a string token; that is the only case where
                       replacing the invalid sequence avoids triggering an error and
                       the string attributes actually get passed to a handler. */
                    parser->outputAttributes |= JSON_ContainsReplacedCharacter;
                }
                if (!ProcessCodepoint(parser, REPLACEMENT_CHARACTER_CODEPOINT, DECODER_SEQUENCE_LENGTH(output)))
                {
                    return JSON_Failure;
                }
                if (result == SEQUENCE_INVALID_EXCLUSIVE)
                {
                    i--;
                }
            }
            else
            {
                SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
                return JSON_Failure;
            }
            break;
        }
        i++;
    }
    return JSON_Success;
}

static JSON_Status FlushDecoder(JSON_Parser parser)
{
    /* If the input was 1, 2, or 3 bytes long, and the input encoding was not
       explicitly specified by the client, we can sometimes make a reasonable
       guess. If the input was 1 or 3 bytes long, the only encoding that could
       possibly be valid JSON is UF-8. If the input was 2 bytes long, we try
       to match the following patterns in order, where .. is any byte value
       and nz is any non-zero byte value:
         FF FE => UTF-16LE with BOM
         FE FF => UTF-16BE with BOM
         nz nz => UTF-8
         nz 00 => UTF-16LE
         00 nz => UTF-16BE
         .. .. => unknown encoding */
    if (parser->inputEncoding == JSON_UnknownEncoding &&
        parser->decoder.state != DECODER_RESET)
    {
        unsigned char bytes[3];
        size_t length = 0;
        bytes[0] = parser->decoder.bits >> 24;
        bytes[1] = parser->decoder.bits >> 16;
        bytes[2] = parser->decoder.bits >> 8;
        switch (parser->decoder.state)
        {
        case DECODER_PROCESSED_1_OF_4:
            parser->inputEncoding = JSON_UTF8;
            length = 1;
            break;

        case DECODER_PROCESSED_2_OF_4:
            if (bytes[0] == 0xFF && bytes[1] == 0xFE)
            {
                /* FF FE */
                parser->inputEncoding = JSON_UTF16LE;
            }
            else if (bytes[0] == 0xFE && bytes[1] == 0xFF)
            {
                /* FE FF */
                parser->inputEncoding = JSON_UTF16BE;
            }
            else if (bytes[0] != 0x00)
            {
                /* nz nz or
                   nz 00 */
                parser->inputEncoding = bytes[1] ? JSON_UTF8 : JSON_UTF16LE;
            }
            else if (bytes[1] != 0x00)
            {
                /* 00 nz */
                parser->inputEncoding = JSON_UTF16BE;
            }
            else
            {
                SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
                return JSON_Failure;
            }
            length = 2;
            break;

        case DECODER_PROCESSED_3_OF_4:
            parser->inputEncoding = JSON_UTF8;
            length = 3;
            break;
        }

        /* Reset the decoder before reprocessing the bytes. */
        parser->decoder.state = DECODER_RESET;
        parser->decoder.bits = 0;
        if (!ProcessInputBytes(parser, bytes, length))
        {
            return JSON_Failure;
        }
    }

    /* The decoder should be idle when parsing finishes. */
    if (parser->decoder.state != DECODER_RESET)
    {
        SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
        return JSON_Failure;
    }
    return JSON_Success;
}

/* Library API functions. */

JSON_Parser JSON_CALL JSON_Parser_Create(const JSON_Parser_MemorySuite* pMemorySuite)
{
    JSON_Parser parser;
    JSON_Parser_MemorySuite memorySuite;
    if (pMemorySuite)
    {
        memorySuite = *pMemorySuite;
        if (!memorySuite.malloc || !memorySuite.realloc || !memorySuite.free)
        {
            /* The full memory suite must be specified. */
            return NULL;
        }
    }
    else
    {
        memorySuite.malloc = &DefaultMallocHandler;
        memorySuite.realloc = &DefaultReallocHandler;
        memorySuite.free = &DefaultFreeHandler;
    }
    parser = (JSON_Parser)memorySuite.malloc(NULL, sizeof(struct JSON_Parser_Data));
    if (!parser)
    {
        return NULL;
    }
    parser->memorySuite = memorySuite;
    ResetParserData(parser, 0/* isInitialized */);
    return parser;
}

JSON_Status JSON_CALL JSON_Parser_Free(JSON_Parser parser)
{
    if (!parser || (parser->parserStatus & PARSER_IN_HANDLER))
    {
        return JSON_Failure;
    }
    if (parser->pOutputBuffer != parser->defaultOutputBuffer)
    {
        CallFree(parser, parser->pOutputBuffer);
    }
    if (parser->pSymbolStack != parser->defaultSymbolStack)
    {
        CallFree(parser, parser->pSymbolStack);
    }
    while (parser->pMemberNames)
    {
        PopMemberNameList(parser);
    }
    parser->parserStatus |= PARSER_IN_MEMORY_HANDLER;
    parser->memorySuite.free(NULL, parser);
    return JSON_Success;
}

JSON_Status JSON_CALL JSON_Parser_Reset(JSON_Parser parser)
{
    if (!parser || (parser->parserStatus & PARSER_IN_HANDLER))
    {
        return JSON_Failure;
    }
    ResetParserData(parser, 1/* isInitialized */);
    return JSON_Success;
}

JSON_Error JSON_CALL JSON_Parser_GetError(JSON_Parser parser)
{
    return parser ? (JSON_Error)parser->error : JSON_Error_None;
}

JSON_Status JSON_CALL JSON_Parser_GetErrorLocation(JSON_Parser parser, JSON_Location* pLocation)
{
    if (!pLocation || !parser || parser->error == JSON_Error_None)
    {
        return JSON_Failure;
    }
    if (parser->errorOffset == ERROR_LOCATION_IS_TOKEN_START)
    {
        pLocation->byte = parser->tokenLocationByte;
        pLocation->line = parser->tokenLocationLine;
        pLocation->column = parser->tokenLocationColumn;
    }
    else
    {
        pLocation->byte = parser->codepointLocationByte - (minEncodingSequenceLengths[parser->inputEncoding] * parser->errorOffset);
        pLocation->line = parser->codepointLocationLine;
        pLocation->column = parser->codepointLocationColumn - parser->errorOffset;
    }
    pLocation->depth = parser->depth;
    return JSON_Success;
}

void* JSON_CALL JSON_Parser_GetUserData(JSON_Parser parser)
{
    return parser ? parser->userData : NULL;
}

JSON_Status JSON_CALL JSON_Parser_SetUserData(JSON_Parser parser, void* userData)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->userData = userData;
    return JSON_Success;
}

JSON_Status JSON_CALL JSON_Parser_GetTokenLocation(JSON_Parser parser, JSON_Location* pLocation)
{
    if (!parser || !pLocation || !(parser->parserStatus & PARSER_IN_PARSE_HANDLER))
    {
        return JSON_Failure;
    }
    pLocation->byte = parser->tokenLocationByte;
    pLocation->line = parser->tokenLocationLine;
    pLocation->column = parser->tokenLocationColumn;
    pLocation->depth = parser->depth;
    return JSON_Success;
}

JSON_Parser_NullHandler JSON_CALL JSON_Parser_GetNullHandler(JSON_Parser parser)
{
    return parser ? parser->nullHandler : NULL;
}

JSON_Status JSON_CALL JSON_Parser_SetNullHandler(JSON_Parser parser, JSON_Parser_NullHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->nullHandler = handler;
    return JSON_Success;
}

JSON_Parser_BooleanHandler JSON_CALL JSON_Parser_GetBooleanHandler(JSON_Parser parser)
{
    return parser ? parser->booleanHandler : NULL;
}

JSON_Status JSON_CALL JSON_Parser_SetBooleanHandler(JSON_Parser parser, JSON_Parser_BooleanHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->booleanHandler = handler;
    return JSON_Success;
}

JSON_Parser_StringHandler JSON_CALL JSON_Parser_GetStringHandler(JSON_Parser parser)
{
    return parser ? parser->stringHandler : NULL;
}

JSON_Status JSON_CALL JSON_Parser_SetStringHandler(JSON_Parser parser, JSON_Parser_StringHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->stringHandler = handler;
    return JSON_Success;
}

JSON_Parser_NumberHandler JSON_CALL JSON_Parser_GetNumberHandler(JSON_Parser parser)
{
    return parser ? parser->numberHandler : NULL;
}

JSON_Status JSON_CALL JSON_Parser_SetNumberHandler(JSON_Parser parser, JSON_Parser_NumberHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->numberHandler = handler;
    return JSON_Success;
}

JSON_Parser_RawNumberHandler JSON_CALL JSON_Parser_GetRawNumberHandler(JSON_Parser parser)
{
    return parser ? parser->rawNumberHandler : NULL;
}

JSON_Status JSON_CALL JSON_Parser_SetRawNumberHandler(JSON_Parser parser, JSON_Parser_RawNumberHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->rawNumberHandler = handler;
    return JSON_Success;
}

JSON_Parser_SpecialNumberHandler JSON_CALL JSON_Parser_GetSpecialNumberHandler(JSON_Parser parser)
{
    return parser ? parser->specialNumberHandler : NULL;
}

JSON_Status JSON_CALL JSON_Parser_SetSpecialNumberHandler(JSON_Parser parser, JSON_Parser_SpecialNumberHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->specialNumberHandler = handler;
    return JSON_Success;
}

JSON_Parser_StartObjectHandler JSON_CALL JSON_Parser_GetStartObjectHandler(JSON_Parser parser)
{
    return parser ? parser->startObjectHandler : NULL;
}

JSON_Status JSON_CALL JSON_Parser_SetStartObjectHandler(JSON_Parser parser, JSON_Parser_StartObjectHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->startObjectHandler = handler;
    return JSON_Success;
}

JSON_Parser_EndObjectHandler JSON_CALL JSON_Parser_GetEndObjectHandler(JSON_Parser parser)
{
    return parser ? parser->endObjectHandler : NULL;
}

JSON_Status JSON_CALL JSON_Parser_SetEndObjectHandler(JSON_Parser parser, JSON_Parser_EndObjectHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->endObjectHandler = handler;
    return JSON_Success;
}

JSON_Parser_ObjectMemberHandler JSON_CALL JSON_Parser_GetObjectMemberHandler(JSON_Parser parser)
{
    return parser ? parser->objectMemberHandler : NULL;
}

JSON_Status JSON_CALL JSON_Parser_SetObjectMemberHandler(JSON_Parser parser, JSON_Parser_ObjectMemberHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->objectMemberHandler = handler;
    return JSON_Success;
}

JSON_Parser_StartArrayHandler JSON_CALL JSON_Parser_GetStartArrayHandler(JSON_Parser parser)
{
    return parser ? parser->startArrayHandler : NULL;
}

JSON_Status JSON_CALL JSON_Parser_SetStartArrayHandler(JSON_Parser parser, JSON_Parser_StartArrayHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->startArrayHandler = handler;
    return JSON_Success;
}

JSON_Parser_EndArrayHandler JSON_CALL JSON_Parser_GetEndArrayHandler(JSON_Parser parser)
{
    return parser ? parser->endArrayHandler : NULL;
}

JSON_Status JSON_CALL JSON_Parser_SetEndArrayHandler(JSON_Parser parser, JSON_Parser_EndArrayHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->endArrayHandler = handler;
    return JSON_Success;
}

JSON_Parser_ArrayItemHandler JSON_CALL JSON_Parser_GetArrayItemHandler(JSON_Parser parser)
{
    return parser ? parser->arrayItemHandler : NULL;
}

JSON_Status JSON_CALL JSON_Parser_SetArrayItemHandler(JSON_Parser parser, JSON_Parser_ArrayItemHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->arrayItemHandler = handler;
    return JSON_Success;
}

JSON_Boolean JSON_CALL JSON_Parser_StartedParsing(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_STARTED)) ? JSON_True : JSON_False;
}

JSON_Boolean JSON_CALL JSON_Parser_FinishedParsing(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_FINISHED)) ? JSON_True : JSON_False;
}

JSON_Encoding JSON_CALL JSON_Parser_GetInputEncoding(JSON_Parser parser)
{
    return parser ? (JSON_Encoding)parser->inputEncoding : JSON_UnknownEncoding;
}

JSON_Status JSON_CALL JSON_Parser_SetInputEncoding(JSON_Parser parser, JSON_Encoding encoding)
{
    if (!parser || (parser->parserStatus & PARSER_STARTED) || encoding < JSON_UnknownEncoding || encoding > JSON_UTF32BE)
    {
        return JSON_Failure;
    }
    parser->inputEncoding = (unsigned char)encoding;
    return JSON_Success;
}

JSON_Encoding JSON_CALL JSON_Parser_GetOutputEncoding(JSON_Parser parser)
{
    return parser ? (JSON_Encoding)parser->outputEncoding : JSON_UTF8;
}

JSON_Status JSON_CALL JSON_Parser_SetOutputEncoding(JSON_Parser parser, JSON_Encoding encoding)
{
    if (!parser || (parser->parserStatus & PARSER_STARTED) || encoding <= JSON_UnknownEncoding || encoding > JSON_UTF32BE)
    {
        return JSON_Failure;
    }
    parser->outputEncoding = (unsigned char)encoding;
    return JSON_Success;
}

size_t JSON_CALL JSON_Parser_GetMaxOutputStringLength(JSON_Parser parser)
{
    return parser ? parser->maxOutputStringLength : (size_t)-1;
}

JSON_Status JSON_CALL JSON_Parser_SetMaxOutputStringLength(JSON_Parser parser, size_t maxLength)
{
    if (!parser || (parser->parserStatus & PARSER_STARTED))
    {
        return JSON_Failure;
    }
    parser->maxOutputStringLength = maxLength;
    return JSON_Success;
}

JSON_Boolean JSON_CALL JSON_Parser_GetAllowBOM(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_ALLOW_BOM)) ? JSON_True : JSON_False;
}

JSON_Status JSON_CALL JSON_Parser_SetAllowBOM(JSON_Parser parser, JSON_Boolean allowBOM)
{
    if (!parser || (parser->parserStatus & PARSER_STARTED))
    {
        return JSON_Failure;
    }
    if (allowBOM)
    {
        parser->parserStatus |= PARSER_ALLOW_BOM;
    }
    else
    {
        parser->parserStatus &= ~PARSER_ALLOW_BOM;
    }
    return JSON_Success;
}

JSON_Boolean JSON_CALL JSON_Parser_GetAllowComments(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_ALLOW_COMMENTS)) ? JSON_True : JSON_False;
}

JSON_Status JSON_CALL JSON_Parser_SetAllowComments(JSON_Parser parser, JSON_Boolean allowComments)
{
    if (!parser || (parser->parserStatus & PARSER_STARTED))
    {
        return JSON_Failure;
    }
    if (allowComments)
    {
        parser->parserStatus |= PARSER_ALLOW_COMMENTS;
    }
    else
    {
        parser->parserStatus &= ~PARSER_ALLOW_COMMENTS;
    }
    return JSON_Success;
}

JSON_Boolean JSON_CALL JSON_Parser_GetAllowTrailingCommas(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_ALLOW_TRAILING_COMMAS)) ? JSON_True : JSON_False;
}

JSON_Status JSON_CALL JSON_Parser_SetAllowTrailingCommas(JSON_Parser parser, JSON_Boolean allowTrailingCommas)
{
    if (!parser || (parser->parserStatus & PARSER_STARTED))
    {
        return JSON_Failure;
    }
    if (allowTrailingCommas)
    {
        parser->parserStatus |= PARSER_ALLOW_TRAILING_COMMAS;
    }
    else
    {
        parser->parserStatus &= ~PARSER_ALLOW_TRAILING_COMMAS;
    }
    return JSON_Success;
}

JSON_Boolean JSON_CALL JSON_Parser_GetAllowSpecialNumbers(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_ALLOW_SPECIAL_NUMBERS)) ? JSON_True : JSON_False;
}

JSON_Status JSON_CALL JSON_Parser_SetAllowSpecialNumbers(JSON_Parser parser, JSON_Boolean allowSpecialNumbers)
{
    if (!parser || (parser->parserStatus & PARSER_STARTED))
    {
        return JSON_Failure;
    }
    if (allowSpecialNumbers)
    {
        parser->parserStatus |= PARSER_ALLOW_SPECIAL_NUMBERS;
    }
    else
    {
        parser->parserStatus &= ~PARSER_ALLOW_SPECIAL_NUMBERS;
    }
    return JSON_Success;
}

JSON_Boolean JSON_CALL JSON_Parser_GetAllowHexNumbers(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_ALLOW_HEX_NUMBERS)) ? JSON_True : JSON_False;
}

JSON_Status JSON_CALL JSON_Parser_SetAllowHexNumbers(JSON_Parser parser, JSON_Boolean allowHexNumbers)
{
    if (!parser || (parser->parserStatus & PARSER_STARTED))
    {
        return JSON_Failure;
    }
    if (allowHexNumbers)
    {
        parser->parserStatus |= PARSER_ALLOW_HEX_NUMBERS;
    }
    else
    {
        parser->parserStatus &= ~PARSER_ALLOW_HEX_NUMBERS;
    }
    return JSON_Success;
}

JSON_Boolean JSON_CALL JSON_Parser_GetReplaceInvalidEncodingSequences(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_REPLACE_INVALID_ENCODING_SEQUENCES)) ? JSON_True : JSON_False;
}

JSON_Status JSON_CALL JSON_Parser_SetReplaceInvalidEncodingSequences(JSON_Parser parser, JSON_Boolean replaceInvalidEncodingSequences)
{
    if (!parser || (parser->parserStatus & PARSER_STARTED))
    {
        return JSON_Failure;
    }
    if (replaceInvalidEncodingSequences)
    {
        parser->parserStatus |= PARSER_REPLACE_INVALID_ENCODING_SEQUENCES;
    }
    else
    {
        parser->parserStatus &= ~PARSER_REPLACE_INVALID_ENCODING_SEQUENCES;
    }
    return JSON_Success;
}

JSON_Boolean JSON_CALL JSON_Parser_GetTrackObjectMembers(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_TRACK_OBJECT_MEMBERS)) ? JSON_True : JSON_False;
}

JSON_Status JSON_CALL JSON_Parser_SetTrackObjectMembers(JSON_Parser parser, JSON_Boolean trackObjectMembers)
{
    if (!parser || (parser->parserStatus & PARSER_STARTED))
    {
        return JSON_Failure;
    }
    if (trackObjectMembers)
    {
        parser->parserStatus |= PARSER_TRACK_OBJECT_MEMBERS;
    }
    else
    {
        parser->parserStatus &= ~PARSER_TRACK_OBJECT_MEMBERS;
    }
    return JSON_Success;
}

JSON_Status JSON_CALL JSON_Parser_Parse(JSON_Parser parser, const char* pBytes, size_t length, JSON_Boolean isFinal)
{
    JSON_Status status = JSON_Failure;
    if (parser && !(parser->parserStatus & (PARSER_FINISHED | PARSER_IN_HANDLER)))
    {
        int finishedParsing = 0;
        parser->parserStatus |= PARSER_STARTED;
        if (ProcessInputBytes(parser, (const unsigned char*)pBytes, length))
        {
            /* New input was parsed successfully. */
            if (isFinal)
            {
                /* Make sure there is nothing pending in the decoder, lexer, or parser. */
                if (FlushDecoder(parser) && FlushLexer(parser) && FlushParser(parser))
                {
                    status = JSON_Success;
                }
                finishedParsing = 1;
            }
            else
            {
                status = JSON_Success;
            }
        }
        else
        {
            /* New input failed to parse. */
            finishedParsing = 1;
        }
        if (finishedParsing)
        {
            parser->parserStatus |= PARSER_FINISHED;
        }
    }
    return status;
}

/* This array must match the order and number of the JSON_Error enum. */
static const char* errorStrings[] =
{
    /* JSON_Error_None */                            "no error",
    /* JSON_Error_OutOfMemory */                     "the parser could not allocate enough memory",
    /* JSON_Error_AbortedByHandler */                "parsing was aborted by a handler",
    /* JSON_Error_BOMNotAllowed */                   "the input begins with a byte-order mark (BOM), which is not allowed by RFC 4627",
    /* JSON_Error_InvalidEncodingSequence */         "the input contains a byte or sequence of bytes that is not valid for the input encoding",
    /* JSON_Error_UnknownToken */                    "the input contains an unknown token",
    /* JSON_Error_UnexpectedToken */                 "the input contains an unexpected token",
    /* JSON_Error_IncompleteToken */                 "the input ends in the middle of a token",
    /* JSON_Error_MoreTokensExpected */              "the input ends when more tokens are expected",
    /* JSON_Error_UnescapedControlCharacter */       "the input contains a string containing an unescaped control character (U+0000 - U+001F)",
    /* JSON_Error_InvalidEscapeSequence */           "the input contains a string containing an invalid escape sequence",
    /* JSON_Error_UnpairedSurrogateEscapeSequence */ "the input contains a string containing an unmatched UTF-16 surrogate codepoint",
    /* JSON_Error_TooLongString */                   "the input contains a string that is too long",
    /* JSON_Error_InvalidNumber */                   "the input contains an invalid number",
    /* JSON_Error_TooLongNumber */                   "the input contains a number that is too long",
    /* JSON_Error_DuplicateObjectMember */           "the input contains an object with duplicate members"
};

const char* JSON_CALL JSON_ErrorString(JSON_Error error)
{
    return ((unsigned int)error < (sizeof(errorStrings) / sizeof(errorStrings[0])))
        ? errorStrings[error]
        : "";
}
