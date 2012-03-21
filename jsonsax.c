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
#define MAX_NUMBER_CHARACTERS        63
#define DEFAULT_OUTPUT_BUFFER_LENGTH (MAX_NUMBER_CHARACTERS + 1)
#define DEFAULT_SYMBOL_STACK_SIZE    32

/* 32-bit unsigned integer type (compiler-dependent). */
#if defined(_MSC_VER)
typedef unsigned __int32 JSON_UInt32;
#else
#include <stdint.h>
typedef uint32_t JSON_UInt32;
#endif

/* Especially-relevant Unicode codepoints. */
#define BACKSPACE_CODEPOINT         0x0008
#define TAB_CODEPOINT               0x0009
#define LINE_FEED_CODEPOINT         0x000A
#define FORM_FEED_CODEPOINT         0x000C
#define CARRIAGE_RETURN_CODEPOINT   0x000D
#define FIRST_NON_CONTROL_CODEPOINT 0x0020
#define FIRST_NON_ASCII_CODEPOINT   0x0080
#define FIRST_2_BYTE_UTF8_CODEPOINT 0x0080
#define FIRST_3_BYTE_UTF8_CODEPOINT 0x0800
#define BOM_CODEPOINT               0xFEFF
#define FIRST_NON_BMP_CODEPOINT     0x10000
#define FIRST_4_BYTE_UTF8_CODEPOINT 0x10000
#define MAX_CODEPOINT               0x10FFFF
#define EOF_CODEPOINT               0xFFFFFFFF

/* Bit-masking macros. */
#define BOTTOM_BIT(b)    ((b) & 0x1)
#define BOTTOM_2_BITS(b) ((b) & 0x3)
#define BOTTOM_3_BITS(b) ((b) & 0x7)
#define BOTTOM_4_BITS(b) ((b) & 0xF)
#define BOTTOM_5_BITS(b) ((b) & 0x1F)
#define BOTTOM_6_BITS(b) ((b) & 0x3F)
#define BOTTOM_7_BITS(b) ((b) & 0x7F)

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

/* Combinable parser status bits. */
typedef enum tag_ParserStatus
{
    PARSER_RESET                  = 0,
    PARSER_STARTED                = 1 << 0,
    PARSER_FINISHED               = 1 << 1,
    PARSER_IN_CALLBACK            = 1 << 2,
    PARSER_AFTER_CARRIAGE_RETURN  = 1 << 3,
    PARSER_ALLOW_BOM              = 1 << 4,
    PARSER_ALLOW_TRAILING_COMMAS  = 1 << 5,
    PARSER_ALLOW_NAN_AND_INFINITY = 1 << 6,
    PARSER_TRACK_OBJECT_MEMBERS   = 1 << 7
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
    LEXER_IN_NUMBER_SIGNIFICAND_AFTER_MINUS                 = 14,
    LEXER_IN_NUMBER_SIGNIFICAND_AFTER_LEADING_ZERO          = 15,
    LEXER_IN_NUMBER_SIGNIFICAND_INTEGER_DIGITS              = 16,
    LEXER_IN_NUMBER_SIGNIFICAND_AFTER_DOT                   = 17,
    LEXER_IN_NUMBER_SIGNIFICAND_FRACTIONAL_DIGITS           = 18,
    LEXER_IN_NUMBER_AFTER_E                                 = 19,
    LEXER_IN_NUMBER_AFTER_EXPONENT_SIGN                     = 20,
    LEXER_IN_NUMBER_EXPONENT_DIGITS                         = 21
} LexerState;

/* Mutually-exclusive decoder states. The values are defined so that the
   bottom 4 bits of a value indicate the total number of bytes in the
   sequences being decoded. */
typedef enum tag_DecoderState
{
    DECODER_PROCESSED_0_OF_1 = 0x01,
    DECODER_PROCESSED_1_OF_2 = 0x12,
    DECODER_PROCESSED_1_OF_3 = 0x13,
    DECODER_PROCESSED_2_OF_3 = 0x23,
    DECODER_PROCESSED_1_OF_4 = 0x14,
    DECODER_PROCESSED_2_OF_4 = 0x24,
    DECODER_PROCESSED_3_OF_4 = 0x34
} DecoderState;

#define DECODER_SEQUENCE_LENGTH(state) ((state) & 0xF)

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
struct JSON_ParserData
{
    JSON_MemorySuite         memorySuite;
    void*                    userData;
    JSON_NullHandler         nullHandler;
    JSON_BooleanHandler      booleanHandler;
    JSON_StringHandler       stringHandler;
    JSON_NumberHandler       numberHandler;
    JSON_RawNumberHandler    rawNumberHandler;
    JSON_StartObjectHandler  startObjectHandler;
    JSON_EndObjectHandler    endObjectHandler;
    JSON_ObjectMemberHandler objectMemberHandler;
    JSON_StartArrayHandler   startArrayHandler;
    JSON_EndArrayHandler     endArrayHandler;
    JSON_ArrayItemHandler    arrayItemHandler;
    unsigned char            inputEncoding;     /* real type is JSON_Encoding */
    unsigned char            outputEncoding;    /* real type is JSON_Encoding */
    unsigned char            parserStatus;      /* real type is ParserStatus */
    unsigned char            lexerState;        /* real type is LexerState */
    unsigned char            token;             /* real type is Symbol */
    unsigned char            decoderState;      /* real type is DecoderState */
    unsigned char            error;             /* real type is JSON_Error */
    unsigned char            outputAttributes;  /* real type is JSON_StringAttributes */
    JSON_UInt32              lexerBits;
    JSON_UInt32              decoderBits;
    JSON_Location            codepointLocation;
    JSON_Location            tokenLocation;
    JSON_Location            errorLocation;
    unsigned char*           pOutputBuffer;     /* initially set to defaultOutputBuffer */
    size_t                   outputBufferLength;
    size_t                   outputBufferUsed;
    size_t                   maxOutputStringLength;
    unsigned char*           pSymbolStack;      /* initially set to defaultSymbolStack */
    size_t                   symbolStackSize;
    size_t                   symbolStackUsed;
    MemberNames*             pMemberNames;
    unsigned char            defaultOutputBuffer[DEFAULT_OUTPUT_BUFFER_LENGTH];
    unsigned char            defaultSymbolStack[DEFAULT_SYMBOL_STACK_SIZE];
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
    parser->parserStatus |= PARSER_IN_CALLBACK;
    p = parser->memorySuite.malloc(parser, size);
    parser->parserStatus &= ~PARSER_IN_CALLBACK;
    return p;
}

