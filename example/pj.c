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

/* pj (short for "print JSON") is a simple demonstration of the JSON_Parser
 * and JSON_Writer APIs. It parses JSON input from stdin or a specified file
 * and rewrites it to stdout, either prettified (the default) or compacted.
 * Refer to the usage message for more options.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "jsonsax.h"

#define OPTION_HELP                    "--help"
#define OPTION_COMPACT                 "--compact"
#define OPTION_UTF8                    "--utf-8"
#define OPTION_UTF16LE                 "--utf-16le"
#define OPTION_UTF16BE                 "--utf-16be"
#define OPTION_UTF32LE                 "--utf-32le"
#define OPTION_UTF32BE                 "--utf-32be"
#define OPTION_CRLF                    "--crlf"
#define OPTION_ALLOW_BOM               "--allow-bom"
#define OPTION_ALLOW_COMMENTS          "--allow-comments"
#define OPTION_ALLOW_SPECIAL_NUMBERS   "--allow-special-numbers"
#define OPTION_ALLOW_HEX_NUMBERS       "--allow-hex-numbers"
#define OPTION_ALLOW_CONTROL_CHARS     "--allow-control-chars"
#define OPTION_ALLOW_DUPLICATES        "--allow-duplicates"
#define OPTION_REPLACE_INVALID         "--replace-invalid"
#define OPTION_ESCAPE_NON_ASCII        "--escape-non-ascii"

typedef enum tag_OutputMode
{
    Pretty  = 0,
    Compact = 1,
    Usage   = 2
} OutputMode;

typedef struct tag_Context
{
    JSON_Parser parser;
    JSON_Writer writer;
    FILE*       input;
    OutputMode  outputMode;
    int         inEmptyContainer;
} Context;

static void InitContext(Context* pCtx)
{
    pCtx->parser = NULL;
    pCtx->writer = NULL;
    pCtx->input = NULL;
    pCtx->outputMode = Pretty;
    pCtx->inEmptyContainer = 0;
}

static void UninitContext(Context* pCtx)
{
    JSON_Parser_Free(pCtx->parser);
    JSON_Writer_Free(pCtx->writer);
    if (pCtx->input)
    {
        fclose(pCtx->input);
    }
}

static JSON_Writer_HandlerResult JSON_CALL OutputHandler(JSON_Writer writer, const char* pBytes, size_t length)
{
    (void)writer; /* unused */
    return fwrite(pBytes, 1, length, stdout) == length ? JSON_Writer_Continue : JSON_Writer_Abort;
}

static int WriteIndent(Context* pCtx)
{
    JSON_Location location;
    JSON_Parser_GetTokenLocation(pCtx->parser, &location);
    return JSON_Writer_WriteNewLine(pCtx->writer) && JSON_Writer_WriteSpace(pCtx->writer, 2 * location.depth);
}

static JSON_Parser_HandlerResult JSON_CALL EncodingDetectedHandler(JSON_Parser parser)
{
    /* This handler is only registered if no output encoding was specified. In that
       case, we just make the output encoding match the input encoding. */
    Context* pCtx = (Context*)JSON_Parser_GetUserData(parser);
    JSON_Writer_SetOutputEncoding(pCtx->writer, JSON_Parser_GetInputEncoding(pCtx->parser));
    return JSON_Parser_Continue;
}

static JSON_Parser_HandlerResult JSON_CALL NullHandler(JSON_Parser parser)
{
    Context* pCtx = (Context*)JSON_Parser_GetUserData(parser);
    pCtx->inEmptyContainer = 0;
    return JSON_Writer_WriteNull(pCtx->writer) ? JSON_Parser_Continue : JSON_Parser_Abort;
}

static JSON_Parser_HandlerResult JSON_CALL BooleanHandler(JSON_Parser parser, JSON_Boolean value)
{
    Context* pCtx = (Context*)JSON_Parser_GetUserData(parser);
    pCtx->inEmptyContainer = 0;
    return JSON_Writer_WriteBoolean(pCtx->writer, value) ? JSON_Parser_Continue : JSON_Parser_Abort;
}

static JSON_Parser_HandlerResult JSON_CALL StringHandler(JSON_Parser parser, char* pValue, size_t length, JSON_StringAttributes attributes)
{
    Context* pCtx = (Context*)JSON_Parser_GetUserData(parser);
    (void)attributes; /* unused */
    pCtx->inEmptyContainer = 0;
    return JSON_Writer_WriteString(pCtx->writer, pValue, length, JSON_UTF8) ? JSON_Parser_Continue : JSON_Parser_Abort;
}

static JSON_Parser_HandlerResult JSON_CALL NumberHandler(JSON_Parser parser, char* pValue, size_t length, JSON_NumberAttributes attributes)
{
    Context* pCtx = (Context*)JSON_Parser_GetUserData(parser);
    (void)attributes; /* unused */
    pCtx->inEmptyContainer = 0;
    return JSON_Writer_WriteNumber(pCtx->writer, pValue, length, JSON_UTF8) ? JSON_Parser_Continue : JSON_Parser_Abort;
}

static JSON_Parser_HandlerResult JSON_CALL SpecialNumberHandler(JSON_Parser parser, JSON_SpecialNumber value)
{
    Context* pCtx = (Context*)JSON_Parser_GetUserData(parser);
    pCtx->inEmptyContainer = 0;
    return JSON_Writer_WriteSpecialNumber(pCtx->writer, value) ? JSON_Parser_Continue : JSON_Parser_Abort;
}

static JSON_Parser_HandlerResult JSON_CALL StartObjectHandler(JSON_Parser parser)
{
    Context* pCtx = (Context*)JSON_Parser_GetUserData(parser);
    pCtx->inEmptyContainer = 1;
    return JSON_Writer_WriteStartObject(pCtx->writer) ? JSON_Parser_Continue : JSON_Parser_Abort;
}

static JSON_Parser_HandlerResult JSON_CALL EndObjectHandler(JSON_Parser parser)
{
    Context* pCtx = (Context*)JSON_Parser_GetUserData(parser);
    if (!pCtx->inEmptyContainer)
    {
        if (pCtx->outputMode == Pretty && !WriteIndent(pCtx))
        {
            return JSON_Parser_Abort;
        }
    }
    pCtx->inEmptyContainer = 0;
    return JSON_Writer_WriteEndObject(pCtx->writer) ? JSON_Parser_Continue : JSON_Parser_Abort;
}

static JSON_Parser_HandlerResult JSON_CALL ObjectMemberHandler(JSON_Parser parser, char* pValue, size_t length, JSON_StringAttributes attributes)
{
    Context* pCtx = (Context*)JSON_Parser_GetUserData(parser);
    (void)attributes; /* unused */
    if (!pCtx->inEmptyContainer)
    {
        if (!JSON_Writer_WriteComma(pCtx->writer))
        {
            return JSON_Parser_Abort;
        }
    }
    pCtx->inEmptyContainer = 0;
    return ((pCtx->outputMode != Pretty || WriteIndent(pCtx)) &&
            JSON_Writer_WriteString(pCtx->writer, pValue, length, JSON_UTF8) &&
            (pCtx->outputMode != Pretty || JSON_Writer_WriteSpace(pCtx->writer, 1)) &&
            JSON_Writer_WriteColon(pCtx->writer) &&
            (pCtx->outputMode != Pretty || JSON_Writer_WriteSpace(pCtx->writer, 1))) ? JSON_Parser_Continue : JSON_Parser_Abort;
}

static JSON_Parser_HandlerResult JSON_CALL StartArrayHandler(JSON_Parser parser)
{
    Context* pCtx = (Context*)JSON_Parser_GetUserData(parser);
    pCtx->inEmptyContainer = 1;
    return JSON_Writer_WriteStartArray(pCtx->writer) ? JSON_Parser_Continue : JSON_Parser_Abort;
}

static JSON_Parser_HandlerResult JSON_CALL EndArrayHandler(JSON_Parser parser)
{
    Context* pCtx = (Context*)JSON_Parser_GetUserData(parser);
    if (!pCtx->inEmptyContainer)
    {
        if (pCtx->outputMode == Pretty && !WriteIndent(pCtx))
        {
            return JSON_Parser_Abort;
        }
    }
    pCtx->inEmptyContainer = 0;
    return JSON_Writer_WriteEndArray(pCtx->writer) ? JSON_Parser_Continue : JSON_Parser_Abort;
}

static JSON_Parser_HandlerResult JSON_CALL ArrayItemHandler(JSON_Parser parser)
{
    Context* pCtx = (Context*)JSON_Parser_GetUserData(parser);
    if (!pCtx->inEmptyContainer)
    {
        if (!JSON_Writer_WriteComma(pCtx->writer))
        {
            return JSON_Parser_Abort;
        }
    }
    pCtx->inEmptyContainer = 0;
    return (pCtx->outputMode != Pretty || WriteIndent(pCtx)) ? JSON_Parser_Continue : JSON_Parser_Abort;
}

typedef struct tag_Option
{
    const char* name;
    const char* description;
} Option;