static void* JSON_CALL CallRealloc(JSON_Parser parser, void* ptr, size_t size)
{
    void* p;
    parser->parserStatus |= PARSER_IN_CALLBACK;
    p = parser->memorySuite.realloc(parser, ptr, size);
    parser->parserStatus &= ~PARSER_IN_CALLBACK;
    return p;
}

static void JSON_CALL CallFree(JSON_Parser parser, void* ptr)
{
    parser->parserStatus |= PARSER_IN_CALLBACK;
    parser->memorySuite.free(parser, ptr);
    parser->parserStatus &= ~PARSER_IN_CALLBACK;
}

/* Parser functions. */

static void SetErrorAtCodepoint(JSON_Parser parser, JSON_Error error)
{
    parser->errorLocation = parser->codepointLocation;
    parser->error = error;
}

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

static void SetErrorAtStringEscapeSequenceStart(JSON_Parser parser, JSON_Error error, size_t codepointsAgo)
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
    parser->errorLocation.byte = parser->codepointLocation.byte - (minEncodingSequenceLengths[parser->inputEncoding] * codepointsAgo);
    parser->errorLocation.line = parser->codepointLocation.line;
    parser->errorLocation.column = parser->codepointLocation.column - codepointsAgo;
    parser->error = error;
}

static void SetErrorAtToken(JSON_Parser parser, JSON_Error error)
{
    parser->errorLocation = parser->tokenLocation;
    parser->error = error;
}

static JSON_Status PushObject(JSON_Parser parser)
{
    if (parser->parserStatus & PARSER_TRACK_OBJECT_MEMBERS)
    {
        MemberNames* pNames;
        pNames = (MemberNames*)CallMalloc(parser, sizeof(MemberNames));
        if (!pNames)
        {
            SetErrorAtCodepoint(parser, JSON_Error_OutOfMemory);
            return JSON_Failure;
        }
        pNames->pAncestor = parser->pMemberNames;
        pNames->pFirstName = NULL;
        parser->pMemberNames = pNames;
    }
    return JSON_Success;
}

static void PopObject(JSON_Parser parser)
{
    if (parser->parserStatus & PARSER_TRACK_OBJECT_MEMBERS)
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
}

static JSON_Status AddMemberNameToObject(JSON_Parser parser)
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
    parser->startObjectHandler = NULL;
    parser->endObjectHandler = NULL;
    parser->objectMemberHandler = NULL;
    parser->startArrayHandler = NULL;
    parser->endArrayHandler = NULL;
    parser->arrayItemHandler = NULL;
    parser->inputEncoding = JSON_UnknownEncoding;
    parser->outputEncoding = JSON_UTF8;
    parser->parserStatus = PARSER_RESET;
    parser->lexerState = LEXER_IDLE;
    parser->lexerBits = 0;
    parser->token = TOKEN_NULL;
    parser->decoderState = DECODER_PROCESSED_0_OF_1;
    parser->decoderBits = 0;
    parser->codepointLocation.byte = 0;
    parser->codepointLocation.line = 0;
    parser->codepointLocation.column = 0;
    parser->tokenLocation.byte = 0;
    parser->tokenLocation.line = 0;
    parser->tokenLocation.column = 0;
    parser->errorLocation.byte = 0;
    parser->errorLocation.line = 0;
    parser->errorLocation.column = 0;
    parser->error = JSON_Error_None;
    parser->outputAttributes = JSON_SimpleString;
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
            PopObject(parser);
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
        SetErrorAtCodepoint(parser, JSON_Error_IncompleteInput);
        return JSON_Failure;
    }
    return JSON_Success;
}

static double InterpretNumber(JSON_Parser parser)
{
    /* The C standard does not provide a locale-independent floating-point
       number parser. We have to change the decimal point in our string to
       the locale-specific character before calling strtod(). We are
       assuming here that the decimal point is always a single character
       in every locale. */
    double value;
    char localeDecimalPoint = *(localeconv()->decimal_point);
    if (localeDecimalPoint != '.')
    {
        unsigned char* pDecimalPoint;
        for (pDecimalPoint = parser->pOutputBuffer; *pDecimalPoint; pDecimalPoint++)
        while (*pDecimalPoint)
        {
            if (*pDecimalPoint == '.')
            {
                *pDecimalPoint = (unsigned char)localeDecimalPoint;
                break;
            }
        }
    }
    value = strtod((const char*)parser->pOutputBuffer, NULL);
    return value;
}

static JSON_Status MakeSimpleCallback(JSON_Parser parser, JSON_NullHandler handler)
{
    if (handler)
    {
        JSON_HandlerResult result;
        parser->parserStatus |= PARSER_IN_CALLBACK;
        result = handler(parser, &parser->tokenLocation);
        parser->parserStatus &= ~PARSER_IN_CALLBACK;
        if (result != JSON_ContinueParsing)
        {
            SetErrorAtToken(parser, JSON_Error_AbortedByHandler);
            return JSON_Failure;
        }
    }
    return JSON_Success;
}

static JSON_Status MakeBooleanCallback(JSON_Parser parser, JSON_BooleanHandler handler)
{
    if (handler)
    {
        JSON_HandlerResult result;
        parser->parserStatus |= PARSER_IN_CALLBACK;
        result = handler(parser, &parser->tokenLocation, parser->token == TOKEN_TRUE ? JSON_True : JSON_False);
        parser->parserStatus &= ~PARSER_IN_CALLBACK;
        if (result != JSON_ContinueParsing)
        {
            SetErrorAtToken(parser, JSON_Error_AbortedByHandler);
            return JSON_Failure;
        }
    }
    return JSON_Success;
}