static void PrintUsage(FILE* f)
{
    static const Option options[] =
    {
        { OPTION_COMPACT,               "Output without any whitespace" },
        { OPTION_UTF8,                  "Output UTF-8" },
        { OPTION_UTF16LE,               "Output UTF-16LE" },
        { OPTION_UTF16BE,               "Output UTF-16BE" },
        { OPTION_UTF32LE,               "Output UTF-32LE" },
        { OPTION_UTF32BE,               "Output UTF-32BE" },
        { OPTION_CRLF,                  "Output CRLF for newlines (LF is the default)" },
        { OPTION_ALLOW_BOM,             "Allow the input to be prefixed by a UTF BOM" },
        { OPTION_ALLOW_COMMENTS,        "Allow Javascript-style comments (they will be stripped)" },
        { OPTION_ALLOW_SPECIAL_NUMBERS, "Allow NaN, Infinity, and -Infinity literals" },
        { OPTION_ALLOW_HEX_NUMBERS,     "Allow Javascript-style positive hexadecimal integers" },
        { OPTION_ALLOW_CONTROL_CHARS,   "Allow ASCII control characters (U+0000 - U+001F) in strings" },
        { OPTION_ALLOW_DUPLICATES,      "Allow objects to contain duplicate members" },
        { OPTION_REPLACE_INVALID,       "Replace invalid encoding sequences with U+FFFD" },
        { OPTION_ESCAPE_NON_ASCII,      "Escape all non-ASCII characters in the output" },
        { OPTION_HELP,                  "Print this message" }
    };

    int i;
    fputs("Usage: pj [OPTIONS] [FILE]\n"
          "Options:\n", f);
    for (i = 0; i < sizeof(options)/sizeof(options[0]); i++)
    {
        fprintf(f, "  %-25s %s\n", options[i].name, options[i].description);
    }
}

static int Configure(Context* pCtx, int argc, char* argv[])
{
    int i;
    pCtx->input = stdin;
    JSON_Parser_SetTrackObjectMembers(pCtx->parser, JSON_True);
    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], OPTION_HELP))
        {
            pCtx->outputMode = Usage;
            return 1;
        }
        else if (!strcmp(argv[i], OPTION_COMPACT))
        {
            pCtx->outputMode = Compact;
        }
        else if (!strcmp(argv[i], OPTION_ALLOW_BOM))
        {
            JSON_Parser_SetAllowBOM(pCtx->parser, JSON_True);
        }
        else if (!strcmp(argv[i], OPTION_ALLOW_COMMENTS))
        {
            JSON_Parser_SetAllowComments(pCtx->parser, JSON_True);
        }
        else if (!strcmp(argv[i], OPTION_ALLOW_SPECIAL_NUMBERS))
        {
            JSON_Parser_SetAllowSpecialNumbers(pCtx->parser, JSON_True);
        }
        else if (!strcmp(argv[i], OPTION_ALLOW_HEX_NUMBERS))
        {
            JSON_Parser_SetAllowHexNumbers(pCtx->parser, JSON_True);
        }
        else if (!strcmp(argv[i], OPTION_ALLOW_CONTROL_CHARS))
        {
            JSON_Parser_SetAllowUnescapedControlCharacters(pCtx->parser, JSON_True);
        }
        else if (!strcmp(argv[i], OPTION_REPLACE_INVALID))
        {
            JSON_Parser_SetReplaceInvalidEncodingSequences(pCtx->parser, JSON_True);
        }
        else if (!strcmp(argv[i], OPTION_ALLOW_DUPLICATES))
        {
            JSON_Parser_SetTrackObjectMembers(pCtx->parser, JSON_False);
        }
        else if (!strcmp(argv[i], OPTION_UTF8))
        {
            JSON_Writer_SetOutputEncoding(pCtx->writer, JSON_UTF8);
        }
        else if (!strcmp(argv[i], OPTION_UTF16LE))
        {
            JSON_Writer_SetOutputEncoding(pCtx->writer, JSON_UTF16LE);
        }
        else if (!strcmp(argv[i], OPTION_UTF16BE))
        {
            JSON_Writer_SetOutputEncoding(pCtx->writer, JSON_UTF16BE);
        }
        else if (!strcmp(argv[i], OPTION_UTF32LE))
        {
            JSON_Writer_SetOutputEncoding(pCtx->writer, JSON_UTF32LE);
        }
        else if (!strcmp(argv[i], OPTION_UTF32BE))
        {
            JSON_Writer_SetOutputEncoding(pCtx->writer, JSON_UTF32BE);
        }
        else if (!strcmp(argv[i], OPTION_CRLF))
        {
            JSON_Writer_SetUseCRLF(pCtx->writer, JSON_True);
        }
        else if (!strcmp(argv[i], OPTION_ESCAPE_NON_ASCII))
        {
            JSON_Writer_SetEscapeAllNonASCIICharacters(pCtx->writer, JSON_True);
        }
        else if (i != argc - 1)
        {
            PrintUsage(stderr);
            return 0;
        }
        else
        {
            pCtx->input = fopen(argv[i], "rb");
            if (!pCtx->input)
            {
                fprintf(stderr, "Error: could not open file \"%s\".\n", argv[i]);
                return 0;
            }
        }
    }
    if (JSON_Parser_GetInputEncoding(pCtx->parser) == JSON_UnknownEncoding)
    {
        JSON_Parser_SetEncodingDetectedHandler(pCtx->parser, &EncodingDetectedHandler);
    }
    JSON_Parser_SetNullHandler(pCtx->parser, &NullHandler);
    JSON_Parser_SetBooleanHandler(pCtx->parser, &BooleanHandler);
    JSON_Parser_SetStringHandler(pCtx->parser, &StringHandler);
    JSON_Parser_SetNumberHandler(pCtx->parser, &NumberHandler);
    JSON_Parser_SetSpecialNumberHandler(pCtx->parser, &SpecialNumberHandler);
    JSON_Parser_SetStartObjectHandler(pCtx->parser, &StartObjectHandler);
    JSON_Parser_SetEndObjectHandler(pCtx->parser, &EndObjectHandler);
    JSON_Parser_SetObjectMemberHandler(pCtx->parser, &ObjectMemberHandler);
    JSON_Parser_SetStartArrayHandler(pCtx->parser, &StartArrayHandler);
    JSON_Parser_SetEndArrayHandler(pCtx->parser, &EndArrayHandler);
    JSON_Parser_SetArrayItemHandler(pCtx->parser, &ArrayItemHandler);
    JSON_Parser_SetUserData(pCtx->parser, pCtx);
    JSON_Writer_SetOutputHandler(pCtx->writer, &OutputHandler);
    JSON_Writer_SetUserData(pCtx->writer, pCtx);
    return 1;
}