static JSON_Status MakeStringCallback(JSON_Parser parser, JSON_StringHandler handler, int handleDuplicateMemberResult)
{
    if (handler)
    {
        /* Strings should be null-terminated in the appropriate output encoding
           before being passed to parse callbacks. Note that the length passed
           to the callback does not include the null terminator, so we need to
           save the length before we null-terminate the string. */
        JSON_HandlerResult result;
        size_t lengthNotIncludingNullTerminator = parser->outputBufferUsed;
        if (!NullTerminateStringOutput(parser))
        {
            return JSON_Failure;
        }
        parser->parserStatus |= PARSER_IN_CALLBACK;
        result = handler(parser, &parser->tokenLocation, (const char*)parser->pOutputBuffer, lengthNotIncludingNullTerminator, parser->outputAttributes);
        parser->parserStatus &= ~PARSER_IN_CALLBACK;
        if (result != JSON_ContinueParsing)
        {
            SetErrorAtToken(parser, (handleDuplicateMemberResult && result == JSON_TreatAsDuplicateObjectMember)
                            ? JSON_Error_DuplicateObjectMember
                            : JSON_Error_AbortedByHandler);
            return JSON_Failure;
        }
    }
    return JSON_Success;
}

static JSON_Status MakeNumberCallback(JSON_Parser parser, JSON_RawNumberHandler rawHandler, JSON_NumberHandler handler)
{
    /* Note that this is called for NaN, Infinity, and -Infinity, in addition
       to "normal" number tokens. */
    if (rawHandler || handler)
    {
        /* Numbers should be null-terminated with a single null terminator byte
           before being converted to a double and/or passed to callbacks. */
        JSON_HandlerResult result;
        const char* pRawNumber;
        switch (parser->token)
        {
        case TOKEN_NAN:
            pRawNumber = "NaN";
            break;

        case TOKEN_INFINITY:
            pRawNumber = "Infinity";
            break;

        case TOKEN_NEGATIVE_INFINITY:
            pRawNumber = "-Infinity";
            break;

        default:
            pRawNumber = (const char*)parser->pOutputBuffer;
            NullTerminateNumberOutput(parser);
            break;
        }
        parser->parserStatus |= PARSER_IN_CALLBACK;
        if (rawHandler)
        {
            result = rawHandler(parser, &parser->tokenLocation, pRawNumber);
        }
        else
        {
            double value;
            JSON_NumberType type;
            switch (parser->token)
            {
            case TOKEN_NAN:
                value = 0.0;
                type = JSON_NaN;
                break;

            case TOKEN_INFINITY:
                value = HUGE_VAL;
                type = JSON_Infinity;
                break;

            case TOKEN_NEGATIVE_INFINITY:
                value = -HUGE_VAL;
                type = JSON_NegativeInfinity;
                break;

            default:
                value = InterpretNumber(parser);
                type = JSON_NormalNumber;
                break;
            }
            result = handler(parser, &parser->tokenLocation, value, type);
        }
        parser->parserStatus &= ~PARSER_IN_CALLBACK;
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
   callbacks invoked when certain non-terminals are replaced.

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
                if (!MakeSimpleCallback(parser, parser->nullHandler))
                {
                    return JSON_Failure;
                }
                break;

            case 2: /* VALUE => true */
            case 3: /* VALUE => false */
                if (!MakeBooleanCallback(parser, parser->booleanHandler))
                {
                    return JSON_Failure;
                }
                break;

            case 4: /* VALUE => string */
                if (!MakeStringCallback(parser, parser->stringHandler, 0/* handleDuplicateMemberResult */))
                {
                    return JSON_Failure;
                }
                break;

            case 5: /* VALUE => number */
                if (!MakeNumberCallback(parser, parser->rawNumberHandler, parser->numberHandler))
                {
                    return JSON_Failure;
                }
                break;

            case 6: /* VALUE => { MEMBERS } */
                if (!MakeSimpleCallback(parser, parser->startObjectHandler) ||
                    !PushObject(parser))
                {
                    return JSON_Failure;
                }
                symbolsToPush[0] = TOKEN_RIGHT_CURLY;
                symbolsToPush[1] = NT_MEMBERS;
                numberOfSymbolsToPush = 2;
                break;

            case 7: /* VALUE => [ ITEMS ] */
                if (!MakeSimpleCallback(parser, parser->startArrayHandler))
                {
                    return JSON_Failure;
                }
                symbolsToPush[0] = TOKEN_RIGHT_SQUARE;
                symbolsToPush[1] = NT_ITEMS;
                numberOfSymbolsToPush = 2;
                break;

            case 8: /* MEMBERS => MEMBER MORE_MEMBERS */
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
                PopObject(parser);
                if (!MakeSimpleCallback(parser, parser->endObjectHandler))
                {
                    return JSON_Failure;
                }
                processTopSymbol = 1;
                break;

            case 10: /* MEMBER => string : VALUE */
                if (!AddMemberNameToObject(parser) || /* will fail if member is duplicate */
                    !MakeStringCallback(parser, parser->objectMemberHandler, 1/* handleDuplicateMemberResult */))
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
                if (!MakeSimpleCallback(parser, parser->endArrayHandler))
                {
                    return JSON_Failure;
                }
                processTopSymbol = 1;
                break;

            case 17: /* ITEM => VALUE */
                if (!MakeSimpleCallback(parser, parser->arrayItemHandler))
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

    /* Reset the lexer to prepare for the next token. */
    parser->lexerState = LEXER_IDLE;
    parser->lexerBits = 0;
    parser->token = TOKEN_NULL;
    parser->outputAttributes = JSON_SimpleString;
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
    parser->tokenLocation = parser->codepointLocation;
}

static JSON_Status ProcessCodepoint(JSON_Parser parser)
{
    JSON_UInt32 c = parser->decoderBits;
    size_t encodedLength = DECODER_SEQUENCE_LENGTH(parser->decoderState);
    JSON_UInt32 codepointToOutput = EOF_CODEPOINT;
    int tokenFinished = 0;

    /* If the previous codepoint was U+000D (CARRIAGE RETURN), and the current
       codepoint is U+000A (LINE FEED), then treat the 2 codepoints as a single
       line break. */
    if (parser->parserStatus & PARSER_AFTER_CARRIAGE_RETURN)
    {
        if (c == LINE_FEED_CODEPOINT)
        {
            parser->codepointLocation.line--;
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
            parser->lexerState = LEXER_IN_NUMBER_SIGNIFICAND_AFTER_MINUS;
        }
        else if (c == '0')
        {
            StartToken(parser, TOKEN_NUMBER);
            codepointToOutput = '0';
            parser->lexerState = LEXER_IN_NUMBER_SIGNIFICAND_AFTER_LEADING_ZERO;
        }
        else if (c >= '1' && c <= '9')
        {
            StartToken(parser, TOKEN_NUMBER);
            codepointToOutput = c;
            parser->lexerState = LEXER_IN_NUMBER_SIGNIFICAND_INTEGER_DIGITS;
        }
        else if (c == ' ' || c == TAB_CODEPOINT || c == LINE_FEED_CODEPOINT ||
                 c == CARRIAGE_RETURN_CODEPOINT || c == EOF_CODEPOINT)
        {
            /* Ignore whitespace between tokens. */
        }
        else if (c == BOM_CODEPOINT && parser->codepointLocation.byte == 0)
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
        else if (c == 'N' && (parser->parserStatus & PARSER_ALLOW_NAN_AND_INFINITY))
        {
            StartToken(parser, TOKEN_NAN);
            parser->lexerBits = NAN_LITERAL_EXPECTED_CHARS_START_INDEX;
            parser->lexerState = LEXER_IN_LITERAL;
        }
        else if (c == 'I' && (parser->parserStatus & PARSER_ALLOW_NAN_AND_INFINITY))
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
                int codepointsAgo = 2 /* for "\u" */ + byteNumber;
                SetErrorAtStringEscapeSequenceStart(parser, JSON_Error_InvalidEscapeSequence, (size_t)codepointsAgo);
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

    case LEXER_IN_NUMBER_SIGNIFICAND_AFTER_MINUS:
        if (c == EOF_CODEPOINT)
        {
            /* FlushLexer() will trigger the appropriate error. */
        }
        else if (c == 'I' && (parser->parserStatus & PARSER_ALLOW_NAN_AND_INFINITY))
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
                parser->lexerState = LEXER_IN_NUMBER_SIGNIFICAND_AFTER_LEADING_ZERO;
            }
            else if (c >= '1' && c <= '9')
            {
                codepointToOutput = c;
                parser->lexerState = LEXER_IN_NUMBER_SIGNIFICAND_INTEGER_DIGITS;
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

    case LEXER_IN_NUMBER_SIGNIFICAND_AFTER_LEADING_ZERO:
        if (c == '.')
        {
            codepointToOutput = '.';
            parser->lexerState = LEXER_IN_NUMBER_SIGNIFICAND_AFTER_DOT;
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

    case LEXER_IN_NUMBER_SIGNIFICAND_INTEGER_DIGITS:
        if (c >= '0' && c <= '9')
        {
            codepointToOutput = c;
        }
        else if (c == '.')
        {
            codepointToOutput = '.';
            parser->lexerState = LEXER_IN_NUMBER_SIGNIFICAND_AFTER_DOT;
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

    case LEXER_IN_NUMBER_SIGNIFICAND_AFTER_DOT:
        if (c == EOF_CODEPOINT)
        {
            /* FlushLexer() will trigger the appropriate error. */
        }
        else if (c >= '0' && c <= '9')
        {
            codepointToOutput = c;
            parser->lexerState = LEXER_IN_NUMBER_SIGNIFICAND_FRACTIONAL_DIGITS;
        }
        else
        {
            SetErrorAtToken(parser, JSON_Error_InvalidNumber);
            return JSON_Failure;
        }
        break;

    case LEXER_IN_NUMBER_SIGNIFICAND_FRACTIONAL_DIGITS:
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
        parser->codepointLocation.byte += encodedLength;
        if (c == CARRIAGE_RETURN_CODEPOINT || c == LINE_FEED_CODEPOINT)
        {
            /* The next character will begin a new line. */
            parser->codepointLocation.line++;
            parser->codepointLocation.column = 0;
        }
        else
        {
            /* The next character will be on the same line. */
            parser->codepointLocation.column++;
        }
    }

    /* Reset the decoder to prepare for the next codepoint. */
    parser->decoderState = DECODER_PROCESSED_0_OF_1;
    parser->decoderBits = 0;
    return JSON_Success;
}

static JSON_Status FlushLexer(JSON_Parser parser)
{
    /* Push the EOF codepoint to the lexer so that it can finish the pending
       token, if any. The EOF codepoint is never emitted by the decoder
       itself, since it is outside the Unicode range and therefore cannot
       be encoded in any of the possible input encodings. */
    parser->decoderBits = EOF_CODEPOINT;
    if (!ProcessCodepoint(parser))
    {
        return JSON_Failure;
    }

    /* The lexer should be idle when parsing finishes. */
    if (parser->lexerState != LEXER_IDLE)
    {
        SetErrorAtCodepoint(parser, JSON_Error_IncompleteInput);
        return JSON_Failure;
    }
    return JSON_Success;
}

/* Decoder functions. */

/* Forward declaration. */
static JSON_Status ProcessInputBytes(JSON_Parser parser, const unsigned char* pBytes, size_t length);

static JSON_Status ProcessUnknownByte(JSON_Parser parser, unsigned char b)
{
    /* When the input encoding is unknown, the first 4 bytes of input are
       recorded in decoderBits. */

    unsigned char bytes[4];
    switch (parser->decoderState)
    {
    case DECODER_PROCESSED_0_OF_1:
        parser->decoderBits = b << 24;
        parser->decoderState = DECODER_PROCESSED_1_OF_4;
        break;

    case DECODER_PROCESSED_1_OF_4:
        parser->decoderBits |= b << 16;
        parser->decoderState = DECODER_PROCESSED_2_OF_4;
        break;

    case DECODER_PROCESSED_2_OF_4:
        parser->decoderBits |= b << 8;
        parser->decoderState = DECODER_PROCESSED_3_OF_4;
        break;

    case DECODER_PROCESSED_3_OF_4:
        bytes[0] = parser->decoderBits >> 24;
        bytes[1] = parser->decoderBits >> 16;
        bytes[2] = parser->decoderBits >> 8;
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
                /* nz 00 00 nz */
                SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
                return JSON_Failure;
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
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }

        /* Reset the decoder before reprocessing the bytes. */
        parser->decoderState = DECODER_PROCESSED_0_OF_1;
        parser->decoderBits = 0;
        return ProcessInputBytes(parser, bytes, 4);
    }

    /* We don't have 4 bytes yet. */
    return JSON_Success;
}

static JSON_Status ProcessUTF8Byte(JSON_Parser parser, unsigned char b)
{
    /* When the input encoding is UTF-8, the decoded codepoint's bits are
       recorded in the bottom 3 bytes of decoderBits as they are decoded.
       The top byte is not used. */

    switch (parser->decoderState)
    {
    case DECODER_PROCESSED_0_OF_1:
        if (IS_UTF8_SINGLE_BYTE(b))
        {
            parser->decoderBits = b;
            if (!ProcessCodepoint(parser))
            {
                return JSON_Failure;
            }
        }
        else if (IS_UTF8_FIRST_BYTE_OF_2(b))
        {
            /* UTF-8 2-byte sequences that are overlong encodings can be
               detected from just the first byte (C0 or C1). */
            parser->decoderBits = BOTTOM_5_BITS(b) << 6;
            if (parser->decoderBits < FIRST_2_BYTE_UTF8_CODEPOINT)
            {
                SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
                return JSON_Failure;
            }
            parser->decoderState = DECODER_PROCESSED_1_OF_2;
        }
        else if (IS_UTF8_FIRST_BYTE_OF_3(b))
        {
            parser->decoderBits = BOTTOM_4_BITS(b) << 12;
            parser->decoderState = DECODER_PROCESSED_1_OF_3;
        }
        else if (IS_UTF8_FIRST_BYTE_OF_4(b))
        {
            /* Some UTF-8 4-byte sequences that encode out-of-range
               codepoints can be detected from the first byte (F5 - FF). */
            parser->decoderBits = BOTTOM_3_BITS(b) << 18;
            if (parser->decoderBits > MAX_CODEPOINT)
            {
                SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
                return JSON_Failure;
            }
            parser->decoderState = DECODER_PROCESSED_1_OF_4;
        }
        else
        {
            /* The byte is of the form 11111xxx or 10xxxxxx, and is not
               a valid first byte for a UTF-8 sequence. */
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        break;

    case DECODER_PROCESSED_1_OF_2:
        if (IS_UTF8_CONTINUATION_BYTE(b))
        {
            parser->decoderBits |= BOTTOM_6_BITS(b);
            if (!ProcessCodepoint(parser))
            {
                return JSON_Failure;
            }
        }
        else
        {
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        break;

    case DECODER_PROCESSED_1_OF_3:
        if (IS_UTF8_CONTINUATION_BYTE(b))
        {
            /* UTF-8 3-byte sequences that are overlong encodings or encode
               surrogate codepoints can be detected after 2 bytes. */
            parser->decoderBits |= BOTTOM_6_BITS(b) << 6;
            if ((parser->decoderBits < FIRST_3_BYTE_UTF8_CODEPOINT) || IS_SURROGATE(parser->decoderBits))
            {
                SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
                return JSON_Failure;
            }
            parser->decoderState = DECODER_PROCESSED_2_OF_3;
        }
        else
        {
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        break;

    case DECODER_PROCESSED_2_OF_3:
        if (IS_UTF8_CONTINUATION_BYTE(b))
        {
            parser->decoderBits |= BOTTOM_6_BITS(b);
            if (!ProcessCodepoint(parser))
            {
                return JSON_Failure;
            }
        }
        else
        {
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        break;

    case DECODER_PROCESSED_1_OF_4:
        if (IS_UTF8_CONTINUATION_BYTE(b))
        {
            /* UTF-8 4-byte sequences that are overlong encodings or encode
               out-of-range codepoints can be detected after 2 bytes. */
            parser->decoderBits |= BOTTOM_6_BITS(b) << 12;
            if (parser->decoderBits < FIRST_4_BYTE_UTF8_CODEPOINT || parser->decoderBits > MAX_CODEPOINT)
            {
                SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
                return JSON_Failure;
            }
            parser->decoderState = DECODER_PROCESSED_2_OF_4;
        }
        else
        {
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        break;

    case DECODER_PROCESSED_2_OF_4:
        if (IS_UTF8_CONTINUATION_BYTE(b))
        {
            parser->decoderBits |= BOTTOM_6_BITS(b) << 6;
            parser->decoderState = DECODER_PROCESSED_3_OF_4;
        }
        else
        {
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        break;

    case DECODER_PROCESSED_3_OF_4:
        if (IS_UTF8_CONTINUATION_BYTE(b))
        {
            parser->decoderBits |= BOTTOM_6_BITS(b);
            if (!ProcessCodepoint(parser))
            {
                return JSON_Failure;
            }
        }
        else
        {
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        break;
    }

    return JSON_Success;
}

static JSON_Status ProcessUTF16LEByte(JSON_Parser parser, unsigned char b)
{
    /* When the input encoding is UTF-16, the decoded codepoint's bits are
       recorded in the bottom 2 bytes of decoderBits as they are decoded. If
       those 2 bytes form a leading surrogate, the decoder treats the
       surrogate pair as a single 4-byte sequence, shifts the leading
       surrogate into the high 2 bytes of decoderBits, and decodes the
       trailing surrogate's bits in the bottom 2 bytes of decoderBits. */

    switch (parser->decoderState)
    {
    case DECODER_PROCESSED_0_OF_1:
        parser->decoderBits = b;
        parser->decoderState = DECODER_PROCESSED_1_OF_2;
        break;

    case DECODER_PROCESSED_1_OF_2:
        parser->decoderBits |= b << 8;
        if (IS_TRAILING_SURROGATE(parser->decoderBits))
        {
            /* A trailing surrogate cannot appear on its own. */
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        if (IS_LEADING_SURROGATE(parser->decoderBits))
        {
            /* A leading surrogate implies a 4-byte surrogate pair. */
            parser->decoderBits <<= 16;
            parser->decoderState = DECODER_PROCESSED_2_OF_4;
        }
        else if (!ProcessCodepoint(parser))
        {
            return JSON_Failure;
        }
        break;

    case DECODER_PROCESSED_2_OF_4:
        parser->decoderBits |= b;
        parser->decoderState = DECODER_PROCESSED_3_OF_4;
        break;

    case DECODER_PROCESSED_3_OF_4:
        parser->decoderBits |= b << 8;
        if (!IS_TRAILING_SURROGATE(parser->decoderBits & 0xFFFF))
        {
            /* A leading surrogate must be followed by a trailing one. */
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        parser->decoderBits = CODEPOINT_FROM_SURROGATES(parser->decoderBits);
        if (!ProcessCodepoint(parser))
        {
            return JSON_Failure;
        }
        break;
    }

    return JSON_Success;
}

static JSON_Status ProcessUTF16BEByte(JSON_Parser parser, unsigned char b)
{
    /* When the input encoding is UTF-16, the decoded codepoint's bits are
       recorded in the bottom 2 bytes of decoderBits as they are decoded. If
       those 2 bytes form a leading surrogate, the decoder treats the
       surrogate pair as a single 4-byte sequence, shifts the leading
       surrogate into the high 2 bytes of decoderBits, and decodes the
       trailing surrogate's bits in the bottom 2 bytes of decoderBits. */

    switch (parser->decoderState)
    {
    case DECODER_PROCESSED_0_OF_1:
        parser->decoderBits = b << 8;
        if (IS_TRAILING_SURROGATE(parser->decoderBits))
        {
            /* A trailing surrogate cannot appear on its own. */
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        parser->decoderState = DECODER_PROCESSED_1_OF_2;
        break;

    case DECODER_PROCESSED_1_OF_2:
        parser->decoderBits |= b;
        if (IS_LEADING_SURROGATE(parser->decoderBits))
        {
            /* A leading surrogate implies a 4-byte surrogate pair. */
            parser->decoderBits <<= 16;
            parser->decoderState = DECODER_PROCESSED_2_OF_4;
        }
        else if (!ProcessCodepoint(parser))
        {
            return JSON_Failure;
        }
        break;

    case DECODER_PROCESSED_2_OF_4:
        parser->decoderBits |= b << 8;
        if (!IS_TRAILING_SURROGATE(parser->decoderBits & 0xFFFF))
        {
            /* A leading surrogate must be followed by a trailing one. */
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        parser->decoderState = DECODER_PROCESSED_3_OF_4;
        break;

    case DECODER_PROCESSED_3_OF_4:
        parser->decoderBits |= b;
        parser->decoderBits = CODEPOINT_FROM_SURROGATES(parser->decoderBits);
        if (!ProcessCodepoint(parser))
        {
            return JSON_Failure;
        }
        break;
    }

    return JSON_Success;
}

static JSON_Status ProcessUTF32LEByte(JSON_Parser parser, unsigned char b)
{
    /* When the input encoding is UTF-32, the decoded codepoint's bits are
       recorded in decoderBits as they are decoded. */

    switch (parser->decoderState)
    {
    case DECODER_PROCESSED_0_OF_1:
        parser->decoderBits = b;
        parser->decoderState = DECODER_PROCESSED_1_OF_4;
        break;

    case DECODER_PROCESSED_1_OF_4:
        parser->decoderBits |= b << 8;
        parser->decoderState = DECODER_PROCESSED_2_OF_4;
        break;

    case DECODER_PROCESSED_2_OF_4:
        /* All UTF-32LE sequences that encode surrogate codepoints can be
           detected after 3 bytes, since the fourth byte is assumed to be 0.
           Additionally, some UTF-32LE sequences that encode out-of-range
           codepoints can be detected after 3 bytes. */
        parser->decoderBits |= b << 16;
        if (IS_SURROGATE(parser->decoderBits) || parser->decoderBits > MAX_CODEPOINT)
        {
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        parser->decoderState = DECODER_PROCESSED_3_OF_4;
        break;

    case DECODER_PROCESSED_3_OF_4:
        /* UTF-32LE sequences that encode out-of-range codepoints can be
           detected after 4 bytes. */
        parser->decoderBits |= b << 24;
        if (parser->decoderBits > MAX_CODEPOINT)
        {
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        if (!ProcessCodepoint(parser))
        {
            return JSON_Failure;
        }
        break;
    }

    return JSON_Success;
}

static JSON_Status ProcessUTF32BEByte(JSON_Parser parser, unsigned char b)
{
    /* When the input encoding is UTF-32, the decoded codepoint's bits are
       recorded in decoderBits as they are decoded. */

    switch (parser->decoderState)
    {
    case DECODER_PROCESSED_0_OF_1:
        /* Some UTF-32BE sequences that encode out-of-range codepoints can be
           detected from just the first byte, which SHOULD always be 0. */
        parser->decoderBits = b << 24;
        if (parser->decoderBits > MAX_CODEPOINT)
        {
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        parser->decoderState = DECODER_PROCESSED_1_OF_4;
        break;

    case DECODER_PROCESSED_1_OF_4:
        /* All UTF-32BE sequences that encode out-of-range codepoints can be
           detected after 2 bytes. */
        parser->decoderBits |= b << 16;
        if (parser->decoderBits > MAX_CODEPOINT)
        {
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        parser->decoderState = DECODER_PROCESSED_2_OF_4;
        break;

    case DECODER_PROCESSED_2_OF_4:
        /* UTF-32BE sequences that encode surrogate codepoints can be
           detected after 3 bytes. */
        parser->decoderBits |= b << 8;
        if (IS_SURROGATE(parser->decoderBits))
        {
            SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
            return JSON_Failure;
        }
        parser->decoderState = DECODER_PROCESSED_3_OF_4;
        break;

    case DECODER_PROCESSED_3_OF_4:
        parser->decoderBits |= b;
        if (!ProcessCodepoint(parser))
        {
            return JSON_Failure;
        }
        break;
    }

    return JSON_Success;
}

typedef JSON_Status (*ByteDecoder)(JSON_Parser parser, unsigned char b);

/* This array must match the order and number of the JSON_Encoding enum. */
static const ByteDecoder byteDecoders[] =
{
    /* JSON_UnknownEncoding */ &ProcessUnknownByte,
    /* JSON_UTF8 */            &ProcessUTF8Byte,
    /* JSON_UTF16LE */         &ProcessUTF16LEByte,
    /* JSON_UTF16BE */         &ProcessUTF16BEByte,
    /* JSON_UTF32LE */         &ProcessUTF32LEByte,
    /* JSON_UTF32BE */         &ProcessUTF32BEByte
};

JSON_Status ProcessInputBytes(JSON_Parser parser, const unsigned char* pBytes, size_t length)
{
    /* Note that if length is 0, pBytes is allowed to be NULL. */
    size_t i;
    for (i = 0; i < length; i++)
    {
        if (!byteDecoders[parser->inputEncoding](parser, pBytes[i]))
        {
            return JSON_Failure;
        }
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
    if (parser->decoderState != DECODER_PROCESSED_0_OF_1 &&
        parser->inputEncoding == JSON_UnknownEncoding)
    {
        unsigned char bytes[3];
        size_t length = 0;
        bytes[0] = parser->decoderBits >> 24;
        bytes[1] = parser->decoderBits >> 16;
        bytes[2] = parser->decoderBits >> 8;
        switch (parser->decoderState)
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
        parser->decoderState = DECODER_PROCESSED_0_OF_1;
        parser->decoderBits = 0;
        if (!ProcessInputBytes(parser, bytes, length))
        {
            return JSON_Failure;
        }
    }

    /* The decoder should be idle when parsing finishes. */
    if (parser->decoderState != DECODER_PROCESSED_0_OF_1)
    {
        SetErrorAtCodepoint(parser, JSON_Error_InvalidEncodingSequence);
        return JSON_Failure;
    }
    return JSON_Success;
}

/* Library API functions. */

JSON_Parser JSON_CALL JSON_CreateParser(const JSON_MemorySuite* pMemorySuite)
{
    JSON_Parser parser;
    JSON_MemorySuite memorySuite;
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
    parser = (JSON_Parser)memorySuite.malloc(NULL, sizeof(struct JSON_ParserData));
    if (!parser)
    {
        return NULL;
    }
    parser->memorySuite = memorySuite;
    ResetParserData(parser, 0/* isInitialized */);
    return parser;
}

JSON_Status JSON_CALL JSON_FreeParser(JSON_Parser parser)
{
    if (!parser || (parser->parserStatus & PARSER_IN_CALLBACK))
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
        PopObject(parser);
    }
    parser->parserStatus |= PARSER_IN_CALLBACK;
    parser->memorySuite.free(NULL, parser);
    return JSON_Success;
}

JSON_Status JSON_CALL JSON_ResetParser(JSON_Parser parser)
{
    if (!parser || (parser->parserStatus & PARSER_IN_CALLBACK))
    {
        return JSON_Failure;
    }
    ResetParserData(parser, 1/* isInitialized */);
    return JSON_Success;
}

JSON_Error JSON_CALL JSON_GetError(JSON_Parser parser)
{
    return parser ? (JSON_Error)parser->error : JSON_Error_None;
}

void JSON_CALL JSON_GetErrorLocation(JSON_Parser parser, JSON_Location* pLocation)
{
    if (pLocation)
    {
        if (!parser || (parser->error == JSON_Error_None))
        {
            pLocation->byte = 0;
            pLocation->line = 0;
            pLocation->column = 0;
        }
        else
        {
            *pLocation = parser->errorLocation;
        }
    }
}

size_t JSON_CALL JSON_GetErrorLocationByte(JSON_Parser parser)
{
    return parser ? parser->errorLocation.byte : 0;
}

size_t JSON_CALL JSON_GetErrorLocationLine(JSON_Parser parser)
{
    return parser ? parser->errorLocation.line : 0;
}

size_t JSON_CALL JSON_GetErrorLocationColumn(JSON_Parser parser)
{
    return parser ? parser->errorLocation.column : 0;
}

/* This array must match the order and number of the JSON_Error enum. */
static const char* errorStrings[] =
{
    /* JSON_Error_None */                            "no error",
    /* JSON_Error_OutOfMemory */                     "the parser could not allocate enough memory",
    /* JSON_Error_AbortedByHandler */                "parsing was aborted by a handler",
    /* JSON_Error_BOMNotAllowed */                   "the input begins with a byte-order mark (BOM), which is not allowed by RFC 4627",
    /* JSON_Error_IncompleteInput */                 "the input is incomplete",
    /* JSON_Error_InvalidEncodingSequence */         "the input contains a byte or sequence of bytes that is not valid for the input encoding",
    /* JSON_Error_UnknownToken */                    "the input contains an unknown token",
    /* JSON_Error_UnexpectedToken */                 "the input contains an unexpected token",
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

void* JSON_CALL JSON_GetUserData(JSON_Parser parser)
{
    return parser ? parser->userData : NULL;
}

JSON_Status JSON_CALL JSON_SetUserData(JSON_Parser parser, void* userData)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->userData = userData;
    return JSON_Success;
}

JSON_NullHandler JSON_CALL JSON_GetNullHandler(JSON_Parser parser)
{
    return parser ? parser->nullHandler : NULL;
}

JSON_Status JSON_CALL JSON_SetNullHandler(JSON_Parser parser, JSON_NullHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->nullHandler = handler;
    return JSON_Success;
}

JSON_BooleanHandler JSON_CALL JSON_GetBooleanHandler(JSON_Parser parser)
{
    return parser ? parser->booleanHandler : NULL;
}

JSON_Status JSON_CALL JSON_SetBooleanHandler(JSON_Parser parser, JSON_BooleanHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->booleanHandler = handler;
    return JSON_Success;
}

JSON_StringHandler JSON_CALL JSON_GetStringHandler(JSON_Parser parser)
{
    return parser ? parser->stringHandler : NULL;
}

JSON_Status JSON_CALL JSON_SetStringHandler(JSON_Parser parser, JSON_StringHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->stringHandler = handler;
    return JSON_Success;
}

JSON_NumberHandler JSON_CALL JSON_GetNumberHandler(JSON_Parser parser)
{
    return parser ? parser->numberHandler : NULL;
}

JSON_Status JSON_CALL JSON_SetNumberHandler(JSON_Parser parser, JSON_NumberHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->numberHandler = handler;
    return JSON_Success;
}

JSON_RawNumberHandler JSON_CALL JSON_GetRawNumberHandler(JSON_Parser parser)
{
    return parser ? parser->rawNumberHandler : NULL;
}

JSON_Status JSON_CALL JSON_SetRawNumberHandler(JSON_Parser parser, JSON_RawNumberHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->rawNumberHandler = handler;
    return JSON_Success;
}

JSON_StartObjectHandler JSON_CALL JSON_GetStartObjectHandler(JSON_Parser parser)
{
    return parser ? parser->startObjectHandler : NULL;
}

JSON_Status JSON_CALL JSON_SetStartObjectHandler(JSON_Parser parser, JSON_StartObjectHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->startObjectHandler = handler;
    return JSON_Success;
}

JSON_EndObjectHandler JSON_CALL JSON_GetEndObjectHandler(JSON_Parser parser)
{
    return parser ? parser->endObjectHandler : NULL;
}

JSON_Status JSON_CALL JSON_SetEndObjectHandler(JSON_Parser parser, JSON_EndObjectHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->endObjectHandler = handler;
    return JSON_Success;
}

JSON_ObjectMemberHandler JSON_CALL JSON_GetObjectMemberHandler(JSON_Parser parser)
{
    return parser ? parser->objectMemberHandler : NULL;
}

JSON_Status JSON_CALL JSON_SetObjectMemberHandler(JSON_Parser parser, JSON_ObjectMemberHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->objectMemberHandler = handler;
    return JSON_Success;
}

JSON_StartArrayHandler JSON_CALL JSON_GetStartArrayHandler(JSON_Parser parser)
{
    return parser ? parser->startArrayHandler : NULL;
}

JSON_Status JSON_CALL JSON_SetStartArrayHandler(JSON_Parser parser, JSON_StartArrayHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->startArrayHandler = handler;
    return JSON_Success;
}

JSON_EndArrayHandler JSON_CALL JSON_GetEndArrayHandler(JSON_Parser parser)
{
    return parser ? parser->endArrayHandler : NULL;
}

JSON_Status JSON_CALL JSON_SetEndArrayHandler(JSON_Parser parser, JSON_EndArrayHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->endArrayHandler = handler;
    return JSON_Success;
}

JSON_ArrayItemHandler JSON_CALL JSON_GetArrayItemHandler(JSON_Parser parser)
{
    return parser ? parser->arrayItemHandler : NULL;
}

JSON_Status JSON_CALL JSON_SetArrayItemHandler(JSON_Parser parser, JSON_ArrayItemHandler handler)
{
    if (!parser)
    {
        return JSON_Failure;
    }
    parser->arrayItemHandler = handler;
    return JSON_Success;
}

JSON_Boolean JSON_CALL JSON_StartedParsing(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_STARTED)) ? JSON_True : JSON_False;
}

JSON_Boolean JSON_CALL JSON_FinishedParsing(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_FINISHED)) ? JSON_True : JSON_False;
}

JSON_Encoding JSON_CALL JSON_GetInputEncoding(JSON_Parser parser)
{
    return parser ? (JSON_Encoding)parser->inputEncoding : JSON_UnknownEncoding;
}

JSON_Status JSON_CALL JSON_SetInputEncoding(JSON_Parser parser, JSON_Encoding encoding)
{
    if (!parser || (parser->parserStatus & PARSER_STARTED) || encoding < JSON_UnknownEncoding || encoding > JSON_UTF32BE)
    {
        return JSON_Failure;
    }
    parser->inputEncoding = (unsigned char)encoding;
    return JSON_Success;
}

JSON_Encoding JSON_CALL JSON_GetOutputEncoding(JSON_Parser parser)
{
    return parser ? (JSON_Encoding)parser->outputEncoding : JSON_UTF8;
}

JSON_Status JSON_CALL JSON_SetOutputEncoding(JSON_Parser parser, JSON_Encoding encoding)
{
    if (!parser || (parser->parserStatus & PARSER_STARTED) || encoding <= JSON_UnknownEncoding || encoding > JSON_UTF32BE)
    {
        return JSON_Failure;
    }
    parser->outputEncoding = (unsigned char)encoding;
    return JSON_Success;
}

size_t JSON_CALL JSON_GetMaxOutputStringLength(JSON_Parser parser)
{
    return parser ? parser->maxOutputStringLength : (size_t)-1;
}

JSON_Status JSON_CALL JSON_SetMaxOutputStringLength(JSON_Parser parser, size_t maxLength)
{
    if (!parser || (parser->parserStatus & PARSER_STARTED))
    {
        return JSON_Failure;
    }
    parser->maxOutputStringLength = maxLength;
    return JSON_Success;
}

JSON_Boolean JSON_CALL JSON_GetAllowBOM(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_ALLOW_BOM)) ? JSON_True : JSON_False;
}

JSON_Status JSON_CALL JSON_SetAllowBOM(JSON_Parser parser, JSON_Boolean allowBOM)
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

JSON_Boolean JSON_CALL JSON_GetAllowTrailingCommas(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_ALLOW_TRAILING_COMMAS)) ? JSON_True : JSON_False;
}

JSON_Status JSON_CALL JSON_SetAllowTrailingCommas(JSON_Parser parser, JSON_Boolean allowTrailingCommas)
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

JSON_Boolean JSON_CALL JSON_GetAllowNaNAndInfinity(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_ALLOW_NAN_AND_INFINITY)) ? JSON_True : JSON_False;
}

JSON_Status JSON_CALL JSON_SetAllowNaNAndInfinity(JSON_Parser parser, JSON_Boolean allowNaNAndInfinity)
{
    if (!parser || (parser->parserStatus & PARSER_STARTED))
    {
        return JSON_Failure;
    }
    if (allowNaNAndInfinity)
    {
        parser->parserStatus |= PARSER_ALLOW_NAN_AND_INFINITY;
    }
    else
    {
        parser->parserStatus &= ~PARSER_ALLOW_NAN_AND_INFINITY;
    }
    return JSON_Success;
}

JSON_Boolean JSON_CALL JSON_GetTrackObjectMembers(JSON_Parser parser)
{
    return (parser && (parser->parserStatus & PARSER_TRACK_OBJECT_MEMBERS)) ? JSON_True : JSON_False;
}

JSON_Status JSON_CALL JSON_SetTrackObjectMembers(JSON_Parser parser, JSON_Boolean trackObjectMembers)
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

JSON_Status JSON_CALL JSON_Parse(JSON_Parser parser, const char* pBytes, size_t length, JSON_Boolean isFinal)
{
    JSON_Status status = JSON_Failure;
    if (parser && !(parser->parserStatus & (PARSER_FINISHED | PARSER_IN_CALLBACK)))
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