static void LogError(Context* pCtx)
{
    fflush(stdout); /* avoid interleaving stdout and stderr */
    if (JSON_Parser_GetError(pCtx->parser) != JSON_Error_AbortedByHandler)
    {
        JSON_Error error = JSON_Parser_GetError(pCtx->parser);
        JSON_Location errorLocation = { 0, 0, 0 };
        (void)JSON_Parser_GetErrorLocation(pCtx->parser, &errorLocation);
        fprintf(stderr, "Error: invalid JSON at line %d, column %d (input byte %d) - %s.\n",
                (int)errorLocation.line + 1,
                (int)errorLocation.column + 1,
                (int)errorLocation.byte,
                JSON_ErrorString(error));
    }
    else if (JSON_Writer_GetError(pCtx->writer) != JSON_Error_AbortedByHandler)
    {
        fprintf(stderr, "Error: could not write output - %s.\n", JSON_ErrorString(JSON_Writer_GetError(pCtx->writer)));
    }
    else
    {
        fputs("Error: could not write output.\n", stderr);
    }
}

static int Process(Context* pCtx)
{
    if (pCtx->outputMode == Usage)
    {
        PrintUsage(stdout);
    }
    else
    {
        while (!feof(pCtx->input))
        {
            char chunk[1024];
            size_t length = fread(chunk, 1, sizeof(chunk), pCtx->input);
            if (!length && !feof(pCtx->input))
            {
                fflush(stdout); /* avoid interleaving stdout and stderr */
                fputs("Error: could not read input.\n", stderr);
                return 0;
            }
            if (!JSON_Parser_Parse(pCtx->parser, chunk, length, JSON_False))
            {
                LogError(pCtx);
                return 0;
            }
        }
        if (!JSON_Parser_Parse(pCtx->parser, NULL, 0, JSON_True) || /* finish parsing */
            (pCtx->outputMode == Pretty && !JSON_Writer_WriteNewLine(pCtx->writer)))
        {
            LogError(pCtx);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char* argv[])
{
    int status = 1;
    Context ctx;

    InitContext(&ctx);

    ctx.parser = JSON_Parser_Create(NULL);
    ctx.writer = JSON_Writer_Create(NULL);
    if (!ctx.parser || !ctx.writer)
    {
        fputs("Error: could not allocate memory.\n", stderr);
    }
    else if (Configure(&ctx, argc, argv) && Process(&ctx))
    {
        status = 0; /* success! */
    }

    UninitContext(&ctx);
    return status;
}
