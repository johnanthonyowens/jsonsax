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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "jsonsax.h"

static int s_failureCount = 0;
static int s_failMalloc = 0;
static int s_failRealloc = 0;
static int s_failHandler = 0;
static int s_misbehaveInHandler = 0;
static size_t s_blocksAllocated = 0;
static size_t s_bytesAllocated = 0;

#define HANDLER_STRING(p) ((p) ? "non-NULL" : "NULL")

/* The length and content of this array MUST correspond exactly with the
   JSON_Error_XXX enumeration values defined in jsonsax.h. */
static const char* errorNames[] =
{
    "",
    "OutOfMemory",
    "AbortedByHandler",
    "BOMNotAllowed",
    "InvalidEncodingSequence",
    "UnknownToken",
    "UnexpectedToken",
    "IncompleteToken",
    "ExpectedMoreTokens",
    "UnescapedControlCharacter",
    "InvalidEscapeSequence",
    "UnpairedSurrogateEscapeSequence",
    "TooLongString",
    "InvalidNumber",
    "TooLongNumber",
    "DuplicateObjectMember",
    "StoppedAfterEmbeddedDocument"
};

static void* JSON_CALL ReallocHandler(void* caller, void* ptr, size_t size)
{
    size_t* pBlock = NULL;
    (void)caller; /* unused */
    if ((!ptr && !s_failMalloc) || (ptr && !s_failRealloc))
    {
        size_t newBlockSize = sizeof(size_t) + size;
        size_t oldBlockSize;
        pBlock = (size_t*)ptr;
        if (pBlock)
        {
            pBlock--; /* actual block begins before client's pointer */
            oldBlockSize = *pBlock;
        }
        else
        {
            oldBlockSize = 0;
        }
        pBlock = (size_t*)realloc(pBlock, newBlockSize);
        if (pBlock)
        {
            if (!oldBlockSize)
            {
                s_blocksAllocated++;
            }
            s_bytesAllocated += newBlockSize - oldBlockSize;
            *pBlock = newBlockSize;
            pBlock++; /* return address to memory after block size */
        }
    }
    return pBlock;
}

static void JSON_CALL FreeHandler(void* caller, void* ptr)
{
    (void)caller; /* unused */
    if (ptr)
    {
        size_t* pBlock = (size_t*)ptr;
        pBlock--; /* actual block begins before client's pointer */
        s_blocksAllocated--;
        s_bytesAllocated -= *pBlock;
        free(pBlock);
    }
}

static char s_outputBuffer[4096]; /* big enough for all unit tests */
int s_outputLength = 0;

static void OutputFormatted(const char* pFormat, ...)
{
    va_list args;
    int length;
    va_start(args, pFormat);
    length = vsprintf(&s_outputBuffer[s_outputLength], pFormat, args);
    va_end(args);
    s_outputLength += length;
    s_outputBuffer[s_outputLength] = 0;
}

static void OutputCharacter(char c)
{
    s_outputBuffer[s_outputLength] = c;
    s_outputLength++;
    s_outputBuffer[s_outputLength] = 0;
}

static void OutputSeparator(void)
{
    if (s_outputLength && s_outputBuffer[s_outputLength] != ' ')
    {
        OutputCharacter(' ');
    }
}

static void OutputByteCode(unsigned char b)
{
    static const char hexDigits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
    OutputCharacter(hexDigits[b >> 4]);
    OutputCharacter(hexDigits[b & 0xF]);
}

static int IsSimpleCharacter(unsigned char b)
{
    return b > 0x20 && b < 0x7F && b != '_';
}

static void OutputByteSequence(const unsigned char* pBytes, size_t length, JSON_Encoding encoding)
{
    size_t i;
    switch (encoding)
    {
    case JSON_UTF8:
        for (i = 0; i < length; i++)
        {
            if (IsSimpleCharacter(pBytes[i]))
            {
                OutputCharacter((char)pBytes[i]);
            }
            else
            {
                OutputCharacter('<');
                OutputByteCode(pBytes[i]);
                OutputCharacter('>');
            }
        }
        break;

    case JSON_UTF16LE:
        for (i = 0; i < length; i += 2)
        {
            if (IsSimpleCharacter(pBytes[i]) &&
                pBytes[i + 1] == 0)
            {
                OutputCharacter((char)pBytes[i]);
                OutputCharacter('_');
            }
            else
            {
                OutputCharacter('<');
                OutputByteCode(pBytes[i]);
                OutputCharacter(' ');
                OutputByteCode(pBytes[i + 1]);
                OutputCharacter('>');
            }
        }
        break;

    case JSON_UTF16BE:
        for (i = 0; i < length; i += 2)
        {
            if (IsSimpleCharacter(pBytes[i + 1]) &&
                pBytes[i] == 0)
            {
                OutputCharacter('_');
                OutputCharacter((char)pBytes[i + 1]);
            }
            else
            {
                OutputCharacter('<');
                OutputByteCode(pBytes[i]);
                OutputCharacter(' ');
                OutputByteCode(pBytes[i + 1]);
                OutputCharacter('>');
            }
        }
        break;

    case JSON_UTF32LE:
        for (i = 0; i < length; i += 4)
        {
            if (IsSimpleCharacter(pBytes[i]) &&
                pBytes[i + 1] == 0 &&
                pBytes[i + 2] == 0 &&
                pBytes[i + 3] == 0)
            {
                OutputCharacter((char)pBytes[i]);
                OutputCharacter('_');
                OutputCharacter('_');
                OutputCharacter('_');
            }
            else
            {
                OutputCharacter('<');
                OutputByteCode(pBytes[i]);
                OutputCharacter(' ');
                OutputByteCode(pBytes[i + 1]);
                OutputCharacter(' ');
                OutputByteCode(pBytes[i + 2]);
                OutputCharacter(' ');
                OutputByteCode(pBytes[i + 3]);
                OutputCharacter('>');
            }
        }
        break;

    case JSON_UTF32BE:
        for (i = 0; i < length; i += 4)
        {
            if (IsSimpleCharacter(pBytes[i + 3]) &&
                pBytes[i] == 0 &&
                pBytes[i + 1] == 0 &&
                pBytes[i + 2] == 0)
            {
                OutputCharacter('_');
                OutputCharacter('_');
                OutputCharacter('_');
                OutputCharacter((char)pBytes[i + 3]);
            }
            else
            {
                OutputCharacter('<');
                OutputByteCode(pBytes[i]);
                OutputCharacter(' ');
                OutputByteCode(pBytes[i + 1]);
                OutputCharacter(' ');
                OutputByteCode(pBytes[i + 2]);
                OutputCharacter(' ');
                OutputByteCode(pBytes[i + 3]);
                OutputCharacter('>');
            }
        }
        break;

    default:
        break;
    }
}

static void OutputStringBytes(const unsigned char* pBytes, size_t length, JSON_StringAttributes attributes, JSON_Encoding encoding)
{
    if (attributes != JSON_SimpleString)
    {
        if (attributes & JSON_ContainsNullCharacter)
        {
            OutputCharacter('z');
        }
        if (attributes & JSON_ContainsControlCharacter)
        {
            OutputCharacter('c');
        }
        if (attributes & JSON_ContainsNonASCIICharacter)
        {
            OutputCharacter('a');
        }
        if (attributes & JSON_ContainsNonBMPCharacter)
        {
            OutputCharacter('b');
        }
        if (attributes & JSON_ContainsReplacedCharacter)
        {
            OutputCharacter('r');
        }
        if (length)
        {
            OutputCharacter(' ');
        }
    }
    OutputByteSequence(pBytes, length, encoding);
}

#ifndef JSON_NO_PARSER

static void OutputNumber(const unsigned char* pValue, size_t length, JSON_NumberAttributes attributes, JSON_Encoding encoding)
{
    if (attributes != JSON_SimpleNumber)
    {
        if (attributes & JSON_IsNegative)
        {
            OutputCharacter('-');
        }
        if (attributes & JSON_IsHex)
        {
            OutputCharacter('x');
        }
        if (attributes & JSON_ContainsDecimalPoint)
        {
            OutputCharacter('.');
        }
        if (attributes & JSON_ContainsExponent)
        {
            OutputCharacter('e');
        }
        if (attributes & JSON_ContainsNegativeExponent)
        {
            OutputCharacter('-');
        }
        OutputCharacter(' ');
    }
    OutputByteSequence((const unsigned char*)pValue, length, encoding);
}

static void OutputLocation(const JSON_Location* pLocation)
{
    OutputFormatted("%d,%d,%d,%d", (int)pLocation->byte, (int)pLocation->line, (int)pLocation->column, (int)pLocation->depth);
}

#endif

static int CheckOutput(const char* pExpectedOutput)
{
    if (strcmp(pExpectedOutput, s_outputBuffer))
    {
        printf("FAILURE: output does not match expected\n"
               "  EXPECTED %s\n"
               "  ACTUAL   %s\n", pExpectedOutput, s_outputBuffer);
        return 0;
    }
    return 1;
}

static void ResetOutput(void)
{
    s_outputLength = 0;
    s_outputBuffer[0] = 0;
}

#ifndef JSON_NO_PARSER

typedef struct tag_ParserState
{
    JSON_Error    error;
    JSON_Location errorLocation;
    JSON_Encoding inputEncoding;
} ParserState;

static void InitParserState(ParserState* pState)
{
    pState->error = JSON_Error_None;
    pState->errorLocation.byte = 0;
    pState->errorLocation.line = 0;
    pState->errorLocation.column = 0;
    pState->errorLocation.depth = 0;
    pState->inputEncoding = JSON_UnknownEncoding;
}

static void GetParserState(JSON_Parser parser, ParserState* pState)
{
    pState->error = JSON_Parser_GetError(parser);
    if (JSON_Parser_GetErrorLocation(parser, &pState->errorLocation) != JSON_Success)
    {
        pState->errorLocation.byte = 0;
        pState->errorLocation.line = 0;
        pState->errorLocation.column = 0;
        pState->errorLocation.depth = 0;
    }
    pState->inputEncoding = JSON_Parser_GetInputEncoding(parser);
}

static int ParserStatesAreIdentical(const ParserState* pState1, const ParserState* pState2)
{
    return (pState1->error == pState2->error &&
            pState1->errorLocation.byte == pState2->errorLocation.byte &&
            pState1->errorLocation.line == pState2->errorLocation.line &&
            pState1->errorLocation.column == pState2->errorLocation.column &&
            pState1->errorLocation.depth == pState2->errorLocation.depth &&
            pState1->inputEncoding == pState2->inputEncoding);
}

static int CheckParserState(JSON_Parser parser, const ParserState* pExpectedState)
{
    int isValid;
    ParserState actualState;
    GetParserState(parser, &actualState);
    isValid = ParserStatesAreIdentical(pExpectedState, &actualState);
    if (!isValid)
    {
        printf("FAILURE: parser state does not match\n"
               "  STATE                                 EXPECTED     ACTUAL\n"
               "  JSON_Parser_GetError()                %8d   %8d\n"
               "  JSON_Parser_GetErrorLocation() byte   %8d   %8d\n"
               "  JSON_Parser_GetErrorLocation() line   %8d   %8d\n"
               "  JSON_Parser_GetErrorLocation() column %8d   %8d\n"
               "  JSON_Parser_GetErrorLocation() depth  %8d   %8d\n"
               "  JSON_Parser_GetInputEncoding()        %8d   %8d\n"
               ,
               (int)pExpectedState->error, (int)actualState.error,
               (int)pExpectedState->errorLocation.byte, (int)actualState.errorLocation.byte,
               (int)pExpectedState->errorLocation.line, (int)actualState.errorLocation.line,
               (int)pExpectedState->errorLocation.column, (int)actualState.errorLocation.column,
               (int)pExpectedState->errorLocation.depth, (int)actualState.errorLocation.depth,
               (int)pExpectedState->inputEncoding, (int)actualState.inputEncoding
            );
    }
    return isValid;
}

typedef struct tag_ParserSettings
{
    void*         userData;
    JSON_Encoding inputEncoding;
    JSON_Encoding stringEncoding;
    JSON_Encoding numberEncoding;
    size_t        maxStringLength;
    size_t        maxNumberLength;
    JSON_Boolean  allowBOM;
    JSON_Boolean  allowComments;
    JSON_Boolean  allowSpecialNumbers;
    JSON_Boolean  allowHexNumbers;
    JSON_Boolean  allowUnescapedControlCharacters;
    JSON_Boolean  replaceInvalidEncodingSequences;
    JSON_Boolean  trackObjectMembers;
    JSON_Boolean  stopAfterEmbeddedDocument;
} ParserSettings;

static void InitParserSettings(ParserSettings* pSettings)
{
    pSettings->userData = NULL;
    pSettings->inputEncoding = JSON_UnknownEncoding;
    pSettings->stringEncoding = JSON_UTF8;
    pSettings->numberEncoding = JSON_UTF8;
    pSettings->maxStringLength = (size_t)-1;
    pSettings->maxNumberLength = (size_t)-1;
    pSettings->allowBOM = JSON_False;
    pSettings->allowComments = JSON_False;
    pSettings->allowSpecialNumbers = JSON_False;
    pSettings->allowHexNumbers = JSON_False;
    pSettings->allowUnescapedControlCharacters = JSON_False;
    pSettings->replaceInvalidEncodingSequences = JSON_False;
    pSettings->trackObjectMembers = JSON_False;
    pSettings->stopAfterEmbeddedDocument = JSON_False;
}

static void GetParserSettings(JSON_Parser parser, ParserSettings* pSettings)
{
    pSettings->userData = JSON_Parser_GetUserData(parser);
    pSettings->inputEncoding = JSON_Parser_GetInputEncoding(parser);
    pSettings->stringEncoding = JSON_Parser_GetStringEncoding(parser);
    pSettings->numberEncoding = JSON_Parser_GetNumberEncoding(parser);
    pSettings->maxStringLength = JSON_Parser_GetMaxStringLength(parser);
    pSettings->maxNumberLength = JSON_Parser_GetMaxNumberLength(parser);
    pSettings->allowBOM = JSON_Parser_GetAllowBOM(parser);
    pSettings->allowComments = JSON_Parser_GetAllowComments(parser);
    pSettings->allowSpecialNumbers = JSON_Parser_GetAllowSpecialNumbers(parser);
    pSettings->allowHexNumbers = JSON_Parser_GetAllowHexNumbers(parser);
    pSettings->allowUnescapedControlCharacters = JSON_Parser_GetAllowUnescapedControlCharacters(parser);
    pSettings->replaceInvalidEncodingSequences = JSON_Parser_GetReplaceInvalidEncodingSequences(parser);
    pSettings->trackObjectMembers = JSON_Parser_GetTrackObjectMembers(parser);
    pSettings->stopAfterEmbeddedDocument = JSON_Parser_GetStopAfterEmbeddedDocument(parser);
}

static int ParserSettingsAreIdentical(const ParserSettings* pSettings1, const ParserSettings* pSettings2)
{
    return (pSettings1->userData == pSettings2->userData &&
            pSettings1->inputEncoding == pSettings2->inputEncoding &&
            pSettings1->stringEncoding == pSettings2->stringEncoding &&
            pSettings1->numberEncoding == pSettings2->numberEncoding &&
            pSettings1->maxStringLength == pSettings2->maxStringLength &&
            pSettings1->maxNumberLength == pSettings2->maxNumberLength &&
            pSettings1->allowBOM == pSettings2->allowBOM &&
            pSettings1->allowComments == pSettings2->allowComments &&
            pSettings1->allowSpecialNumbers == pSettings2->allowSpecialNumbers &&
            pSettings1->allowHexNumbers == pSettings2->allowHexNumbers &&
            pSettings1->allowUnescapedControlCharacters == pSettings2->allowUnescapedControlCharacters &&
            pSettings1->replaceInvalidEncodingSequences == pSettings2->replaceInvalidEncodingSequences &&
            pSettings1->trackObjectMembers == pSettings2->trackObjectMembers &&
            pSettings1->stopAfterEmbeddedDocument == pSettings2->stopAfterEmbeddedDocument);
}

static int CheckParserSettings(JSON_Parser parser, const ParserSettings* pExpectedSettings)
{
    int identical;
    ParserSettings actualSettings;
    GetParserSettings(parser, &actualSettings);
    identical = ParserSettingsAreIdentical(pExpectedSettings, &actualSettings);
    if (!identical)
    {
        printf("FAILURE: parser settings do not match\n"
               "  SETTINGS                                         EXPECTED     ACTUAL\n"
               "  JSON_Parser_GetUserData()                        %8p   %8p\n"
               "  JSON_Parser_GetInputEncoding()                   %8d   %8d\n"
               "  JSON_Parser_GetStringEncoding()                  %8d   %8d\n"
               "  JSON_Parser_GetNumberEncoding()                  %8d   %8d\n"
               "  JSON_Parser_GetMaxStringLength()                 %8d   %8d\n"
               "  JSON_Parser_GetMaxNumberLength()                 %8d   %8d\n"
               ,
               pExpectedSettings->userData, actualSettings.userData,
               (int)pExpectedSettings->inputEncoding, (int)actualSettings.inputEncoding,
               (int)pExpectedSettings->stringEncoding, (int)actualSettings.stringEncoding,
               (int)pExpectedSettings->numberEncoding, (int)actualSettings.numberEncoding,
               (int)pExpectedSettings->maxStringLength, (int)actualSettings.maxStringLength,
               (int)pExpectedSettings->maxNumberLength, (int)actualSettings.maxNumberLength
            );
        printf("  JSON_Parser_GetAllowBOM()                        %8d   %8d\n"
               "  JSON_Parser_GetAllowComments()                   %8d   %8d\n"
               "  JSON_Parser_GetAllowSpecialNumbers()             %8d   %8d\n"
               "  JSON_Parser_GetAllowHexNumbers()                 %8d   %8d\n"
               "  JSON_Parser_GetAllowUnescapedControlCharacters() %8d   %8d\n"
               "  JSON_Parser_GetReplaceInvalidEncodingSequences() %8d   %8d\n"
               "  JSON_Parser_GetTrackObjectMembers()              %8d   %8d\n"
               "  JSON_Parser_GetStopAfterEmbeddedDocument()       %8d   %8d\n"
               ,
               (int)pExpectedSettings->allowBOM, (int)actualSettings.allowBOM,
               (int)pExpectedSettings->allowComments, (int)actualSettings.allowComments,
               (int)pExpectedSettings->allowSpecialNumbers, (int)actualSettings.allowSpecialNumbers,
               (int)pExpectedSettings->allowHexNumbers, (int)actualSettings.allowHexNumbers,
               (int)pExpectedSettings->allowUnescapedControlCharacters, (int)actualSettings.allowUnescapedControlCharacters,
               (int)pExpectedSettings->replaceInvalidEncodingSequences, (int)actualSettings.replaceInvalidEncodingSequences,
               (int)pExpectedSettings->trackObjectMembers, (int)actualSettings.trackObjectMembers,
               (int)pExpectedSettings->stopAfterEmbeddedDocument, (int)actualSettings.stopAfterEmbeddedDocument
            );
    }
    return identical;
}

typedef struct tag_ParserHandlers
{
    JSON_Parser_EncodingDetectedHandler encodingDetectedHandler;
    JSON_Parser_NullHandler             nullHandler;
    JSON_Parser_BooleanHandler          booleanHandler;
    JSON_Parser_StringHandler           stringHandler;
    JSON_Parser_NumberHandler           numberHandler;
    JSON_Parser_SpecialNumberHandler    specialNumberHandler;
    JSON_Parser_StartObjectHandler      startObjectHandler;
    JSON_Parser_EndObjectHandler        endObjectHandler;
    JSON_Parser_ObjectMemberHandler     objectMemberHandler;
    JSON_Parser_StartArrayHandler       startArrayHandler;
    JSON_Parser_EndArrayHandler         endArrayHandler;
    JSON_Parser_ArrayItemHandler        arrayItemHandler;
} ParserHandlers;

static void InitParserHandlers(ParserHandlers* pHandlers)
{
    pHandlers->encodingDetectedHandler = NULL;
    pHandlers->nullHandler = NULL;
    pHandlers->booleanHandler = NULL;
    pHandlers->stringHandler = NULL;
    pHandlers->numberHandler = NULL;
    pHandlers->specialNumberHandler = NULL;
    pHandlers->startObjectHandler = NULL;
    pHandlers->endObjectHandler = NULL;
    pHandlers->objectMemberHandler = NULL;
    pHandlers->startArrayHandler = NULL;
    pHandlers->endArrayHandler = NULL;
    pHandlers->arrayItemHandler = NULL;
}

static void GetParserHandlers(JSON_Parser parser, ParserHandlers* pHandlers)
{
    pHandlers->encodingDetectedHandler = JSON_Parser_GetEncodingDetectedHandler(parser);
    pHandlers->nullHandler = JSON_Parser_GetNullHandler(parser);
    pHandlers->booleanHandler = JSON_Parser_GetBooleanHandler(parser);
    pHandlers->stringHandler = JSON_Parser_GetStringHandler(parser);
    pHandlers->numberHandler = JSON_Parser_GetNumberHandler(parser);
    pHandlers->specialNumberHandler = JSON_Parser_GetSpecialNumberHandler(parser);
    pHandlers->startObjectHandler = JSON_Parser_GetStartObjectHandler(parser);
    pHandlers->endObjectHandler = JSON_Parser_GetEndObjectHandler(parser);
    pHandlers->objectMemberHandler = JSON_Parser_GetObjectMemberHandler(parser);
    pHandlers->startArrayHandler = JSON_Parser_GetStartArrayHandler(parser);
    pHandlers->endArrayHandler = JSON_Parser_GetEndArrayHandler(parser);
    pHandlers->arrayItemHandler = JSON_Parser_GetArrayItemHandler(parser);
}

static int ParserHandlersAreIdentical(const ParserHandlers* pHandlers1, const ParserHandlers* pHandlers2)
{
    return (pHandlers1->encodingDetectedHandler == pHandlers2->encodingDetectedHandler &&
            pHandlers1->nullHandler == pHandlers2->nullHandler &&
            pHandlers1->booleanHandler == pHandlers2->booleanHandler &&
            pHandlers1->stringHandler == pHandlers2->stringHandler &&
            pHandlers1->numberHandler == pHandlers2->numberHandler &&
            pHandlers1->specialNumberHandler == pHandlers2->specialNumberHandler &&
            pHandlers1->startObjectHandler == pHandlers2->startObjectHandler &&
            pHandlers1->endObjectHandler == pHandlers2->endObjectHandler &&
            pHandlers1->objectMemberHandler == pHandlers2->objectMemberHandler &&
            pHandlers1->startArrayHandler == pHandlers2->startArrayHandler &&
            pHandlers1->endArrayHandler == pHandlers2->endArrayHandler &&
            pHandlers1->arrayItemHandler == pHandlers2->arrayItemHandler);
}

static int CheckParserHandlers(JSON_Parser parser, const ParserHandlers* pExpectedHandlers)
{
    int identical;
    ParserHandlers actualHandlers;
    GetParserHandlers(parser, &actualHandlers);
    identical = ParserHandlersAreIdentical(pExpectedHandlers, &actualHandlers);
    if (!identical)
    {
        printf("FAILURE: parser handlers do not match\n"
               "  HANDLERS                                 EXPECTED     ACTUAL\n"
               "  JSON_Parser_GetEncodingDetectedHandler() %8s   %8s\n"
               "  JSON_Parser_GetNullHandler()             %8s   %8s\n"
               "  JSON_Parser_GetBooleanHandler()          %8s   %8s\n"
               "  JSON_Parser_GetStringHandler()           %8s   %8s\n"
               "  JSON_Parser_GetNumberHandler()           %8s   %8s\n"
               "  JSON_Parser_GetSpecialNumberHandler()    %8s   %8s\n"
               ,
               HANDLER_STRING(pExpectedHandlers->encodingDetectedHandler), HANDLER_STRING(actualHandlers.encodingDetectedHandler),
               HANDLER_STRING(pExpectedHandlers->nullHandler), HANDLER_STRING(actualHandlers.nullHandler),
               HANDLER_STRING(pExpectedHandlers->booleanHandler), HANDLER_STRING(actualHandlers.booleanHandler),
               HANDLER_STRING(pExpectedHandlers->stringHandler), HANDLER_STRING(actualHandlers.stringHandler),
               HANDLER_STRING(pExpectedHandlers->numberHandler), HANDLER_STRING(actualHandlers.numberHandler),
               HANDLER_STRING(pExpectedHandlers->specialNumberHandler), HANDLER_STRING(actualHandlers.specialNumberHandler)
            );
        printf("  JSON_Parser_GetStartObjectHandler()      %8s   %8s\n"
               "  JSON_Parser_GetEndObjectHandler()        %8s   %8s\n"
               "  JSON_Parser_GetObjectMemberHandler()     %8s   %8s\n"
               "  JSON_Parser_GetStartArrayHandler()       %8s   %8s\n"
               "  JSON_Parser_GetEndArrayHandler()         %8s   %8s\n"
               "  JSON_Parser_GetArrayItemHandler()        %8s   %8s\n"
               ,
               HANDLER_STRING(pExpectedHandlers->startObjectHandler), HANDLER_STRING(actualHandlers.startObjectHandler),
               HANDLER_STRING(pExpectedHandlers->endObjectHandler), HANDLER_STRING(actualHandlers.endObjectHandler),
               HANDLER_STRING(pExpectedHandlers->objectMemberHandler), HANDLER_STRING(actualHandlers.objectMemberHandler),
               HANDLER_STRING(pExpectedHandlers->startArrayHandler), HANDLER_STRING(actualHandlers.startArrayHandler),
               HANDLER_STRING(pExpectedHandlers->endArrayHandler), HANDLER_STRING(actualHandlers.endArrayHandler),
               HANDLER_STRING(pExpectedHandlers->arrayItemHandler), HANDLER_STRING(actualHandlers.arrayItemHandler)
            );
    }
    return identical;
}

static int CheckParserHasDefaultValues(JSON_Parser parser)
{
    ParserState state;
    ParserSettings settings;
    ParserHandlers handlers;
    InitParserState(&state);
    InitParserSettings(&settings);
    InitParserHandlers(&handlers);
    return CheckParserState(parser, &state) &&
           CheckParserSettings(parser, &settings) &&
           CheckParserHandlers(parser, &handlers);
}

static int CheckParserCreate(const JSON_MemorySuite* pMemorySuite, JSON_Status expectedStatus, JSON_Parser* pParser)
{
    *pParser = JSON_Parser_Create(pMemorySuite);
    if (expectedStatus == JSON_Success && !*pParser)
    {
        printf("FAILURE: expected JSON_Parser_Create() to return a parser instance\n");
        return 0;
    }
    if (expectedStatus == JSON_Failure && *pParser)
    {
        printf("FAILURE: expected JSON_Parser_Create() to return NULL\n");
        JSON_Parser_Free(*pParser);
        *pParser = NULL;
        return 0;
    }
    return 1;
}

static int CheckParserCreateWithCustomMemorySuite(JSON_ReallocHandler r, JSON_FreeHandler f, JSON_Status expectedStatus, JSON_Parser* pParser)
{
    JSON_MemorySuite memorySuite;
    memorySuite.userData = NULL;
    memorySuite.realloc = r;
    memorySuite.free = f;
    return CheckParserCreate(&memorySuite, expectedStatus, pParser);
}

static int CheckParserReset(JSON_Parser parser, JSON_Status expectedStatus)
{
    if (JSON_Parser_Reset(parser) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_Reset() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserFree(JSON_Parser parser, JSON_Status expectedStatus)
{
    if (JSON_Parser_Free(parser) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_Free() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserGetError(JSON_Parser parser, JSON_Location* pLocation, JSON_Status expectedStatus)
{
    if (JSON_Parser_GetErrorLocation(parser, pLocation) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_GetErrorLocation() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserGetTokenLocation(JSON_Parser parser, JSON_Location* pLocation, JSON_Status expectedStatus)
{
    if (JSON_Parser_GetTokenLocation(parser, pLocation) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_GetTokenLocation() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserGetAfterTokenLocation(JSON_Parser parser, JSON_Location* pLocation, JSON_Status expectedStatus)
{
    if (JSON_Parser_GetAfterTokenLocation(parser, pLocation) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_GetAfterTokenLocation() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetUserData(JSON_Parser parser, void* userData, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetUserData(parser, userData) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetUserData() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetInputEncoding(JSON_Parser parser, JSON_Encoding encoding, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetInputEncoding(parser, encoding) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetInputEncoding() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetStringEncoding(JSON_Parser parser, JSON_Encoding encoding, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetStringEncoding(parser, encoding) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetStringEncoding() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetNumberEncoding(JSON_Parser parser, JSON_Encoding encoding, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetNumberEncoding(parser, encoding) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetNumberEncoding() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetMaxStringLength(JSON_Parser parser, size_t maxLength, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetMaxStringLength(parser, maxLength) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetMaxStringLength() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetMaxNumberLength(JSON_Parser parser, size_t maxLength, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetMaxNumberLength(parser, maxLength) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetMaxNumberLength() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetAllowBOM(JSON_Parser parser, JSON_Boolean allowBOM, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetAllowBOM(parser, allowBOM) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetAllowBOM() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetAllowComments(JSON_Parser parser, JSON_Boolean allowComments, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetAllowComments(parser, allowComments) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetAllowComments() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetAllowSpecialNumbers(JSON_Parser parser, JSON_Boolean allowSpecialNumbers, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetAllowSpecialNumbers(parser, allowSpecialNumbers) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetAllowSpecialNumbers() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetAllowHexNumbers(JSON_Parser parser, JSON_Boolean allowHexNumbers, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetAllowHexNumbers(parser, allowHexNumbers) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetAllowHexNumbers() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetAllowUnescapedControlCharacters(JSON_Parser parser, JSON_Boolean allowUnescapedControlCharacters, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetAllowUnescapedControlCharacters(parser, allowUnescapedControlCharacters) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetAllowUnescapedControlCharacters() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetReplaceInvalidEncodingSequences(JSON_Parser parser, JSON_Boolean replaceInvalidEncodingSequences, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetReplaceInvalidEncodingSequences(parser, replaceInvalidEncodingSequences) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetReplaceInvalidEncodingSequences() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetTrackObjectMembers(JSON_Parser parser, JSON_Boolean trackObjectMembers, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetTrackObjectMembers(parser, trackObjectMembers) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetTrackObjectMembers() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetStopAfterEmbeddedDocument(JSON_Parser parser, JSON_Boolean stopAfterEmbeddedDocument, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetStopAfterEmbeddedDocument(parser, stopAfterEmbeddedDocument) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetStopAfterEmbeddedDocument() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetEncodingDetectedHandler(JSON_Parser parser, JSON_Parser_EncodingDetectedHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetEncodingDetectedHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetEncodingDetectedHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetNullHandler(JSON_Parser parser, JSON_Parser_NullHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetNullHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetNullHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetBooleanHandler(JSON_Parser parser, JSON_Parser_BooleanHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetBooleanHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetBooleanHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetStringHandler(JSON_Parser parser, JSON_Parser_StringHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetStringHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetStringHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetNumberHandler(JSON_Parser parser, JSON_Parser_NumberHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetNumberHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetNumberHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetSpecialNumberHandler(JSON_Parser parser, JSON_Parser_SpecialNumberHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetSpecialNumberHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetSpecialNumberHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetStartObjectHandler(JSON_Parser parser, JSON_Parser_StartObjectHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetStartObjectHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetStartObjectHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetEndObjectHandler(JSON_Parser parser, JSON_Parser_EndObjectHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetEndObjectHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetEndObjectHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetObjectMemberHandler(JSON_Parser parser, JSON_Parser_ObjectMemberHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetObjectMemberHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetObjectMemberHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetStartArrayHandler(JSON_Parser parser, JSON_Parser_StartArrayHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetStartArrayHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetStartArrayHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetEndArrayHandler(JSON_Parser parser, JSON_Parser_EndArrayHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetEndArrayHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetEndArrayHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserSetArrayItemHandler(JSON_Parser parser, JSON_Parser_ArrayItemHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetArrayItemHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetArrayItemHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParserParse(JSON_Parser parser, const char* pBytes, size_t length, JSON_Boolean isFinal, JSON_Status expectedStatus)
{
    if (JSON_Parser_Parse(parser, pBytes, length, isFinal) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_Parse() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int TryToMisbehaveInParseHandler(JSON_Parser parser)
{
    if (!CheckParserFree(parser, JSON_Failure) ||
        !CheckParserReset(parser, JSON_Failure) ||
        !CheckParserGetTokenLocation(parser, NULL, JSON_Failure) ||
        !CheckParserGetAfterTokenLocation(parser, NULL, JSON_Failure) ||
        !CheckParserSetInputEncoding(parser, JSON_UTF32LE, JSON_Failure) ||
        !CheckParserSetStringEncoding(parser, JSON_UTF32LE, JSON_Failure) ||
        !CheckParserSetNumberEncoding(parser, JSON_UTF32LE, JSON_Failure) ||
        !CheckParserSetMaxStringLength(parser, 1, JSON_Failure) ||
        !CheckParserSetMaxNumberLength(parser, 1, JSON_Failure) ||
        !CheckParserSetAllowBOM(parser, JSON_True, JSON_Failure) ||
        !CheckParserSetAllowComments(parser, JSON_True, JSON_Failure) ||
        !CheckParserSetAllowSpecialNumbers(parser, JSON_True, JSON_Failure) ||
        !CheckParserSetAllowHexNumbers(parser, JSON_True, JSON_Failure) ||
        !CheckParserSetAllowUnescapedControlCharacters(parser, JSON_True, JSON_Failure) ||
        !CheckParserSetReplaceInvalidEncodingSequences(parser, JSON_True, JSON_Failure) ||
        !CheckParserSetTrackObjectMembers(parser, JSON_True, JSON_Failure) ||
        !CheckParserSetStopAfterEmbeddedDocument(parser, JSON_True, JSON_Failure) ||
        !CheckParserParse(parser, " ", 1, JSON_False, JSON_Failure))
    {
        return 1;
    }
    return 0;
}

static JSON_Parser_HandlerResult JSON_CALL EncodingDetectedHandler(JSON_Parser parser)
{
    JSON_Location location;
    const char* pszEncoding = "";
    if (s_failHandler)
    {
        return JSON_Parser_Abort;
    }
    if (s_misbehaveInHandler && TryToMisbehaveInParseHandler(parser))
    {
        return JSON_Parser_Abort;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Failure)
    {
        return JSON_Parser_Abort;
    }
    if (JSON_Parser_GetAfterTokenLocation(parser, &location) != JSON_Failure)
    {
        return JSON_Parser_Abort;
    }
    switch (JSON_Parser_GetInputEncoding(parser))
    {
    case JSON_UTF8:
        pszEncoding = "8";
        break;
    case JSON_UTF16LE:
        pszEncoding = "16LE";
        break;
    case JSON_UTF16BE:
        pszEncoding = "16BE";
        break;
    case JSON_UTF32LE:
        pszEncoding = "32LE";
        break;
    case JSON_UTF32BE:
        pszEncoding = "32BE";
        break;
    default:
        break;
    }
    OutputSeparator();
    OutputFormatted("u(%s)", pszEncoding);
    return JSON_Parser_Continue;
}

static JSON_Parser_HandlerResult JSON_CALL NullHandler(JSON_Parser parser)
{
    JSON_Location location, afterLocation;
    if (s_failHandler)
    {
        return JSON_Parser_Abort;
    }
    if (s_misbehaveInHandler && TryToMisbehaveInParseHandler(parser))
    {
        return JSON_Parser_Abort;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success ||
        JSON_Parser_GetAfterTokenLocation(parser, &afterLocation) != JSON_Success)
    {
        return JSON_Parser_Abort;
    }
    OutputSeparator();
    OutputFormatted("n:");
    OutputLocation(&location);
    OutputFormatted("-");
    OutputLocation(&afterLocation);
    return JSON_Parser_Continue;
}

static JSON_Parser_HandlerResult JSON_CALL BooleanHandler(JSON_Parser parser, JSON_Boolean value)
{
    JSON_Location location, afterLocation;
    if (s_failHandler)
    {
        return JSON_Parser_Abort;
    }
    if (s_misbehaveInHandler && TryToMisbehaveInParseHandler(parser))
    {
        return JSON_Parser_Abort;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success ||
        JSON_Parser_GetAfterTokenLocation(parser, &afterLocation) != JSON_Success)
    {
        return JSON_Parser_Abort;
    }
    OutputSeparator();
    OutputFormatted("%s:", (value == JSON_True) ? "t" : "f");
    OutputLocation(&location);
    OutputFormatted("-");
    OutputLocation(&afterLocation);
    return JSON_Parser_Continue;
}

static JSON_Parser_HandlerResult JSON_CALL StringHandler(JSON_Parser parser, char* pValue, size_t length, JSON_StringAttributes attributes)
{
    JSON_Location location, afterLocation;
    if (s_failHandler)
    {
        return JSON_Parser_Abort;
    }
    if (s_misbehaveInHandler && TryToMisbehaveInParseHandler(parser))
    {
        return JSON_Parser_Abort;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success ||
        JSON_Parser_GetAfterTokenLocation(parser, &afterLocation) != JSON_Success)
    {
        return JSON_Parser_Abort;
    }
    OutputSeparator();
    OutputFormatted("s(");
    OutputStringBytes((const unsigned char*)pValue, length, attributes, JSON_Parser_GetStringEncoding(parser));
    OutputFormatted("):");
    OutputLocation(&location);
    OutputFormatted("-");
    OutputLocation(&afterLocation);
    memset(pValue, 0, length); /* test that the buffer is really writable */
    return JSON_Parser_Continue;
}

static JSON_Parser_HandlerResult JSON_CALL NumberHandler(JSON_Parser parser, char* pValue, size_t length, JSON_NumberAttributes attributes)
{
    JSON_Location location, afterLocation;
    if (s_failHandler)
    {
        return JSON_Parser_Abort;
    }
    if (s_misbehaveInHandler && TryToMisbehaveInParseHandler(parser))
    {
        return JSON_Parser_Abort;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success ||
        JSON_Parser_GetAfterTokenLocation(parser, &afterLocation) != JSON_Success)
    {
        return JSON_Parser_Abort;
    }
    OutputSeparator();
    OutputFormatted("#(");
    OutputNumber((const unsigned char*)pValue, length, attributes, JSON_Parser_GetNumberEncoding(parser));
    OutputFormatted("):");
    OutputLocation(&location);
    OutputFormatted("-");
    OutputLocation(&afterLocation);
    memset(pValue, 0, length); /* test that the buffer is really writable */
    return JSON_Parser_Continue;
}

static JSON_Parser_HandlerResult JSON_CALL SpecialNumberHandler(JSON_Parser parser, JSON_SpecialNumber value)
{
    JSON_Location location, afterLocation;
    const char* pValue;
    if (s_failHandler)
    {
        return JSON_Parser_Abort;
    }
    if (s_misbehaveInHandler && TryToMisbehaveInParseHandler(parser))
    {
        return JSON_Parser_Abort;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success ||
        JSON_Parser_GetAfterTokenLocation(parser, &afterLocation) != JSON_Success)
    {
        return JSON_Parser_Abort;
    }
    switch (value)
    {
    case JSON_NaN:
        pValue = "NaN";
        break;
    case JSON_Infinity:
        pValue = "Infinity";
        break;
    case JSON_NegativeInfinity:
        pValue = "-Infinity";
        break;
    default:
        pValue = "UNKNOWN";
        break;
    }
    OutputSeparator();
    OutputFormatted("#(%s):", pValue);
    OutputLocation(&location);
    OutputFormatted("-");
    OutputLocation(&afterLocation);
    return JSON_Parser_Continue;
}

static JSON_Parser_HandlerResult JSON_CALL StartObjectHandler(JSON_Parser parser)
{
    JSON_Location location, afterLocation;
    if (s_failHandler)
    {
        return JSON_Parser_Abort;
    }
    if (s_misbehaveInHandler && TryToMisbehaveInParseHandler(parser))
    {
        return JSON_Parser_Abort;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success ||
        JSON_Parser_GetAfterTokenLocation(parser, &afterLocation) != JSON_Success)
    {
        return JSON_Parser_Abort;
    }
    OutputSeparator();
    OutputFormatted("{:");
    OutputLocation(&location);
    OutputFormatted("-");
    OutputLocation(&afterLocation);
    return JSON_Parser_Continue;
}

static JSON_Parser_HandlerResult JSON_CALL EndObjectHandler(JSON_Parser parser)
{
    JSON_Location location, afterLocation;
    if (s_failHandler)
    {
        return JSON_Parser_Abort;
    }
    if (s_misbehaveInHandler && TryToMisbehaveInParseHandler(parser))
    {
        return JSON_Parser_Abort;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success ||
        JSON_Parser_GetAfterTokenLocation(parser, &afterLocation) != JSON_Success)
    {
        return JSON_Parser_Abort;
    }
    OutputSeparator();
    OutputFormatted("}:");
    OutputLocation(&location);
    OutputFormatted("-");
    OutputLocation(&afterLocation);
    return JSON_Parser_Continue;
}

static JSON_Parser_HandlerResult JSON_CALL ObjectMemberHandler(JSON_Parser parser, char* pValue, size_t length, JSON_StringAttributes attributes)
{
    JSON_Location location, afterLocation;
    if (s_failHandler)
    {
        return JSON_Parser_Abort;
    }
    if (s_misbehaveInHandler && TryToMisbehaveInParseHandler(parser))
    {
        return JSON_Parser_Abort;
    }
    if (attributes == JSON_SimpleString && !strcmp(pValue, "duplicate"))
    {
        return JSON_Parser_TreatAsDuplicateObjectMember;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success ||
        JSON_Parser_GetAfterTokenLocation(parser, &afterLocation) != JSON_Success)
    {
        return JSON_Parser_Abort;
    }
    OutputSeparator();
    OutputFormatted("m(");
    OutputStringBytes((const unsigned char*)pValue, length, attributes, JSON_Parser_GetStringEncoding(parser));
    OutputFormatted("):");
    OutputLocation(&location);
    OutputFormatted("-");
    OutputLocation(&afterLocation);
    memset(pValue, 0, length); /* test that the buffer is really writable */
    return JSON_Parser_Continue;
}

static JSON_Parser_HandlerResult JSON_CALL StartArrayHandler(JSON_Parser parser)
{
    JSON_Location location, afterLocation;
    if (s_failHandler)
    {
        return JSON_Parser_Abort;
    }
    if (s_misbehaveInHandler && TryToMisbehaveInParseHandler(parser))
    {
        return JSON_Parser_Abort;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success ||
        JSON_Parser_GetAfterTokenLocation(parser, &afterLocation) != JSON_Success)
    {
        return JSON_Parser_Abort;
    }
    OutputSeparator();
    OutputFormatted("[:");
    OutputLocation(&location);
    OutputFormatted("-");
    OutputLocation(&afterLocation);
    return JSON_Parser_Continue;
}

static JSON_Parser_HandlerResult JSON_CALL EndArrayHandler(JSON_Parser parser)
{
    JSON_Location location, afterLocation;
    if (s_failHandler)
    {
        return JSON_Parser_Abort;
    }
    if (s_misbehaveInHandler && TryToMisbehaveInParseHandler(parser))
    {
        return JSON_Parser_Abort;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success ||
        JSON_Parser_GetAfterTokenLocation(parser, &afterLocation) != JSON_Success)
    {
        return JSON_Parser_Abort;
    }
    OutputSeparator();
    OutputFormatted("]:");
    OutputLocation(&location);
    OutputFormatted("-");
    OutputLocation(&afterLocation);
    return JSON_Parser_Continue;
}

static JSON_Parser_HandlerResult JSON_CALL ArrayItemHandler(JSON_Parser parser)
{
    JSON_Location location, afterLocation;
    if (s_failHandler)
    {
        return JSON_Parser_Abort;
    }
    if (s_misbehaveInHandler && TryToMisbehaveInParseHandler(parser))
    {
        return JSON_Parser_Abort;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success ||
        JSON_Parser_GetAfterTokenLocation(parser, &afterLocation) != JSON_Success)
    {
        return JSON_Parser_Abort;
    }
    OutputSeparator();
    OutputFormatted("i:");
    OutputLocation(&location);
    OutputFormatted("-");
    OutputLocation(&afterLocation);
    return JSON_Parser_Continue;
}

typedef enum tag_ParserParam
{
    Standard = 0,

    /* Bottom 4 bits are input encoding. */
    DefaultIn = 0 << 0,
    UTF8In    = 1 << 0,
    UTF16LEIn = 2 << 0,
    UTF16BEIn = 3 << 0,
    UTF32LEIn = 4 << 0,
    UTF32BEIn = 5 << 0,

    /* Next 4 bits are output encoding. */
    DefaultOut = 0 << 4,
    UTF8Out    = 1 << 4,
    UTF16LEOut = 2 << 4,
    UTF16BEOut = 3 << 4,
    UTF32LEOut = 4 << 4,
    UTF32BEOut = 5 << 4,

    /* Next 2 bits are max string length. */
    DefaultMaxStringLength = 0 << 8,
    MaxStringLength0       = 1 << 8,
    MaxStringLength1       = 2 << 8,
    MaxStringLength2       = 3 << 8,

    /* Next 2 bits are max number length. */
    DefaultMaxNumberLength = 0 << 10,
    MaxNumberLength0       = 1 << 10,
    MaxNumberLength1       = 2 << 10,
    MaxNumberLength2       = 3 << 10,

    /* Rest of bits are settings. */
    AllowBOM                        = 1 << 12,
    AllowComments                   = 1 << 13,
    AllowSpecialNumbers             = 1 << 14,
    AllowHexNumbers                 = 1 << 15,
    AllowUnescapedControlCharacters = 1 << 16,
    ReplaceInvalidEncodingSequences = 1 << 17,
    TrackObjectMembers              = 1 << 18,
    StopAfterEmbeddedDocument       = 1 << 19
} ParserParam;
typedef unsigned int ParserParams;

typedef struct tag_ParseTest
{
    const char*   pName;
    ParserParams  parserParams;
    const char*   pInput;
    size_t        length;
    JSON_Boolean  isFinal;
    JSON_Encoding inputEncoding;
    const char*   pOutput;
} ParseTest;

static void RunParseTest(const ParseTest* pTest)
{
    JSON_Parser parser = NULL;
    ParserSettings settings;
    ParserState state;
    printf("Test parsing %s ... ", pTest->pName);

    InitParserSettings(&settings);
    if ((pTest->parserParams & 0xF) != DefaultIn)
    {
        settings.inputEncoding = (JSON_Encoding)(pTest->parserParams & 0xF);
    }
    if ((pTest->parserParams & 0xF0) != DefaultOut)
    {
        settings.stringEncoding = settings.numberEncoding = (JSON_Encoding)((pTest->parserParams >> 4) & 0xF);
    }
    if ((pTest->parserParams & 0x300) != DefaultMaxStringLength)
    {
        settings.maxStringLength = (size_t)((pTest->parserParams >> 8) & 0x3) - 1;
    }
    if ((pTest->parserParams & 0xC00) != DefaultMaxNumberLength)
    {
        settings.maxNumberLength = (size_t)((pTest->parserParams >> 10) & 0x3) - 1;
    }
    settings.allowBOM = (JSON_Boolean)((pTest->parserParams >> 12) & 0x1);
    settings.allowComments = (JSON_Boolean)((pTest->parserParams >> 13) & 0x1);
    settings.allowSpecialNumbers = (JSON_Boolean)((pTest->parserParams >> 14) & 0x1);
    settings.allowHexNumbers = (JSON_Boolean)((pTest->parserParams >> 15) & 0x1);
    settings.allowUnescapedControlCharacters = (JSON_Boolean)((pTest->parserParams >> 16) & 0x1);
    settings.replaceInvalidEncodingSequences = (JSON_Boolean)((pTest->parserParams >> 17) & 0x1);
    settings.trackObjectMembers = (JSON_Boolean)((pTest->parserParams >> 18) & 0x1);
    settings.stopAfterEmbeddedDocument = (JSON_Boolean)((pTest->parserParams >> 19) & 0x1);

    InitParserState(&state);
    state.inputEncoding = pTest->inputEncoding;
    ResetOutput();

    if (CheckParserCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParserSetEncodingDetectedHandler(parser, &EncodingDetectedHandler, JSON_Success) &&
        CheckParserSetNullHandler(parser, &NullHandler, JSON_Success) &&
        CheckParserSetBooleanHandler(parser, &BooleanHandler, JSON_Success) &&
        CheckParserSetStringHandler(parser, &StringHandler, JSON_Success) &&
        CheckParserSetNumberHandler(parser, &NumberHandler, JSON_Success) &&
        CheckParserSetSpecialNumberHandler(parser, &SpecialNumberHandler, JSON_Success) &&
        CheckParserSetStartObjectHandler(parser, &StartObjectHandler, JSON_Success) &&
        CheckParserSetEndObjectHandler(parser, &EndObjectHandler, JSON_Success) &&
        CheckParserSetObjectMemberHandler(parser, &ObjectMemberHandler, JSON_Success) &&
        CheckParserSetStartArrayHandler(parser, &StartArrayHandler, JSON_Success) &&
        CheckParserSetEndArrayHandler(parser, &EndArrayHandler, JSON_Success) &&
        CheckParserSetArrayItemHandler(parser, &ArrayItemHandler, JSON_Success) &&
        CheckParserSetInputEncoding(parser, settings.inputEncoding, JSON_Success) &&
        CheckParserSetStringEncoding(parser, settings.stringEncoding, JSON_Success) &&
        CheckParserSetNumberEncoding(parser, settings.numberEncoding, JSON_Success) &&
        CheckParserSetMaxStringLength(parser, settings.maxStringLength, JSON_Success) &&
        CheckParserSetMaxNumberLength(parser, settings.maxNumberLength, JSON_Success) &&
        CheckParserSetAllowBOM(parser, settings.allowBOM, JSON_Success) &&
        CheckParserSetAllowComments(parser, settings.allowComments, JSON_Success) &&
        CheckParserSetAllowSpecialNumbers(parser, settings.allowSpecialNumbers, JSON_Success) &&
        CheckParserSetAllowHexNumbers(parser, settings.allowHexNumbers, JSON_Success) &&
        CheckParserSetAllowUnescapedControlCharacters(parser, settings.allowUnescapedControlCharacters, JSON_Success) &&
        CheckParserSetReplaceInvalidEncodingSequences(parser, settings.replaceInvalidEncodingSequences, JSON_Success) &&
        CheckParserSetTrackObjectMembers(parser, settings.trackObjectMembers, JSON_Success) &&
        CheckParserSetStopAfterEmbeddedDocument(parser, settings.stopAfterEmbeddedDocument, JSON_Success))
    {
        JSON_Parser_Parse(parser, pTest->pInput, pTest->length, pTest->isFinal);
        state.error = JSON_Parser_GetError(parser);
        JSON_Parser_GetErrorLocation(parser, &state.errorLocation);
        if (state.error != JSON_Error_None)
        {
            OutputSeparator();
            OutputFormatted("!(%s):", errorNames[state.error]);
            OutputLocation(&state.errorLocation);
        }
        if (CheckParserState(parser, &state) && CheckOutput(pTest->pOutput))
        {
            printf("OK\n");
        }
        else
        {
            s_failureCount++;
        }
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
    ResetOutput();
}

static void TestParserCreate(void)
{
    JSON_Parser parser = NULL;
    printf("Test creating parser ... ");
    if (CheckParserCreate(NULL, JSON_Success, &parser) &&
        CheckParserHasDefaultValues(parser))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestParserCreateWithCustomMemorySuite(void)
{
    JSON_Parser parser = NULL;
    printf("Test creating parser with custom memory suite ... ");
    if (CheckParserCreateWithCustomMemorySuite(NULL, NULL, JSON_Failure, &parser) &&
        CheckParserCreateWithCustomMemorySuite(&ReallocHandler, NULL, JSON_Failure, &parser) &
        CheckParserCreateWithCustomMemorySuite(NULL, &FreeHandler, JSON_Failure, &parser) &&
        CheckParserCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParserHasDefaultValues(parser))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestParserCreateMallocFailure(void)
{
    JSON_Parser parser = NULL;
    printf("Test creating parser malloc failure ... ");
    s_failMalloc = 1;
    if (CheckParserCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Failure, &parser))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    s_failMalloc = 0;
    JSON_Parser_Free(parser);
}

static void TestParserSetSettings(void)
{
    JSON_Parser parser = NULL;
    ParserSettings settings;
    printf("Test setting parser settings ... ");
    InitParserSettings(&settings);
    settings.userData = (void*)1;
    settings.inputEncoding = JSON_UTF16LE;
    settings.stringEncoding = JSON_UTF32LE;
    settings.numberEncoding = JSON_UTF32BE;
    settings.maxStringLength = 2;
    settings.maxNumberLength = 3;
    settings.allowBOM = JSON_True;
    settings.allowComments = JSON_True;
    settings.allowSpecialNumbers = JSON_True;
    settings.allowHexNumbers = JSON_True;
    settings.allowUnescapedControlCharacters = JSON_True;
    settings.replaceInvalidEncodingSequences = JSON_True;
    settings.trackObjectMembers = JSON_True;
    settings.stopAfterEmbeddedDocument = JSON_True;
    if (CheckParserCreate(NULL, JSON_Success, &parser) &&
        CheckParserSetUserData(parser, settings.userData, JSON_Success) &&
        CheckParserSetInputEncoding(parser, settings.inputEncoding, JSON_Success) &&
        CheckParserSetStringEncoding(parser, settings.stringEncoding, JSON_Success) &&
        CheckParserSetNumberEncoding(parser, settings.numberEncoding, JSON_Success) &&
        CheckParserSetMaxStringLength(parser, settings.maxStringLength, JSON_Success) &&
        CheckParserSetMaxNumberLength(parser, settings.maxNumberLength, JSON_Success) &&
        CheckParserSetAllowBOM(parser, settings.allowBOM, JSON_Success) &&
        CheckParserSetAllowComments(parser, settings.allowComments, JSON_Success) &&
        CheckParserSetAllowSpecialNumbers(parser, settings.allowSpecialNumbers, JSON_Success) &&
        CheckParserSetAllowHexNumbers(parser, settings.allowHexNumbers, JSON_Success) &&
        CheckParserSetAllowUnescapedControlCharacters(parser, settings.allowUnescapedControlCharacters, JSON_Success) &&
        CheckParserSetReplaceInvalidEncodingSequences(parser, settings.replaceInvalidEncodingSequences, JSON_Success) &&
        CheckParserSetTrackObjectMembers(parser, settings.trackObjectMembers, JSON_Success) &&
        CheckParserSetStopAfterEmbeddedDocument(parser, settings.stopAfterEmbeddedDocument, JSON_Success) &&
        CheckParserSettings(parser, &settings))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestParserSetInvalidSettings(void)
{
    JSON_Parser parser = NULL;
    ParserSettings settings;
    printf("Test setting invalid parser settings ... ");
    InitParserSettings(&settings);
    if (CheckParserCreate(NULL, JSON_Success, &parser) &&
        CheckParserSetInputEncoding(parser, (JSON_Encoding)(JSON_UTF32BE + 1), JSON_Failure) &&
        CheckParserSetStringEncoding(parser, JSON_UnknownEncoding, JSON_Failure) &&
        CheckParserSetStringEncoding(parser, (JSON_Encoding)(JSON_UTF32BE + 1), JSON_Failure) &&
        CheckParserSetNumberEncoding(parser, JSON_UnknownEncoding, JSON_Failure) &&
        CheckParserSetNumberEncoding(parser, (JSON_Encoding)(JSON_UTF32BE + 1), JSON_Failure) &&
        CheckParserParse(parser, NULL, 1, JSON_False, JSON_Failure) &&
        CheckParserSettings(parser, &settings))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestParserSetHandlers(void)
{
    JSON_Parser parser = NULL;
    ParserHandlers handlers;
    printf("Test setting parser handlers ... ");
    InitParserHandlers(&handlers);
    handlers.encodingDetectedHandler = &EncodingDetectedHandler;
    handlers.nullHandler = &NullHandler;
    handlers.booleanHandler = &BooleanHandler;
    handlers.stringHandler = &StringHandler;
    handlers.numberHandler = &NumberHandler;
    handlers.specialNumberHandler = &SpecialNumberHandler;
    handlers.startObjectHandler = &StartObjectHandler;
    handlers.endObjectHandler = &EndObjectHandler;
    handlers.objectMemberHandler = &ObjectMemberHandler;
    handlers.startArrayHandler = &StartArrayHandler;
    handlers.endArrayHandler = &EndArrayHandler;
    handlers.arrayItemHandler = &ArrayItemHandler;
    if (CheckParserCreate(NULL, JSON_Success, &parser) &&
        CheckParserSetEncodingDetectedHandler(parser, handlers.encodingDetectedHandler, JSON_Success) &&
        CheckParserSetNullHandler(parser, handlers.nullHandler, JSON_Success) &&
        CheckParserSetBooleanHandler(parser, handlers.booleanHandler, JSON_Success) &&
        CheckParserSetStringHandler(parser, handlers.stringHandler, JSON_Success) &&
        CheckParserSetNumberHandler(parser, handlers.numberHandler, JSON_Success) &&
        CheckParserSetSpecialNumberHandler(parser, handlers.specialNumberHandler, JSON_Success) &&
        CheckParserSetStartObjectHandler(parser, handlers.startObjectHandler, JSON_Success) &&
        CheckParserSetEndObjectHandler(parser, handlers.endObjectHandler, JSON_Success) &&
        CheckParserSetObjectMemberHandler(parser, handlers.objectMemberHandler, JSON_Success) &&
        CheckParserSetStartArrayHandler(parser, handlers.startArrayHandler, JSON_Success) &&
        CheckParserSetEndArrayHandler(parser, handlers.endArrayHandler, JSON_Success) &&
        CheckParserSetArrayItemHandler(parser, handlers.arrayItemHandler, JSON_Success) &&
        CheckParserHandlers(parser, &handlers))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestParserReset(void)
{
    JSON_Parser parser = NULL;
    ParserState state;
    ParserSettings settings;
    ParserHandlers handlers;
    printf("Test resetting parser ... ");
    InitParserState(&state);
    InitParserSettings(&settings);
    InitParserHandlers(&handlers);
    if (CheckParserCreate(NULL, JSON_Success, &parser) &&
        CheckParserSetUserData(parser, (void*)1, JSON_Success) &&
        CheckParserSetInputEncoding(parser, JSON_UTF16LE, JSON_Success) &&
        CheckParserSetStringEncoding(parser, JSON_UTF16LE, JSON_Success) &&
        CheckParserSetNumberEncoding(parser, JSON_UTF16LE, JSON_Success) &&
        CheckParserSetMaxStringLength(parser, 32, JSON_Success) &&
        CheckParserSetMaxNumberLength(parser, 32, JSON_Success) &&
        CheckParserSetAllowBOM(parser, JSON_True, JSON_Success) &&
        CheckParserSetAllowComments(parser, JSON_True, JSON_Success) &&
        CheckParserSetAllowSpecialNumbers(parser, JSON_True, JSON_Success) &&
        CheckParserSetAllowHexNumbers(parser, JSON_True, JSON_Success) &&
        CheckParserSetAllowUnescapedControlCharacters(parser, JSON_True, JSON_Success) &&
        CheckParserSetReplaceInvalidEncodingSequences(parser, JSON_True, JSON_Success) &&
        CheckParserSetTrackObjectMembers(parser, JSON_True, JSON_Success) &&
        CheckParserSetEncodingDetectedHandler(parser, &EncodingDetectedHandler, JSON_Success) &&
        CheckParserSetNullHandler(parser, &NullHandler, JSON_Success) &&
        CheckParserSetBooleanHandler(parser, &BooleanHandler, JSON_Success) &&
        CheckParserSetStringHandler(parser, &StringHandler, JSON_Success) &&
        CheckParserSetNumberHandler(parser, &NumberHandler, JSON_Success) &&
        CheckParserSetSpecialNumberHandler(parser, &SpecialNumberHandler, JSON_Success) &&
        CheckParserSetStartObjectHandler(parser, &StartObjectHandler, JSON_Success) &&
        CheckParserSetEndObjectHandler(parser, &EndObjectHandler, JSON_Success) &&
        CheckParserSetObjectMemberHandler(parser, &ObjectMemberHandler, JSON_Success) &&
        CheckParserSetStartArrayHandler(parser, &StartArrayHandler, JSON_Success) &&
        CheckParserSetEndArrayHandler(parser, &EndArrayHandler, JSON_Success) &&
        CheckParserSetArrayItemHandler(parser, &ArrayItemHandler, JSON_Success) &&
        CheckParserParse(parser, "7\x00", 2, JSON_True, JSON_Success) &&
        CheckParserReset(parser, JSON_Success) &&
        CheckParserState(parser, &state) &&
        CheckParserSettings(parser, &settings) &&
        CheckParserHandlers(parser, &handlers))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestParserMisbehaveInCallbacks(void)
{
    JSON_Parser parser = NULL;
    printf("Test parser misbehaving in callbacks ... ");
    s_misbehaveInHandler = 1;
    if (CheckParserCreate(NULL, JSON_Success, &parser) &&
        CheckParserSetEncodingDetectedHandler(parser, &EncodingDetectedHandler, JSON_Success) &&
        CheckParserParse(parser, "null", 4, JSON_True, JSON_Success) &&

        CheckParserCreate(NULL, JSON_Success, &parser) &&
        CheckParserSetNullHandler(parser, &NullHandler, JSON_Success) &&
        CheckParserParse(parser, "null", 4, JSON_True, JSON_Success) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetBooleanHandler(parser, &BooleanHandler, JSON_Success) &&
        CheckParserParse(parser, "true", 4, JSON_True, JSON_Success) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetStringHandler(parser, &StringHandler, JSON_Success) &&
        CheckParserParse(parser, "\"\"", 2, JSON_True, JSON_Success) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetNumberHandler(parser, &NumberHandler, JSON_Success) &&
        CheckParserParse(parser, "7", 1, JSON_True, JSON_Success) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetAllowSpecialNumbers(parser, JSON_True, JSON_Success) &&
        CheckParserSetSpecialNumberHandler(parser, &SpecialNumberHandler, JSON_Success) &&
        CheckParserParse(parser, "NaN", 3, JSON_True, JSON_Success) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetStartObjectHandler(parser, &StartObjectHandler, JSON_Success) &&
        CheckParserParse(parser, "{\"x\":0}", 7, JSON_True, JSON_Success) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetEndObjectHandler(parser, &EndObjectHandler, JSON_Success) &&
        CheckParserParse(parser, "{\"x\":0}", 7, JSON_True, JSON_Success) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetObjectMemberHandler(parser, &ObjectMemberHandler, JSON_Success) &&
        CheckParserParse(parser, "{\"x\":0}", 7, JSON_True, JSON_Success) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetStartArrayHandler(parser, &StartArrayHandler, JSON_Success) &&
        CheckParserParse(parser, "[0]", 3, JSON_True, JSON_Success) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetEndArrayHandler(parser, &EndArrayHandler, JSON_Success) &&
        CheckParserParse(parser, "[0]", 3, JSON_True, JSON_Success) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetArrayItemHandler(parser, &ArrayItemHandler, JSON_Success) &&
        CheckParserParse(parser, "[0]", 3, JSON_True, JSON_Success))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    s_misbehaveInHandler = 0;
    JSON_Parser_Free(parser);
}

static void TestParserAbortInCallbacks(void)
{
    JSON_Parser parser = NULL;
    ParserState state;
    printf("Test parser aborting in callbacks ... ");
    InitParserState(&state);
    state.error = JSON_Error_AbortedByHandler;
    state.errorLocation.byte = 0;
    state.errorLocation.line = 0;
    state.errorLocation.column = 0;
    state.errorLocation.depth = 0;
    state.inputEncoding = JSON_UTF8;
    s_failHandler = 1;
    if (CheckParserCreate(NULL, JSON_Success, &parser) &&
        CheckParserSetEncodingDetectedHandler(parser, &EncodingDetectedHandler, JSON_Success) &&
        CheckParserParse(parser, "    ", 4, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        !!(state.errorLocation.byte = 1) && /* hacky */
        !!(state.errorLocation.column = 1) &&

        CheckParserCreate(NULL, JSON_Success, &parser) &&
        CheckParserSetNullHandler(parser, &NullHandler, JSON_Success) &&
        CheckParserParse(parser, " null", 6, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetBooleanHandler(parser, &BooleanHandler, JSON_Success) &&
        CheckParserParse(parser, " true", 6, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetStringHandler(parser, &StringHandler, JSON_Success) &&
        CheckParserParse(parser, " \"\"", 3, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetNumberHandler(parser, &NumberHandler, JSON_Success) &&
        CheckParserParse(parser, " 7", 2, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetAllowSpecialNumbers(parser, JSON_True, JSON_Success) &&
        CheckParserSetSpecialNumberHandler(parser, &SpecialNumberHandler, JSON_Success) &&
        CheckParserParse(parser, " NaN", 4, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetStartObjectHandler(parser, &StartObjectHandler, JSON_Success) &&
        CheckParserParse(parser, " {}", 3, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetEndObjectHandler(parser, &EndObjectHandler, JSON_Success) &&
        CheckParserParse(parser, "{}", 2, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetStartArrayHandler(parser, &StartArrayHandler, JSON_Success) &&
        CheckParserParse(parser, " []", 3, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetEndArrayHandler(parser, &EndArrayHandler, JSON_Success) &&
        CheckParserParse(parser, "[]", 2, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        !!(state.errorLocation.depth = 1) && /* hacky! */

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetObjectMemberHandler(parser, &ObjectMemberHandler, JSON_Success) &&
        CheckParserParse(parser, "{\"x\":0}", 7, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckParserReset(parser, JSON_Success) &&
        CheckParserSetArrayItemHandler(parser, &ArrayItemHandler, JSON_Success) &&
        CheckParserParse(parser, "[0]", 3, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    s_failHandler = 0;
    JSON_Parser_Free(parser);
}

static void TestParserStringMallocFailure(void)
{
    int succeeded = 0;
    JSON_Parser parser = NULL;
    ParserState state;
    printf("Test parser string malloc failure ... ");
    InitParserState(&state);
    state.error = JSON_Error_OutOfMemory;
    state.inputEncoding = JSON_UTF8;
    if (CheckParserCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParserParse(parser, "\"", 1, JSON_False, JSON_Success))
    {
        s_failMalloc = 1;
        for (;;)
        {
            if (JSON_Parser_Parse(parser, "a", 1, JSON_False) == JSON_Failure)
            {
                break;
            }
        }
        JSON_Parser_GetErrorLocation(parser, &state.errorLocation);
        if (CheckParserState(parser, &state))
        {
            succeeded = 1;
        }
        s_failMalloc = 0;
    }
    if (succeeded)
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestParserStringReallocFailure(void)
{
    int succeeded = 0;
    JSON_Parser parser = NULL;
    ParserState state;
    printf("Test parser string realloc failure ... ");
    InitParserState(&state);
    state.error = JSON_Error_OutOfMemory;
    state.inputEncoding = JSON_UTF8;
    if (CheckParserCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParserParse(parser, "\"", 1, JSON_False, JSON_Success))
    {
        s_failRealloc = 1;
        for (;;)
        {
            if (JSON_Parser_Parse(parser, "a", 1, JSON_False) == JSON_Failure)
            {
                break;
            }
        }
        JSON_Parser_GetErrorLocation(parser, &state.errorLocation);
        if (CheckParserState(parser, &state))
        {
            succeeded = 1;
        }
        s_failRealloc = 0;
    }
    if (succeeded)
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestParserStackMallocFailure(void)
{
    int succeeded = 0;
    JSON_Parser parser = NULL;
    ParserState state;
    printf("Test parser stack malloc failure ... ");
    InitParserState(&state);
    state.error = JSON_Error_OutOfMemory;
    state.inputEncoding = JSON_UTF8;
    if (CheckParserCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &parser))
    {
        s_failMalloc = 1;
        for (;;)
        {
            if (JSON_Parser_Parse(parser, "{\"a\":", 5, JSON_False) == JSON_Failure)
            {
                break;
            }
        }
        JSON_Parser_GetErrorLocation(parser, &state.errorLocation);
        if (CheckParserState(parser, &state))
        {
            succeeded = 1;
        }
        s_failMalloc = 0;
    }
    if (succeeded)
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestParserStackReallocFailure(void)
{
    int succeeded = 0;
    JSON_Parser parser = NULL;
    ParserState state;
    printf("Test parser stack realloc failure ... ");
    InitParserState(&state);
    state.error = JSON_Error_OutOfMemory;
    state.inputEncoding = JSON_UTF8;
    if (CheckParserCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &parser))
    {
        s_failRealloc = 1;
        for (;;)
        {
            if (JSON_Parser_Parse(parser, "{\"a\":", 5, JSON_False) == JSON_Failure)
            {
                break;
            }
        }
        JSON_Parser_GetErrorLocation(parser, &state.errorLocation);
        if (CheckParserState(parser, &state))
        {
            succeeded = 1;
        }
        s_failRealloc = 0;
    }
    if (succeeded)
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestParserDuplicateMemberTrackingMallocFailure(void)
{
    int succeeded = 0;
    JSON_Parser parser = NULL;
    ParserState state;
    printf("Test parser duplicate member tracking malloc failure ... ");
    InitParserState(&state);
    state.error = JSON_Error_OutOfMemory;
    state.errorLocation.byte = 1;
    state.errorLocation.column = 1;
    state.inputEncoding = JSON_UTF8;
    if (CheckParserCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParserSetTrackObjectMembers(parser, JSON_True, JSON_Success))
    {
        s_failMalloc = 1;
        if (CheckParserParse(parser, "{\"a\":0}", 7, JSON_True, JSON_Failure) &&
            CheckParserState(parser, &state))
        {
            succeeded = 1;
        }
        s_failMalloc = 0;
    }
    if (succeeded)
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestParserMissing(void)
{
    ParserState state;
    ParserSettings settings;
    ParserHandlers handlers;
    JSON_Location errorLocation;
    printf("Test NULL parser instance ... ");
    InitParserState(&state);
    InitParserSettings(&settings);
    InitParserHandlers(&handlers);
    if (CheckParserState(NULL, &state) &&
        CheckParserSettings(NULL, &settings) &&
        CheckParserHandlers(NULL, &handlers) &&
        CheckParserFree(NULL, JSON_Failure) &&
        CheckParserReset(NULL, JSON_Failure) &&
        CheckParserSetUserData(NULL, (void*)1, JSON_Failure) &&
        CheckParserGetError(NULL, &errorLocation, JSON_Failure) &&
        CheckParserSetInputEncoding(NULL, JSON_UTF16LE, JSON_Failure) &&
        CheckParserSetStringEncoding(NULL, JSON_UTF16LE, JSON_Failure) &&
        CheckParserSetNumberEncoding(NULL, JSON_UTF16LE, JSON_Failure) &&
        CheckParserSetMaxStringLength(NULL, 128, JSON_Failure) &&
        CheckParserSetMaxNumberLength(NULL, 128, JSON_Failure) &&
        CheckParserSetEncodingDetectedHandler(NULL, &EncodingDetectedHandler, JSON_Failure) &&
        CheckParserSetNullHandler(NULL, &NullHandler, JSON_Failure) &&
        CheckParserSetBooleanHandler(NULL, &BooleanHandler, JSON_Failure) &&
        CheckParserSetStringHandler(NULL, &StringHandler, JSON_Failure) &&
        CheckParserSetNumberHandler(NULL, &NumberHandler, JSON_Failure) &&
        CheckParserSetSpecialNumberHandler(NULL, &SpecialNumberHandler, JSON_Failure) &&
        CheckParserSetStartObjectHandler(NULL, &StartObjectHandler, JSON_Failure) &&
        CheckParserSetEndObjectHandler(NULL, &EndObjectHandler, JSON_Failure) &&
        CheckParserSetObjectMemberHandler(NULL, &ObjectMemberHandler, JSON_Failure) &&
        CheckParserSetStartArrayHandler(NULL, &StartArrayHandler, JSON_Failure) &&
        CheckParserSetEndArrayHandler(NULL, &EndArrayHandler, JSON_Failure) &&
        CheckParserSetArrayItemHandler(NULL, &ArrayItemHandler, JSON_Failure) &&
        CheckParserParse(NULL, "7", 1, JSON_True, JSON_Failure))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
}

static void TestParserGetErrorLocationNullLocation(void)
{
    JSON_Parser parser = NULL;
    printf("Test parser get error location with NULL location ... ");
    if (CheckParserCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParserParse(parser, "!", 1, JSON_True, JSON_Failure) &&
        CheckParserGetError(parser, NULL, JSON_Failure))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestParserGetErrorLocationNoError(void)
{
    JSON_Parser parser = NULL;
    JSON_Location location = { 100, 200, 300, 400 };
    printf("Test parser get error location when no error occurred ... ");
    if (CheckParserCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParserParse(parser, "7", 1, JSON_True, JSON_Success) &&
        CheckParserGetError(parser, &location, JSON_Failure))
    {
        if (location.byte != 100 || location.line != 200 || location.column != 300 || location.depth != 400)
        {
            printf("FAILURE: JSON_Parser_GetErrorLocation() modified the location when it shouldn't have\n");
            s_failureCount++;
        }
        else
        {
            printf("OK\n");
        }
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestParserGetTokenLocationOutsideHandler(void)
{
    JSON_Parser parser = NULL;
    JSON_Location location = { 100, 200, 300, 400 };
    printf("Test parser get token location when not in a parse handler  ... ");
    if (CheckParserCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParserParse(parser, "7", 1, JSON_True, JSON_Success) &&
        CheckParserGetTokenLocation(parser, &location, JSON_Failure))
    {
        if (location.byte != 100 || location.line != 200 || location.column != 300 || location.depth != 400)
        {
            printf("FAILURE: JSON_Parser_GetTokenLocation() modified the location when it shouldn't have\n");
            s_failureCount++;
        }
        else
        {
            printf("OK\n");
        }
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestParserGetAfterTokenLocationOutsideHandler(void)
{
    JSON_Parser parser = NULL;
    JSON_Location location = { 100, 200, 300, 400 };
    printf("Test parser get after-token location when not in a parse handler  ... ");
    if (CheckParserCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParserParse(parser, "7", 1, JSON_True, JSON_Success) &&
        CheckParserGetAfterTokenLocation(parser, &location, JSON_Failure))
    {
        if (location.byte != 100 || location.line != 200 || location.column != 300 || location.depth != 400)
        {
            printf("FAILURE: JSON_Parser_GetAfterTokenLocation() modified the location when it shouldn't have\n");
            s_failureCount++;
        }
        else
        {
            printf("OK\n");
        }
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

#define PARSE_TEST(name, params, input, final, enc, output) { name, params, input, sizeof(input) - 1, final, JSON_##enc, output },

#define FINAL   JSON_True
#define PARTIAL JSON_False

static const ParseTest s_parseTests[] =
{

/* input encoding detection */

PARSE_TEST("infer input encoding from 0 bytes", Standard, "", FINAL, UnknownEncoding, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("infer input encoding from 1 byte (1)", Standard, "7", FINAL, UTF8, "u(8) #(7):0,0,0,0-1,0,1,0")
PARSE_TEST("infer input encoding from 1 byte (2)", Standard, " ", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):1,0,1,0")
PARSE_TEST("infer input encoding from 1 byte (3)", Standard, "\xFF", FINAL, UTF8, "u(8) !(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("infer input encoding from 2 bytes (1)", Standard, "{}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 }:1,0,1,0-2,0,2,0")
PARSE_TEST("infer input encoding from 2 bytes (2)", Standard, "7\x00", FINAL, UTF16LE, "u(16LE) #(7):0,0,0,0-2,0,1,0")
PARSE_TEST("infer input encoding from 2 bytes (3)", Standard, "\x00" "7", FINAL, UTF16BE, "u(16BE) #(7):0,0,0,0-2,0,1,0")
PARSE_TEST("infer input encoding from 2 bytes (4)", AllowBOM, "\xFF\xFE", FINAL, UTF16LE, "u(16LE) !(ExpectedMoreTokens):2,0,1,0")
PARSE_TEST("infer input encoding from 2 bytes (5)", AllowBOM, "\xFE\xFF", FINAL, UTF16BE, "u(16BE) !(ExpectedMoreTokens):2,0,1,0")
PARSE_TEST("infer input encoding from 2 bytes (6)", Standard, "\xFF\xFF", FINAL, UTF8, "u(8) !(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("infer input encoding from 3 bytes (1)", Standard, "{ }", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 }:2,0,2,0-3,0,3,0")
PARSE_TEST("infer input encoding from 3 bytes (2)", AllowBOM, "\xEF\xBB\xBF", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):3,0,1,0")
PARSE_TEST("infer input encoding from 3 bytes (3)", AllowBOM, "\xFF\xFF\xFF", FINAL, UTF8, "u(8) !(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("infer input encoding from 3 bytes (4)", Standard, "\xFF\xFF\xFF", FINAL, UTF8, "u(8) !(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("infer input encoding from 4 bytes (1)", Standard, "1234", FINAL, UTF8, "u(8) #(1234):0,0,0,0-4,0,4,0")
PARSE_TEST("infer input encoding from 4 bytes (2)", Standard, "   7", FINAL, UTF8, "u(8) #(7):3,0,3,0-4,0,4,0")
PARSE_TEST("infer input encoding from 4 bytes (3)", Standard, "\x00 \x00" "7", FINAL, UTF16BE, "u(16BE) #(7):2,0,1,0-4,0,2,0")
PARSE_TEST("infer input encoding from 4 bytes (4)", Standard, " \x00" "7\x00", FINAL, UTF16LE, "u(16LE) #(7):2,0,1,0-4,0,2,0")
PARSE_TEST("infer input encoding from 4 bytes (5)", Standard, "\x00\x00\x00" "7", FINAL, UTF32BE, "u(32BE) #(7):0,0,0,0-4,0,1,0")
PARSE_TEST("infer input encoding from 4 bytes (6)", Standard, "7\x00\x00\x00", FINAL, UTF32LE, "u(32LE) #(7):0,0,0,0-4,0,1,0")
PARSE_TEST("no input encoding starts <00 00 00 00>", Standard, "\x00\x00\x00\x00", FINAL, UnknownEncoding, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("no input encoding starts <nz 00 00 nz>", Standard, " \x00\x00 ", FINAL, UnknownEncoding, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 BOM not allowed", Standard, "\xEF\xBB\xBF" "7", PARTIAL, UTF8, "u(8) !(BOMNotAllowed):0,0,0,0")
PARSE_TEST("UTF-16LE BOM not allowed", Standard, "\xFF\xFE" "7\x00", PARTIAL, UTF16LE, "u(16LE) !(BOMNotAllowed):0,0,0,0")
PARSE_TEST("UTF-16BE BOM not allowed", Standard, "\xFE\xFF\x00" "7", PARTIAL, UTF16BE, "u(16BE) !(BOMNotAllowed):0,0,0,0")
PARSE_TEST("UTF-32LE BOM not allowed", Standard, "\xFF\xFE\x00\x00" "7\x00\x00\x00", PARTIAL, UTF32LE, "u(32LE) !(BOMNotAllowed):0,0,0,0")
PARSE_TEST("UTF-32BE BOM not allowed", Standard, "\x00\x00\xFE\xFF\x00\x00\x00" "7", PARTIAL, UTF32BE, "u(32BE) !(BOMNotAllowed):0,0,0,0")
PARSE_TEST("UTF-8 BOM allowed", AllowBOM, "\xEF\xBB\xBF" "7", FINAL, UTF8, "u(8) #(7):3,0,1,0-4,0,2,0")
PARSE_TEST("UTF-16LE BOM allowed", AllowBOM, "\xFF\xFE" "7\x00", FINAL, UTF16LE, "u(16LE) #(7):2,0,1,0-4,0,2,0")
PARSE_TEST("UTF-16BE BOM allowed", AllowBOM, "\xFE\xFF\x00" "7", FINAL, UTF16BE, "u(16BE) #(7):2,0,1,0-4,0,2,0")
PARSE_TEST("UTF-32LE BOM allowed", AllowBOM, "\xFF\xFE\x00\x00" "7\x00\x00\x00", FINAL, UTF32LE, "u(32LE) #(7):4,0,1,0-8,0,2,0")
PARSE_TEST("UTF-32BE BOM allowed", AllowBOM, "\x00\x00\xFE\xFF\x00\x00\x00" "7", FINAL, UTF32BE, "u(32BE) #(7):4,0,1,0-8,0,2,0")
PARSE_TEST("UTF-8 BOM allowed but no content", AllowBOM, "\xEF\xBB\xBF", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):3,0,1,0")
PARSE_TEST("UTF-16LE BOM allowed but no content", AllowBOM, "\xFF\xFE", FINAL, UTF16LE, "u(16LE) !(ExpectedMoreTokens):2,0,1,0")
PARSE_TEST("UTF-16BE BOM allowed but no content", AllowBOM, "\xFE\xFF", FINAL, UTF16BE, "u(16BE) !(ExpectedMoreTokens):2,0,1,0")
PARSE_TEST("UTF-32LE BOM allowed but no content", AllowBOM, "\xFF\xFE\x00\x00", FINAL, UTF32LE, "u(32LE) !(ExpectedMoreTokens):4,0,1,0")
PARSE_TEST("UTF-32BE BOM allowed but no content", AllowBOM, "\x00\x00\xFE\xFF", FINAL, UTF32BE, "u(32BE) !(ExpectedMoreTokens):4,0,1,0")

/* invalid input encoding sequences */

PARSE_TEST("UTF-8 truncated sequence (1)", UTF8In, "\xC2", FINAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 truncated sequence (2)", UTF8In, "\xE0", FINAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 truncated sequence (3)", UTF8In, "\xE0\xBF", FINAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 truncated sequence (4)", UTF8In, "\xF0\xBF", FINAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 truncated sequence (5)", UTF8In, "\xF0\xBF\xBF", FINAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 overlong 2-byte sequence not allowed (1)", UTF8In, "\xC0", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 overlong 2-byte sequence not allowed (2)", UTF8In, "\xC1", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 overlong 3-byte sequence not allowed (1)", UTF8In, "\xE0\x80", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 overlong 3-byte sequence not allowed (2)", UTF8In, "\xE0\x9F", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 encoded surrogate not allowed (1)", UTF8In, "\xED\xA0", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 encoded surrogate not allowed (2)", UTF8In, "\xED\xBF", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 overlong 4-byte sequence not allowed (1)", UTF8In, "\xF0\x80", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 overlong 4-byte sequence not allowed (2)", UTF8In, "\xF0\x8F", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 encoded out-of-range codepoint not allowed (1)", UTF8In, "\xF4\x90", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid leading byte not allowed (1)", UTF8In, "\x80", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid leading byte not allowed (2)", UTF8In, "\xBF", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid leading byte not allowed (3)", UTF8In, "\xF5", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid leading byte not allowed (4)", UTF8In, "\xFF", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid continuation byte not allowed (1)", UTF8In, "\xC2\x7F", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid continuation byte not allowed (2)", UTF8In, "\xC2\xC0", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid continuation byte not allowed (3)", UTF8In, "\xE1\x7F", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid continuation byte not allowed (4)", UTF8In, "\xE1\xC0", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid continuation byte not allowed (5)", UTF8In, "\xE1\xBF\x7F", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid continuation byte not allowed (6)", UTF8In, "\xE1\xBF\xC0", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid continuation byte not allowed (7)", UTF8In, "\xF1\x7F", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid continuation byte not allowed (8)", UTF8In, "\xF1\xC0", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid continuation byte not allowed (9)", UTF8In, "\xF1\xBF\x7F", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid continuation byte not allowed (10)", UTF8In, "\xF1\xBF\xC0", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid continuation byte not allowed (11)", UTF8In, "\xF1\xBF\xBF\x7F", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 invalid continuation byte not allowed (12)", UTF8In, "\xF1\xBF\xBF\xC0", PARTIAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-16LE truncated sequence", UTF16LEIn | UTF16LEOut, " ", FINAL, UTF16LE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-16LE standalone trailing surrogate not allowed (1)", UTF16LEIn | UTF16LEOut, "\x00\xDC", PARTIAL, UTF16LE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-16LE standalone trailing surrogate not allowed (2)", UTF16LEIn | UTF16LEOut, "\xFF\xDF", PARTIAL, UTF16LE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-16LE standalone leading surrogate not allowed (1)", UTF16LEIn | UTF16LEOut, "\x00\xD8\x00_", PARTIAL, UTF16LE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-16LE standalone leading surrogate not allowed (2)", UTF16LEIn | UTF16LEOut, "\xFF\xDB\x00_", PARTIAL, UTF16LE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-16LE standalone leading surrogate not allowed (3)", UTF16LEIn | UTF16LEOut, "\xFF\xDB_", FINAL, UTF16LE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-16BE truncated sequence", UTF16BEIn | UTF16BEOut, "\x00", FINAL, UTF16BE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-16BE standalone trailing surrogate not allowed (1)", UTF16BEIn | UTF16BEOut, "\xDC\x00", PARTIAL, UTF16BE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-16BE standalone trailing surrogate not allowed (2)", UTF16BEIn | UTF16BEOut, "\xDF\xFF", PARTIAL, UTF16BE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-16BE standalone leading surrogate not allowed (1)", UTF16BEIn | UTF16BEOut, "\xD8\x00\x00_", PARTIAL, UTF16BE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-16BE standalone leading surrogate not allowed (2)", UTF16BEIn | UTF16BEOut, "\xDB\xFF\x00_", PARTIAL, UTF16BE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-16BE standalone leading surrogate not allowed (3)", UTF16BEIn | UTF16BEOut, "\xDB\xFF", FINAL, UTF16BE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-32LE truncated sequence (1)", UTF32LEIn | UTF32LEOut, " ", FINAL, UTF32LE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-32LE truncated sequence (2)", UTF32LEIn | UTF32LEOut, " \x00", FINAL, UTF32LE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-32LE truncated sequence (3)", UTF32LEIn | UTF32LEOut, " \x00\x00", FINAL, UTF32LE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-32LE encoded surrogate not allowed (1)", UTF32LEIn | UTF32LEOut, "\x00\xD8\x00\x00", PARTIAL, UTF32LE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-32LE encoded surrogate not allowed (2)", UTF32LEIn | UTF32LEOut, "\x00\xDF\x00\x00", PARTIAL, UTF32LE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-32LE encoded out-of-range codepoint not allowed (1)", UTF32LEIn | UTF32LEOut, "\x00\x00\x11\x00", PARTIAL, UTF32LE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-32LE encoded out-of-range codepoint not allowed (2)", UTF32LEIn | UTF32LEOut, "\xFF\xFF\xFF\xFF", PARTIAL, UTF32LE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-32BE truncated sequence (1)", UTF32BEIn | UTF32BEOut, "\x00", FINAL, UTF32BE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-32BE truncated sequence (2)", UTF32BEIn | UTF32BEOut, "\x00\x00", FINAL, UTF32BE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-32BE truncated sequence (3)", UTF32BEIn | UTF32BEOut, "\x00\x00\x00", FINAL, UTF32BE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-32BE encoded surrogate not allowed (1)", UTF32BEIn | UTF32BEOut, "\x00\x00\xD8\x00", PARTIAL, UTF32BE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-32BE encoded surrogate not allowed (2)", UTF32BEIn | UTF32BEOut, "\x00\x00\xDF\xFF", PARTIAL, UTF32BE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-32BE encoded out-of-range codepoint not allowed (1)", UTF32BEIn | UTF32BEOut, "\x00\x11\x00\x00", PARTIAL, UTF32BE, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-32BE encoded out-of-range codepoint not allowed (2)", UTF32BEIn | UTF32BEOut, "\xFF\xFF\xFF\xFF", PARTIAL, UTF32BE, "!(InvalidEncodingSequence):0,0,0,0")

/* replace invalid input encoding sequences */

PARSE_TEST("replace UTF-8 truncated 2-byte sequence (1)", ReplaceInvalidEncodingSequences, "\"abc\xC2\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0")
PARSE_TEST("replace UTF-8 truncated 2-byte sequence (2)", ReplaceInvalidEncodingSequences, "\"abc\xC2\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 truncated 3-byte sequence (1)", ReplaceInvalidEncodingSequences, "\"abc\xE0\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0")
PARSE_TEST("replace UTF-8 truncated 3-byte sequence (2)", ReplaceInvalidEncodingSequences, "\"abc\xE0\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 truncated 3-byte sequence (3)", ReplaceInvalidEncodingSequences, "\"abc\xE0\xBF\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-7,0,6,0")
PARSE_TEST("replace UTF-8 truncated 3-byte sequence (4)", ReplaceInvalidEncodingSequences, "\"abc\xE0\xBF\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-7,0,6,0 !(UnknownToken):7,0,6,0")
PARSE_TEST("replace UTF-8 truncated 4-byte sequence (1)", ReplaceInvalidEncodingSequences, "\"abc\xF0\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0")
PARSE_TEST("replace UTF-8 truncated 4-byte sequence (2)", ReplaceInvalidEncodingSequences, "\"abc\xF0\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 truncated 4-byte sequence (3)", ReplaceInvalidEncodingSequences, "\"abc\xF0\xBF\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-7,0,6,0")
PARSE_TEST("replace UTF-8 truncated 4-byte sequence (4)", ReplaceInvalidEncodingSequences, "\"abc\xF0\xBF\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-7,0,6,0 !(UnknownToken):7,0,6,0")
PARSE_TEST("replace UTF-8 truncated 4-byte sequence (5)", ReplaceInvalidEncodingSequences, "\"abc\xF0\xBF\xBF\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-8,0,6,0")
PARSE_TEST("replace UTF-8 truncated 4-byte sequence (6)", ReplaceInvalidEncodingSequences, "\"abc\xF0\xBF\xBF\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-8,0,6,0 !(UnknownToken):8,0,6,0")
PARSE_TEST("replace UTF-8 overlong 2-byte sequence (1)", ReplaceInvalidEncodingSequences, "\"abc\xC0\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0")
PARSE_TEST("replace UTF-8 overlong 2-byte sequence (2)", ReplaceInvalidEncodingSequences, "\"abc\xC0\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 overlong 2-byte sequence (3)", ReplaceInvalidEncodingSequences, "\"abc\xC1\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0")
PARSE_TEST("replace UTF-8 overlong 2-byte sequence (4)", ReplaceInvalidEncodingSequences, "\"abc\xC1\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 overlong 3-byte sequence (1)", ReplaceInvalidEncodingSequences, "\"abc\xE0\x80\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0")
PARSE_TEST("replace UTF-8 overlong 3-byte sequence (2)", ReplaceInvalidEncodingSequences, "\"abc\xE0\x80\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 overlong 3-byte sequence (3)", ReplaceInvalidEncodingSequences, "\"abc\xE0\x9F\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0")
PARSE_TEST("replace UTF-8 overlong 3-byte sequence (4)", ReplaceInvalidEncodingSequences, "\"abc\xE0\x9F\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 encoded surrogate (1)", ReplaceInvalidEncodingSequences, "\"abc\xED\xA0\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0")
PARSE_TEST("replace UTF-8 encoded surrogate (2)", ReplaceInvalidEncodingSequences, "\"abc\xED\xA0\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 encoded surrogate (3)", ReplaceInvalidEncodingSequences, "\"abc\xED\xBF\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0")
PARSE_TEST("replace UTF-8 encoded surrogate (4)", ReplaceInvalidEncodingSequences, "\"abc\xED\xBF\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 overlong 4-byte sequence (1)", ReplaceInvalidEncodingSequences, "\"abc\xF0\x80\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0")
PARSE_TEST("replace UTF-8 overlong 4-byte sequence (2)", ReplaceInvalidEncodingSequences, "\"abc\xF0\x80\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 overlong 4-byte sequence (3)", ReplaceInvalidEncodingSequences, "\"abc\xF0\x8F\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0")
PARSE_TEST("replace UTF-8 overlong 4-byte sequence (4)", ReplaceInvalidEncodingSequences, "\"abc\xF0\x8F\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 encoded out-of-range codepoint (1)", ReplaceInvalidEncodingSequences, "\"abc\xF4\x90\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0")
PARSE_TEST("replace UTF-8 encoded out-of-range codepoint (2)", ReplaceInvalidEncodingSequences, "\"abc\xF4\x90\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 invalid leading byte (1)", ReplaceInvalidEncodingSequences, "\"abc\x80\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0")
PARSE_TEST("replace UTF-8 invalid leading byte (2)", ReplaceInvalidEncodingSequences, "\"abc\x80\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 invalid leading byte (3)", ReplaceInvalidEncodingSequences, "\"abc\xBF\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0")
PARSE_TEST("replace UTF-8 invalid leading byte (4)", ReplaceInvalidEncodingSequences, "\"abc\xBF\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 invalid leading byte (5)", ReplaceInvalidEncodingSequences, "\"abc\xF5\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0")
PARSE_TEST("replace UTF-8 invalid leading byte (6)", ReplaceInvalidEncodingSequences, "\"abc\xF5\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 invalid leading byte (7)", ReplaceInvalidEncodingSequences, "\"abc\xFF\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0")
PARSE_TEST("replace UTF-8 invalid leading byte (8)", ReplaceInvalidEncodingSequences, "\"abc\xFF\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD>):0,0,0,0-6,0,6,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (1)", ReplaceInvalidEncodingSequences, "\"abc\xC2\x7F\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><7F>):0,0,0,0-7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (2)", ReplaceInvalidEncodingSequences, "\"abc\xC2\x7F\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><7F>):0,0,0,0-7,0,7,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (3)", ReplaceInvalidEncodingSequences, "\"abc\xC2\xC0\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (4)", ReplaceInvalidEncodingSequences, "\"abc\xC2\xC0\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (5)", ReplaceInvalidEncodingSequences, "\"abc\xE1\x7F\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><7F>):0,0,0,0-7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (6)", ReplaceInvalidEncodingSequences, "\"abc\xE1\x7F\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><7F>):0,0,0,0-7,0,7,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (7)", ReplaceInvalidEncodingSequences, "\"abc\xE1\xC0\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (8)", ReplaceInvalidEncodingSequences, "\"abc\xE1\xC0\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (9)", ReplaceInvalidEncodingSequences, "\"abc\xE1\xBF\x7F\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><7F>):0,0,0,0-8,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (10)", ReplaceInvalidEncodingSequences, "\"abc\xE1\xBF\x7F\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><7F>):0,0,0,0-8,0,7,0 !(UnknownToken):8,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (11)", ReplaceInvalidEncodingSequences, "\"abc\xE1\xBF\xC0\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-8,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (12)", ReplaceInvalidEncodingSequences, "\"abc\xE1\xBF\xC0\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-8,0,7,0 !(UnknownToken):8,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (13)", ReplaceInvalidEncodingSequences, "\"abc\xF1\x7F\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><7F>):0,0,0,0-7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (14)", ReplaceInvalidEncodingSequences, "\"abc\xF1\x7F\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><7F>):0,0,0,0-7,0,7,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (15)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xC0\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (16)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xC0\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-7,0,7,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (17)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\x7F\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><7F>):0,0,0,0-8,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (18)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\x7F\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><7F>):0,0,0,0-8,0,7,0 !(UnknownToken):8,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (19)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\xC0\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-8,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (20)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\xC0\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-8,0,7,0 !(UnknownToken):8,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (21)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\xBF\x7F\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><7F>):0,0,0,0-9,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (22)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\xBF\x7F\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><7F>):0,0,0,0-9,0,7,0 !(UnknownToken):9,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (23)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\xBF\xC0\"", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-9,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (24)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\xBF\xC0\"!", FINAL, UTF8, "u(8) s(ar abc<EF><BF><BD><EF><BF><BD>):0,0,0,0-9,0,7,0 !(UnknownToken):9,0,7,0")
PARSE_TEST("Unicode 5.2.0 replacement example (1)", ReplaceInvalidEncodingSequences, "   \"\x61\xF1\x80\x80\xE1\x80\xC2\x62\x80\x63\x80\xBF\x64\"", FINAL, UTF8, "u(8) s(ar a<EF><BF><BD><EF><BF><BD><EF><BF><BD>b<EF><BF><BD>c<EF><BF><BD><EF><BF><BD>d):3,0,3,0-18,0,15,0")
PARSE_TEST("Unicode 5.2.0 replacement example (2)", ReplaceInvalidEncodingSequences, "   \"\x61\xF1\x80\x80\xE1\x80\xC2\x62\x80\x63\x80\xBF\x64\"!", FINAL, UTF8, "u(8) s(ar a<EF><BF><BD><EF><BF><BD><EF><BF><BD>b<EF><BF><BD>c<EF><BF><BD><EF><BF><BD>d):3,0,3,0-18,0,15,0 !(UnknownToken):18,0,15,0")
PARSE_TEST("replace UTF-16LE standalone trailing surrogate (1)", ReplaceInvalidEncodingSequences, "\"\x00" "_\x00" "\x00\xDC" "\"\x00", FINAL, UTF16LE, "u(16LE) s(ar <5F><EF><BF><BD>):0,0,0,0-8,0,4,0")
PARSE_TEST("replace UTF-16LE standalone trailing surrogate (2)", ReplaceInvalidEncodingSequences, "\"\x00" "_\x00" "\x00\xDC" "\"\x00" "!\x00", FINAL, UTF16LE, "u(16LE) s(ar <5F><EF><BF><BD>):0,0,0,0-8,0,4,0 !(UnknownToken):8,0,4,0")
PARSE_TEST("replace UTF-16LE standalone trailing surrogate (3)", ReplaceInvalidEncodingSequences, "\"\x00" "_\x00" "\xFF\xDF" "\"\x00", FINAL, UTF16LE, "u(16LE) s(ar <5F><EF><BF><BD>):0,0,0,0-8,0,4,0")
PARSE_TEST("replace UTF-16LE standalone trailing surrogate (4)", ReplaceInvalidEncodingSequences, "\"\x00" "_\x00" "\xFF\xDF" "\"\x00" "!\x00", FINAL, UTF16LE, "u(16LE) s(ar <5F><EF><BF><BD>):0,0,0,0-8,0,4,0 !(UnknownToken):8,0,4,0")
PARSE_TEST("replace UTF-16LE standalone leading surrogate (1)", ReplaceInvalidEncodingSequences,  "\"\x00" "_\x00" "\x00\xD8" "_\x00" "\"\x00", FINAL, UTF16LE, "u(16LE) s(ar <5F><EF><BF><BD><5F>):0,0,0,0-10,0,5,0")
PARSE_TEST("replace UTF-16LE standalone leading surrogate (2)", ReplaceInvalidEncodingSequences,  "\"\x00" "_\x00" "\x00\xD8" "_\x00" "\"\x00" "!\x00", FINAL, UTF16LE, "u(16LE) s(ar <5F><EF><BF><BD><5F>):0,0,0,0-10,0,5,0 !(UnknownToken):10,0,5,0")
PARSE_TEST("replace UTF-16LE standalone leading surrogate (3)", ReplaceInvalidEncodingSequences,  "\"\x00" "_\x00" "\xFF\xDB" "_\x00" "\"\x00", FINAL, UTF16LE, "u(16LE) s(ar <5F><EF><BF><BD><5F>):0,0,0,0-10,0,5,0")
PARSE_TEST("replace UTF-16LE standalone leading surrogate (4)", ReplaceInvalidEncodingSequences,  "\"\x00" "_\x00" "\xFF\xDB" "_\x00" "\"\x00" "!\x00", FINAL, UTF16LE, "u(16LE) s(ar <5F><EF><BF><BD><5F>):0,0,0,0-10,0,5,0 !(UnknownToken):10,0,5,0")
PARSE_TEST("replace UTF-16BE standalone trailing surrogate (1)", ReplaceInvalidEncodingSequences, "\x00\"" "\x00_" "\xDC\x00" "\x00\"", FINAL, UTF16BE, "u(16BE) s(ar <5F><EF><BF><BD>):0,0,0,0-8,0,4,0")
PARSE_TEST("replace UTF-16BE standalone trailing surrogate (2)", ReplaceInvalidEncodingSequences, "\x00\"" "\x00_" "\xDC\x00" "\x00\"" "\x00!", FINAL, UTF16BE, "u(16BE) s(ar <5F><EF><BF><BD>):0,0,0,0-8,0,4,0 !(UnknownToken):8,0,4,0")
PARSE_TEST("replace UTF-16BE standalone trailing surrogate (3)", ReplaceInvalidEncodingSequences, "\x00\"" "\x00_" "\xDF\xFF" "\x00\"", FINAL, UTF16BE, "u(16BE) s(ar <5F><EF><BF><BD>):0,0,0,0-8,0,4,0")
PARSE_TEST("replace UTF-16BE standalone trailing surrogate (4)", ReplaceInvalidEncodingSequences, "\x00\"" "\x00_" "\xDF\xFF" "\x00\"" "\x00!", FINAL, UTF16BE, "u(16BE) s(ar <5F><EF><BF><BD>):0,0,0,0-8,0,4,0 !(UnknownToken):8,0,4,0")
PARSE_TEST("replace UTF-16BE standalone leading surrogate (1)", ReplaceInvalidEncodingSequences,  "\x00\"" "\x00_" "\xD8\x00" "\x00_" "\x00\"", FINAL, UTF16BE, "u(16BE) s(ar <5F><EF><BF><BD><5F>):0,0,0,0-10,0,5,0")
PARSE_TEST("replace UTF-16BE standalone leading surrogate (2)", ReplaceInvalidEncodingSequences,  "\x00\"" "\x00_" "\xD8\x00" "\x00_" "\x00\"" "!\x00", FINAL, UTF16BE, "u(16BE) s(ar <5F><EF><BF><BD><5F>):0,0,0,0-10,0,5,0 !(UnknownToken):10,0,5,0")
PARSE_TEST("replace UTF-16BE standalone leading surrogate (3)", ReplaceInvalidEncodingSequences,  "\x00\"" "\x00_" "\xDB\xFF" "\x00_" "\x00\"", FINAL, UTF16BE, "u(16BE) s(ar <5F><EF><BF><BD><5F>):0,0,0,0-10,0,5,0")
PARSE_TEST("replace UTF-16BE standalone leading surrogate (4)", ReplaceInvalidEncodingSequences,  "\x00\"" "\x00_" "\xDB\xFF" "\x00_" "\x00\"" "!\x00", FINAL, UTF16BE, "u(16BE) s(ar <5F><EF><BF><BD><5F>):0,0,0,0-10,0,5,0 !(UnknownToken):10,0,5,0")
PARSE_TEST("replace UTF-32LE encoded surrogate (1)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\x00\xD8\x00\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "u(32LE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0")
PARSE_TEST("replace UTF-32LE encoded surrogate (2)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\x00\xD8\x00\x00" "\"\x00\x00\x00" "!\x00\x00\x00", FINAL, UTF32LE, "u(32LE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0 !(UnknownToken):12,0,3,0")
PARSE_TEST("replace UTF-32LE encoded surrogate (3)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\xFF\xDF\x00\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "u(32LE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0")
PARSE_TEST("replace UTF-32LE encoded surrogate (4)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\xFF\xDF\x00\x00" "\"\x00\x00\x00" "!\x00\x00\x00", FINAL, UTF32LE, "u(32LE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0 !(UnknownToken):12,0,3,0")
PARSE_TEST("replace UTF-32LE encoded out-of-range codepoint (1)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\x00\x00\x11\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "u(32LE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0")
PARSE_TEST("replace UTF-32LE encoded out-of-range codepoint (2)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\x00\x00\x11\x00" "\"\x00\x00\x00" "!\x00\x00\x00", FINAL, UTF32LE, "u(32LE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0 !(UnknownToken):12,0,3,0")
PARSE_TEST("replace UTF-32LE encoded out-of-range codepoint (3)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\x00\x00\x00\x01" "\"\x00\x00\x00", FINAL, UTF32LE, "u(32LE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0")
PARSE_TEST("replace UTF-32LE encoded out-of-range codepoint (4)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\x00\x00\x00\x01" "\"\x00\x00\x00" "!\x00\x00\x00", FINAL, UTF32LE, "u(32LE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0 !(UnknownToken):12,0,3,0")
PARSE_TEST("replace UTF-32BE encoded surrogate (1)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x00\x00\xD8\x00" "\x00\x00\x00\"", FINAL, UTF32BE, "u(32BE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0")
PARSE_TEST("replace UTF-32BE encoded surrogate (2)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x00\x00\xD8\x00" "\x00\x00\x00\"" "\x00\x00\x00!", FINAL, UTF32BE, "u(32BE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0 !(UnknownToken):12,0,3,0")
PARSE_TEST("replace UTF-32BE encoded surrogate (3)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x00\x00\xDF\xFF" "\x00\x00\x00\"", FINAL, UTF32BE, "u(32BE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0")
PARSE_TEST("replace UTF-32BE encoded surrogate (4)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x00\x00\xDF\xFF" "\x00\x00\x00\"" "\x00\x00\x00!", FINAL, UTF32BE, "u(32BE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0 !(UnknownToken):12,0,3,0")
PARSE_TEST("replace UTF-32BE encoded out-of-range codepoint (1)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x00\x11\x00\x00" "\x00\x00\x00\"", FINAL, UTF32BE, "u(32BE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0")
PARSE_TEST("replace UTF-32BE encoded out-of-range codepoint (2)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x00\x11\x00\x00" "\x00\x00\x00\"" "\x00\x00\x00!", FINAL, UTF32BE, "u(32BE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0 !(UnknownToken):12,0,3,0")
PARSE_TEST("replace UTF-32BE encoded out-of-range codepoint (3)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x01\x00\x00\x00" "\x00\x00\x00\"", FINAL, UTF32BE, "u(32BE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0")
PARSE_TEST("replace UTF-32BE encoded out-of-range codepoint (4)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x01\x00\x00\x00" "\x00\x00\x00\"" "\x00\x00\x00!", FINAL, UTF32BE, "u(32BE) s(ar <EF><BF><BD>):0,0,0,0-12,0,3,0 !(UnknownToken):12,0,3,0")

/* general */

PARSE_TEST("no input bytes (partial)", Standard, "", PARTIAL, UnknownEncoding, "")
PARSE_TEST("no input bytes", Standard, "", FINAL, UnknownEncoding, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("all whitespace (partial) (1)", Standard, " ", PARTIAL, UnknownEncoding, "")
PARSE_TEST("all whitespace (partial) (2)", Standard, "\t", PARTIAL, UnknownEncoding, "")
PARSE_TEST("all whitespace (partial) (3)", Standard, "\r\n", PARTIAL, UnknownEncoding, "")
PARSE_TEST("all whitespace (partial) (4)", Standard, "\r\n\n\r ", PARTIAL, UTF8, "u(8)")
PARSE_TEST("all whitespace (1)", Standard, " ", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):1,0,1,0")
PARSE_TEST("all whitespace (2)", Standard, "\t", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):1,0,1,0")
PARSE_TEST("all whitespace (3)", Standard, "\r\n", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):2,1,0,0")
PARSE_TEST("all whitespace (4)", Standard, "\r\n\n\r ", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):5,3,1,0")
PARSE_TEST("trailing garbage (1)", Standard, "7 !", FINAL, UTF8, "u(8) #(7):0,0,0,0-1,0,1,0 !(UnknownToken):2,0,2,0")
PARSE_TEST("trailing garbage (2)", Standard, "7 {", FINAL, UTF8, "u(8) #(7):0,0,0,0-1,0,1,0 !(UnexpectedToken):2,0,2,0")
PARSE_TEST("trailing garbage (3)", Standard, "7 \xC0", FINAL, UTF8, "u(8) #(7):0,0,0,0-1,0,1,0 !(InvalidEncodingSequence):2,0,2,0")
PARSE_TEST("trailing garbage (4)", Standard, "7 \xC2", FINAL, UTF8, "u(8) #(7):0,0,0,0-1,0,1,0 !(InvalidEncodingSequence):2,0,2,0")
PARSE_TEST("trailing garbage (5)", Standard, "7 [", FINAL, UTF8, "u(8) #(7):0,0,0,0-1,0,1,0 !(UnexpectedToken):2,0,2,0")
PARSE_TEST("trailing garbage (6)", Standard, "7 ,", FINAL, UTF8, "u(8) #(7):0,0,0,0-1,0,1,0 !(UnexpectedToken):2,0,2,0")
PARSE_TEST("trailing garbage (7)", Standard, "7 8", FINAL, UTF8, "u(8) #(7):0,0,0,0-1,0,1,0 !(UnexpectedToken):2,0,2,0")
PARSE_TEST("trailing garbage (8)", Standard, "7 \"", FINAL, UTF8, "u(8) #(7):0,0,0,0-1,0,1,0 !(IncompleteToken):2,0,2,0")

/* null */

PARSE_TEST("null (1)", Standard, "null", FINAL, UTF8, "u(8) n:0,0,0,0-4,0,4,0")
PARSE_TEST("null (2)", Standard, " null ", FINAL, UTF8, "u(8) n:1,0,1,0-5,0,5,0")
PARSE_TEST("n is not a literal", Standard, "n ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("nu is not a literal", Standard, "nu ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("nul is not a literal", Standard, "nul ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("nullx is not a literal", Standard, "nullx", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("null0 is not a literal", Standard, "null0", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("null_ is not a literal", Standard, "null_", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("nullX is not a literal", Standard, "nullX", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("NULL is not a literal", Standard, "NULL", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("null truncated after n", Standard, "n", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("null truncated after nu", Standard, "nu", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("null truncated after nul", Standard, "nul", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")

/* true */

PARSE_TEST("true (1)", Standard, "true", FINAL, UTF8, "u(8) t:0,0,0,0-4,0,4,0")
PARSE_TEST("true (2)", Standard, " true ", FINAL, UTF8, "u(8) t:1,0,1,0-5,0,5,0")
PARSE_TEST("t is not a literal", Standard, "t ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("tr is not a literal", Standard, "tr ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("tru is not a literal", Standard, "tru ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("trux is not a literal", Standard, "trux", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("true0 is not a literal", Standard, "true0", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("true_ is not a literal", Standard, "true__", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("trueX is not a literal", Standard, "trueX", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("TRUE is not a literal", Standard, "TRUE", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("true truncated after t", Standard, "t", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("true truncated after tr", Standard, "tr", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("true truncated after tru", Standard, "tru", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")

/* false */

PARSE_TEST("false (1)", Standard, "false", FINAL, UTF8, "u(8) f:0,0,0,0-5,0,5,0")
PARSE_TEST("false (2)", Standard, " false ", FINAL, UTF8, "u(8) f:1,0,1,0-6,0,6,0")
PARSE_TEST("f is not a literal", Standard, "f ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("fa is not a literal", Standard, "fa ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("fal is not a literal", Standard, "fal ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("falx is not a literal", Standard, "falx", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("fals is not a literal", Standard, "fals", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("false0 is not a literal", Standard, "false0", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("false_ is not a literal", Standard, "false_", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("falseX is not a literal", Standard, "falseX", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("FALSE is not a literal", Standard, "FALSE", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("false truncated after f", Standard, "f", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("false truncated after fa", Standard, "fa", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("false truncated after fal", Standard, "fal", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("false truncated after fals", Standard, "fals", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")

/* NaN */

PARSE_TEST("NaN (1)", AllowSpecialNumbers, "NaN", FINAL, UTF8, "u(8) #(NaN):0,0,0,0-3,0,3,0")
PARSE_TEST("NaN (2)", AllowSpecialNumbers, " NaN ", FINAL, UTF8, "u(8) #(NaN):1,0,1,0-4,0,4,0")
PARSE_TEST("N is not a literal", AllowSpecialNumbers, "N ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Na is not a literal", AllowSpecialNumbers, "Na ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Nax is not a literal", AllowSpecialNumbers, "Nax", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Na0 is not a literal", AllowSpecialNumbers, "NaN0", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("NaN_ is not a literal", AllowSpecialNumbers, "NaN_", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("NaNX is not a literal", AllowSpecialNumbers, "NaNX", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("NAN is not a literal", AllowSpecialNumbers, "NAN", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("NaN truncated after N", AllowSpecialNumbers, "N", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("NaN truncated after Na", AllowSpecialNumbers, "Na", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("NaN not allowed", Standard, "NaN", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")

/* Infinity */

PARSE_TEST("Infinity (1)", AllowSpecialNumbers, "Infinity", FINAL, UTF8, "u(8) #(Infinity):0,0,0,0-8,0,8,0")
PARSE_TEST("Infinity (2)", AllowSpecialNumbers, " Infinity ", FINAL, UTF8, "u(8) #(Infinity):1,0,1,0-9,0,9,0")
PARSE_TEST("I is not a literal", AllowSpecialNumbers, "I ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("In is not a literal", AllowSpecialNumbers, "In ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Inf is not a literal", AllowSpecialNumbers, "Inf ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infi is not a literal", AllowSpecialNumbers, "Infi ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infin is not a literal", AllowSpecialNumbers, "Infin ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infini is not a literal", AllowSpecialNumbers, "Infini ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infinit is not a literal", AllowSpecialNumbers, "Infinit ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infinitx is not a literal", AllowSpecialNumbers, "Infinitx", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infinit0 is not a literal", AllowSpecialNumbers, "Infinit0", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity_ is not a literal", AllowSpecialNumbers, "Infinity_", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("InfinityX is not a literal", AllowSpecialNumbers, "InfinityX", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("INF is not a literal", AllowSpecialNumbers, "INF", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("INFINITY is not a literal", AllowSpecialNumbers, "INFINITY", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity truncated after I", AllowSpecialNumbers, "I", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity truncated after In", AllowSpecialNumbers, "In", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity truncated after Inf", AllowSpecialNumbers, "Inf", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity truncated after Infi", AllowSpecialNumbers, "Infi", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity truncated after Infin", AllowSpecialNumbers, "Infin", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity truncated after Infini", AllowSpecialNumbers, "Infini", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity truncated after Infinit", AllowSpecialNumbers, "Infinit", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity not allowed", Standard, "Infinity", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")

/* -Infinity */

PARSE_TEST("-Infinity (1)", AllowSpecialNumbers, "-Infinity", FINAL, UTF8, "u(8) #(-Infinity):0,0,0,0-9,0,9,0")
PARSE_TEST("-Infinity (2)", AllowSpecialNumbers, " -Infinity ", FINAL, UTF8, "u(8) #(-Infinity):1,0,1,0-10,0,10,0")
PARSE_TEST("-I is not a number", AllowSpecialNumbers, "-I ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-In is not a number", AllowSpecialNumbers, "-In ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Inf is not a number", AllowSpecialNumbers, "-Inf ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infi is not a number", AllowSpecialNumbers, "-Infi ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infin is not a number", AllowSpecialNumbers, "-Infin ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infini is not a number", AllowSpecialNumbers, "-Infini ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinit is not a number", AllowSpecialNumbers, "-Infinit ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinitx is not a number", AllowSpecialNumbers, "-Infinitx", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinit0 is not a number", AllowSpecialNumbers, "-Infinit0", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity_ is not a number", AllowSpecialNumbers, "-Infinity_", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-InfinityX is not a number", AllowSpecialNumbers, "-InfinityX", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-INF is not a number", AllowSpecialNumbers, "-INF", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-INFINITY is not a number", AllowSpecialNumbers, "-INFINITY", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity truncated after I", AllowSpecialNumbers, "-I", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity truncated after In", AllowSpecialNumbers, "-In", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity truncated after Inf", AllowSpecialNumbers, "-Inf", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity truncated after Infi", AllowSpecialNumbers, "-Infi", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity truncated after Infin", AllowSpecialNumbers, "-Infin", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity truncated after Infini", AllowSpecialNumbers, "-Infini", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity truncated after Infinit", AllowSpecialNumbers, "-Infinit", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity not allowed", Standard, "-Infinity", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")

/* numbers */

PARSE_TEST("0 (1)", Standard, "0", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0")
PARSE_TEST("0 (2)", Standard, " 0 ", FINAL, UTF8, "u(8) #(0):1,0,1,0-2,0,2,0")
PARSE_TEST("-0 (1)", Standard, "-0", FINAL, UTF8, "u(8) #(- -0):0,0,0,0-2,0,2,0")
PARSE_TEST("-0 (2)", Standard, " -0 ", FINAL, UTF8, "u(8) #(- -0):1,0,1,0-3,0,3,0")
PARSE_TEST("7 (1)", Standard, "7", FINAL, UTF8, "u(8) #(7):0,0,0,0-1,0,1,0")
PARSE_TEST("7 (2)", Standard, " 7 ", FINAL, UTF8, "u(8) #(7):1,0,1,0-2,0,2,0")
PARSE_TEST("-7 (1)", Standard, "-7", FINAL, UTF8, "u(8) #(- -7):0,0,0,0-2,0,2,0")
PARSE_TEST("-7 (2)", Standard, " -7 ", FINAL, UTF8, "u(8) #(- -7):1,0,1,0-3,0,3,0")
PARSE_TEST("1234567890 (1)", Standard, "1234567890", FINAL, UTF8, "u(8) #(1234567890):0,0,0,0-10,0,10,0")
PARSE_TEST("1234567890 (2)", Standard, " 1234567890 ", FINAL, UTF8, "u(8) #(1234567890):1,0,1,0-11,0,11,0")
PARSE_TEST("-1234567890 (1)", Standard, "-1234567890", FINAL, UTF8, "u(8) #(- -1234567890):0,0,0,0-11,0,11,0")
PARSE_TEST("-1234567890 (2)", Standard, " -1234567890 ", FINAL, UTF8, "u(8) #(- -1234567890):1,0,1,0-12,0,12,0")
PARSE_TEST("0e1 (1)", Standard, "0e1", FINAL, UTF8, "u(8) #(e 0e1):0,0,0,0-3,0,3,0")
PARSE_TEST("0e1 (2)", Standard, " 0e1 ", FINAL, UTF8, "u(8) #(e 0e1):1,0,1,0-4,0,4,0")
PARSE_TEST("1e2 (1)", Standard, "1e2", FINAL, UTF8, "u(8) #(e 1e2):0,0,0,0-3,0,3,0")
PARSE_TEST("1e2 (2)", Standard, " 1e2 ", FINAL, UTF8, "u(8) #(e 1e2):1,0,1,0-4,0,4,0")
PARSE_TEST("0e+1 (1)", Standard, "0e+1", FINAL, UTF8, "u(8) #(e 0e+1):0,0,0,0-4,0,4,0")
PARSE_TEST("0e+1 (2)", Standard, " 0e+1 ", FINAL, UTF8, "u(8) #(e 0e+1):1,0,1,0-5,0,5,0")
PARSE_TEST("1e+2 (1)", Standard, "1e+2", FINAL, UTF8, "u(8) #(e 1e+2):0,0,0,0-4,0,4,0")
PARSE_TEST("1e+2 (2)", Standard, " 1e+2 ", FINAL, UTF8, "u(8) #(e 1e+2):1,0,1,0-5,0,5,0")
PARSE_TEST("0e-1 (1)", Standard, "0e-1", FINAL, UTF8, "u(8) #(e- 0e-1):0,0,0,0-4,0,4,0")
PARSE_TEST("0e-1 (2)", Standard, " 0e-1 ", FINAL, UTF8, "u(8) #(e- 0e-1):1,0,1,0-5,0,5,0")
PARSE_TEST("1e-2 (1)", Standard, "1e-2", FINAL, UTF8, "u(8) #(e- 1e-2):0,0,0,0-4,0,4,0")
PARSE_TEST("1e-2 (2)", Standard, " 1e-2 ", FINAL, UTF8, "u(8) #(e- 1e-2):1,0,1,0-5,0,5,0")
PARSE_TEST("1234567890E0987654321 (1)", Standard, "1234567890E0987654321", FINAL, UTF8, "u(8) #(e 1234567890E0987654321):0,0,0,0-21,0,21,0")
PARSE_TEST("1234567890E0987654321 (2)", Standard, " 1234567890E0987654321 ", FINAL, UTF8, "u(8) #(e 1234567890E0987654321):1,0,1,0-22,0,22,0")
PARSE_TEST("0.0 (1)", Standard, "0.0", FINAL, UTF8, "u(8) #(. 0.0):0,0,0,0-3,0,3,0")
PARSE_TEST("0.0 (2)", Standard, " 0.0 ", FINAL, UTF8, "u(8) #(. 0.0):1,0,1,0-4,0,4,0")
PARSE_TEST("0.12 (1)", Standard, "0.12", FINAL, UTF8, "u(8) #(. 0.12):0,0,0,0-4,0,4,0")
PARSE_TEST("0.12 (2)", Standard, " 0.12 ", FINAL, UTF8, "u(8) #(. 0.12):1,0,1,0-5,0,5,0")
PARSE_TEST("1.2 (1)", Standard, "1.2", FINAL, UTF8, "u(8) #(. 1.2):0,0,0,0-3,0,3,0")
PARSE_TEST("1.2 (2)", Standard, " 1.2 ", FINAL, UTF8, "u(8) #(. 1.2):1,0,1,0-4,0,4,0")
PARSE_TEST("1.23 (1)", Standard, "1.23", FINAL, UTF8, "u(8) #(. 1.23):0,0,0,0-4,0,4,0")
PARSE_TEST("1.23 (2)", Standard, " 1.23 ", FINAL, UTF8, "u(8) #(. 1.23):1,0,1,0-5,0,5,0")
PARSE_TEST("1.23e456 (1)", Standard, "1.23e456", FINAL, UTF8, "u(8) #(.e 1.23e456):0,0,0,0-8,0,8,0")
PARSE_TEST("1.23e456 (2)", Standard, " 1.23e456 ", FINAL, UTF8, "u(8) #(.e 1.23e456):1,0,1,0-9,0,9,0")
PARSE_TEST("1.23e+456 (1)", Standard, "1.23e+456", FINAL, UTF8, "u(8) #(.e 1.23e+456):0,0,0,0-9,0,9,0")
PARSE_TEST("1.23e+456 (2)", Standard, " 1.23e+456 ", FINAL, UTF8, "u(8) #(.e 1.23e+456):1,0,1,0-10,0,10,0")
PARSE_TEST("1.23e-456 (1)", Standard, "1.23e-456", FINAL, UTF8, "u(8) #(.e- 1.23e-456):0,0,0,0-9,0,9,0")
PARSE_TEST("1.23e-456 (2)", Standard, " 1.23e-456 ", FINAL, UTF8, "u(8) #(.e- 1.23e-456):1,0,1,0-10,0,10,0")
PARSE_TEST("-1.23e-456 -> UTF-16LE", UTF16LEOut, "-1.23e-456", FINAL, UTF8, "u(8) #(-.e- -_1_._2_3_e_-_4_5_6_):0,0,0,0-10,0,10,0")
PARSE_TEST("-1.23e-456 -> UTF-16BE", UTF16BEOut, "-1.23e-456", FINAL, UTF8, "u(8) #(-.e- _-_1_._2_3_e_-_4_5_6):0,0,0,0-10,0,10,0")
PARSE_TEST("-1.23e-456 -> UTF-32LE", UTF32LEOut, "-1.23e-456", FINAL, UTF8, "u(8) #(-.e- -___1___.___2___3___e___-___4___5___6___):0,0,0,0-10,0,10,0")
PARSE_TEST("-1.23e-456 -> UTF-32BE", UTF32BEOut, "-1.23e-456", FINAL, UTF8, "u(8) #(-.e- ___-___1___.___2___3___e___-___4___5___6):0,0,0,0-10,0,10,0")

PARSE_TEST("max length number (1)", MaxNumberLength1, "1", FINAL, UTF8, "u(8) #(1):0,0,0,0-1,0,1,0")
PARSE_TEST("max length number (2)", MaxNumberLength2, "-1", FINAL, UTF8, "u(8) #(- -1):0,0,0,0-2,0,2,0")
PARSE_TEST("number encoded in UTF-16LE (1)", UTF16LEIn, "0\x00", FINAL, UTF16LE, "#(0):0,0,0,0-2,0,1,0")
PARSE_TEST("number encoded in UTF-16LE (2)", UTF16LEIn, "-\x00" "1\x00" ".\x00" "2\x00" "3\x00" "e\x00" "-\x00" "4\x00" "5\x00" "6\x00", FINAL, UTF16LE, "#(-.e- -1.23e-456):0,0,0,0-20,0,10,0")
PARSE_TEST("number encoded in UTF-16BE (1)", UTF16BEIn, "\x00" "0", FINAL, UTF16BE, "#(0):0,0,0,0-2,0,1,0")
PARSE_TEST("number encoded in UTF-16BE (2)", UTF16BEIn, "\x00" "-\x00" "1\x00" ".\x00" "2\x00" "3\x00" "e\x00" "-\x00" "4\x00" "5\x00" "6", FINAL, UTF16BE, "#(-.e- -1.23e-456):0,0,0,0-20,0,10,0")
PARSE_TEST("number encoded in UTF-32LE (1)", UTF32LEIn, "0\x00\x00\x00", FINAL, UTF32LE, "#(0):0,0,0,0-4,0,1,0")
PARSE_TEST("number encoded in UTF-32LE (2)", UTF32LEIn, "-\x00\x00\x00" "1\x00\x00\x00" ".\x00\x00\x00" "2\x00\x00\x00" "3\x00\x00\x00" "e\x00\x00\x00" "-\x00\x00\x00" "4\x00\x00\x00" "5\x00\x00\x00" "6\x00\x00\x00", FINAL, UTF32LE, "#(-.e- -1.23e-456):0,0,0,0-40,0,10,0")
PARSE_TEST("number encoded in UTF-32BE (1)", UTF32BEIn, "\x00\x00\x00" "0", FINAL, UTF32BE, "#(0):0,0,0,0-4,0,1,0")
PARSE_TEST("number encoded in UTF-32BE (2)", UTF32BEIn, "\x00\x00\x00" "-\x00\x00\x00" "1\x00\x00\x00" ".\x00\x00\x00" "2\x00\x00\x00" "3\x00\x00\x00" "e\x00\x00\x00" "-\x00\x00\x00" "4\x00\x00\x00" "5\x00\x00\x00" "6", FINAL, UTF32BE, "#(-.e- -1.23e-456):0,0,0,0-40,0,10,0")
PARSE_TEST("number cannot have leading + sign", Standard, "+7", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("number cannot have digits after leading 0 (1)", Standard, "00", FINAL, UTF8, "u(8) !(InvalidNumber):0,0,0,0")
PARSE_TEST("number cannot have digits after leading 0 (2)", Standard, "01", FINAL, UTF8, "u(8) !(InvalidNumber):0,0,0,0")
PARSE_TEST("number cannot have digits after leading 0 (3)", Standard, "-00", FINAL, UTF8, "u(8) !(InvalidNumber):0,0,0,0")
PARSE_TEST("number cannot have digits after leading 0 (4)", Standard, "-01", FINAL, UTF8, "u(8) !(InvalidNumber):0,0,0,0")
PARSE_TEST("number requires digit after - sign", Standard, "-x", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("number truncated after - sign", Standard, "-", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("number requires digit after decimal point", Standard, "7.x", FINAL, UTF8, "u(8) !(InvalidNumber):0,0,0,0")
PARSE_TEST("number truncated after decimal point", Standard, "7.", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("number requires digit or sign after e", Standard, "7ex", FINAL, UTF8, "u(8) !(InvalidNumber):0,0,0,0")
PARSE_TEST("number truncated after e", Standard, "7e", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("number requires digit or sign after E", Standard, "7Ex", FINAL, UTF8, "u(8) !(InvalidNumber):0,0,0,0")
PARSE_TEST("number truncated after E", Standard, "7E", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("number requires digit after exponent + sign", Standard, "7e+x", FINAL, UTF8, "u(8) !(InvalidNumber):0,0,0,0")
PARSE_TEST("number truncated after exponent + sign", Standard, "7e+", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("number requires digit after exponent - sign", Standard, "7e-x", FINAL, UTF8, "u(8) !(InvalidNumber):0,0,0,0")
PARSE_TEST("number truncated after exponent - sign", Standard, "7e-", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("very long number", Standard, "123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890", FINAL, UTF8,
           "u(8) #(123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890):0,0,0,0-120,0,120,0")
PARSE_TEST("too long number (1)", MaxNumberLength0, "1", FINAL, UTF8, "u(8) !(TooLongNumber):0,0,0,0")
PARSE_TEST("too long number (2)", MaxNumberLength1, "-1", FINAL, UTF8, "u(8) !(TooLongNumber):0,0,0,0")
PARSE_TEST("too long number (3)", MaxNumberLength2, "1.0", FINAL, UTF8, "u(8) !(TooLongNumber):0,0,0,0")

/* hex numbers */

PARSE_TEST("hex number not allowed (1)", Standard, "0x0", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(UnknownToken):1,0,1,0")
PARSE_TEST("hex number not allowed (2)", Standard, "0X1", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(UnknownToken):1,0,1,0")
PARSE_TEST("hex number not allowed (3)", Standard, "-0X1", FINAL, UTF8, "u(8) #(- -0):0,0,0,0-2,0,2,0 !(UnknownToken):2,0,2,0")
PARSE_TEST("negative hex number not allowed", AllowHexNumbers, "-0X1", FINAL, UTF8, "u(8) #(- -0):0,0,0,0-2,0,2,0 !(UnknownToken):2,0,2,0")
PARSE_TEST("hex number (1)", AllowHexNumbers, "0x0", FINAL, UTF8, "u(8) #(x 0x0):0,0,0,0-3,0,3,0")
PARSE_TEST("hex number (2)", AllowHexNumbers, "0x1", FINAL, UTF8, "u(8) #(x 0x1):0,0,0,0-3,0,3,0")
PARSE_TEST("hex number (3)", AllowHexNumbers, "0x0000", FINAL, UTF8, "u(8) #(x 0x0000):0,0,0,0-6,0,6,0")
PARSE_TEST("hex number (4)", AllowHexNumbers, "0x123456789abcdefABCDEF", FINAL, UTF8, "u(8) #(x 0x123456789abcdefABCDEF):0,0,0,0-23,0,23,0")
PARSE_TEST("maximum length hex number", AllowHexNumbers, "0x123456789a123456789a123456789a123456789a123456789a123456789a0", FINAL, UTF8, "u(8) #(x 0x123456789a123456789a123456789a123456789a123456789a123456789a0):0,0,0,0-63,0,63,0")
PARSE_TEST("hex number truncated after x", AllowHexNumbers, "0x", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("hex number requires  digit after x", AllowHexNumbers, "0xx", FINAL, UTF8, "u(8) !(InvalidNumber):0,0,0,0")
PARSE_TEST("hex number truncated after X", AllowHexNumbers, "0X", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("hex number requires  digit after X", AllowHexNumbers, "0Xx", FINAL, UTF8, "u(8) !(InvalidNumber):0,0,0,0")
PARSE_TEST("too long hex number (1)", AllowHexNumbers | MaxNumberLength0, "0x0", FINAL, UTF8, "u(8) !(TooLongNumber):0,0,0,0")
PARSE_TEST("too long hex number (2)", AllowHexNumbers | MaxNumberLength1, "0x0", FINAL, UTF8, "u(8) !(TooLongNumber):0,0,0,0")
PARSE_TEST("too long hex number (3)", AllowHexNumbers | MaxNumberLength2, "0x0", FINAL, UTF8, "u(8) !(TooLongNumber):0,0,0,0")
PARSE_TEST("hex number -> UTF-16LE", AllowHexNumbers | UTF16LEOut, "0x0123456789abcdefABCDEF", FINAL, UTF8, "u(8) #(x 0_x_0_1_2_3_4_5_6_7_8_9_a_b_c_d_e_f_A_B_C_D_E_F_):0,0,0,0-24,0,24,0")
PARSE_TEST("hex number -> UTF-16BE", AllowHexNumbers | UTF16BEOut, "0x0123456789abcdefABCDEF", FINAL, UTF8, "u(8) #(x _0_x_0_1_2_3_4_5_6_7_8_9_a_b_c_d_e_f_A_B_C_D_E_F):0,0,0,0-24,0,24,0")
PARSE_TEST("hex number -> UTF-32LE", AllowHexNumbers | UTF32LEOut, "0x0123456789abcdefABCDEF", FINAL, UTF8, "u(8) #(x 0___x___0___1___2___3___4___5___6___7___8___9___a___b___c___d___e___f___A___B___C___D___E___F___):0,0,0,0-24,0,24,0")
PARSE_TEST("hex number -> UTF-32BE", AllowHexNumbers | UTF32BEOut, "0x0123456789abcdefABCDEF", FINAL, UTF8, "u(8) #(x ___0___x___0___1___2___3___4___5___6___7___8___9___a___b___c___d___e___f___A___B___C___D___E___F):0,0,0,0-24,0,24,0")

/* strings */

PARSE_TEST("empty string", Standard, "\"\"", FINAL, UTF8, "u(8) s():0,0,0,0-2,0,2,0")
PARSE_TEST("UTF-8 -> UTF-8",    UTF8In | UTF8Out,    "\"" "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84" "\"", FINAL, UTF8, "s(ab a<C2><A9><E4><B8><81><F0><9F><80><84>):0,0,0,0-12,0,6,0")
PARSE_TEST("UTF-8 -> UTF-16LE", UTF8In | UTF16LEOut, "\"" "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84" "\"", FINAL, UTF8, "s(ab a_<A9 00><01 4E><3C D8><04 DC>):0,0,0,0-12,0,6,0")
PARSE_TEST("UTF-8 -> UTF-16BE", UTF8In | UTF16BEOut, "\"" "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84" "\"", FINAL, UTF8, "s(ab _a<00 A9><4E 01><D8 3C><DC 04>):0,0,0,0-12,0,6,0")
PARSE_TEST("UTF-8 -> UTF-32LE", UTF8In | UTF32LEOut, "\"" "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84" "\"", FINAL, UTF8, "s(ab a___<A9 00 00 00><01 4E 00 00><04 F0 01 00>):0,0,0,0-12,0,6,0")
PARSE_TEST("UTF-8 -> UTF-32BE", UTF8In | UTF32BEOut, "\"" "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84" "\"", FINAL, UTF8, "s(ab ___a<00 00 00 A9><00 00 4E 01><00 01 F0 04>):0,0,0,0-12,0,6,0")
PARSE_TEST("UTF-16LE -> UTF-8",    UTF16LEIn | UTF8Out,    "\"\x00" "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC" "\"\x00", FINAL, UTF16LE, "s(ab a<C2><A9><E4><B8><81><F0><9F><80><84>):0,0,0,0-14,0,6,0")
PARSE_TEST("UTF-16LE -> UTF-16LE", UTF16LEIn | UTF16LEOut, "\"\x00" "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC" "\"\x00", FINAL, UTF16LE, "s(ab a_<A9 00><01 4E><3C D8><04 DC>):0,0,0,0-14,0,6,0")
PARSE_TEST("UTF-16LE -> UTF-16BE", UTF16LEIn | UTF16BEOut, "\"\x00" "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC" "\"\x00", FINAL, UTF16LE, "s(ab _a<00 A9><4E 01><D8 3C><DC 04>):0,0,0,0-14,0,6,0")
PARSE_TEST("UTF-16LE -> UTF-32LE", UTF16LEIn | UTF32LEOut, "\"\x00" "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC" "\"\x00", FINAL, UTF16LE, "s(ab a___<A9 00 00 00><01 4E 00 00><04 F0 01 00>):0,0,0,0-14,0,6,0")
PARSE_TEST("UTF-16LE -> UTF-32BE", UTF16LEIn | UTF32BEOut, "\"\x00" "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC" "\"\x00", FINAL, UTF16LE, "s(ab ___a<00 00 00 A9><00 00 4E 01><00 01 F0 04>):0,0,0,0-14,0,6,0")
PARSE_TEST("UTF-16BE -> UTF-8",    UTF16BEIn | UTF8Out,    "\x00\"" "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04" "\x00\"", FINAL, UTF16BE, "s(ab a<C2><A9><E4><B8><81><F0><9F><80><84>):0,0,0,0-14,0,6,0")
PARSE_TEST("UTF-16BE -> UTF-16LE", UTF16BEIn | UTF16LEOut, "\x00\"" "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04" "\x00\"", FINAL, UTF16BE, "s(ab a_<A9 00><01 4E><3C D8><04 DC>):0,0,0,0-14,0,6,0")
PARSE_TEST("UTF-16BE -> UTF-16BE", UTF16BEIn | UTF16BEOut, "\x00\"" "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04" "\x00\"", FINAL, UTF16BE, "s(ab _a<00 A9><4E 01><D8 3C><DC 04>):0,0,0,0-14,0,6,0")
PARSE_TEST("UTF-16BE -> UTF-32LE", UTF16BEIn | UTF32LEOut, "\x00\"" "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04" "\x00\"", FINAL, UTF16BE, "s(ab a___<A9 00 00 00><01 4E 00 00><04 F0 01 00>):0,0,0,0-14,0,6,0")
PARSE_TEST("UTF-16BE -> UTF-32BE", UTF16BEIn | UTF32BEOut, "\x00\"" "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04" "\x00\"", FINAL, UTF16BE, "s(ab ___a<00 00 00 A9><00 00 4E 01><00 01 F0 04>):0,0,0,0-14,0,6,0")
PARSE_TEST("UTF-32LE -> UTF-8",    UTF32LEIn | UTF8Out,    "\"\x00\x00\x00" "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "s(ab a<C2><A9><E4><B8><81><F0><9F><80><84>):0,0,0,0-24,0,6,0")
PARSE_TEST("UTF-32LE -> UTF-16LE", UTF32LEIn | UTF16LEOut, "\"\x00\x00\x00" "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "s(ab a_<A9 00><01 4E><3C D8><04 DC>):0,0,0,0-24,0,6,0")
PARSE_TEST("UTF-32LE -> UTF-16BE", UTF32LEIn | UTF16BEOut, "\"\x00\x00\x00" "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "s(ab _a<00 A9><4E 01><D8 3C><DC 04>):0,0,0,0-24,0,6,0")
PARSE_TEST("UTF-32LE -> UTF-32LE", UTF32LEIn | UTF32LEOut, "\"\x00\x00\x00" "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "s(ab a___<A9 00 00 00><01 4E 00 00><04 F0 01 00>):0,0,0,0-24,0,6,0")
PARSE_TEST("UTF-32LE -> UTF-32BE", UTF32LEIn | UTF32BEOut, "\"\x00\x00\x00" "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "s(ab ___a<00 00 00 A9><00 00 4E 01><00 01 F0 04>):0,0,0,0-24,0,6,0")
PARSE_TEST("UTF-32BE -> UTF-8",    UTF32BEIn | UTF8Out,    "\x00\x00\x00\"" "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04" "\x00\x00\x00\"", FINAL, UTF32BE, "s(ab a<C2><A9><E4><B8><81><F0><9F><80><84>):0,0,0,0-24,0,6,0")
PARSE_TEST("UTF-32BE -> UTF-16LE", UTF32BEIn | UTF16LEOut, "\x00\x00\x00\"" "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04" "\x00\x00\x00\"", FINAL, UTF32BE, "s(ab a_<A9 00><01 4E><3C D8><04 DC>):0,0,0,0-24,0,6,0")
PARSE_TEST("UTF-32BE -> UTF-16BE", UTF32BEIn | UTF16BEOut, "\x00\x00\x00\"" "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04" "\x00\x00\x00\"", FINAL, UTF32BE, "s(ab _a<00 A9><4E 01><D8 3C><DC 04>):0,0,0,0-24,0,6,0")
PARSE_TEST("UTF-32BE -> UTF-32LE", UTF32BEIn | UTF32LEOut, "\x00\x00\x00\"" "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04" "\x00\x00\x00\"", FINAL, UTF32BE, "s(ab a___<A9 00 00 00><01 4E 00 00><04 F0 01 00>):0,0,0,0-24,0,6,0")
PARSE_TEST("UTF-32BE -> UTF-32BE", UTF32BEIn | UTF32BEOut, "\x00\x00\x00\"" "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04" "\x00\x00\x00\"", FINAL, UTF32BE, "s(ab ___a<00 00 00 A9><00 00 4E 01><00 01 F0 04>):0,0,0,0-24,0,6,0")
PARSE_TEST("all whitespace string", Standard, "\" \\r\\n\\t \"", FINAL, UTF8, "u(8) s(c <20><0D><0A><09><20>):0,0,0,0-10,0,10,0")
PARSE_TEST("ASCII string", Standard, "\"abc DEF 123\"", FINAL, UTF8, "u(8) s(abc<20>DEF<20>123):0,0,0,0-13,0,13,0")
PARSE_TEST("simple string escape sequences", Standard, "\"\\\"\\\\/\\t\\n\\r\\f\\b\"", FINAL, UTF8, "u(8) s(c \"\\/<09><0A><0D><0C><08>):0,0,0,0-17,0,17,0")
PARSE_TEST("string hex escape sequences", Standard, "\"\\u0000\\u0020\\u0aF9\\ufFfF\\uD834\\udd1e\"", FINAL, UTF8, "u(8) s(zcab <00><20><E0><AB><B9><EF><BF><BF><F0><9D><84><9E>):0,0,0,0-38,0,38,0")
PARSE_TEST("string escaped control characters", Standard, "\""
                   "\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\u0008\\u0009\\u000A\\u000B\\u000C\\u000D\\u000E\\u000F"
                   "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017\\u0018\\u0019\\u001A\\u001B\\u001C\\u001D\\u001E\\u001F"
                   "\"", FINAL, UTF8, "u(8) s(zc <00><01><02><03><04><05><06><07><08><09><0A><0B><0C><0D><0E><0F><10><11><12><13><14><15><16><17><18><19><1A><1B><1C><1D><1E><1F>):0,0,0,0-194,0,194,0")
PARSE_TEST("non-control ASCII string", Standard, "\""
                   " !\\u0022#$%&'()+*,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\u005C]^_`abcdefghijklmnopqrstuvwxyz{|}~\\u007F"
                   "\"", FINAL, UTF8, "u(8) s(<20>!\"#$%&'()+*,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^<5F>`abcdefghijklmnopqrstuvwxyz{|}~<7F>):0,0,0,0-113,0,113,0")
PARSE_TEST("long string", Standard, "\""
                   "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
                   "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
                   "\"", FINAL, UTF8, "u(8) s(0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF):0,0,0,0-130,0,130,0")
PARSE_TEST("unterminated string (1)", Standard, "\"", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("unterminated string (2)", Standard, "\"abc", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("string cannot contain unescaped control character (1)", Standard, "\"abc\x00\"", FINAL, UTF8, "u(8) !(UnescapedControlCharacter):4,0,4,0")
PARSE_TEST("string cannot contain unescaped control character (2)", Standard, "\"abc\x09\"", FINAL, UTF8, "u(8) !(UnescapedControlCharacter):4,0,4,0")
PARSE_TEST("string cannot contain unescaped control character (3)", Standard, "\"abc\x0A\"", FINAL, UTF8, "u(8) !(UnescapedControlCharacter):4,0,4,0")
PARSE_TEST("string cannot contain unescaped control character (4)", Standard, "\"abc\x0D\"", FINAL, UTF8, "u(8) !(UnescapedControlCharacter):4,0,4,0")
PARSE_TEST("string cannot contain unescaped control character (5)", Standard, "\"abc\x1F\"", FINAL, UTF8, "u(8) !(UnescapedControlCharacter):4,0,4,0")
PARSE_TEST("unescaped control character (1)", AllowUnescapedControlCharacters, "\"abc\x00\"", FINAL, UTF8, "u(8) s(zc abc<00>):0,0,0,0-6,0,6,0")
PARSE_TEST("unescaped control character (2)", AllowUnescapedControlCharacters, "\"abc\x09\"", FINAL, UTF8, "u(8) s(c abc<09>):0,0,0,0-6,0,6,0")
PARSE_TEST("unescaped control character (3)", AllowUnescapedControlCharacters, "\"abc\x0A\"", FINAL, UTF8, "u(8) s(c abc<0A>):0,0,0,0-6,1,1,0")
PARSE_TEST("unescaped control character (4)", AllowUnescapedControlCharacters, "\"abc\x0D\"", FINAL, UTF8, "u(8) s(c abc<0D>):0,0,0,0-6,1,1,0")
PARSE_TEST("unescaped control character (5)", AllowUnescapedControlCharacters, "\"abc\x1F\"", FINAL, UTF8, "u(8) s(c abc<1F>):0,0,0,0-6,0,6,0")
PARSE_TEST("unescaped newlines in string", AllowUnescapedControlCharacters, "\"\x0D\x0A \x0D \x0A\"!", FINAL, UTF8, "u(8) s(c <0D><0A><20><0D><20><0A>):0,0,0,0-8,3,1,0 !(UnknownToken):8,3,1,0")
PARSE_TEST("string cannot contain invalid escape sequence (1)", Standard, "\"\\v\"", FINAL, UTF8, "u(8) !(InvalidEscapeSequence):1,0,1,0")
PARSE_TEST("string cannot contain invalid escape sequence (2)", Standard, "\"\\x0020\"", FINAL, UTF8, "u(8) !(InvalidEscapeSequence):1,0,1,0")
PARSE_TEST("string cannot contain invalid escape sequence (3)", Standard, "\"\\ \"", FINAL, UTF8, "u(8) !(InvalidEscapeSequence):1,0,1,0")
PARSE_TEST("string truncated after \\", Standard, "\"\\", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\u", Standard, "\"\\u", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\ux", Standard, "\"\\u0", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\uxx", Standard, "\"\\u01", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\uxxx", Standard, "\"\\u01a", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("string requires hex digit after \\u", Standard, "\"\\ux\"", FINAL, UTF8, "u(8) !(InvalidEscapeSequence):1,0,1,0")
PARSE_TEST("string requires hex digit after \\ux", Standard, "\"\\u0x\"", FINAL, UTF8, "u(8) !(InvalidEscapeSequence):1,0,1,0")
PARSE_TEST("string requires hex digit after \\uxx", Standard, "\"\\u01x\"", FINAL, UTF8, "u(8) !(InvalidEscapeSequence):1,0,1,0")
PARSE_TEST("string requires hex digit after \\uxxx", Standard, "\"\\u01ax\"", FINAL, UTF8, "u(8) !(InvalidEscapeSequence):1,0,1,0")
PARSE_TEST("string truncated after escaped leading surrogate", Standard, "\"\\uD800", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("string requires escaped surrogates to appear in valid pairs (1)", Standard, "\"\\uD834\"", FINAL, UTF8, "u(8) !(UnpairedSurrogateEscapeSequence):1,0,1,0")
PARSE_TEST("string requires escaped surrogates to appear in valid pairs (2)", Standard, "\"\\uD834x\"", FINAL, UTF8, "u(8) !(UnpairedSurrogateEscapeSequence):1,0,1,0")
PARSE_TEST("string requires escaped surrogates to appear in valid pairs (3)", Standard, "\"\\uD834\\n\"", FINAL, UTF8, "u(8) !(UnpairedSurrogateEscapeSequence):1,0,1,0")
PARSE_TEST("string requires escaped surrogates to appear in valid pairs (4)", Standard, "\"\\uD834\\u0020\"", FINAL, UTF8, "u(8) !(UnpairedSurrogateEscapeSequence):1,0,1,0")
PARSE_TEST("string requires escaped surrogates to appear in valid pairs (5)", Standard, "\"\\uD834\\uD834\"", FINAL, UTF8, "u(8) !(UnpairedSurrogateEscapeSequence):1,0,1,0")
PARSE_TEST("string requires escaped surrogates to appear in valid pairs (6)", Standard, "\"\\uDC00\"", FINAL, UTF8, "u(8) !(UnpairedSurrogateEscapeSequence):1,0,1,0")
PARSE_TEST("string truncated after \\ of trailing surrogate escape sequence", Standard, "\"\\uD834\\", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\u of trailing surrogate escape sequence", Standard, "\"\\uD834\\u", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\ux of trailing surrogate escape sequence", Standard, "\"\\uD834\\uD", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\uxx of trailing surrogate escape sequence", Standard, "\"\\uD834\\uDD", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\uxxx of trailing surrogate escape sequence", Standard, "\"\\uD834\\uDD1", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("max length 0 string (1)", MaxStringLength0, "\"\"", FINAL, UTF8, "u(8) s():0,0,0,0-2,0,2,0")
PARSE_TEST("max length 0 string (2)", MaxStringLength0, "{\"\":0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m():1,0,1,1-3,0,3,1 #(0):4,0,4,1-5,0,5,1 }:5,0,5,0-6,0,6,0")
PARSE_TEST("max length 0 string (3)", MaxStringLength0, "\"a\"", FINAL, UTF8, "u(8) !(TooLongString):0,0,0,0")
PARSE_TEST("max length 0 string (4)", MaxStringLength0, "{\"a\":0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(TooLongString):1,0,1,1")
PARSE_TEST("max length 1 string (1)", MaxStringLength1, "\"a\"", FINAL, UTF8, "u(8) s(a):0,0,0,0-3,0,3,0")
PARSE_TEST("max length 1 string (2)", MaxStringLength1, "{\"a\":0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 #(0):5,0,5,1-6,0,6,1 }:6,0,6,0-7,0,7,0")
PARSE_TEST("max length 1 string (3)", MaxStringLength1, "\"ab\"", FINAL, UTF8, "u(8) !(TooLongString):0,0,0,0")
PARSE_TEST("max length 1 string (4)", MaxStringLength1, "{\"ab\":0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(TooLongString):1,0,1,1")
PARSE_TEST("max length 1 string (5)", MaxStringLength1, "\"\xE0\xAB\xB9\"", FINAL, UTF8, "u(8) !(TooLongString):0,0,0,0")
PARSE_TEST("max length 2 string (1)", MaxStringLength2, "\"ab\"", FINAL, UTF8, "u(8) s(ab):0,0,0,0-4,0,4,0")
PARSE_TEST("max length 2 string (2)", MaxStringLength2, "{\"ab\":0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(ab):1,0,1,1-5,0,5,1 #(0):6,0,6,1-7,0,7,1 }:7,0,7,0-8,0,8,0")
PARSE_TEST("max length 2 string (3)", MaxStringLength2, "\"abc\"", FINAL, UTF8, "u(8) !(TooLongString):0,0,0,0")
PARSE_TEST("max length 2 string (4)", MaxStringLength2, "{\"abc\":0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(TooLongString):1,0,1,1")
PARSE_TEST("max length 2 string (5)", MaxStringLength2, "\"\xE0\xAB\xB9\"", FINAL, UTF8, "u(8) !(TooLongString):0,0,0,0")

/* objects */

PARSE_TEST("start object", UTF8In, "{", PARTIAL, UTF8, "{:0,0,0,0-1,0,1,0")
PARSE_TEST("empty object", Standard, "{}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 }:1,0,1,0-2,0,2,0")
PARSE_TEST("single-member object", Standard, "{ \"pi\" : 3.14159 }", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(pi):2,0,2,1-6,0,6,1 #(. 3.14159):9,0,9,1-16,0,16,1 }:17,0,17,0-18,0,18,0")
PARSE_TEST("multi-member object", Standard, "{ \"pi\" : 3.14159, \"e\" : 2.71828 }", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(pi):2,0,2,1-6,0,6,1 #(. 3.14159):9,0,9,1-16,0,16,1 m(e):18,0,18,1-21,0,21,1 #(. 2.71828):24,0,24,1-31,0,31,1 }:32,0,32,0-33,0,33,0")
PARSE_TEST("all types of object member values", AllowSpecialNumbers | AllowHexNumbers, "{ \"a\" : null, \"b\" : true, \"c\" : \"foo\", \"d\" : 17, \"e\" : NaN, \"f\": 0xbeef, \"g\" : {}, \"h\" : {}, \"i\" : [] }", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):2,0,2,1-5,0,5,1 n:8,0,8,1-12,0,12,1 m(b):14,0,14,1-17,0,17,1 t:20,0,20,1-24,0,24,1 m(c):26,0,26,1-29,0,29,1 s(foo):32,0,32,1-37,0,37,1 m(d):39,0,39,1-42,0,42,1 #(17):45,0,45,1-47,0,47,1 m(e):49,0,49,1-52,0,52,1 #(NaN):55,0,55,1-58,0,58,1 m(f):60,0,60,1-63,0,63,1 #(x 0xbeef):65,0,65,1-71,0,71,1 m(g):73,0,73,1-76,0,76,1 {:79,0,79,1-80,0,80,1 }:80,0,80,1-81,0,81,1 m(h):83,0,83,1-86,0,86,1 {:89,0,89,1-90,0,90,1 }:90,0,90,1-91,0,91,1 m(i):93,0,93,1-96,0,96,1 [:99,0,99,1-100,0,100,1 ]:100,0,100,1-101,0,101,1 }:102,0,102,0-103,0,103,0")
PARSE_TEST("nested objects", Standard, "{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{}}}}}}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 {:5,0,5,1-6,0,6,1 m(b):6,0,6,2-9,0,9,2 {:10,0,10,2-11,0,11,2 m(c):11,0,11,3-14,0,14,3 {:15,0,15,3-16,0,16,3 m(d):16,0,16,4-19,0,19,4 {:20,0,20,4-21,0,21,4 m(e):21,0,21,5-24,0,24,5 {:25,0,25,5-26,0,26,5 }:26,0,26,5-27,0,27,5 }:27,0,27,4-28,0,28,4 }:28,0,28,3-29,0,29,3 }:29,0,29,2-30,0,30,2 }:30,0,30,1-31,0,31,1 }:31,0,31,0-32,0,32,0")
PARSE_TEST("object members with similar names", Standard, "{\"\":null,\"\\u0000\":0,\"x\":1,\"X\":2,\"x2\":3,\"x\\u0000\":4,\"x\\u0000y\":5}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m():1,0,1,1-3,0,3,1 n:4,0,4,1-8,0,8,1 m(zc <00>):9,0,9,1-17,0,17,1 #(0):18,0,18,1-19,0,19,1 m(x):20,0,20,1-23,0,23,1 #(1):24,0,24,1-25,0,25,1 m(X):26,0,26,1-29,0,29,1 #(2):30,0,30,1-31,0,31,1 m(x2):32,0,32,1-36,0,36,1 #(3):37,0,37,1-38,0,38,1 m(zc x<00>):39,0,39,1-48,0,48,1 #(4):49,0,49,1-50,0,50,1 m(zc x<00>y):51,0,51,1-61,0,61,1 #(5):62,0,62,1-63,0,63,1 }:63,0,63,0-64,0,64,0")
PARSE_TEST("different objects with members with same names", Standard, "{\"foo\":{\"foo\":{\"foo\":3}}}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(foo):1,0,1,1-6,0,6,1 {:7,0,7,1-8,0,8,1 m(foo):8,0,8,2-13,0,13,2 {:14,0,14,2-15,0,15,2 m(foo):15,0,15,3-20,0,20,3 #(3):21,0,21,3-22,0,22,3 }:22,0,22,2-23,0,23,2 }:23,0,23,1-24,0,24,1 }:24,0,24,0-25,0,25,0")
PARSE_TEST("object truncated after left curly brace", Standard, "{", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(ExpectedMoreTokens):1,0,1,1")
PARSE_TEST("object truncated after member name (1)", Standard, "{\"x\"", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 !(ExpectedMoreTokens):4,0,4,1")
PARSE_TEST("object truncated after member name (2)", Standard, "{\"x\":1,\"y\"", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 m(y):7,0,7,1-10,0,10,1 !(ExpectedMoreTokens):10,0,10,1")
PARSE_TEST("object truncated after colon (1)", Standard, "{\"x\":", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 !(ExpectedMoreTokens):5,0,5,1")
PARSE_TEST("object truncated after colon (2)", Standard, "{\"x\":1,\"y\":", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 m(y):7,0,7,1-10,0,10,1 !(ExpectedMoreTokens):11,0,11,1")
PARSE_TEST("object truncated after member value (1)", Standard, "{\"x\":1", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 !(ExpectedMoreTokens):6,0,6,1")
PARSE_TEST("object truncated after member value (2)", Standard, "{\"x\":1,\"y\":2", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 m(y):7,0,7,1-10,0,10,1 #(2):11,0,11,1-12,0,12,1 !(ExpectedMoreTokens):12,0,12,1")
PARSE_TEST("object truncated after comma (1)", Standard, "{\"x\":1,", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 !(ExpectedMoreTokens):7,0,7,1")
PARSE_TEST("object truncated after comma (2)", Standard, "{\"x\":1,\"y\":2,", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 m(y):7,0,7,1-10,0,10,1 #(2):11,0,11,1-12,0,12,1 !(ExpectedMoreTokens):13,0,13,1")
PARSE_TEST("object requires string member names (1)", Standard, "{null:1}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("object requires string member names (2)", Standard, "{true:1}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("object requires string member names (3)", Standard, "{false:1}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("object requires string member names (4)", Standard, "{7:1}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("object requires string member names (5)", Standard, "{[]:1}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("object requires string member names (6)", Standard, "{{}:1}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("object member requires value (1)", Standard, "{\"x\"}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 !(UnexpectedToken):4,0,4,1")
PARSE_TEST("object member requires value (2)", Standard, "{\"x\":}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 !(UnexpectedToken):5,0,5,1")
PARSE_TEST("object member missing (1)", Standard, "{,\"y\":2}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("object member missing (2)", Standard, "{\"x\":1,,\"y\":2}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 !(UnexpectedToken):7,0,7,1")
PARSE_TEST("object member missing (3)", Standard, "{\"x\":1,}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 !(UnexpectedToken):7,0,7,1")
PARSE_TEST("object members require comma separator", Standard, "{\"x\":1 \"y\":2}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 !(UnexpectedToken):7,0,7,1")
PARSE_TEST("object members must be unique (1)", TrackObjectMembers, "{\"x\":1,\"x\":2}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 !(DuplicateObjectMember):7,0,7,1")
PARSE_TEST("object members must be unique (2)", TrackObjectMembers, "{\"x\":1,\"y\":2,\"x\":3}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 m(y):7,0,7,1-10,0,10,1 #(2):11,0,11,1-12,0,12,1 !(DuplicateObjectMember):13,0,13,1")
PARSE_TEST("object members must be unique (3)", TrackObjectMembers, "{\"x\":1,\"y\":{\"TRUE\":true,\"FALSE\":false},\"x\":3}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 m(y):7,0,7,1-10,0,10,1 {:11,0,11,1-12,0,12,1 m(TRUE):12,0,12,2-18,0,18,2 t:19,0,19,2-23,0,23,2 m(FALSE):24,0,24,2-31,0,31,2 f:32,0,32,2-37,0,37,2 }:37,0,37,1-38,0,38,1 !(DuplicateObjectMember):39,0,39,1")
PARSE_TEST("object members must be unique (4)", TrackObjectMembers, "{\"x\":1,\"y\":{\"TRUE\":true,\"TRUE\":true},\"z\":3}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 m(y):7,0,7,1-10,0,10,1 {:11,0,11,1-12,0,12,1 m(TRUE):12,0,12,2-18,0,18,2 t:19,0,19,2-23,0,23,2 !(DuplicateObjectMember):24,0,24,2")
PARSE_TEST("object members must be unique (5)", TrackObjectMembers, "{\"x\":1,\"y\":2,\"y\":3}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 m(y):7,0,7,1-10,0,10,1 #(2):11,0,11,1-12,0,12,1 !(DuplicateObjectMember):13,0,13,1")
PARSE_TEST("allow duplicate object members (1)", Standard, "{\"x\":1,\"x\":2}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 m(x):7,0,7,1-10,0,10,1 #(2):11,0,11,1-12,0,12,1 }:12,0,12,0-13,0,13,0")
PARSE_TEST("allow duplicate object members (2)", Standard, "{\"x\":1,\"y\":2,\"x\":3}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 m(y):7,0,7,1-10,0,10,1 #(2):11,0,11,1-12,0,12,1 m(x):13,0,13,1-16,0,16,1 #(3):17,0,17,1-18,0,18,1 }:18,0,18,0-19,0,19,0")
PARSE_TEST("allow duplicate object members (3)", Standard, "{\"x\":1,\"y\":{\"TRUE\":true,\"FALSE\":false},\"x\":3}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 m(y):7,0,7,1-10,0,10,1 {:11,0,11,1-12,0,12,1 m(TRUE):12,0,12,2-18,0,18,2 t:19,0,19,2-23,0,23,2 m(FALSE):24,0,24,2-31,0,31,2 f:32,0,32,2-37,0,37,2 }:37,0,37,1-38,0,38,1 m(x):39,0,39,1-42,0,42,1 #(3):43,0,43,1-44,0,44,1 }:44,0,44,0-45,0,45,0")
PARSE_TEST("allow duplicate object members (4)", Standard, "{\"x\":1,\"y\":{\"TRUE\":true,\"TRUE\":true},\"z\":3}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 m(y):7,0,7,1-10,0,10,1 {:11,0,11,1-12,0,12,1 m(TRUE):12,0,12,2-18,0,18,2 t:19,0,19,2-23,0,23,2 m(TRUE):24,0,24,2-30,0,30,2 t:31,0,31,2-35,0,35,2 }:35,0,35,1-36,0,36,1 m(z):37,0,37,1-40,0,40,1 #(3):41,0,41,1-42,0,42,1 }:42,0,42,0-43,0,43,0")
PARSE_TEST("allow duplicate object members (5)", Standard, "{\"x\":1,\"y\":2,\"y\":3}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(x):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 m(y):7,0,7,1-10,0,10,1 #(2):11,0,11,1-12,0,12,1 m(y):13,0,13,1-16,0,16,1 #(3):17,0,17,1-18,0,18,1 }:18,0,18,0-19,0,19,0")
PARSE_TEST("detect duplicate object member in callback", Standard, "{\"duplicate\":0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(DuplicateObjectMember):1,0,1,1")
PARSE_TEST("empty string object member name (1)", Standard, "{\"\":0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m():1,0,1,1-3,0,3,1 #(0):4,0,4,1-5,0,5,1 }:5,0,5,0-6,0,6,0")
PARSE_TEST("empty string object member name (2)", TrackObjectMembers, "{\"\":0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m():1,0,1,1-3,0,3,1 #(0):4,0,4,1-5,0,5,1 }:5,0,5,0-6,0,6,0")
PARSE_TEST("empty string object member name (3)", TrackObjectMembers, "{\"\":0,\"x\":1}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m():1,0,1,1-3,0,3,1 #(0):4,0,4,1-5,0,5,1 m(x):6,0,6,1-9,0,9,1 #(1):10,0,10,1-11,0,11,1 }:11,0,11,0-12,0,12,0")
PARSE_TEST("empty string object member name (4)", TrackObjectMembers, "{\"\":0,\"\":1}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m():1,0,1,1-3,0,3,1 #(0):4,0,4,1-5,0,5,1 !(DuplicateObjectMember):6,0,6,1")

/* arrays */

PARSE_TEST("start array", UTF8In, "[", PARTIAL, UTF8, "[:0,0,0,0-1,0,1,0")
PARSE_TEST("empty array", Standard, "[]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 ]:1,0,1,0-2,0,2,0")
PARSE_TEST("single-item array", Standard, "[ 3.14159 ]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:2,0,2,1-9,0,9,1 #(. 3.14159):2,0,2,1-9,0,9,1 ]:10,0,10,0-11,0,11,0")
PARSE_TEST("multi-item array", Standard, "[ 3.14159, 2.71828 ]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:2,0,2,1-9,0,9,1 #(. 3.14159):2,0,2,1-9,0,9,1 i:11,0,11,1-18,0,18,1 #(. 2.71828):11,0,11,1-18,0,18,1 ]:19,0,19,0-20,0,20,0")
PARSE_TEST("all types of array items", AllowSpecialNumbers | AllowHexNumbers, "[ null, true, \"foo\", 17, NaN, 0xbeef, {}, [] ]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:2,0,2,1-6,0,6,1 n:2,0,2,1-6,0,6,1 i:8,0,8,1-12,0,12,1 t:8,0,8,1-12,0,12,1 i:14,0,14,1-19,0,19,1 s(foo):14,0,14,1-19,0,19,1 i:21,0,21,1-23,0,23,1 #(17):21,0,21,1-23,0,23,1 i:25,0,25,1-28,0,28,1 #(NaN):25,0,25,1-28,0,28,1 i:30,0,30,1-36,0,36,1 #(x 0xbeef):30,0,30,1-36,0,36,1 i:38,0,38,1-39,0,39,1 {:38,0,38,1-39,0,39,1 }:39,0,39,1-40,0,40,1 i:42,0,42,1-43,0,43,1 [:42,0,42,1-43,0,43,1 ]:43,0,43,1-44,0,44,1 ]:45,0,45,0-46,0,46,0")
PARSE_TEST("nested arrays", Standard, "[[],[[],[[],[[],[[],[]]]]]]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 [:1,0,1,1-2,0,2,1 ]:2,0,2,1-3,0,3,1 i:4,0,4,1-5,0,5,1 [:4,0,4,1-5,0,5,1 i:5,0,5,2-6,0,6,2 [:5,0,5,2-6,0,6,2 ]:6,0,6,2-7,0,7,2 i:8,0,8,2-9,0,9,2 [:8,0,8,2-9,0,9,2 i:9,0,9,3-10,0,10,3 [:9,0,9,3-10,0,10,3 ]:10,0,10,3-11,0,11,3 i:12,0,12,3-13,0,13,3 [:12,0,12,3-13,0,13,3 i:13,0,13,4-14,0,14,4 [:13,0,13,4-14,0,14,4 ]:14,0,14,4-15,0,15,4 i:16,0,16,4-17,0,17,4 [:16,0,16,4-17,0,17,4 i:17,0,17,5-18,0,18,5 [:17,0,17,5-18,0,18,5 ]:18,0,18,5-19,0,19,5 i:20,0,20,5-21,0,21,5 [:20,0,20,5-21,0,21,5 ]:21,0,21,5-22,0,22,5 ]:22,0,22,4-23,0,23,4 ]:23,0,23,3-24,0,24,3 ]:24,0,24,2-25,0,25,2 ]:25,0,25,1-26,0,26,1 ]:26,0,26,0-27,0,27,0")
PARSE_TEST("array truncated after left square brace", Standard, "[", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(ExpectedMoreTokens):1,0,1,1")
PARSE_TEST("array truncated after item value (1)", Standard, "[1", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(1):1,0,1,1-2,0,2,1 !(ExpectedMoreTokens):2,0,2,1")
PARSE_TEST("array truncated after item value (2)", Standard, "[1,2", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(1):1,0,1,1-2,0,2,1 i:3,0,3,1-4,0,4,1 #(2):3,0,3,1-4,0,4,1 !(ExpectedMoreTokens):4,0,4,1")
PARSE_TEST("array truncated after comma (1)", Standard, "[1,", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(1):1,0,1,1-2,0,2,1 !(ExpectedMoreTokens):3,0,3,1")
PARSE_TEST("array truncated after comma (2)", Standard, "[1,2,", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(1):1,0,1,1-2,0,2,1 i:3,0,3,1-4,0,4,1 #(2):3,0,3,1-4,0,4,1 !(ExpectedMoreTokens):5,0,5,1")
PARSE_TEST("array item missing (1)", Standard, "[,2]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("array item missing (2)", Standard, "[1,,2]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(1):1,0,1,1-2,0,2,1 !(UnexpectedToken):3,0,3,1")
PARSE_TEST("array item missing (3)", Standard, "[1,]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(1):1,0,1,1-2,0,2,1 !(UnexpectedToken):3,0,3,1")
PARSE_TEST("array items require comma separator", Standard, "[1 2]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(1):1,0,1,1-2,0,2,1 !(UnexpectedToken):3,0,3,1")

/* comments */

PARSE_TEST("single-line comment not allowed (1)", Standard, "0 // comment", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(UnknownToken):2,0,2,0")
PARSE_TEST("single-line comment not allowed (2)", Standard, "// comment\r\n0", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("multi-line comment not allowed (1)", Standard, "0 /* comment */", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(UnknownToken):2,0,2,0")
PARSE_TEST("multi-line comment not allowed (2)", Standard, "/* comment */0", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("multi-line comment not allowed (3)", Standard, "/* comment \r\n * / * /*/0", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("multi-line comment not allowed (4)", Standard, "/* comment \r\n * / * /*/\r\n0", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("single-line comment (1)", AllowComments, "0 //", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0")
PARSE_TEST("single-line comment (2)", AllowComments, "0 // comment", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0")
PARSE_TEST("single-line comment (3)", AllowComments, "// comment\r\n0", FINAL, UTF8, "u(8) #(0):12,1,0,0-13,1,1,0")
PARSE_TEST("single-line comment with extra slashes", AllowComments, "0 ////////////", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0")
PARSE_TEST("single-line comment in object (1)", AllowComments, "{// comment\n\"a\":0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):12,1,0,1-15,1,3,1 #(0):16,1,4,1-17,1,5,1 }:17,1,5,0-18,1,6,0")
PARSE_TEST("single-line comment in object (2)", AllowComments, "{\"a\"// comment\n:0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 #(0):16,1,1,1-17,1,2,1 }:17,1,2,0-18,1,3,0")
PARSE_TEST("single-line comment in object (3)", AllowComments, "{\"a\":// comment\n0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 #(0):16,1,0,1-17,1,1,1 }:17,1,1,0-18,1,2,0")
PARSE_TEST("single-line comment in object (4)", AllowComments, "{\"a\":0// comment\n}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 #(0):5,0,5,1-6,0,6,1 }:17,1,0,0-18,1,1,0")
PARSE_TEST("single-line comment in object (5)", AllowComments, "{\"a\":0// comment\n,\"b\":1}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 #(0):5,0,5,1-6,0,6,1 m(b):18,1,1,1-21,1,4,1 #(1):22,1,5,1-23,1,6,1 }:23,1,6,0-24,1,7,0")
PARSE_TEST("single-line comment in object (6)", AllowComments, "{\"a\":0,// comment\n\"b\":1}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 #(0):5,0,5,1-6,0,6,1 m(b):18,1,0,1-21,1,3,1 #(1):22,1,4,1-23,1,5,1 }:23,1,5,0-24,1,6,0")
PARSE_TEST("single-line comment in array (1)", AllowComments, "[// comment\n0]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:12,1,0,1-13,1,1,1 #(0):12,1,0,1-13,1,1,1 ]:13,1,1,0-14,1,2,0")
PARSE_TEST("single-line comment in array (2)", AllowComments, "[0// comment\n]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(0):1,0,1,1-2,0,2,1 ]:13,1,0,0-14,1,1,0")
PARSE_TEST("single-line comment in array (3)", AllowComments, "[0// comment\n,1]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(0):1,0,1,1-2,0,2,1 i:14,1,1,1-15,1,2,1 #(1):14,1,1,1-15,1,2,1 ]:15,1,2,0-16,1,3,0")
PARSE_TEST("single-line comment in array (4)", AllowComments, "[0,// comment\n1]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(0):1,0,1,1-2,0,2,1 i:14,1,0,1-15,1,1,1 #(1):14,1,0,1-15,1,1,1 ]:15,1,1,0-16,1,2,0")
PARSE_TEST("multi-line comment (1)", AllowComments, "0 /**/", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0")
PARSE_TEST("multi-line comment (2)", AllowComments, "0 /* comment */", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0")
PARSE_TEST("multi-line comment (3)", AllowComments, "/* comment */0", FINAL, UTF8, "u(8) #(0):13,0,13,0-14,0,14,0")
PARSE_TEST("multi-line comment (4)", AllowComments, "/* comment \r\n * / * /*/0", FINAL, UTF8, "u(8) #(0):23,1,10,0-24,1,11,0")
PARSE_TEST("multi-line comment (5)", AllowComments, "/* comment \r\n * / * /*/\r\n0", FINAL, UTF8, "u(8) #(0):25,2,0,0-26,2,1,0")
PARSE_TEST("multi-line comment with extra stars", AllowComments, "0 /************/", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0")
PARSE_TEST("multi-line comment in object (1)", AllowComments, "{/* comment */\"a\":0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):14,0,14,1-17,0,17,1 #(0):18,0,18,1-19,0,19,1 }:19,0,19,0-20,0,20,0")
PARSE_TEST("multi-line comment in object (2)", AllowComments, "{\"a\"/* comment */:0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 #(0):18,0,18,1-19,0,19,1 }:19,0,19,0-20,0,20,0")
PARSE_TEST("multi-line comment in object (3)", AllowComments, "{\"a\":/* comment */0}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 #(0):18,0,18,1-19,0,19,1 }:19,0,19,0-20,0,20,0")
PARSE_TEST("multi-line comment in object (4)", AllowComments, "{\"a\":0/* comment */}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 #(0):5,0,5,1-6,0,6,1 }:19,0,19,0-20,0,20,0")
PARSE_TEST("multi-line comment in object (5)", AllowComments, "{\"a\":0/* comment */,\"b\":1}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 #(0):5,0,5,1-6,0,6,1 m(b):20,0,20,1-23,0,23,1 #(1):24,0,24,1-25,0,25,1 }:25,0,25,0-26,0,26,0")
PARSE_TEST("multi-line comment in object (6)", AllowComments, "{\"a\":0,/* comment */\"b\":1}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 #(0):5,0,5,1-6,0,6,1 m(b):20,0,20,1-23,0,23,1 #(1):24,0,24,1-25,0,25,1 }:25,0,25,0-26,0,26,0")
PARSE_TEST("multi-line comment in array (1)", AllowComments, "[/* comment */0]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:14,0,14,1-15,0,15,1 #(0):14,0,14,1-15,0,15,1 ]:15,0,15,0-16,0,16,0")
PARSE_TEST("multi-line comment in array (2)", AllowComments, "[0/* comment */]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(0):1,0,1,1-2,0,2,1 ]:15,0,15,0-16,0,16,0")
PARSE_TEST("multi-line comment in array (3)", AllowComments, "[0/* comment */,1]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(0):1,0,1,1-2,0,2,1 i:16,0,16,1-17,0,17,1 #(1):16,0,16,1-17,0,17,1 ]:17,0,17,0-18,0,18,0")
PARSE_TEST("multi-line comment in array (4)", AllowComments, "[0,/* comment */1]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(0):1,0,1,1-2,0,2,1 i:16,0,16,1-17,0,17,1 #(1):16,0,16,1-17,0,17,1 ]:17,0,17,0-18,0,18,0")
PARSE_TEST("unclosed multi-line comment (1)", AllowComments, "/*", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("unclosed multi-line comment (2)", AllowComments, "/* comment", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("just a comment (1)", AllowComments, "//", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):2,0,2,0")
PARSE_TEST("just a comment (2)", AllowComments, "/**/", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):4,0,4,0")
PARSE_TEST("comment between tokens (1)", AllowComments, "[//\n]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 ]:4,1,0,0-5,1,1,0")
PARSE_TEST("comment between tokens (2)", AllowComments, "[/**/]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 ]:5,0,5,0-6,0,6,0")
PARSE_TEST("lone forward slash (1)", AllowComments, "/", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("lone forward slash (2)", AllowComments, "/ ", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")

/* random tokens */

PARSE_TEST("random ]", Standard, "]", FINAL, UTF8, "u(8) !(UnexpectedToken):0,0,0,0")
PARSE_TEST("random }", Standard, "}", FINAL, UTF8, "u(8) !(UnexpectedToken):0,0,0,0")
PARSE_TEST("random :", Standard, ":", FINAL, UTF8, "u(8) !(UnexpectedToken):0,0,0,0")
PARSE_TEST("random ,", Standard, ",", FINAL, UTF8, "u(8) !(UnexpectedToken):0,0,0,0")
PARSE_TEST("single-quoted strings not allowed", Standard, "'abc'", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("random \\", Standard, "\\n", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("random /", Standard, "/", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")

/* multi-line input */

PARSE_TEST("multi-line input", Standard, "[\r 1,\n  2,\r\n\r\n   3]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:3,1,1,1-4,1,2,1 #(1):3,1,1,1-4,1,2,1 i:8,2,2,1-9,2,3,1 #(2):8,2,2,1-9,2,3,1 i:17,4,3,1-18,4,4,1 #(3):17,4,3,1-18,4,4,1 ]:18,4,4,0-19,4,5,0")
PARSE_TEST("multi-line input error (1)", Standard, "[\r1", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:2,1,0,1-3,1,1,1 #(1):2,1,0,1-3,1,1,1 !(ExpectedMoreTokens):3,1,1,1")
PARSE_TEST("multi-line input error (2)", Standard, "[\n1", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:2,1,0,1-3,1,1,1 #(1):2,1,0,1-3,1,1,1 !(ExpectedMoreTokens):3,1,1,1")
PARSE_TEST("multi-line input error (3)", Standard, "[\r\n1", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:3,1,0,1-4,1,1,1 #(1):3,1,0,1-4,1,1,1 !(ExpectedMoreTokens):4,1,1,1")
PARSE_TEST("multi-line input error (4)", Standard, "[\r1,\n2\r\n", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:2,1,0,1-3,1,1,1 #(1):2,1,0,1-3,1,1,1 i:5,2,0,1-6,2,1,1 #(2):5,2,0,1-6,2,1,1 !(ExpectedMoreTokens):8,3,0,1")
PARSE_TEST("multi-line input error (5)", Standard, "[\r\"x\n", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(UnescapedControlCharacter):4,1,2,1")
PARSE_TEST("multi-line input error (6)", Standard, "[\n\"x\n", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(UnescapedControlCharacter):4,1,2,1")
PARSE_TEST("multi-line input error (7)", Standard, "[\r\n\"x\r\n", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(UnescapedControlCharacter):5,1,2,1")

/* embedded document */

PARSE_TEST("embedded empty document", StopAfterEmbeddedDocument, "", FINAL, UnknownEncoding, "!(ExpectedMoreTokens):0,0,0,0")

PARSE_TEST("embedded document invalid sequence (1)", StopAfterEmbeddedDocument, "\xFF", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded document invalid sequence (2)", StopAfterEmbeddedDocument, "\xFF\xFF", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded document invalid sequence (3)", StopAfterEmbeddedDocument, "\xFF\xFF\xFF", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded document invalid sequence (4)", StopAfterEmbeddedDocument, "\x00\x00\x00\x00", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("embedded document invalid sequence (5)", StopAfterEmbeddedDocument, "\x00\x00", FINAL, UnknownEncoding, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded document invalid sequence (6)", StopAfterEmbeddedDocument, " \x00\x00 ", FINAL, UTF8, "u(8) !(UnknownToken):1,0,1,0")
PARSE_TEST("embedded document invalid sequence (7)", StopAfterEmbeddedDocument, "{\xFF ", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(InvalidEncodingSequence):1,0,1,1")

PARSE_TEST("embedded document invalid UTF-16LE sequence (1)", UTF16LEIn | StopAfterEmbeddedDocument, "\x00", FINAL, UTF16LE, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded document invalid UTF-16LE sequence (2)", UTF16LEIn | StopAfterEmbeddedDocument, " \x00 ", FINAL, UTF16LE, "!(ExpectedMoreTokens):2,0,1,0")
PARSE_TEST("embedded document invalid UTF-16LE sequence (3)", UTF16LEIn | StopAfterEmbeddedDocument, "{\x00\xFF", FINAL, UTF16LE, "{:0,0,0,0-2,0,1,0 !(InvalidEncodingSequence):2,0,1,1")

PARSE_TEST("embedded document invalid UTF-16BE sequence (1)", UTF16BEIn | StopAfterEmbeddedDocument, "\x00", FINAL, UTF16BE, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded document invalid UTF-16BE sequence (2)", UTF16BEIn | StopAfterEmbeddedDocument, "\x00 \x00", FINAL, UTF16BE, "!(ExpectedMoreTokens):2,0,1,0")
PARSE_TEST("embedded document invalid UTF-16BE sequence (3)", UTF16BEIn | StopAfterEmbeddedDocument, "\x00{\xFF", FINAL, UTF16BE, "{:0,0,0,0-2,0,1,0 !(InvalidEncodingSequence):2,0,1,1")

PARSE_TEST("embedded document invalid UTF-32LE sequence (1)", UTF32LEIn | StopAfterEmbeddedDocument, "\x00", FINAL, UTF32LE, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded document invalid UTF-32LE sequence (2)", UTF32LEIn | StopAfterEmbeddedDocument, "\x00\x00", FINAL, UTF32LE, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded document invalid UTF-32LE sequence (3)", UTF32LEIn | StopAfterEmbeddedDocument, "\x00\x00\x00", FINAL, UTF32LE, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded document invalid UTF-32LE sequence (4)", UTF32LEIn | StopAfterEmbeddedDocument, "\x00\x00\x00\xFF", FINAL, UTF32LE, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded document invalid UTF-32LE sequence (5)", UTF32LEIn | StopAfterEmbeddedDocument, "{\x00\x00\x00\x00\x00\x00\xFF", FINAL, UTF32LE, "{:0,0,0,0-4,0,1,0 !(InvalidEncodingSequence):4,0,1,1")

PARSE_TEST("embedded document invalid UTF-32BE sequence (1)", UTF32BEIn | StopAfterEmbeddedDocument, "\x00", FINAL, UTF32BE, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded document invalid UTF-32BE sequence (2)", UTF32BEIn | StopAfterEmbeddedDocument, "\x00\x00", FINAL, UTF32BE, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded document invalid UTF-32BE sequence (3)", UTF32BEIn | StopAfterEmbeddedDocument, "\x00\x00\x00", FINAL, UTF32BE, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded document invalid UTF-32BE sequence (4)", UTF32BEIn | StopAfterEmbeddedDocument, "\xFF\x00\x00\x00", FINAL, UTF32BE, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded document invalid UTF-32BE sequence (5)", UTF32BEIn | StopAfterEmbeddedDocument, "\x00\x00\x00{\xFF\x00\x00\x00", FINAL, UTF32BE, "{:0,0,0,0-4,0,1,0 !(InvalidEncodingSequence):4,0,1,1")

PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (1)", StopAfterEmbeddedDocument, "\xC2", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (2)", StopAfterEmbeddedDocument, "\xE0", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (3)", StopAfterEmbeddedDocument, "\xE0\xBF", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (4)", StopAfterEmbeddedDocument, "\xF0\xBF", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (5)", StopAfterEmbeddedDocument, "\xF0\xBF\xBF", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (6)", StopAfterEmbeddedDocument, "\xC0", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (7)", StopAfterEmbeddedDocument, "\xE0\x80", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (8)", StopAfterEmbeddedDocument, "\xED\xA0", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (9)", StopAfterEmbeddedDocument, "\xF0\x80", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (10)", StopAfterEmbeddedDocument, "\xF4\x90", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (11)", StopAfterEmbeddedDocument, "\x80", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (12)", StopAfterEmbeddedDocument, "\xC2\x7F", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (13)", StopAfterEmbeddedDocument, "\xE1\xBF\x7F", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (14)", StopAfterEmbeddedDocument, "\xF1\x7F", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (15)", StopAfterEmbeddedDocument, "\xF1\xBF\x7F", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("embedded empty document followed by invalid UTF-8 sequence (16)", StopAfterEmbeddedDocument, "\xF1\xBF\xBF\x7F", FINAL, UTF8, "u(8) !(ExpectedMoreTokens):0,0,0,0")

PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (1)", StopAfterEmbeddedDocument, "0\xC2", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (2)", StopAfterEmbeddedDocument, "0\xE0", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (3)", StopAfterEmbeddedDocument, "0\xE0\xBF", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (4)", StopAfterEmbeddedDocument, "0\xF0\xBF", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (5)", StopAfterEmbeddedDocument, "0\xF0\xBF\xBF", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (6)", StopAfterEmbeddedDocument, "0\xC0", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (7)", StopAfterEmbeddedDocument, "0\xE0\x80", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (8)", StopAfterEmbeddedDocument, "0\xED\xA0", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (9)", StopAfterEmbeddedDocument, "0\xF0\x80", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (10)", StopAfterEmbeddedDocument, "0\xF4\x90", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (11)", StopAfterEmbeddedDocument, "0\x80", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (12)", StopAfterEmbeddedDocument, "0\xC2\x7F", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (13)", StopAfterEmbeddedDocument, "0\xE1\xBF\x7F", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (14)", StopAfterEmbeddedDocument, "0\xF1\x7F", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (15)", StopAfterEmbeddedDocument, "0\xF1\xBF\x7F", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded one-character document followed by invalid UTF-8 sequence (16)", StopAfterEmbeddedDocument, "0\xF1\xBF\xBF\x7F", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")

PARSE_TEST("embedded null (1)", StopAfterEmbeddedDocument, "nul", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("embedded null (2)", StopAfterEmbeddedDocument, "nulx", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("embedded null (3)", StopAfterEmbeddedDocument, "null", FINAL, UTF8, "u(8) n:0,0,0,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")
PARSE_TEST("embedded null (4)", StopAfterEmbeddedDocument, " null ", FINAL, UTF8, "u(8) n:1,0,1,0-5,0,5,0 !(StoppedAfterEmbeddedDocument):5,0,5,0")
PARSE_TEST("embedded null (5)", StopAfterEmbeddedDocument, "null!", FINAL, UTF8, "u(8) n:0,0,0,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")
PARSE_TEST("embedded null (6)", StopAfterEmbeddedDocument, " null!", FINAL, UTF8, "u(8) n:1,0,1,0-5,0,5,0 !(StoppedAfterEmbeddedDocument):5,0,5,0")
PARSE_TEST("embedded null (7)", StopAfterEmbeddedDocument, "nullx", FINAL, UTF8, "u(8) n:0,0,0,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")
PARSE_TEST("embedded null (8)", StopAfterEmbeddedDocument, "[nullx]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(UnknownToken):1,0,1,1")
PARSE_TEST("embedded null (9)", StopAfterEmbeddedDocument, "{\"a\":nullx}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(UnknownToken):5,0,5,1")

PARSE_TEST("embedded true (1)", StopAfterEmbeddedDocument, "tru", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("embedded true (2)", StopAfterEmbeddedDocument, "trux", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("embedded true (3)", StopAfterEmbeddedDocument, "true", FINAL, UTF8, "u(8) t:0,0,0,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")
PARSE_TEST("embedded true (4)", StopAfterEmbeddedDocument, " true ", FINAL, UTF8, "u(8) t:1,0,1,0-5,0,5,0 !(StoppedAfterEmbeddedDocument):5,0,5,0")
PARSE_TEST("embedded true (5)", StopAfterEmbeddedDocument, "true!", FINAL, UTF8, "u(8) t:0,0,0,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")
PARSE_TEST("embedded true (6)", StopAfterEmbeddedDocument, " true!", FINAL, UTF8, "u(8) t:1,0,1,0-5,0,5,0 !(StoppedAfterEmbeddedDocument):5,0,5,0")
PARSE_TEST("embedded true (7)", StopAfterEmbeddedDocument, "truex", FINAL, UTF8, "u(8) t:0,0,0,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")
PARSE_TEST("embedded true (8)", StopAfterEmbeddedDocument, "[truex]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(UnknownToken):1,0,1,1")
PARSE_TEST("embedded true (9)", StopAfterEmbeddedDocument, "{\"a\":truex}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(UnknownToken):5,0,5,1")

PARSE_TEST("embedded false (1)", StopAfterEmbeddedDocument, "fals", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("embedded false (2)", StopAfterEmbeddedDocument, "falsx", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("embedded false (3)", StopAfterEmbeddedDocument, "false", FINAL, UTF8, "u(8) f:0,0,0,0-5,0,5,0 !(StoppedAfterEmbeddedDocument):5,0,5,0")
PARSE_TEST("embedded false (5)", StopAfterEmbeddedDocument, " false ", FINAL, UTF8, "u(8) f:1,0,1,0-6,0,6,0 !(StoppedAfterEmbeddedDocument):6,0,6,0")
PARSE_TEST("embedded false (6)", StopAfterEmbeddedDocument, "false!", FINAL, UTF8, "u(8) f:0,0,0,0-5,0,5,0 !(StoppedAfterEmbeddedDocument):5,0,5,0")
PARSE_TEST("embedded false (6)", StopAfterEmbeddedDocument, " false!", FINAL, UTF8, "u(8) f:1,0,1,0-6,0,6,0 !(StoppedAfterEmbeddedDocument):6,0,6,0")
PARSE_TEST("embedded false (7)", StopAfterEmbeddedDocument, "falsex", FINAL, UTF8, "u(8) f:0,0,0,0-5,0,5,0 !(StoppedAfterEmbeddedDocument):5,0,5,0")
PARSE_TEST("embedded false (8)", StopAfterEmbeddedDocument, "[falsex]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(UnknownToken):1,0,1,1")
PARSE_TEST("embedded false (9)", StopAfterEmbeddedDocument, "{\"a\":falsex}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(UnknownToken):5,0,5,1")

PARSE_TEST("embedded NaN (1)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "Na", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("embedded NaN (2)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "Nax", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("embedded NaN (3)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "NaN", FINAL, UTF8, "u(8) #(NaN):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded NaN (4)", AllowSpecialNumbers | StopAfterEmbeddedDocument, " NaN ", FINAL, UTF8, "u(8) #(NaN):1,0,1,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")
PARSE_TEST("embedded NaN (5)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "NaN!", FINAL, UTF8, "u(8) #(NaN):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded NaN (6)", AllowSpecialNumbers | StopAfterEmbeddedDocument, " NaN!", FINAL, UTF8, "u(8) #(NaN):1,0,1,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")
PARSE_TEST("embedded NaN (7)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "NaNx", FINAL, UTF8, "u(8) #(NaN):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded NaN (8)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "[NaNx]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(UnknownToken):1,0,1,1")
PARSE_TEST("embedded NaN (9)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "{\"a\":NaNx}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(UnknownToken):5,0,5,1")

PARSE_TEST("embedded Infinity (1)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "Infinit", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("embedded Infinity (2)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "Infinitx", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("embedded Infinity (3)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "Infinity", FINAL, UTF8, "u(8) #(Infinity):0,0,0,0-8,0,8,0 !(StoppedAfterEmbeddedDocument):8,0,8,0")
PARSE_TEST("embedded Infinity (4)", AllowSpecialNumbers | StopAfterEmbeddedDocument, " Infinity ", FINAL, UTF8, "u(8) #(Infinity):1,0,1,0-9,0,9,0 !(StoppedAfterEmbeddedDocument):9,0,9,0")
PARSE_TEST("embedded Infinity (5)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "Infinity!", FINAL, UTF8, "u(8) #(Infinity):0,0,0,0-8,0,8,0 !(StoppedAfterEmbeddedDocument):8,0,8,0")
PARSE_TEST("embedded Infinity (6)", AllowSpecialNumbers | StopAfterEmbeddedDocument, " Infinity!", FINAL, UTF8, "u(8) #(Infinity):1,0,1,0-9,0,9,0 !(StoppedAfterEmbeddedDocument):9,0,9,0")
PARSE_TEST("embedded Infinity (7)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "Infinityx", FINAL, UTF8, "u(8) #(Infinity):0,0,0,0-8,0,8,0 !(StoppedAfterEmbeddedDocument):8,0,8,0")
PARSE_TEST("embedded Infinity (8)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "[Infinityx]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(UnknownToken):1,0,1,1")
PARSE_TEST("embedded Infinity (9)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "{\"a\":Infinityx}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(UnknownToken):5,0,5,1")

PARSE_TEST("embedded -Infinity (1)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "-Infinit", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("embedded -Infinity (2)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "-Infinitx", FINAL, UTF8, "u(8) !(UnknownToken):0,0,0,0")
PARSE_TEST("embedded -Infinity (3)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "-Infinity", FINAL, UTF8, "u(8) #(-Infinity):0,0,0,0-9,0,9,0 !(StoppedAfterEmbeddedDocument):9,0,9,0")
PARSE_TEST("embedded -Infinity (4)", AllowSpecialNumbers | StopAfterEmbeddedDocument, " -Infinity ", FINAL, UTF8, "u(8) #(-Infinity):1,0,1,0-10,0,10,0 !(StoppedAfterEmbeddedDocument):10,0,10,0")
PARSE_TEST("embedded -Infinity (5)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "-Infinity!", FINAL, UTF8, "u(8) #(-Infinity):0,0,0,0-9,0,9,0 !(StoppedAfterEmbeddedDocument):9,0,9,0")
PARSE_TEST("embedded -Infinity (6)", AllowSpecialNumbers | StopAfterEmbeddedDocument, " -Infinity!", FINAL, UTF8, "u(8) #(-Infinity):1,0,1,0-10,0,10,0 !(StoppedAfterEmbeddedDocument):10,0,10,0")
PARSE_TEST("embedded -Infinity (7)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "-Infinityx", FINAL, UTF8, "u(8) #(-Infinity):0,0,0,0-9,0,9,0 !(StoppedAfterEmbeddedDocument):9,0,9,0")
PARSE_TEST("embedded -Infinity (8)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "[-Infinityx]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(UnknownToken):1,0,1,1")
PARSE_TEST("embedded -Infinity (9)", AllowSpecialNumbers | StopAfterEmbeddedDocument, "{\"a\":-Infinityx}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(UnknownToken):5,0,5,1")

PARSE_TEST("embedded 0 (1)", StopAfterEmbeddedDocument, "0", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 0 (2)", StopAfterEmbeddedDocument, " 0 ", FINAL, UTF8, "u(8) #(0):1,0,1,0-2,0,2,0 !(StoppedAfterEmbeddedDocument):2,0,2,0")
PARSE_TEST("embedded 0 (3)", StopAfterEmbeddedDocument, "00", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 0 (4)", StopAfterEmbeddedDocument, "01", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 0 (5)", StopAfterEmbeddedDocument, "0!", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 0 (6)", StopAfterEmbeddedDocument, "0x", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 0 (7)", StopAfterEmbeddedDocument, "[01]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(InvalidNumber):1,0,1,1")
PARSE_TEST("embedded 0 (8)", StopAfterEmbeddedDocument, "{\"a\":01}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(InvalidNumber):5,0,5,1")

PARSE_TEST("embedded -0 (1)", StopAfterEmbeddedDocument, "-0", FINAL, UTF8, "u(8) #(- -0):0,0,0,0-2,0,2,0 !(StoppedAfterEmbeddedDocument):2,0,2,0")
PARSE_TEST("embedded -0 (2)", StopAfterEmbeddedDocument, " -0 ", FINAL, UTF8, "u(8) #(- -0):1,0,1,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded -0 (3)", StopAfterEmbeddedDocument, "-00", FINAL, UTF8, "u(8) #(- -0):0,0,0,0-2,0,2,0 !(StoppedAfterEmbeddedDocument):2,0,2,0")
PARSE_TEST("embedded -0 (4)", StopAfterEmbeddedDocument, "-01", FINAL, UTF8, "u(8) #(- -0):0,0,0,0-2,0,2,0 !(StoppedAfterEmbeddedDocument):2,0,2,0")
PARSE_TEST("embedded -0 (5)", StopAfterEmbeddedDocument, "-0!", FINAL, UTF8, "u(8) #(- -0):0,0,0,0-2,0,2,0 !(StoppedAfterEmbeddedDocument):2,0,2,0")
PARSE_TEST("embedded -0 (6)", StopAfterEmbeddedDocument, "-0x", FINAL, UTF8, "u(8) #(- -0):0,0,0,0-2,0,2,0 !(StoppedAfterEmbeddedDocument):2,0,2,0")
PARSE_TEST("embedded -0 (7)", StopAfterEmbeddedDocument, "[-01]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(InvalidNumber):1,0,1,1")
PARSE_TEST("embedded -0 (8)", StopAfterEmbeddedDocument, "{\"a\":-01}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(InvalidNumber):5,0,5,1")

PARSE_TEST("embedded 123 (1)", StopAfterEmbeddedDocument, "123", FINAL, UTF8, "u(8) #(123):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded 123 (2)", StopAfterEmbeddedDocument, " 123 ", FINAL, UTF8, "u(8) #(123):1,0,1,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")
PARSE_TEST("embedded 123 (5)", StopAfterEmbeddedDocument, "123!", FINAL, UTF8, "u(8) #(123):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded 123 (6)", StopAfterEmbeddedDocument, "123e", FINAL, UTF8, "u(8) #(123):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded 123 (7)", StopAfterEmbeddedDocument, "[123e]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(InvalidNumber):1,0,1,1")
PARSE_TEST("embedded 123 (8)", StopAfterEmbeddedDocument, "{\"a\":123e}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(InvalidNumber):5,0,5,1")

PARSE_TEST("embedded 1. (1)", StopAfterEmbeddedDocument, "1.", FINAL, UTF8, "u(8) #(1):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 1. (2)", StopAfterEmbeddedDocument, "1.!", FINAL, UTF8, "u(8) #(1):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 1. (3)", StopAfterEmbeddedDocument, "[123.]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(InvalidNumber):1,0,1,1")
PARSE_TEST("embedded 1. (4)", StopAfterEmbeddedDocument, "{\"a\":1.}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(InvalidNumber):5,0,5,1")

PARSE_TEST("embedded 1e (1)", StopAfterEmbeddedDocument, "1e", FINAL, UTF8, "u(8) #(1):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 1e (2)", StopAfterEmbeddedDocument, "1e!", FINAL, UTF8, "u(8) #(1):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 1e (3)", StopAfterEmbeddedDocument, "[123e]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(InvalidNumber):1,0,1,1")
PARSE_TEST("embedded 1e (4)", StopAfterEmbeddedDocument, "{\"a\":1e}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(InvalidNumber):5,0,5,1")

PARSE_TEST("embedded 1e+ (1)", StopAfterEmbeddedDocument, "1e+", FINAL, UTF8, "u(8) #(1):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 1e+ (2)", StopAfterEmbeddedDocument, "1e+!", FINAL, UTF8, "u(8) #(1):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 1e+ (3)", StopAfterEmbeddedDocument, "[123e+]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(InvalidNumber):1,0,1,1")
PARSE_TEST("embedded 1e+ (4)", StopAfterEmbeddedDocument, "{\"a\":1e+}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(InvalidNumber):5,0,5,1")

PARSE_TEST("embedded 1e- (1)", StopAfterEmbeddedDocument, "1e-", FINAL, UTF8, "u(8) #(1):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 1e- (2)", StopAfterEmbeddedDocument, "1e-!", FINAL, UTF8, "u(8) #(1):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 1e- (3)", StopAfterEmbeddedDocument, "[123e-]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(InvalidNumber):1,0,1,1")
PARSE_TEST("embedded 1e- (4)", StopAfterEmbeddedDocument, "{\"a\":1e-}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(InvalidNumber):5,0,5,1")

PARSE_TEST("embedded 1.2e (1)", StopAfterEmbeddedDocument, "1.2e", FINAL, UTF8, "u(8) #(. 1.2):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded 1.2e (2)", StopAfterEmbeddedDocument, "1.2e!", FINAL, UTF8, "u(8) #(. 1.2):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded 1.2e (3)", StopAfterEmbeddedDocument, "[1.2e]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(InvalidNumber):1,0,1,1")
PARSE_TEST("embedded 1.2e (4)", StopAfterEmbeddedDocument, "{\"a\":1.2e}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(InvalidNumber):5,0,5,1")

PARSE_TEST("embedded 1.2e+ (1)", StopAfterEmbeddedDocument, "1.2e+", FINAL, UTF8, "u(8) #(. 1.2):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded 1.2e+ (2)", StopAfterEmbeddedDocument, "1.2e+!", FINAL, UTF8, "u(8) #(. 1.2):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded 1.2e+ (3)", StopAfterEmbeddedDocument, "[1.2e+]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(InvalidNumber):1,0,1,1")
PARSE_TEST("embedded 1.2e+ (4)", StopAfterEmbeddedDocument, "{\"a\":1.2e+}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(InvalidNumber):5,0,5,1")

PARSE_TEST("embedded 1.2e- (1)", StopAfterEmbeddedDocument, "1.2e-", FINAL, UTF8, "u(8) #(. 1.2):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded 1.2e- (2)", StopAfterEmbeddedDocument, "1.2e-!", FINAL, UTF8, "u(8) #(. 1.2):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded 1.2e- (3)", StopAfterEmbeddedDocument, "[1.2e-]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(InvalidNumber):1,0,1,1")
PARSE_TEST("embedded 1.2e- (4)", StopAfterEmbeddedDocument, "{\"a\":1.2e-}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(InvalidNumber):5,0,5,1")

PARSE_TEST("embedded 0x (1)", AllowHexNumbers | StopAfterEmbeddedDocument, "0x", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 0x (2)", AllowHexNumbers | StopAfterEmbeddedDocument, "0x!", FINAL, UTF8, "u(8) #(0):0,0,0,0-1,0,1,0 !(StoppedAfterEmbeddedDocument):1,0,1,0")
PARSE_TEST("embedded 0x (3)", AllowHexNumbers | StopAfterEmbeddedDocument, "[0x]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(InvalidNumber):1,0,1,1")
PARSE_TEST("embedded 0x (4)", AllowHexNumbers | StopAfterEmbeddedDocument, "{\"a\":0x}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 !(InvalidNumber):5,0,5,1")

PARSE_TEST("embedded -12 (1)", StopAfterEmbeddedDocument, "-12", FINAL, UTF8, "u(8) #(- -12):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded -12 (2)", StopAfterEmbeddedDocument, " -12 ", FINAL, UTF8, "u(8) #(- -12):1,0,1,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")
PARSE_TEST("embedded 1.2 (1)", StopAfterEmbeddedDocument, "1.2", FINAL, UTF8, "u(8) #(. 1.2):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded 1.2 (2)", StopAfterEmbeddedDocument, " 1.2 ", FINAL, UTF8, "u(8) #(. 1.2):1,0,1,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")
PARSE_TEST("embedded 1e2 (1)", StopAfterEmbeddedDocument, "1e2", FINAL, UTF8, "u(8) #(e 1e2):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded 1e2 (2)", StopAfterEmbeddedDocument, " 1e2 ", FINAL, UTF8, "u(8) #(e 1e2):1,0,1,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")
PARSE_TEST("embedded 1e+2 (1)", StopAfterEmbeddedDocument, "1e+2", FINAL, UTF8, "u(8) #(e 1e+2):0,0,0,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")
PARSE_TEST("embedded 1e+2 (2)", StopAfterEmbeddedDocument, " 1e+2 ", FINAL, UTF8, "u(8) #(e 1e+2):1,0,1,0-5,0,5,0 !(StoppedAfterEmbeddedDocument):5,0,5,0")
PARSE_TEST("embedded 1e-2 (1)", StopAfterEmbeddedDocument, "1e-2", FINAL, UTF8, "u(8) #(e- 1e-2):0,0,0,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")
PARSE_TEST("embedded 1e-2 (2)", StopAfterEmbeddedDocument, " 1e-2 ", FINAL, UTF8, "u(8) #(e- 1e-2):1,0,1,0-5,0,5,0 !(StoppedAfterEmbeddedDocument):5,0,5,0")
PARSE_TEST("embedded 1.2e3 (1)", StopAfterEmbeddedDocument, "1.2e3", FINAL, UTF8, "u(8) #(.e 1.2e3):0,0,0,0-5,0,5,0 !(StoppedAfterEmbeddedDocument):5,0,5,0")
PARSE_TEST("embedded 1.2e3 (2)", StopAfterEmbeddedDocument, " 1.2e3 ", FINAL, UTF8, "u(8) #(.e 1.2e3):1,0,1,0-6,0,6,0 !(StoppedAfterEmbeddedDocument):6,0,6,0")
PARSE_TEST("embedded 1.2e+3 (1)", StopAfterEmbeddedDocument, "1.2e+3", FINAL, UTF8, "u(8) #(.e 1.2e+3):0,0,0,0-6,0,6,0 !(StoppedAfterEmbeddedDocument):6,0,6,0")
PARSE_TEST("embedded 1.2e+3 (2)", StopAfterEmbeddedDocument, " 1.2e+3 ", FINAL, UTF8, "u(8) #(.e 1.2e+3):1,0,1,0-7,0,7,0 !(StoppedAfterEmbeddedDocument):7,0,7,0")
PARSE_TEST("embedded 1.2e-3 (1)", StopAfterEmbeddedDocument, "1.2e-3", FINAL, UTF8, "u(8) #(.e- 1.2e-3):0,0,0,0-6,0,6,0 !(StoppedAfterEmbeddedDocument):6,0,6,0")
PARSE_TEST("embedded 1.2e-3 (2)", StopAfterEmbeddedDocument, " 1.2e-3 ", FINAL, UTF8, "u(8) #(.e- 1.2e-3):1,0,1,0-7,0,7,0 !(StoppedAfterEmbeddedDocument):7,0,7,0")
PARSE_TEST("embedded 0x1 (1)", AllowHexNumbers | StopAfterEmbeddedDocument, "0x1", FINAL, UTF8, "u(8) #(x 0x1):0,0,0,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded 0x1 (2)", AllowHexNumbers | StopAfterEmbeddedDocument, " 0x1 ", FINAL, UTF8, "u(8) #(x 0x1):1,0,1,0-4,0,4,0 !(StoppedAfterEmbeddedDocument):4,0,4,0")

PARSE_TEST("embedded 1.2e- in UTF-16LE", StopAfterEmbeddedDocument, "1\x00" ".\x00" "2\x00" "e\x00" "-\x00", FINAL, UTF16LE, "u(16LE) #(. 1.2):0,0,0,0-6,0,3,0 !(StoppedAfterEmbeddedDocument):6,0,3,0")
PARSE_TEST("embedded 12. in UTF-16BE", StopAfterEmbeddedDocument, "\x00" "1\x00" "2\x00" ".", FINAL, UTF16BE, "u(16BE) #(12):0,0,0,0-4,0,2,0 !(StoppedAfterEmbeddedDocument):4,0,2,0")
PARSE_TEST("embedded 1.2e in UTF-32LE", StopAfterEmbeddedDocument, "1\x00\x00\x00" ".\x00\x00\x00" "2\x00\x00\x00" "e\x00\x00\x00" "-\x00\x00\x00", FINAL, UTF32LE, "u(32LE) #(. 1.2):0,0,0,0-12,0,3,0 !(StoppedAfterEmbeddedDocument):12,0,3,0")
PARSE_TEST("embedded 00 in UTF-32BE", StopAfterEmbeddedDocument, "\x00\x00\x00" "0\x00\x00\x00" "0\x00\x00\x00" "0", FINAL, UTF32BE, "u(32BE) #(0):0,0,0,0-4,0,1,0 !(StoppedAfterEmbeddedDocument):4,0,1,0")

PARSE_TEST("embedded empty string (1)", StopAfterEmbeddedDocument, "\"\"", FINAL, UTF8, "u(8) s():0,0,0,0-2,0,2,0 !(StoppedAfterEmbeddedDocument):2,0,2,0")
PARSE_TEST("embedded empty string (2)", StopAfterEmbeddedDocument, "\"\"junk", FINAL, UTF8, "u(8) s():0,0,0,0-2,0,2,0 !(StoppedAfterEmbeddedDocument):2,0,2,0")
PARSE_TEST("embedded string (1)", StopAfterEmbeddedDocument, "\"foo\"", FINAL, UTF8, "u(8) s(foo):0,0,0,0-5,0,5,0 !(StoppedAfterEmbeddedDocument):5,0,5,0")
PARSE_TEST("embedded string (2)", StopAfterEmbeddedDocument, "\"foo\"junk", FINAL, UTF8, "u(8) s(foo):0,0,0,0-5,0,5,0 !(StoppedAfterEmbeddedDocument):5,0,5,0")
PARSE_TEST("embedded string with newline", StopAfterEmbeddedDocument, "\"foo\\nbar\"", FINAL, UTF8, "u(8) s(c foo<0A>bar):0,0,0,0-10,0,10,0 !(StoppedAfterEmbeddedDocument):10,0,10,0")
PARSE_TEST("embedded string with unescaped newline (1)", StopAfterEmbeddedDocument, "\"foo\nbar\"", FINAL, UTF8, "u(8) !(UnescapedControlCharacter):4,0,4,0")
PARSE_TEST("embedded string with unescaped newline (2)", AllowUnescapedControlCharacters | StopAfterEmbeddedDocument, "\"foo\nbar\"", FINAL, UTF8, "u(8) s(c foo<0A>bar):0,0,0,0-9,1,4,0 !(StoppedAfterEmbeddedDocument):9,1,4,0")
PARSE_TEST("unfinished embedded string (1)", StopAfterEmbeddedDocument, "\"foo", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("unfinished embedded string (2)", StopAfterEmbeddedDocument, "\"foo\xFF", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("embedded string containing invalid sequence (1)", StopAfterEmbeddedDocument, "\"foo\xFF\"", FINAL, UTF8, "u(8) !(IncompleteToken):0,0,0,0")
PARSE_TEST("embedded string containing invalid sequence (2)", ReplaceInvalidEncodingSequences | StopAfterEmbeddedDocument, "\"foo\xFF\"", FINAL, UTF8, "u(8) s(ar foo<EF><BF><BD>):0,0,0,0-6,0,6,0 !(StoppedAfterEmbeddedDocument):6,0,6,0")

PARSE_TEST("embedded empty array (1)", StopAfterEmbeddedDocument, "[]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 ]:1,0,1,0-2,0,2,0 !(StoppedAfterEmbeddedDocument):2,0,2,0")
PARSE_TEST("embedded empty array (2)", StopAfterEmbeddedDocument, "[]!", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 ]:1,0,1,0-2,0,2,0 !(StoppedAfterEmbeddedDocument):2,0,2,0")
PARSE_TEST("embedded array (1)", StopAfterEmbeddedDocument, "[1]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(1):1,0,1,1-2,0,2,1 ]:2,0,2,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded array (2)", StopAfterEmbeddedDocument, "[1]!", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 #(1):1,0,1,1-2,0,2,1 ]:2,0,2,0-3,0,3,0 !(StoppedAfterEmbeddedDocument):3,0,3,0")
PARSE_TEST("embedded nested array (1)", StopAfterEmbeddedDocument, "[[1]]", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 [:1,0,1,1-2,0,2,1 i:2,0,2,2-3,0,3,2 #(1):2,0,2,2-3,0,3,2 ]:3,0,3,1-4,0,4,1 ]:4,0,4,0-5,0,5,0 !(StoppedAfterEmbeddedDocument):5,0,5,0")
PARSE_TEST("embedded nested array (2)", StopAfterEmbeddedDocument, "[[1]]!", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 i:1,0,1,1-2,0,2,1 [:1,0,1,1-2,0,2,1 i:2,0,2,2-3,0,3,2 #(1):2,0,2,2-3,0,3,2 ]:3,0,3,1-4,0,4,1 ]:4,0,4,0-5,0,5,0 !(StoppedAfterEmbeddedDocument):5,0,5,0")
PARSE_TEST("embedded unclosed array (1)", StopAfterEmbeddedDocument, "[", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(ExpectedMoreTokens):1,0,1,1")
PARSE_TEST("embedded unclosed array (2)", StopAfterEmbeddedDocument, "[!", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(UnknownToken):1,0,1,1")
PARSE_TEST("embedded unclosed array (2)", StopAfterEmbeddedDocument, "[\xFF", FINAL, UTF8, "u(8) [:0,0,0,0-1,0,1,0 !(InvalidEncodingSequence):1,0,1,1")

PARSE_TEST("embedded empty object (1)", StopAfterEmbeddedDocument, "{}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 }:1,0,1,0-2,0,2,0 !(StoppedAfterEmbeddedDocument):2,0,2,0")
PARSE_TEST("embedded empty object (2)", StopAfterEmbeddedDocument, "{}!", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 }:1,0,1,0-2,0,2,0 !(StoppedAfterEmbeddedDocument):2,0,2,0")
PARSE_TEST("embedded object (1)", StopAfterEmbeddedDocument, "{\"a\":1}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 }:6,0,6,0-7,0,7,0 !(StoppedAfterEmbeddedDocument):7,0,7,0")
PARSE_TEST("embedded object (2)", StopAfterEmbeddedDocument, "{\"a\":1}!", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 #(1):5,0,5,1-6,0,6,1 }:6,0,6,0-7,0,7,0 !(StoppedAfterEmbeddedDocument):7,0,7,0")
PARSE_TEST("embedded nested object (1)", StopAfterEmbeddedDocument, "{\"a\":{\"b\":1}}", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 {:5,0,5,1-6,0,6,1 m(b):6,0,6,2-9,0,9,2 #(1):10,0,10,2-11,0,11,2 }:11,0,11,1-12,0,12,1 }:12,0,12,0-13,0,13,0 !(StoppedAfterEmbeddedDocument):13,0,13,0")
PARSE_TEST("embedded nested object (2)", StopAfterEmbeddedDocument, "{\"a\":{\"b\":1}}!", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 m(a):1,0,1,1-4,0,4,1 {:5,0,5,1-6,0,6,1 m(b):6,0,6,2-9,0,9,2 #(1):10,0,10,2-11,0,11,2 }:11,0,11,1-12,0,12,1 }:12,0,12,0-13,0,13,0 !(StoppedAfterEmbeddedDocument):13,0,13,0")
PARSE_TEST("embedded unclosed object (1)", StopAfterEmbeddedDocument, "{", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(ExpectedMoreTokens):1,0,1,1")
PARSE_TEST("embedded unclosed object (2)", StopAfterEmbeddedDocument, "{!", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(UnknownToken):1,0,1,1")
PARSE_TEST("embedded unclosed object (2)", StopAfterEmbeddedDocument, "{\xFF", FINAL, UTF8, "u(8) {:0,0,0,0-1,0,1,0 !(InvalidEncodingSequence):1,0,1,1")

};

static void TestParserParse(void)
{
    size_t i;
    for  (i = 0; i < sizeof(s_parseTests)/sizeof(s_parseTests[0]); i++)
    {
        RunParseTest(&s_parseTests[i]);
    }
}

#endif /* JSON_NO_PARSER */

#ifndef JSON_NO_WRITER

typedef struct tag_WriterState
{
    JSON_Error error;
} WriterState;

static void InitWriterState(WriterState* pState)
{
    pState->error = JSON_Error_None;
}

static void GetWriterState(JSON_Writer writer, WriterState* pState)
{
    pState->error = JSON_Writer_GetError(writer);
}

static int WriterStatesAreIdentical(const WriterState* pState1, const WriterState* pState2)
{
    return (pState1->error == pState2->error);
}

static int CheckWriterState(JSON_Writer writer, const WriterState* pExpectedState)
{
    int isValid;
    WriterState actualState;
    GetWriterState(writer, &actualState);
    isValid = WriterStatesAreIdentical(pExpectedState, &actualState);
    if (!isValid)
    {
        printf("FAILURE: writer state does not match\n"
               "  STATE                                 EXPECTED     ACTUAL\n"
               "  JSON_Writer_GetError()                %8d   %8d\n"
               ,
               (int)pExpectedState->error, (int)actualState.error
            );
    }
    return isValid;
}

typedef struct tag_WriterSettings
{
    void*         userData;
    JSON_Encoding outputEncoding;
    JSON_Boolean  useCRLF;
    JSON_Boolean  replaceInvalidEncodingSequences;
    JSON_Boolean  escapeAllNonASCIICharacters;
} WriterSettings;

static void InitWriterSettings(WriterSettings* pSettings)
{
    pSettings->userData = NULL;
    pSettings->outputEncoding = JSON_UTF8;
    pSettings->useCRLF = JSON_False;
    pSettings->replaceInvalidEncodingSequences = JSON_False;
    pSettings->escapeAllNonASCIICharacters = JSON_False;
}

static void GetWriterSettings(JSON_Writer writer, WriterSettings* pSettings)
{
    pSettings->userData = JSON_Writer_GetUserData(writer);
    pSettings->outputEncoding = JSON_Writer_GetOutputEncoding(writer);
    pSettings->useCRLF = JSON_Writer_GetUseCRLF(writer);
    pSettings->replaceInvalidEncodingSequences = JSON_Writer_GetReplaceInvalidEncodingSequences(writer);
    pSettings->escapeAllNonASCIICharacters = JSON_Writer_GetEscapeAllNonASCIICharacters(writer);
}

static int WriterSettingsAreIdentical(const WriterSettings* pSettings1, const WriterSettings* pSettings2)
{
    return (pSettings1->userData == pSettings2->userData &&
            pSettings1->outputEncoding == pSettings2->outputEncoding &&
            pSettings1->useCRLF == pSettings2->useCRLF &&
            pSettings1->replaceInvalidEncodingSequences == pSettings2->replaceInvalidEncodingSequences &&
            pSettings1->escapeAllNonASCIICharacters == pSettings2->escapeAllNonASCIICharacters);
}

static int CheckWriterSettings(JSON_Writer writer, const WriterSettings* pExpectedSettings)
{
    int identical;
    WriterSettings actualSettings;
    GetWriterSettings(writer, &actualSettings);
    identical = WriterSettingsAreIdentical(pExpectedSettings, &actualSettings);
    if (!identical)
    {
        printf("FAILURE: writer settings do not match\n"
               "  SETTINGS                                         EXPECTED     ACTUAL\n"
               "  JSON_Writer_GetUserData()                        %8p   %8p\n"
               "  JSON_Writer_GetOutputEncoding()                  %8d   %8d\n"
               "  JSON_Writer_GetUseCRLF()                         %8d   %8d\n"
               "  JSON_Writer_GetReplaceInvalidEncodingSequences() %8d   %8d\n"
               "  JSON_Writer_GetEscapeAllNonASCIICharacters()     %8d   %8d\n"
               ,
               pExpectedSettings->userData, actualSettings.userData,
               (int)pExpectedSettings->outputEncoding, (int)actualSettings.outputEncoding,
               (int)pExpectedSettings->useCRLF, (int)actualSettings.useCRLF,
               (int)pExpectedSettings->replaceInvalidEncodingSequences, (int)actualSettings.replaceInvalidEncodingSequences,
               (int)pExpectedSettings->escapeAllNonASCIICharacters, (int)actualSettings.escapeAllNonASCIICharacters
            );
    }
    return identical;
}

typedef struct tag_WriterHandlers
{
    JSON_Writer_OutputHandler outputHandler;
} WriterHandlers;

static void InitWriterHandlers(WriterHandlers* pHandlers)
{
    pHandlers->outputHandler = NULL;
}

static void GetWriterHandlers(JSON_Writer writer, WriterHandlers* pHandlers)
{
    pHandlers->outputHandler = JSON_Writer_GetOutputHandler(writer);
}

static int WriterHandlersAreIdentical(const WriterHandlers* pHandlers1, const WriterHandlers* pHandlers2)
{
    return (pHandlers1->outputHandler == pHandlers2->outputHandler);
}

static int CheckWriterHandlers(JSON_Writer writer, const WriterHandlers* pExpectedHandlers)
{
    int identical;
    WriterHandlers actualHandlers;
    GetWriterHandlers(writer, &actualHandlers);
    identical = WriterHandlersAreIdentical(pExpectedHandlers, &actualHandlers);
    if (!identical)
    {
        printf("FAILURE: writer handlers do not match\n"
               "  HANDLERS                             EXPECTED     ACTUAL\n"
               "  JSON_Writer_GetOutputHandler()       %8s   %8s\n"
               ,
               HANDLER_STRING(pExpectedHandlers->outputHandler), HANDLER_STRING(actualHandlers.outputHandler)
            );
    }
    return identical;
}

static int CheckWriterHasDefaultValues(JSON_Writer writer)
{
    WriterState state;
    WriterSettings settings;
    WriterHandlers handlers;
    InitWriterState(&state);
    InitWriterSettings(&settings);
    InitWriterHandlers(&handlers);
    return CheckWriterState(writer, &state) &&
           CheckWriterSettings(writer, &settings) &&
           CheckWriterHandlers(writer, &handlers);
}

static int CheckWriterCreate(const JSON_MemorySuite* pMemorySuite, JSON_Status expectedStatus, JSON_Writer* pWriter)
{
    *pWriter = JSON_Writer_Create(pMemorySuite);
    if (expectedStatus == JSON_Success && !*pWriter)
    {
        printf("FAILURE: expected JSON_Writer_Create() to return a writer instance\n");
        return 0;
    }
    if (expectedStatus == JSON_Failure && *pWriter)
    {
        printf("FAILURE: expected JSON_Writer_Create() to return NULL\n");
        JSON_Writer_Free(*pWriter);
        *pWriter = NULL;
        return 0;
    }
    return 1;
}

static int CheckWriterCreateWithCustomMemorySuite(JSON_ReallocHandler r, JSON_FreeHandler f, JSON_Status expectedStatus, JSON_Writer* pWriter)
{
    JSON_MemorySuite memorySuite;
    memorySuite.userData = NULL;
    memorySuite.realloc = r;
    memorySuite.free = f;
    return CheckWriterCreate(&memorySuite, expectedStatus, pWriter);
}

static int CheckWriterReset(JSON_Writer writer, JSON_Status expectedStatus)
{
    if (JSON_Writer_Reset(writer) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_Reset() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterFree(JSON_Writer writer, JSON_Status expectedStatus)
{
    if (JSON_Writer_Free(writer) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_Free() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterSetUserData(JSON_Writer writer, void* userData, JSON_Status expectedStatus)
{
    if (JSON_Writer_SetUserData(writer, userData) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_SetUserData() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterSetOutputEncoding(JSON_Writer writer, JSON_Encoding encoding, JSON_Status expectedStatus)
{
    if (JSON_Writer_SetOutputEncoding(writer, encoding) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_SetOutputEncoding() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterSetUseCRLF(JSON_Writer writer, JSON_Boolean useCRLF, JSON_Status expectedStatus)
{
    if (JSON_Writer_SetUseCRLF(writer, useCRLF) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_SetUseCRLF() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterSetReplaceInvalidEncodingSequences(JSON_Writer writer, JSON_Boolean replaceInvalidEncodingSequences, JSON_Status expectedStatus)
{
    if (JSON_Writer_SetReplaceInvalidEncodingSequences(writer, replaceInvalidEncodingSequences) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_SetReplaceInvalidEncodingSequences() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterSetEscapeAllNonASCIICharacters(JSON_Writer writer, JSON_Boolean escapeAllNonASCIICharacters, JSON_Status expectedStatus)
{
    if (JSON_Writer_SetEscapeAllNonASCIICharacters(writer, escapeAllNonASCIICharacters) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_SetEscapeAllNonASCIICharacters() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterSetOutputHandler(JSON_Writer writer, JSON_Writer_OutputHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Writer_SetOutputHandler(writer, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_SetOutputHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterWriteNull(JSON_Writer writer, JSON_Status expectedStatus)
{
    if (JSON_Writer_WriteNull(writer) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_WriteNull() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterWriteBoolean(JSON_Writer writer, JSON_Boolean value, JSON_Status expectedStatus)
{
    if (JSON_Writer_WriteBoolean(writer, value) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_WriteBoolean() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterWriteString(JSON_Writer writer, const char* pValue, size_t length, JSON_Encoding encoding, JSON_Status expectedStatus)
{
    if (JSON_Writer_WriteString(writer, pValue, length, encoding) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_WriteString() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterWriteNumber(JSON_Writer writer, const char* pValue, size_t length, JSON_Encoding encoding, JSON_Status expectedStatus)
{
    if (JSON_Writer_WriteNumber(writer, pValue, length, encoding) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_WriteNumber() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterWriteSpecialNumber(JSON_Writer writer, JSON_SpecialNumber value, JSON_Status expectedStatus)
{
    if (JSON_Writer_WriteSpecialNumber(writer, value) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_WriteSpecialNumber() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterWriteStartObject(JSON_Writer writer, JSON_Status expectedStatus)
{
    if (JSON_Writer_WriteStartObject(writer) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_WriteStartObject() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterWriteEndObject(JSON_Writer writer, JSON_Status expectedStatus)
{
    if (JSON_Writer_WriteEndObject(writer) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_WriteEndObject() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterWriteStartArray(JSON_Writer writer, JSON_Status expectedStatus)
{
    if (JSON_Writer_WriteStartArray(writer) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_WriteStartArray() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterWriteEndArray(JSON_Writer writer, JSON_Status expectedStatus)
{
    if (JSON_Writer_WriteEndArray(writer) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_WriteEndArray() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterWriteColon(JSON_Writer writer, JSON_Status expectedStatus)
{
    if (JSON_Writer_WriteColon(writer) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_WriteColon() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterWriteComma(JSON_Writer writer, JSON_Status expectedStatus)
{
    if (JSON_Writer_WriteComma(writer) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_WriteComma() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterWriteSpace(JSON_Writer writer, size_t numberOfSpaces, JSON_Status expectedStatus)
{
    if (JSON_Writer_WriteSpace(writer, numberOfSpaces) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_WriteSpace() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckWriterWriteNewLine(JSON_Writer writer, JSON_Status expectedStatus)
{
    if (JSON_Writer_WriteNewLine(writer) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Writer_WriteNewLine() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int TryToMisbehaveInWriteHandler(JSON_Writer writer)
{
    if (!CheckWriterFree(writer, JSON_Failure) ||
        !CheckWriterReset(writer, JSON_Failure) ||
        !CheckWriterSetOutputEncoding(writer, JSON_UTF32LE, JSON_Failure) ||
        !CheckWriterSetUseCRLF(writer, JSON_True, JSON_Failure) ||
        !CheckWriterSetReplaceInvalidEncodingSequences(writer, JSON_True, JSON_Failure) ||
        !CheckWriterSetEscapeAllNonASCIICharacters(writer, JSON_True, JSON_Failure) ||
        !CheckWriterWriteNull(writer, JSON_Failure) ||
        !CheckWriterWriteBoolean(writer, JSON_True, JSON_Failure) ||
        !CheckWriterWriteString(writer, "abc", 3, JSON_UTF8, JSON_Failure) ||
        !CheckWriterWriteNumber(writer, "0", 1, JSON_UTF8, JSON_Failure) ||
        !CheckWriterWriteSpecialNumber(writer, JSON_NaN, JSON_Failure) ||
        !CheckWriterWriteStartObject(writer, JSON_Failure) ||
        !CheckWriterWriteEndObject(writer, JSON_Failure) ||
        !CheckWriterWriteStartArray(writer, JSON_Failure) ||
        !CheckWriterWriteEndArray(writer, JSON_Failure) ||
        !CheckWriterWriteColon(writer, JSON_Failure) ||
        !CheckWriterWriteComma(writer, JSON_Failure) ||
        !CheckWriterWriteSpace(writer, 3, JSON_Failure) ||
        !CheckWriterWriteNewLine(writer, JSON_Failure))
    {
        return 1;
    }
    return 0;
}

static JSON_Writer_HandlerResult JSON_CALL OutputHandler(JSON_Writer writer, const char* pBytes, size_t length)
{
    if (s_failHandler)
    {
        return JSON_Writer_Abort;
    }
    if (s_misbehaveInHandler && TryToMisbehaveInWriteHandler(writer))
    {
        return JSON_Writer_Abort;
    }
    OutputStringBytes((const unsigned char*)pBytes, length, JSON_SimpleString, JSON_Writer_GetOutputEncoding(writer));
    return JSON_Writer_Continue;
}

static void TestWriterCreate(void)
{
    JSON_Writer writer = NULL;
    printf("Test creating writer ... ");
    if (CheckWriterCreate(NULL, JSON_Success, &writer) &&
        CheckWriterHasDefaultValues(writer))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
}

static void TestWriterCreateWithCustomMemorySuite(void)
{
    JSON_Writer writer = NULL;
    printf("Test creating writer with custom memory suite ... ");
    if (CheckWriterCreateWithCustomMemorySuite(NULL, NULL, JSON_Failure, &writer) &&
        CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, NULL, JSON_Failure, &writer) &&
        CheckWriterCreateWithCustomMemorySuite(NULL, &FreeHandler, JSON_Failure, &writer) &&
        CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &writer) &&
        CheckWriterHasDefaultValues(writer))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
}

static void TestWriterCreateMallocFailure(void)
{
    JSON_Writer writer = NULL;
    printf("Test creating writer malloc failure ... ");
    s_failMalloc = 1;
    if (CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Failure, &writer))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    s_failMalloc = 0;
    JSON_Writer_Free(writer);
}

static void TestWriterMissing(void)
{
    WriterState state;
    WriterSettings settings;
    WriterHandlers handlers;
    printf("Test NULL writer instance ... ");
    InitWriterState(&state);
    InitWriterSettings(&settings);
    InitWriterHandlers(&handlers);
    if (CheckWriterState(NULL, &state) &&
        CheckWriterSettings(NULL, &settings) &&
        CheckWriterHandlers(NULL, &handlers) &&
        CheckWriterFree(NULL, JSON_Failure) &&
        CheckWriterReset(NULL, JSON_Failure) &&
        CheckWriterSetUserData(NULL, (void*)1, JSON_Failure) &&
        CheckWriterSetOutputEncoding(NULL, JSON_UTF16LE, JSON_Failure) &&
        CheckWriterSetOutputHandler(NULL, &OutputHandler, JSON_Failure) &&
        CheckWriterWriteNull(NULL, JSON_Failure) &&
        CheckWriterWriteBoolean(NULL, JSON_True, JSON_Failure) &&
        CheckWriterWriteString(NULL, "abc", 3, JSON_UTF8, JSON_Failure) &&
        CheckWriterWriteNumber(NULL, "0", 1, JSON_UTF8, JSON_Failure) &&
        CheckWriterWriteSpecialNumber(NULL, JSON_NaN, JSON_Failure) &&
        CheckWriterWriteStartObject(NULL, JSON_Failure) &&
        CheckWriterWriteEndObject(NULL, JSON_Failure) &&
        CheckWriterWriteStartArray(NULL, JSON_Failure) &&
        CheckWriterWriteEndArray(NULL, JSON_Failure) &&
        CheckWriterWriteColon(NULL, JSON_Failure) &&
        CheckWriterWriteComma(NULL, JSON_Failure) &&
        CheckWriterWriteSpace(NULL, 3, JSON_Failure) &&
        CheckWriterWriteNewLine(NULL, JSON_Failure))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
}

static void TestWriterSetSettings(void)
{
    JSON_Writer writer = NULL;
    WriterSettings settings;
    printf("Test setting writer settings ... ");
    InitWriterSettings(&settings);
    settings.userData = (void*)1;
    settings.outputEncoding = JSON_UTF16LE;
    settings.replaceInvalidEncodingSequences = JSON_True;
    if (CheckWriterCreate(NULL, JSON_Success, &writer) &&
        CheckWriterSetUserData(writer, settings.userData, JSON_Success) &&
        CheckWriterSetOutputEncoding(writer, settings.outputEncoding, JSON_Success) &&
        CheckWriterSetUseCRLF(writer, settings.useCRLF, JSON_Success) &&
        CheckWriterSetReplaceInvalidEncodingSequences(writer, settings.replaceInvalidEncodingSequences, JSON_Success) &&
        CheckWriterSetEscapeAllNonASCIICharacters(writer, settings.escapeAllNonASCIICharacters, JSON_Success) &&
        CheckWriterSettings(writer, &settings))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
}

static void TestWriterSetInvalidSettings(void)
{
    JSON_Writer writer = NULL;
    WriterSettings settings;
    printf("Test setting invalid writer settings ... ");
    InitWriterSettings(&settings);
    if (CheckWriterCreate(NULL, JSON_Success, &writer) &&
        CheckWriterSetOutputEncoding(writer, JSON_UnknownEncoding, JSON_Failure) &&
        CheckWriterSetOutputEncoding(writer, (JSON_Encoding)(JSON_UTF32BE + 1), JSON_Failure) &&
        CheckWriterWriteString(writer, NULL, 1, JSON_UTF8, JSON_Failure) &&
        CheckWriterWriteString(writer, "a", 1, JSON_UnknownEncoding, JSON_Failure) &&
        CheckWriterWriteString(writer, "a", 1, (JSON_Encoding)(JSON_UTF32BE + 1), JSON_Failure) &&
        CheckWriterWriteNumber(writer, NULL, 1, JSON_UTF8, JSON_Failure) &&
        CheckWriterWriteNumber(writer, "0", 1, JSON_UnknownEncoding, JSON_Failure) &&
        CheckWriterWriteNumber(writer, "0", 1, (JSON_Encoding)(JSON_UTF32BE + 1), JSON_Failure) &&
        CheckWriterSettings(writer, &settings))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
}

static void TestWriterSetHandlers(void)
{
    JSON_Writer writer = NULL;
    WriterHandlers handlers;
    printf("Test setting writer handlers ... ");
    InitWriterHandlers(&handlers);
    handlers.outputHandler = &OutputHandler;
    if (CheckWriterCreate(NULL, JSON_Success, &writer) &&
        CheckWriterSetOutputHandler(writer, handlers.outputHandler, JSON_Success) &&
        CheckWriterHandlers(writer, &handlers))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
}

static void TestWriterReset(void)
{
    JSON_Writer writer = NULL;
    WriterState state;
    WriterSettings settings;
    WriterHandlers handlers;
    printf("Test resetting writer ... ");
    InitWriterState(&state);
    InitWriterSettings(&settings);
    InitWriterHandlers(&handlers);
    if (CheckWriterCreate(NULL, JSON_Success, &writer) &&
        CheckWriterSetUserData(writer, (void*)1, JSON_Success) &&
        CheckWriterSetOutputEncoding(writer, JSON_UTF16LE, JSON_Success) &&
        CheckWriterSetUseCRLF(writer, JSON_True, JSON_Success) &&
        CheckWriterSetReplaceInvalidEncodingSequences(writer, JSON_True, JSON_Success) &&
        CheckWriterSetEscapeAllNonASCIICharacters(writer, JSON_True, JSON_Success) &&
        CheckWriterSetOutputHandler(writer, &OutputHandler, JSON_Success) &&
        CheckWriterWriteNull(writer, JSON_Success) &&
        CheckWriterReset(writer, JSON_Success) &&
        CheckWriterState(writer, &state) &&
        CheckWriterSettings(writer, &settings) &&
        CheckWriterHandlers(writer, &handlers))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
}

static void TestWriterMisbehaveInCallbacks(void)
{
    JSON_Writer writer = NULL;
    printf("Test writer misbehaving in callbacks ... ");
    s_misbehaveInHandler = 1;
    if (CheckWriterCreate(NULL, JSON_Success, &writer) &&
        CheckWriterSetOutputHandler(writer, &OutputHandler, JSON_Success) &&
        CheckWriterWriteNull(writer, JSON_Success))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    s_misbehaveInHandler = 0;
    JSON_Writer_Free(writer);
}

static void TestWriterAbortInCallbacks(void)
{
    JSON_Writer writer = NULL;
    WriterState state;
    printf("Test writer aborting in callbacks ... ");
    InitWriterState(&state);
    state.error = JSON_Error_AbortedByHandler;
    s_failHandler = 1;
    if (CheckWriterCreate(NULL, JSON_Success, &writer) &&
        CheckWriterSetOutputHandler(writer, &OutputHandler, JSON_Success) &&
        CheckWriterWriteNull(writer, JSON_Failure) &&
        CheckWriterState(writer, &state))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    s_failHandler = 0;
    JSON_Writer_Free(writer);
}

static void TestWriterStackMallocFailure(void)
{
    int succeeded = 0;
    JSON_Writer writer = NULL;
    WriterState state;
    printf("Test writer stack malloc failure ... ");
    InitWriterState(&state);
    state.error = JSON_Error_OutOfMemory;
    if (CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &writer))
    {
        s_failMalloc = 1;
        for (;;)
        {
            if (JSON_Writer_WriteStartArray(writer) == JSON_Failure)
            {
                break;
            }
        }
        if (CheckWriterState(writer, &state))
        {
            succeeded = 1;
        }
        s_failMalloc = 0;
    }
    if (succeeded)
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
}

static void TestWriterStackReallocFailure(void)
{
    int succeeded = 0;
    JSON_Writer writer = NULL;
    WriterState state;
    printf("Test writer stack realloc failure ... ");
    InitWriterState(&state);
    state.error = JSON_Error_OutOfMemory;
    if (CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &writer))
    {
        s_failRealloc = 1;
        for (;;)
        {
            if (JSON_Writer_WriteStartArray(writer) == JSON_Failure)
            {
                break;
            }
        }
        if (CheckWriterState(writer, &state))
        {
            succeeded = 1;
        }
        s_failRealloc = 0;
    }
    if (succeeded)
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
}

typedef struct tag_WriteTest
{
    const char*   pName;
    JSON_Encoding inputEncoding;
    JSON_Encoding outputEncoding;
    JSON_Boolean  replaceInvalidEncodingSequences;
    JSON_Boolean  escapeAllNonASCIICharacters;
    const char*   pInput;
    size_t        length; /* overloaded for boolean, specialnumber */
    const char*   pOutput;
} WriteTest;

#define REPLACE JSON_True
#define NO_REPLACE JSON_False

#define ESCAPE_ALL JSON_True
#define NO_ESCAPE_ALL JSON_False

#define WRITE_NULL_TEST(name, out_enc, output) { name, JSON_UnknownEncoding, JSON_##out_enc, NO_REPLACE, NO_ESCAPE_ALL, NULL, 0, output },

static void RunWriteNullTest(const WriteTest* pTest)
{
    JSON_Writer writer = NULL;
    WriterSettings settings;
    WriterState state;
    printf("Test writing null %s ... ", pTest->pName);

    InitWriterSettings(&settings);
    settings.outputEncoding = pTest->outputEncoding;

    InitWriterState(&state);
    ResetOutput();

    if (CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &writer) &&
        CheckWriterSetOutputHandler(writer, &OutputHandler, JSON_Success) &&
        CheckWriterSetOutputEncoding(writer, settings.outputEncoding, JSON_Success))
    {
        if (JSON_Writer_WriteNull(writer) != JSON_Success)
        {
            state.error = JSON_Writer_GetError(writer);
            if (state.error != JSON_Error_None)
            {
                OutputSeparator();
                OutputFormatted("!(%s)", errorNames[state.error]);
            }
        }
        if (CheckWriterState(writer, &state) && CheckOutput(pTest->pOutput))
        {
            printf("OK\n");
        }
        else
        {
            s_failureCount++;
        }
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
    ResetOutput();
}

static const WriteTest s_writeNullTests[] =
{

WRITE_NULL_TEST("-> UTF-8",    UTF8,    "null")
WRITE_NULL_TEST("-> UTF-16LE", UTF16LE, "n_u_l_l_")
WRITE_NULL_TEST("-> UTF-16BE", UTF16BE, "_n_u_l_l")
WRITE_NULL_TEST("-> UTF-32LE", UTF32LE, "n___u___l___l___")
WRITE_NULL_TEST("-> UTF-32BE", UTF32BE, "___n___u___l___l")

};

static void TestWriterWriteNull(void)
{
    size_t i;
    for  (i = 0; i < sizeof(s_writeNullTests)/sizeof(s_writeNullTests[0]); i++)
    {
        RunWriteNullTest(&s_writeNullTests[i]);
    }
}

#define WRITE_BOOLEAN_TEST(name, out_enc, input, output) { name, JSON_UnknownEncoding, JSON_##out_enc, NO_REPLACE, NO_ESCAPE_ALL, NULL, (size_t)input, output },

static void RunWriteBooleanTest(const WriteTest* pTest)
{
    JSON_Writer writer = NULL;
    WriterSettings settings;
    WriterState state;
    printf("Test writing boolean %s ... ", pTest->pName);

    InitWriterSettings(&settings);
    settings.outputEncoding = pTest->outputEncoding;

    InitWriterState(&state);
    ResetOutput();

    if (CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &writer) &&
        CheckWriterSetOutputHandler(writer, &OutputHandler, JSON_Success) &&
        CheckWriterSetOutputEncoding(writer, settings.outputEncoding, JSON_Success))
    {
        if (JSON_Writer_WriteBoolean(writer, (JSON_Boolean)pTest->length) != JSON_Success)
        {
            state.error = JSON_Writer_GetError(writer);
            if (state.error != JSON_Error_None)
            {
                OutputSeparator();
                OutputFormatted("!(%s)", errorNames[state.error]);
            }
        }
        if (CheckWriterState(writer, &state) && CheckOutput(pTest->pOutput))
        {
            printf("OK\n");
        }
        else
        {
            s_failureCount++;
        }
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
    ResetOutput();
}

static const WriteTest s_writeBooleanTests[] =
{

WRITE_BOOLEAN_TEST("true -> UTF-8",     UTF8,    JSON_True, "true")
WRITE_BOOLEAN_TEST("true -> UTF-16LE",  UTF16LE, JSON_True, "t_r_u_e_")
WRITE_BOOLEAN_TEST("true -> UTF-16BE",  UTF16BE, JSON_True, "_t_r_u_e")
WRITE_BOOLEAN_TEST("true -> UTF-32LE",  UTF32LE, JSON_True, "t___r___u___e___")
WRITE_BOOLEAN_TEST("true -> UTF-32BE",  UTF32BE, JSON_True, "___t___r___u___e")
WRITE_BOOLEAN_TEST("false -> UTF-8",    UTF8,    JSON_False, "false")
WRITE_BOOLEAN_TEST("false -> UTF-16LE", UTF16LE, JSON_False, "f_a_l_s_e_")
WRITE_BOOLEAN_TEST("false -> UTF-16BE", UTF16BE, JSON_False, "_f_a_l_s_e")
WRITE_BOOLEAN_TEST("false -> UTF-32LE", UTF32LE, JSON_False, "f___a___l___s___e___")
WRITE_BOOLEAN_TEST("false -> UTF-32BE", UTF32BE, JSON_False, "___f___a___l___s___e")

};

static void TestWriterWriteBoolean(void)
{
    size_t i;
    for  (i = 0; i < sizeof(s_writeBooleanTests)/sizeof(s_writeBooleanTests[0]); i++)
    {
        RunWriteBooleanTest(&s_writeBooleanTests[i]);
    }
}

static void RunWriteStringTest(const WriteTest* pTest)
{
    JSON_Writer writer = NULL;
    WriterSettings settings;
    WriterState state;
    printf("Test writing string %s ... ", pTest->pName);

    InitWriterSettings(&settings);
    settings.outputEncoding = pTest->outputEncoding;
    settings.replaceInvalidEncodingSequences = pTest->replaceInvalidEncodingSequences;
    settings.escapeAllNonASCIICharacters = pTest->escapeAllNonASCIICharacters;

    InitWriterState(&state);
    ResetOutput();

    if (CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &writer) &&
        CheckWriterSetOutputHandler(writer, &OutputHandler, JSON_Success) &&
        CheckWriterSetOutputEncoding(writer, settings.outputEncoding, JSON_Success) &&
        CheckWriterSetReplaceInvalidEncodingSequences(writer, settings.replaceInvalidEncodingSequences, JSON_Success) &&
        CheckWriterSetEscapeAllNonASCIICharacters(writer, settings.escapeAllNonASCIICharacters, JSON_Success))
    {
        if (JSON_Writer_WriteString(writer, pTest->pInput, pTest->length, pTest->inputEncoding) != JSON_Success)
        {
            state.error = JSON_Writer_GetError(writer);
            if (state.error != JSON_Error_None)
            {
                OutputSeparator();
                OutputFormatted("!(%s)", errorNames[state.error]);
            }
        }
        if (CheckWriterState(writer, &state) && CheckOutput(pTest->pOutput))
        {
            printf("OK\n");
        }
        else
        {
            s_failureCount++;
        }
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
    ResetOutput();
}

#define WRITE_STRING_TEST(name, in_enc, out_enc, replace, escapeall, input, output) { name, JSON_##in_enc, JSON_##out_enc, replace, escapeall, input, sizeof(input) - 1, output },

static const WriteTest s_writeStringTests[] =
{

WRITE_STRING_TEST("empty string UTF-8 -> UTF-8",       UTF8,    UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "", "\"\"")
WRITE_STRING_TEST("empty string UTF-8 -> UTF-16LE",    UTF8,    UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "", "\"_\"_")
WRITE_STRING_TEST("empty string UTF-8 -> UTF-16BE",    UTF8,    UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "", "_\"_\"")
WRITE_STRING_TEST("empty string UTF-8 -> UTF-32LE",    UTF8,    UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "", "\"___\"___")
WRITE_STRING_TEST("empty string UTF-8 -> UTF-32BE",    UTF8,    UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "", "___\"___\"")
WRITE_STRING_TEST("empty string UTF-16LE -> UTF-8",    UTF16LE, UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "", "\"\"")
WRITE_STRING_TEST("empty string UTF-16LE -> UTF-16LE", UTF16LE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "", "\"_\"_")
WRITE_STRING_TEST("empty string UTF-16LE -> UTF-16BE", UTF16LE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "", "_\"_\"")
WRITE_STRING_TEST("empty string UTF-16LE -> UTF-32LE", UTF16LE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "", "\"___\"___")
WRITE_STRING_TEST("empty string UTF-16LE -> UTF-32BE", UTF16LE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "", "___\"___\"")
WRITE_STRING_TEST("empty string UTF-16BE -> UTF-8",    UTF16BE, UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "", "\"\"")
WRITE_STRING_TEST("empty string UTF-16BE -> UTF-16LE", UTF16BE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "", "\"_\"_")
WRITE_STRING_TEST("empty string UTF-16BE -> UTF-16BE", UTF16BE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "", "_\"_\"")
WRITE_STRING_TEST("empty string UTF-16BE -> UTF-32LE", UTF16BE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "", "\"___\"___")
WRITE_STRING_TEST("empty string UTF-16BE -> UTF-32BE", UTF16BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "", "___\"___\"")
WRITE_STRING_TEST("empty string UTF-32LE -> UTF-8",    UTF32LE, UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "", "\"\"")
WRITE_STRING_TEST("empty string UTF-32LE -> UTF-16LE", UTF32LE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "", "\"_\"_")
WRITE_STRING_TEST("empty string UTF-32LE -> UTF-16BE", UTF32LE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "", "_\"_\"")
WRITE_STRING_TEST("empty string UTF-32LE -> UTF-32LE", UTF32LE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "", "\"___\"___")
WRITE_STRING_TEST("empty string UTF-32LE -> UTF-32BE", UTF32LE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "", "___\"___\"")
WRITE_STRING_TEST("empty string UTF-32BE -> UTF-8",    UTF32BE, UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "", "\"\"")
WRITE_STRING_TEST("empty string UTF-32BE -> UTF-16LE", UTF32BE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "", "\"_\"_")
WRITE_STRING_TEST("empty string UTF-32BE -> UTF-16BE", UTF32BE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "", "_\"_\"")
WRITE_STRING_TEST("empty string UTF-32BE -> UTF-32LE", UTF32BE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "", "\"___\"___")
WRITE_STRING_TEST("empty string UTF-32BE -> UTF-32BE", UTF32BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "", "___\"___\"")

WRITE_STRING_TEST("UTF-8 -> UTF-8",       UTF8,    UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84", "\"a<C2><A9><E4><B8><81><F0><9F><80><84>\"")
WRITE_STRING_TEST("UTF-8 -> UTF-16LE",    UTF8,    UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84", "\"_a_<A9 00><01 4E><3C D8><04 DC>\"_")
WRITE_STRING_TEST("UTF-8 -> UTF-16BE",    UTF8,    UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84", "_\"_a<00 A9><4E 01><D8 3C><DC 04>_\"")
WRITE_STRING_TEST("UTF-8 -> UTF-32LE",    UTF8,    UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84", "\"___a___<A9 00 00 00><01 4E 00 00><04 F0 01 00>\"___")
WRITE_STRING_TEST("UTF-8 -> UTF-32BE",    UTF8,    UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84", "___\"___a<00 00 00 A9><00 00 4E 01><00 01 F0 04>___\"")
WRITE_STRING_TEST("UTF-16LE -> UTF-8",    UTF16LE, UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC", "\"a<C2><A9><E4><B8><81><F0><9F><80><84>\"")
WRITE_STRING_TEST("UTF-16LE -> UTF-16LE", UTF16LE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC", "\"_a_<A9 00><01 4E><3C D8><04 DC>\"_")
WRITE_STRING_TEST("UTF-16LE -> UTF-16BE", UTF16LE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC", "_\"_a<00 A9><4E 01><D8 3C><DC 04>_\"")
WRITE_STRING_TEST("UTF-16LE -> UTF-32LE", UTF16LE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC", "\"___a___<A9 00 00 00><01 4E 00 00><04 F0 01 00>\"___")
WRITE_STRING_TEST("UTF-16LE -> UTF-32BE", UTF16LE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC", "___\"___a<00 00 00 A9><00 00 4E 01><00 01 F0 04>___\"")
WRITE_STRING_TEST("UTF-16BE -> UTF-8",    UTF16BE, UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04", "\"a<C2><A9><E4><B8><81><F0><9F><80><84>\"")
WRITE_STRING_TEST("UTF-16BE -> UTF-16LE", UTF16BE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04", "\"_a_<A9 00><01 4E><3C D8><04 DC>\"_")
WRITE_STRING_TEST("UTF-16BE -> UTF-16BE", UTF16BE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04", "_\"_a<00 A9><4E 01><D8 3C><DC 04>_\"")
WRITE_STRING_TEST("UTF-16BE -> UTF-32LE", UTF16BE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04", "\"___a___<A9 00 00 00><01 4E 00 00><04 F0 01 00>\"___")
WRITE_STRING_TEST("UTF-16BE -> UTF-32BE", UTF16BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04", "___\"___a<00 00 00 A9><00 00 4E 01><00 01 F0 04>___\"")
WRITE_STRING_TEST("UTF-32LE -> UTF-8",    UTF32LE, UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00", "\"a<C2><A9><E4><B8><81><F0><9F><80><84>\"")
WRITE_STRING_TEST("UTF-32LE -> UTF-16LE", UTF32LE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00", "\"_a_<A9 00><01 4E><3C D8><04 DC>\"_")
WRITE_STRING_TEST("UTF-32LE -> UTF-16BE", UTF32LE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00", "_\"_a<00 A9><4E 01><D8 3C><DC 04>_\"")
WRITE_STRING_TEST("UTF-32LE -> UTF-32LE", UTF32LE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00", "\"___a___<A9 00 00 00><01 4E 00 00><04 F0 01 00>\"___")
WRITE_STRING_TEST("UTF-32LE -> UTF-32BE", UTF32LE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00", "___\"___a<00 00 00 A9><00 00 4E 01><00 01 F0 04>___\"")
WRITE_STRING_TEST("UTF-32BE -> UTF-8",    UTF32BE, UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04", "\"a<C2><A9><E4><B8><81><F0><9F><80><84>\"")
WRITE_STRING_TEST("UTF-32BE -> UTF-16LE", UTF32BE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04", "\"_a_<A9 00><01 4E><3C D8><04 DC>\"_")
WRITE_STRING_TEST("UTF-32BE -> UTF-16BE", UTF32BE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04", "_\"_a<00 A9><4E 01><D8 3C><DC 04>_\"")
WRITE_STRING_TEST("UTF-32BE -> UTF-32LE", UTF32BE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04", "\"___a___<A9 00 00 00><01 4E 00 00><04 F0 01 00>\"___")
WRITE_STRING_TEST("UTF-32BE -> UTF-32BE", UTF32BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04", "___\"___a<00 00 00 A9><00 00 4E 01><00 01 F0 04>___\"")

WRITE_STRING_TEST("UTF-8 -> escaped UTF-8",          UTF8,    UTF8,    NO_REPLACE, ESCAPE_ALL, "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84", "\"a\\u00A9\\u4E01\\uD83C\\uDC04\"")
WRITE_STRING_TEST("UTF-8 -> escaped UTF-16LE",       UTF8,    UTF16LE, NO_REPLACE, ESCAPE_ALL, "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84", "\"_a_\\_u_0_0_A_9_\\_u_4_E_0_1_\\_u_D_8_3_C_\\_u_D_C_0_4_\"_")
WRITE_STRING_TEST("UTF-8 -> escaped UTF-16BE",       UTF8,    UTF16BE, NO_REPLACE, ESCAPE_ALL, "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84", "_\"_a_\\_u_0_0_A_9_\\_u_4_E_0_1_\\_u_D_8_3_C_\\_u_D_C_0_4_\"")
WRITE_STRING_TEST("UTF-8 -> escaped UTF-32LE",       UTF8,    UTF32LE, NO_REPLACE, ESCAPE_ALL, "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84", "\"___a___\\___u___0___0___A___9___\\___u___4___E___0___1___\\___u___D___8___3___C___\\___u___D___C___0___4___\"___")
WRITE_STRING_TEST("UTF-8 -> escaped UTF-32BE",       UTF8,    UTF32BE, NO_REPLACE, ESCAPE_ALL, "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84", "___\"___a___\\___u___0___0___A___9___\\___u___4___E___0___1___\\___u___D___8___3___C___\\___u___D___C___0___4___\"")

/* escape sequences */

WRITE_STRING_TEST("simple escape sequences -> UTF-8",    UTF8, UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "\\" "\"" "/" "\t" "\n" "\r" "\f" "\b", "\"\\\\\\\"\\/\\t\\n\\r\\f\\b\"")
WRITE_STRING_TEST("simple escape sequences -> UTF-16LE", UTF8, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\\" "\"" "/" "\t" "\n" "\r" "\f" "\b", "\"_\\_\\_\\_\"_\\_/_\\_t_\\_n_\\_r_\\_f_\\_b_\"_")
WRITE_STRING_TEST("simple escape sequences -> UTF-16BE", UTF8, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\\" "\"" "/" "\t" "\n" "\r" "\f" "\b", "_\"_\\_\\_\\_\"_\\_/_\\_t_\\_n_\\_r_\\_f_\\_b_\"")
WRITE_STRING_TEST("simple escape sequences -> UTF-32LE", UTF8, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\\" "\"" "/" "\t" "\n" "\r" "\f" "\b", "\"___\\___\\___\\___\"___\\___/___\\___t___\\___n___\\___r___\\___f___\\___b___\"___")
WRITE_STRING_TEST("simple escape sequences -> UTF-32BE", UTF8, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\\" "\"" "/" "\t" "\n" "\r" "\f" "\b", "___\"___\\___\\___\\___\"___\\___/___\\___t___\\___n___\\___r___\\___f___\\___b___\"")

WRITE_STRING_TEST("unprintable ASCII characters hex escape sequences -> UTF-8",    UTF8, UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "\x00\x1F\x7F", "\"\\u0000\\u001F\\u007F\"")
WRITE_STRING_TEST("unprintable ASCII characters hex escape sequences -> UTF-16LE", UTF8, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x1F\x7F", "\"_\\_u_0_0_0_0_\\_u_0_0_1_F_\\_u_0_0_7_F_\"_")
WRITE_STRING_TEST("unprintable ASCII characters hex escape sequences -> UTF-16BE", UTF8, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x1F\x7F", "_\"_\\_u_0_0_0_0_\\_u_0_0_1_F_\\_u_0_0_7_F_\"")
WRITE_STRING_TEST("unprintable ASCII characters hex escape sequences -> UTF-32LE", UTF8, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x1F\x7F", "\"___\\___u___0___0___0___0___\\___u___0___0___1___F___\\___u___0___0___7___F___\"___")
WRITE_STRING_TEST("unprintable ASCII characters hex escape sequences -> UTF-32BE", UTF8, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x1F\x7F", "___\"___\\___u___0___0___0___0___\\___u___0___0___1___F___\\___u___0___0___7___F___\"")

WRITE_STRING_TEST("BMP noncharacter hex escape sequences -> UTF-8",    UTF16BE, UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "\x00\xFE\x00\xFF\xFF\xFE\xFF\xFF", "\"\\u00FE\\u00FF\\uFFFE\\uFFFF\"")
WRITE_STRING_TEST("BMP noncharacter hex escape sequences -> UTF-16LE", UTF16BE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\xFE\x00\xFF\xFF\xFE\xFF\xFF", "\"_\\_u_0_0_F_E_\\_u_0_0_F_F_\\_u_F_F_F_E_\\_u_F_F_F_F_\"_")
WRITE_STRING_TEST("BMP noncharacter hex escape sequences -> UTF-16BE", UTF16BE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\xFE\x00\xFF\xFF\xFE\xFF\xFF", "_\"_\\_u_0_0_F_E_\\_u_0_0_F_F_\\_u_F_F_F_E_\\_u_F_F_F_F_\"")
WRITE_STRING_TEST("BMP noncharacter hex escape sequences -> UTF-32LE", UTF16BE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\xFE\x00\xFF\xFF\xFE\xFF\xFF", "\"___\\___u___0___0___F___E___\\___u___0___0___F___F___\\___u___F___F___F___E___\\___u___F___F___F___F___\"___")
WRITE_STRING_TEST("BMP noncharacter hex escape sequences -> UTF-32BE", UTF16BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\xFE\x00\xFF\xFF\xFE\xFF\xFF", "___\"___\\___u___0___0___F___E___\\___u___0___0___F___F___\\___u___F___F___F___E___\\___u___F___F___F___F___\"")

WRITE_STRING_TEST("more BMP noncharacter hex escape sequences -> UTF-8",    UTF16BE, UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "\xFD\xD0\xFD\xEF", "\"\\uFDD0\\uFDEF\"")
WRITE_STRING_TEST("more BMP noncharacter hex escape sequences -> UTF-16LE", UTF16BE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\xFD\xD0\xFD\xEF", "\"_\\_u_F_D_D_0_\\_u_F_D_E_F_\"_")
WRITE_STRING_TEST("more BMP noncharacter hex escape sequences -> UTF-16BE", UTF16BE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\xFD\xD0\xFD\xEF", "_\"_\\_u_F_D_D_0_\\_u_F_D_E_F_\"")
WRITE_STRING_TEST("more BMP noncharacter hex escape sequences -> UTF-32LE", UTF16BE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\xFD\xD0\xFD\xEF", "\"___\\___u___F___D___D___0___\\___u___F___D___E___F___\"___")
WRITE_STRING_TEST("more BMP noncharacter hex escape sequences -> UTF-32BE", UTF16BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\xFD\xD0\xFD\xEF", "___\"___\\___u___F___D___D___0___\\___u___F___D___E___F___\"")

WRITE_STRING_TEST("Javascript compatibility hex escape sequences -> UTF-8",    UTF16BE, UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "\x20\x28\x20\x29", "\"\\u2028\\u2029\"")
WRITE_STRING_TEST("Javascript compatibility hex escape sequences -> UTF-16LE", UTF16BE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\x20\x28\x20\x29", "\"_\\_u_2_0_2_8_\\_u_2_0_2_9_\"_")
WRITE_STRING_TEST("Javascript compatibility hex escape sequences -> UTF-16BE", UTF16BE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\x20\x28\x20\x29", "_\"_\\_u_2_0_2_8_\\_u_2_0_2_9_\"")
WRITE_STRING_TEST("Javascript compatibility hex escape sequences -> UTF-32LE", UTF16BE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\x20\x28\x20\x29", "\"___\\___u___2___0___2___8___\\___u___2___0___2___9___\"___")
WRITE_STRING_TEST("Javascript compatibility hex escape sequences -> UTF-32BE", UTF16BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\x20\x28\x20\x29", "___\"___\\___u___2___0___2___8___\\___u___2___0___2___9___\"")

WRITE_STRING_TEST("non-BMP noncharacter hex escape sequences -> UTF-8",    UTF16BE, UTF8,    NO_REPLACE, NO_ESCAPE_ALL, "\xD8\x34\xDD\xFE\xD8\x34\xDD\xFF", "\"\\uD834\\uDDFE\\uD834\\uDDFF\"")
WRITE_STRING_TEST("non-BMP noncharacter hex escape sequences -> UTF-16LE", UTF16BE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\xD8\x34\xDD\xFE\xD8\x34\xDD\xFF", "\"_\\_u_D_8_3_4_\\_u_D_D_F_E_\\_u_D_8_3_4_\\_u_D_D_F_F_\"_")
WRITE_STRING_TEST("non-BMP noncharacter hex escape sequences -> UTF-16BE", UTF16BE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\xD8\x34\xDD\xFE\xD8\x34\xDD\xFF", "_\"_\\_u_D_8_3_4_\\_u_D_D_F_E_\\_u_D_8_3_4_\\_u_D_D_F_F_\"")
WRITE_STRING_TEST("non-BMP noncharacter hex escape sequences -> UTF-32LE", UTF16BE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\xD8\x34\xDD\xFE\xD8\x34\xDD\xFF", "\"___\\___u___D___8___3___4___\\___u___D___D___F___E___\\___u___D___8___3___4___\\___u___D___D___F___F___\"___")
WRITE_STRING_TEST("non-BMP noncharacter hex escape sequences -> UTF-32BE", UTF16BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\xD8\x34\xDD\xFE\xD8\x34\xDD\xFF", "___\"___\\___u___D___8___3___4___\\___u___D___D___F___E___\\___u___D___8___3___4___\\___u___D___D___F___F___\"")

WRITE_STRING_TEST("replacement character in original string (1)", UTF8,    UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xEF\xBF\xBD", "\"<EF><BF><BD>\"")
WRITE_STRING_TEST("replacement character in original string (2)", UTF16LE, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xFD\xFF", "\"<EF><BF><BD>\"")
WRITE_STRING_TEST("replacement character in original string (3)", UTF16BE, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xFF\xFD", "\"<EF><BF><BD>\"")
WRITE_STRING_TEST("replacement character in original string (4)", UTF32LE, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xFD\xFF\x00\x00", "\"<EF><BF><BD>\"")
WRITE_STRING_TEST("replacement character in original string (5)", UTF32BE, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x00\xFF\xFD", "\"<EF><BF><BD>\"")

WRITE_STRING_TEST("very long string", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL,
                  "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
                  "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
                  "\""
                  "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
                  "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
                  "\"")

/* invalid input encoding sequences */

WRITE_STRING_TEST("UTF-8 truncated sequence (1)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xC2", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 truncated sequence (2)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xE0", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 truncated sequence (3)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xE0\xBF", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 truncated sequence (4)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xF0\xBF", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 truncated sequence (5)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xF0\xBF\xBF", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 overlong 2-byte sequence not allowed (1)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xC0", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 overlong 2-byte sequence not allowed (2)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xC1", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 overlong 3-byte sequence not allowed (1)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xE0\x80", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 overlong 3-byte sequence not allowed (2)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xE0\x9F", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 encoded surrogate not allowed (1)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xED\xA0", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 encoded surrogate not allowed (2)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xED\xBF", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 overlong 4-byte sequence not allowed (1)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xF0\x80", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 overlong 4-byte sequence not allowed (2)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xF0\x8F", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 encoded out-of-range codepoint not allowed (1)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xF4\x90", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid leading byte not allowed (1)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\x80", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid leading byte not allowed (2)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xBF", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid leading byte not allowed (3)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xF5", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid leading byte not allowed (4)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xFF", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid continuation byte not allowed (1)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xC2\x7F", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid continuation byte not allowed (2)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xC2\xC0", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid continuation byte not allowed (3)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xE1\x7F", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid continuation byte not allowed (4)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xE1\xC0", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid continuation byte not allowed (5)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xE1\xBF\x7F", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid continuation byte not allowed (6)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xE1\xBF\xC0", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid continuation byte not allowed (7)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xF1\x7F", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid continuation byte not allowed (8)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xF1\xC0", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid continuation byte not allowed (9)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xF1\xBF\x7F", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid continuation byte not allowed (10)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xF1\xBF\xC0", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid continuation byte not allowed (11)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xF1\xBF\xBF\x7F", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-8 invalid continuation byte not allowed (12)", UTF8, UTF8, NO_REPLACE, NO_ESCAPE_ALL, "\xF1\xBF\xBF\xC0", "\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-16LE truncated sequence", UTF16LE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, " ", "\"_ !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-16LE standalone trailing surrogate not allowed (1)", UTF16LE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\xDC", "\"_ !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-16LE standalone trailing surrogate not allowed (2)", UTF16LE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\xFF\xDF", "\"_ !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-16LE standalone leading surrogate not allowed (1)", UTF16LE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\xD8\x00_", "\"_ !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-16LE standalone leading surrogate not allowed (2)", UTF16LE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\xFF\xDB\x00_", "\"_ !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-16LE standalone leading surrogate not allowed (3)", UTF16LE, UTF16LE, NO_REPLACE, NO_ESCAPE_ALL, "\xFF\xDB_", "\"_ !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-16BE truncated sequence", UTF16BE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00", "_\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-16BE standalone trailing surrogate not allowed (1)", UTF16BE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\xDC\x00", "_\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-16BE standalone trailing surrogate not allowed (2)", UTF16BE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\xDF\xFF", "_\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-16BE standalone leading surrogate not allowed (1)", UTF16BE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\xD8\x00\x00_", "_\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-16BE standalone leading surrogate not allowed (2)", UTF16BE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\xDB\xFF\x00_", "_\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-16BE standalone leading surrogate not allowed (3)", UTF16BE, UTF16BE, NO_REPLACE, NO_ESCAPE_ALL, "\xDB\xFF", "_\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-32LE truncated sequence (1)", UTF32LE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, " ", "\"___ !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-32LE truncated sequence (2)", UTF32LE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, " \x00", "\"___ !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-32LE truncated sequence (3)", UTF32LE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, " \x00\x00", "\"___ !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-32LE encoded surrogate not allowed (1)", UTF32LE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\xD8\x00\x00", "\"___ !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-32LE encoded surrogate not allowed (2)", UTF32LE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\xDF\x00\x00", "\"___ !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-32LE encoded out-of-range codepoint not allowed (1)", UTF32LE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x00\x11\x00", "\"___ !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-32LE encoded out-of-range codepoint not allowed (2)", UTF32LE, UTF32LE, NO_REPLACE, NO_ESCAPE_ALL, "\xFF\xFF\xFF\xFF", "\"___ !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-32BE truncated sequence (1)", UTF32BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00", "___\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-32BE truncated sequence (2)", UTF32BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x00", "___\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-32BE truncated sequence (3)", UTF32BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x00\x00", "___\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-32BE encoded surrogate not allowed (1)", UTF32BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x00\xD8\x00", "___\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-32BE encoded surrogate not allowed (2)", UTF32BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x00\xDF\xFF", "___\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-32BE encoded out-of-range codepoint not allowed (1)", UTF32BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\x00\x11\x00\x00", "___\" !(InvalidEncodingSequence)")
WRITE_STRING_TEST("UTF-32BE encoded out-of-range codepoint not allowed (2)", UTF32BE, UTF32BE, NO_REPLACE, NO_ESCAPE_ALL, "\xFF\xFF\xFF\xFF", "___\" !(InvalidEncodingSequence)")

/* replace invalid input encoding sequences */

WRITE_STRING_TEST("replace UTF-8 truncated 2-byte sequence (1)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xC2", "\"abc\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 truncated 3-byte sequence (1)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xE0", "\"abc\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 truncated 3-byte sequence (2)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xE0\xBF", "\"abc\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 truncated 4-byte sequence (1)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xF0", "\"abc\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 truncated 4-byte sequence (2)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xF0\xBF", "\"abc\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 truncated 4-byte sequence (3)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xF0\xBF\xBF", "\"abc\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 overlong 2-byte sequence (1)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xC0", "\"abc\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 overlong 2-byte sequence (2)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xC1", "\"abc\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 overlong 3-byte sequence (1)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xE0\x80", "\"abc\\uFFFD\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 overlong 3-byte sequence (2)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xE0\x9F", "\"abc\\uFFFD\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 encoded surrogate (1)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xED\xA0", "\"abc\\uFFFD\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 encoded surrogate (2)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xED\xBF", "\"abc\\uFFFD\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 overlong 4-byte sequence (1)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xF0\x80", "\"abc\\uFFFD\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 overlong 4-byte sequence (2)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xF0\x8F", "\"abc\\uFFFD\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 encoded out-of-range codepoint (1)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xF4\x90", "\"abc\\uFFFD\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 invalid leading byte (1)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\x80", "\"abc\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 invalid leading byte (2)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xBF", "\"abc\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 invalid leading byte (3)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xF5", "\"abc\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 invalid leading byte (4)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xFF", "\"abc\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 invalid continuation byte (1)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xC2\x7F", "\"abc\\uFFFD\\u007F\"")
WRITE_STRING_TEST("replace UTF-8 invalid continuation byte (2)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xC2\xC0", "\"abc\\uFFFD\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 invalid continuation byte (3)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xE1\x7F", "\"abc\\uFFFD\\u007F\"")
WRITE_STRING_TEST("replace UTF-8 invalid continuation byte (4)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xE1\xC0", "\"abc\\uFFFD\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 invalid continuation byte (5)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xE1\xBF\x7F", "\"abc\\uFFFD\\u007F\"")
WRITE_STRING_TEST("replace UTF-8 invalid continuation byte (6)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xE1\xBF\xC0", "\"abc\\uFFFD\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 invalid continuation byte (7)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xF1\x7F", "\"abc\\uFFFD\\u007F\"")
WRITE_STRING_TEST("replace UTF-8 invalid continuation byte (8)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xF1\xC0", "\"abc\\uFFFD\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 invalid continuation byte (9)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xF1\xBF\x7F", "\"abc\\uFFFD\\u007F\"")
WRITE_STRING_TEST("replace UTF-8 invalid continuation byte (10)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xF1\xBF\xC0", "\"abc\\uFFFD\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-8 invalid continuation byte (11)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xF1\xBF\xBF\x7F", "\"abc\\uFFFD\\u007F\"")
WRITE_STRING_TEST("replace UTF-8 invalid continuation byte (12)", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "abc\xF1\xBF\xBF\xC0", "\"abc\\uFFFD\\uFFFD\"")
WRITE_STRING_TEST("Unicode 5.2.0 replacement example", UTF8, UTF8, REPLACE, NO_ESCAPE_ALL, "\x61\xF1\x80\x80\xE1\x80\xC2\x62\x80\x63\x80\xBF\x64", "\"a\\uFFFD\\uFFFD\\uFFFDb\\uFFFDc\\uFFFD\\uFFFDd\"")
WRITE_STRING_TEST("replace UTF-16LE standalone trailing surrogate (1)", UTF16LE, UTF8, REPLACE, NO_ESCAPE_ALL, "$\x00" "\x00\xDC", "\"$\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-16LE standalone trailing surrogate (2)", UTF16LE, UTF8, REPLACE, NO_ESCAPE_ALL, "$\x00" "\xFF\xDF", "\"$\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-16LE standalone leading surrogate (1)", UTF16LE, UTF8, REPLACE, NO_ESCAPE_ALL,  "$\x00" "\x00\xD8" "$\x00", "\"$\\uFFFD$\"")
WRITE_STRING_TEST("replace UTF-16LE standalone leading surrogate (2)", UTF16LE, UTF8, REPLACE, NO_ESCAPE_ALL,  "$\x00" "\xFF\xDB" "$\x00", "\"$\\uFFFD$\"")
WRITE_STRING_TEST("replace UTF-16BE standalone trailing surrogate (1)", UTF16BE, UTF8, REPLACE, NO_ESCAPE_ALL, "\x00$" "\xDC\x00", "\"$\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-16BE standalone trailing surrogate (2)", UTF16BE, UTF8, REPLACE, NO_ESCAPE_ALL, "\x00$" "\xDF\xFF", "\"$\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-16BE standalone leading surrogate (1)", UTF16BE, UTF8, REPLACE, NO_ESCAPE_ALL,  "\x00$" "\xD8\x00" "\x00$", "\"$\\uFFFD$\"")
WRITE_STRING_TEST("replace UTF-16BE standalone leading surrogate (2)", UTF16BE, UTF8, REPLACE, NO_ESCAPE_ALL,  "\x00$" "\xDB\xFF" "\x00$", "\"$\\uFFFD$\"")
WRITE_STRING_TEST("replace UTF-32LE encoded surrogate (1)", UTF32LE, UTF8, REPLACE, NO_ESCAPE_ALL, "\x00\xD8\x00\x00", "\"\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-32LE encoded surrogate (2)", UTF32LE, UTF8, REPLACE, NO_ESCAPE_ALL, "\xFF\xDF\x00\x00", "\"\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-32LE encoded out-of-range codepoint (1)", UTF32LE, UTF8, REPLACE, NO_ESCAPE_ALL, "\x00\x00\x11\x00", "\"\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-32LE encoded out-of-range codepoint (2)", UTF32LE, UTF8, REPLACE, NO_ESCAPE_ALL, "\x00\x00\x00\x01", "\"\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-32BE encoded surrogate (1)", UTF32BE, UTF8, REPLACE, NO_ESCAPE_ALL, "\x00\x00\xD8\x00", "\"\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-32BE encoded surrogate (2)", UTF32BE, UTF8, REPLACE, NO_ESCAPE_ALL, "\x00\x00\xDF\xFF", "\"\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-32BE encoded out-of-range codepoint (1)", UTF32BE, UTF8, REPLACE, NO_ESCAPE_ALL, "\x00\x11\x00\x00", "\"\\uFFFD\"")
WRITE_STRING_TEST("replace UTF-32BE encoded out-of-range codepoint (2)", UTF32BE, UTF8, REPLACE, NO_ESCAPE_ALL, "\x01\x00\x00\x00", "\"\\uFFFD\"")

};

static void TestWriterWriteString(void)
{
    size_t i;
    for  (i = 0; i < sizeof(s_writeStringTests)/sizeof(s_writeStringTests[0]); i++)
    {
        RunWriteStringTest(&s_writeStringTests[i]);
    }
}

static void TestWriterWriteStringWithInvalidParameters(void)
{
    JSON_Writer writer = NULL;
    printf("Test writing string with invalid parameters ... ");

    if (CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &writer) &&
        CheckWriterWriteString(writer, NULL, 1, JSON_UTF8, JSON_Failure) &&
        CheckWriterWriteString(writer, "a", 1, JSON_UnknownEncoding, JSON_Failure) &&
        CheckWriterWriteString(writer, "a", 1, (JSON_Encoding)(JSON_UTF32BE + 1), JSON_Failure))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
}

static void RunWriteNumberTest(const WriteTest* pTest)
{
    JSON_Writer writer = NULL;
    WriterSettings settings;
    WriterState state;
    printf("Test writing number %s ... ", pTest->pName);

    InitWriterSettings(&settings);
    settings.outputEncoding = pTest->outputEncoding;

    InitWriterState(&state);
    ResetOutput();

    if (CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &writer) &&
        CheckWriterSetOutputHandler(writer, &OutputHandler, JSON_Success) &&
        CheckWriterSetOutputEncoding(writer, settings.outputEncoding, JSON_Success))
    {
        if (JSON_Writer_WriteNumber(writer, pTest->pInput, pTest->length, pTest->inputEncoding) != JSON_Success)
        {
            state.error = JSON_Writer_GetError(writer);
            if (state.error != JSON_Error_None)
            {
                OutputSeparator();
                OutputFormatted("!(%s)", errorNames[state.error]);
            }
        }
        if (CheckWriterState(writer, &state) && CheckOutput(pTest->pOutput))
        {
            printf("OK\n");
        }
        else
        {
            s_failureCount++;
        }
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
    ResetOutput();
}

#define WRITE_NUMBER_TEST(name, in_enc, out_enc, input, output) { name, JSON_##in_enc, JSON_##out_enc, NO_REPLACE, NO_ESCAPE_ALL, input, sizeof(input) - 1, output },

static const WriteTest s_writeNumberTests[] =
{

WRITE_NUMBER_TEST("-0.1e+2 UTF-8 -> UTF-8",    UTF8, UTF8,    "-0.1e+2", "-0.1e+2")
WRITE_NUMBER_TEST("-0.1e+2 UTF-8 -> UTF-16LE", UTF8, UTF16LE, "-0.1e+2", "-_0_._1_e_+_2_")
WRITE_NUMBER_TEST("-0.1e+2 UTF-8 -> UTF-16BE", UTF8, UTF16BE, "-0.1e+2", "_-_0_._1_e_+_2")
WRITE_NUMBER_TEST("-0.1e+2 UTF-8 -> UTF-32LE", UTF8, UTF32LE, "-0.1e+2", "-___0___.___1___e___+___2___")
WRITE_NUMBER_TEST("-0.1e+2 UTF-8 -> UTF-32BE", UTF8, UTF32BE, "-0.1e+2", "___-___0___.___1___e___+___2")

WRITE_NUMBER_TEST("-0.1e+2 UTF-16LE -> UTF-8", UTF16LE, UTF8,    "-\x00" "0\x00" ".\x00" "1\x00" "e\x00" "+\x00" "2\x00", "-0.1e+2")
WRITE_NUMBER_TEST("-0.1e+2 UTF-16BE -> UTF-8", UTF16BE, UTF8,    "\x00" "-\x00" "0\x00" ".\x00" "1\x00" "e\x00" "+\x00" "2", "-0.1e+2")
WRITE_NUMBER_TEST("-0.1e+2 UTF-32LE -> UTF-8", UTF32LE, UTF8,    "-\x00\x00\x00" "0\x00\x00\x00" ".\x00\x00\x00" "1\x00\x00\x00" "e\x00\x00\x00" "+\x00\x00\x00" "2\x00\x00\x00", "-0.1e+2")
WRITE_NUMBER_TEST("-0.1e+2 UTF-32BE -> UTF-8", UTF32BE, UTF8,    "\x00\x00\x00" "-\x00\x00\x00" "0\x00\x00\x00" ".\x00\x00\x00" "1\x00\x00\x00" "e\x00\x00\x00" "+\x00\x00\x00" "2", "-0.1e+2")

WRITE_NUMBER_TEST("bad decimal (1)",  UTF8, UTF8, "-", "- !(InvalidNumber)")
WRITE_NUMBER_TEST("bad decimal (2)",  UTF8, UTF8, " ", "!(InvalidNumber)")
WRITE_NUMBER_TEST("bad decimal (3)",  UTF8, UTF8, " 1", "!(InvalidNumber)")
WRITE_NUMBER_TEST("bad decimal (4)",  UTF8, UTF8, "1 ", "1 !(InvalidNumber)")
WRITE_NUMBER_TEST("bad decimal (5)",  UTF8, UTF8, "01", "0 !(InvalidNumber)")
WRITE_NUMBER_TEST("bad decimal (6)",  UTF8, UTF8, "1x", "1 !(InvalidNumber)")
WRITE_NUMBER_TEST("bad decimal (7)",  UTF8, UTF8, "1.", "1. !(InvalidNumber)")
WRITE_NUMBER_TEST("bad decimal (8)",  UTF8, UTF8, "1e", "1e !(InvalidNumber)")
WRITE_NUMBER_TEST("bad decimal (9)",  UTF8, UTF8, "1e+", "1e+ !(InvalidNumber)")
WRITE_NUMBER_TEST("bad decimal (10)", UTF8, UTF8, "1e-", "1e- !(InvalidNumber)")
WRITE_NUMBER_TEST("bad decimal (11)", UTF8, UTF8, "1ex", "1e !(InvalidNumber)")

WRITE_NUMBER_TEST("hex (1)", UTF8, UTF8, "0x0", "0x0")
WRITE_NUMBER_TEST("hex (1)", UTF8, UTF8, "0X0", "0X0")
WRITE_NUMBER_TEST("hex (2)", UTF8, UTF8, "0x0123456789ABCDEF", "0x0123456789ABCDEF")
WRITE_NUMBER_TEST("hex (3)", UTF8, UTF8, "0X0123456789abcdef", "0X0123456789abcdef")

WRITE_NUMBER_TEST("bad hex not allowed (1)", UTF8, UTF8, "0x", "0x !(InvalidNumber)")
WRITE_NUMBER_TEST("bad hex not allowed (2)", UTF8, UTF8, "0X", "0X !(InvalidNumber)")
WRITE_NUMBER_TEST("bad hex not allowed (3)", UTF8, UTF8, "0x1.", "0x1 !(InvalidNumber)")
WRITE_NUMBER_TEST("bad hex not allowed (4)", UTF8, UTF8, "0x1.0", "0x1 !(InvalidNumber)")
WRITE_NUMBER_TEST("bad hex not allowed (5)", UTF8, UTF8, "0x1e+", "0x1e !(InvalidNumber)")
WRITE_NUMBER_TEST("bad hex not allowed (6)", UTF8, UTF8, "0x1e-", "0x1e !(InvalidNumber)")
WRITE_NUMBER_TEST("bad hex not allowed (7)", UTF8, UTF8, "0x1e+1", "0x1e !(InvalidNumber)")
WRITE_NUMBER_TEST("bad hex not allowed (8)", UTF8, UTF8, "0x1e-1", "0x1e !(InvalidNumber)")
WRITE_NUMBER_TEST("bad hex not allowed (9)", UTF8, UTF8, "-0x1", "-0 !(InvalidNumber)")

WRITE_NUMBER_TEST("invalid UTF-8 encoding (1)", UTF8, UTF8, "1.\x80", "1. !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-8 encoding (2)", UTF8, UTF8, "1.\xC2", "1. !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-8 encoding (3)", UTF8, UTF8, "1.\xE0\xBF", "1. !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-8 encoding (4)", UTF8, UTF8, "1.\xF0\xBF\xBF", "1. !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-8 encoding (5)", UTF8, UTF8, "1.\xC0\x80", "1. !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-8 encoding (6)", UTF8, UTF8, "1.\xC1\x80", "1. !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-8 encoding (7)", UTF8, UTF8, "1.\xE0\x80\x80", "1. !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-8 encoding (8)", UTF8, UTF8, "1.\xE0\x9F\x80", "1. !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-8 encoding (9)", UTF8, UTF8, "1.\xED\xA0\x80", "1. !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-8 encoding (10)", UTF8, UTF8, "1.\xED\xBF\x80", "1. !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-8 encoding (11)", UTF8, UTF8, "1.\xF0\x80\x80\x80", "1. !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-8 encoding (12)", UTF8, UTF8, "1.\xF0\x8F\x80\x80", "1. !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-8 encoding (13)", UTF8, UTF8, "1.\xF4\x90\x80\x80", "1. !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-16LE encoding (1)", UTF16LE, UTF8, "1\x00.", "1 !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-16LE encoding (2)", UTF16LE, UTF8, "1\x00\x00\xD8", "1 !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-16LE encoding (3)", UTF16LE, UTF8, "1\x00\xFF\xDB", "1 !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-16BE encoding (1)", UTF16BE, UTF8, "\x00" "1\x00", "1 !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-16BE encoding (2)", UTF16BE, UTF8, "\x00" "1\xD8\x00", "1 !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-16BE encoding (3)", UTF16BE, UTF8, "\x00" "1\xDB\xFF", "1 !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-32LE encoding (1)", UTF32LE, UTF8, "1\x00\x00\x00.\x00\x00", "1 !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-32LE encoding (2)", UTF32LE, UTF8, "1\x00\x00\x00\x00\xD8\x00\x00", "1 !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-32LE encoding (3)", UTF32LE, UTF8, "1\x00\x00\x00\x00\x00\x00\x01", "1 !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-32BE encoding (1)", UTF32BE, UTF8, "\x00\x00\x00" "1\x00\x00\x00", "1 !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-32BE encoding (2)", UTF32BE, UTF8, "\x00\x00\x00" "1\x00\x00\xDB\x00", "1 !(InvalidEncodingSequence)")
WRITE_NUMBER_TEST("invalid UTF-32BE encoding (3)", UTF32BE, UTF8, "\x00\x00\x00" "1\x01\x00\x00\x00", "1 !(InvalidEncodingSequence)")

};

static void TestWriterWriteNumber(void)
{
    size_t i;
    for  (i = 0; i < sizeof(s_writeNumberTests)/sizeof(s_writeNumberTests[0]); i++)
    {
        RunWriteNumberTest(&s_writeNumberTests[i]);
    }
}

static void TestWriterWriteNumberWithInvalidParameters(void)
{
    JSON_Writer writer = NULL;
    printf("Test writing number with invalid parameters ... ");

    if (CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &writer) &&
        CheckWriterWriteNumber(writer, NULL, 1, JSON_UTF8, JSON_Failure) &&
        CheckWriterWriteNumber(writer, "1", 1, JSON_UnknownEncoding, JSON_Failure) &&
        CheckWriterWriteNumber(writer, "1", 1, (JSON_Encoding)(JSON_UTF32BE + 1), JSON_Failure) &&
        CheckWriterWriteNumber(writer, "1!", 2, JSON_UTF8, JSON_Failure))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
}

#define WRITE_SPECIAL_NUMBER_TEST(name, out_enc, input, output) { name, JSON_UnknownEncoding, JSON_##out_enc, NO_REPLACE, NO_ESCAPE_ALL, NULL, (size_t)input, output },

static void RunWriteSpecialNumberTest(const WriteTest* pTest)
{
    JSON_Writer writer = NULL;
    WriterSettings settings;
    WriterState state;
    printf("Test writing special number %s ... ", pTest->pName);

    InitWriterSettings(&settings);
    settings.outputEncoding = pTest->outputEncoding;

    InitWriterState(&state);
    ResetOutput();

    if (CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &writer) &&
        CheckWriterSetOutputHandler(writer, &OutputHandler, JSON_Success) &&
        CheckWriterSetOutputEncoding(writer, settings.outputEncoding, JSON_Success))
    {
        if (JSON_Writer_WriteSpecialNumber(writer, (JSON_SpecialNumber)pTest->length) != JSON_Success)
        {
            state.error = JSON_Writer_GetError(writer);
            if (state.error != JSON_Error_None)
            {
                OutputSeparator();
                OutputFormatted("!(%s)", errorNames[state.error]);
            }
        }
        if (CheckWriterState(writer, &state) && CheckOutput(pTest->pOutput))
        {
            printf("OK\n");
        }
        else
        {
            s_failureCount++;
        }
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
    ResetOutput();
}

static const WriteTest s_writeSpecialNumberTests[] =
{

WRITE_SPECIAL_NUMBER_TEST("NaN -> UTF-8",           UTF8,    JSON_NaN, "NaN")
WRITE_SPECIAL_NUMBER_TEST("NaN -> UTF-16LE",        UTF16LE, JSON_NaN, "N_a_N_")
WRITE_SPECIAL_NUMBER_TEST("NaN -> UTF-16BE",        UTF16BE, JSON_NaN, "_N_a_N")
WRITE_SPECIAL_NUMBER_TEST("NaN -> UTF-32LE",        UTF32LE, JSON_NaN, "N___a___N___")
WRITE_SPECIAL_NUMBER_TEST("NaN -> UTF-32BE",        UTF32BE, JSON_NaN, "___N___a___N")
WRITE_SPECIAL_NUMBER_TEST("Infinity -> UTF-8",      UTF8,    JSON_Infinity, "Infinity")
WRITE_SPECIAL_NUMBER_TEST("Infinity -> UTF-16LE",   UTF16LE, JSON_Infinity, "I_n_f_i_n_i_t_y_")
WRITE_SPECIAL_NUMBER_TEST("Infinity -> UTF-16BE",   UTF16BE, JSON_Infinity, "_I_n_f_i_n_i_t_y")
WRITE_SPECIAL_NUMBER_TEST("Infinity -> UTF-32LE",   UTF32LE, JSON_Infinity, "I___n___f___i___n___i___t___y___")
WRITE_SPECIAL_NUMBER_TEST("Infinity -> UTF-32BE",   UTF32BE, JSON_Infinity, "___I___n___f___i___n___i___t___y")
WRITE_SPECIAL_NUMBER_TEST("-Infinity -> UTF-8",     UTF8,    JSON_NegativeInfinity, "-Infinity")
WRITE_SPECIAL_NUMBER_TEST("-Infinity -> UTF-16LE",  UTF16LE, JSON_NegativeInfinity, "-_I_n_f_i_n_i_t_y_")
WRITE_SPECIAL_NUMBER_TEST("-Infinity -> UTF-16BE",  UTF16BE, JSON_NegativeInfinity, "_-_I_n_f_i_n_i_t_y")
WRITE_SPECIAL_NUMBER_TEST("-Infinity -> UTF-32LE",  UTF32LE, JSON_NegativeInfinity, "-___I___n___f___i___n___i___t___y___")
WRITE_SPECIAL_NUMBER_TEST("-Infinity -> UTF-32BE",  UTF32BE, JSON_NegativeInfinity, "___-___I___n___f___i___n___i___t___y")

};

static void TestWriterWriteSpecialNumber(void)
{
    size_t i;
    for  (i = 0; i < sizeof(s_writeSpecialNumberTests)/sizeof(s_writeSpecialNumberTests[0]); i++)
    {
        RunWriteSpecialNumberTest(&s_writeSpecialNumberTests[i]);
    }
}

#define WRITE_ARRAY_TEST(name, out_enc, output) { name, JSON_UnknownEncoding, JSON_##out_enc, NO_REPLACE, NO_ESCAPE_ALL, NULL, 0, output },

static void RunWriteArrayTest(const WriteTest* pTest)
{
    JSON_Writer writer = NULL;
    WriterSettings settings;
    WriterState state;
    printf("Test writing array %s ... ", pTest->pName);

    InitWriterSettings(&settings);
    settings.outputEncoding = pTest->outputEncoding;

    InitWriterState(&state);
    ResetOutput();

    if (CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &writer) &&
        CheckWriterSetOutputHandler(writer, &OutputHandler, JSON_Success) &&
        CheckWriterSetOutputEncoding(writer, settings.outputEncoding, JSON_Success))
    {
        if (JSON_Writer_WriteStartArray(writer) != JSON_Success ||
            JSON_Writer_WriteStartArray(writer) != JSON_Success ||
            JSON_Writer_WriteEndArray(writer) != JSON_Success ||
            JSON_Writer_WriteComma(writer) != JSON_Success ||
            JSON_Writer_WriteNumber(writer, "0", 1, JSON_UTF8) != JSON_Success ||
            JSON_Writer_WriteComma(writer) != JSON_Success ||
            JSON_Writer_WriteString(writer, "a", 1, JSON_UTF8) != JSON_Success ||
            JSON_Writer_WriteEndArray(writer) != JSON_Success)
        {
            state.error = JSON_Writer_GetError(writer);
            if (state.error != JSON_Error_None)
            {
                OutputSeparator();
                OutputFormatted("!(%s)", errorNames[state.error]);
            }
        }
        if (CheckWriterState(writer, &state) && CheckOutput(pTest->pOutput))
        {
            printf("OK\n");
        }
        else
        {
            s_failureCount++;
        }
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
    ResetOutput();
}

static const WriteTest s_writeArrayTests[] =
{

WRITE_ARRAY_TEST("-> UTF-8",    UTF8,    "[[],0,\"a\"]")
WRITE_ARRAY_TEST("-> UTF-16LE", UTF16LE, "[_[_]_,_0_,_\"_a_\"_]_")
WRITE_ARRAY_TEST("-> UTF-16BE", UTF16BE, "_[_[_]_,_0_,_\"_a_\"_]")
WRITE_ARRAY_TEST("-> UTF-32LE", UTF32LE, "[___[___]___,___0___,___\"___a___\"___]___")
WRITE_ARRAY_TEST("-> UTF-32BE", UTF32BE, "___[___[___]___,___0___,___\"___a___\"___]")

};

static void TestWriterWriteArray(void)
{
    size_t i;
    for  (i = 0; i < sizeof(s_writeArrayTests)/sizeof(s_writeArrayTests[0]); i++)
    {
        RunWriteArrayTest(&s_writeArrayTests[i]);
    }
}

#define WRITE_OBJECT_TEST(name, out_enc, output) { name, JSON_UnknownEncoding, JSON_##out_enc, NO_REPLACE, NO_ESCAPE_ALL, NULL, 0, output },

static void RunWriteObjectTest(const WriteTest* pTest)
{
    JSON_Writer writer = NULL;
    WriterSettings settings;
    WriterState state;
    printf("Test writing object %s ... ", pTest->pName);

    InitWriterSettings(&settings);
    settings.outputEncoding = pTest->outputEncoding;

    InitWriterState(&state);
    ResetOutput();

    if (CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &writer) &&
        CheckWriterSetOutputHandler(writer, &OutputHandler, JSON_Success) &&
        CheckWriterSetOutputEncoding(writer, settings.outputEncoding, JSON_Success))
    {
        if (JSON_Writer_WriteStartObject(writer) != JSON_Success ||
            JSON_Writer_WriteString(writer, "a", 1, JSON_UTF8) != JSON_Success ||
            JSON_Writer_WriteColon(writer) != JSON_Success ||
            JSON_Writer_WriteStartObject(writer) != JSON_Success ||
            JSON_Writer_WriteEndObject(writer) != JSON_Success ||
            JSON_Writer_WriteComma(writer) != JSON_Success ||
            JSON_Writer_WriteString(writer, "b", 1, JSON_UTF8) != JSON_Success ||
            JSON_Writer_WriteColon(writer) != JSON_Success ||
            JSON_Writer_WriteNumber(writer, "0", 1, JSON_UTF8) != JSON_Success ||
            JSON_Writer_WriteEndObject(writer) != JSON_Success)
        {
            state.error = JSON_Writer_GetError(writer);
            if (state.error != JSON_Error_None)
            {
                OutputSeparator();
                OutputFormatted("!(%s)", errorNames[state.error]);
            }
        }
        if (CheckWriterState(writer, &state) && CheckOutput(pTest->pOutput))
        {
            printf("OK\n");
        }
        else
        {
            s_failureCount++;
        }
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
    ResetOutput();
}

static const WriteTest s_writeObjectTests[] =
{

WRITE_OBJECT_TEST("-> UTF-8",    UTF8,    "{\"a\":{},\"b\":0}")
WRITE_OBJECT_TEST("-> UTF-16LE", UTF16LE, "{_\"_a_\"_:_{_}_,_\"_b_\"_:_0_}_")
WRITE_OBJECT_TEST("-> UTF-16BE", UTF16BE, "_{_\"_a_\"_:_{_}_,_\"_b_\"_:_0_}")
WRITE_OBJECT_TEST("-> UTF-32LE", UTF32LE, "{___\"___a___\"___:___{___}___,___\"___b___\"___:___0___}___")
WRITE_OBJECT_TEST("-> UTF-32BE", UTF32BE, "___{___\"___a___\"___:___{___}___,___\"___b___\"___:___0___}")

};

static void TestWriterWriteObject(void)
{
    size_t i;
    for  (i = 0; i < sizeof(s_writeObjectTests)/sizeof(s_writeObjectTests[0]); i++)
    {
        RunWriteObjectTest(&s_writeObjectTests[i]);
    }
}

#define WRITE_SPACE_TEST(name, out_enc, count, output) { name, JSON_UnknownEncoding, JSON_##out_enc, NO_REPLACE, NO_ESCAPE_ALL, NULL, count, output },

static void RunWriteSpaceTest(const WriteTest* pTest)
{
    JSON_Writer writer = NULL;
    WriterSettings settings;
    WriterState state;
    printf("Test writing space %s ... ", pTest->pName);

    InitWriterSettings(&settings);
    settings.outputEncoding = pTest->outputEncoding;

    InitWriterState(&state);
    ResetOutput();

    if (CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &writer) &&
        CheckWriterSetOutputHandler(writer, &OutputHandler, JSON_Success) &&
        CheckWriterSetOutputEncoding(writer, settings.outputEncoding, JSON_Success))
    {
        if (JSON_Writer_WriteSpace(writer, pTest->length) != JSON_Success)
        {
            state.error = JSON_Writer_GetError(writer);
            if (state.error != JSON_Error_None)
            {
                OutputSeparator();
                OutputFormatted("!(%s)", errorNames[state.error]);
            }
        }
        if (CheckWriterState(writer, &state) && CheckOutput(pTest->pOutput))
        {
            printf("OK\n");
        }
        else
        {
            s_failureCount++;
        }
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
    ResetOutput();
}

static const WriteTest s_writeSpaceTests[] =
{

WRITE_SPACE_TEST("-> UTF-8",    UTF8,    1, "<20>")
WRITE_SPACE_TEST("-> UTF-16LE", UTF16LE, 1, "<20 00>")
WRITE_SPACE_TEST("-> UTF-16BE", UTF16BE, 1, "<00 20>")
WRITE_SPACE_TEST("-> UTF-32LE", UTF32LE, 1, "<20 00 00 00>")
WRITE_SPACE_TEST("-> UTF-32BE", UTF32BE, 1, "<00 00 00 20>")

WRITE_SPACE_TEST("(2) -> UTF-8",    UTF8,    2, "<20><20>")
WRITE_SPACE_TEST("(2) -> UTF-16LE", UTF16LE, 2, "<20 00><20 00>")
WRITE_SPACE_TEST("(2) -> UTF-16BE", UTF16BE, 2, "<00 20><00 20>")
WRITE_SPACE_TEST("(2) -> UTF-32LE", UTF32LE, 2, "<20 00 00 00><20 00 00 00>")
WRITE_SPACE_TEST("(2) -> UTF-32BE", UTF32BE, 2, "<00 00 00 20><00 00 00 20>")

WRITE_SPACE_TEST("(3) -> UTF-8",    UTF8,    3, "<20><20><20>")
WRITE_SPACE_TEST("(3) -> UTF-16LE", UTF16LE, 3, "<20 00><20 00><20 00>")
WRITE_SPACE_TEST("(3) -> UTF-16BE", UTF16BE, 3, "<00 20><00 20><00 20>")
WRITE_SPACE_TEST("(3) -> UTF-32LE", UTF32LE, 3, "<20 00 00 00><20 00 00 00><20 00 00 00>")
WRITE_SPACE_TEST("(3) -> UTF-32BE", UTF32BE, 3, "<00 00 00 20><00 00 00 20><00 00 00 20>")

WRITE_SPACE_TEST("(15) -> UTF-8",    UTF8,    15, "<20><20><20><20><20><20><20><20><20><20><20><20><20><20><20>")
WRITE_SPACE_TEST("(15) -> UTF-16LE", UTF16LE, 15, "<20 00><20 00><20 00><20 00><20 00><20 00><20 00><20 00><20 00><20 00><20 00><20 00><20 00><20 00><20 00>")
WRITE_SPACE_TEST("(15) -> UTF-16BE", UTF16BE, 15, "<00 20><00 20><00 20><00 20><00 20><00 20><00 20><00 20><00 20><00 20><00 20><00 20><00 20><00 20><00 20>")
WRITE_SPACE_TEST("(15) -> UTF-32LE", UTF32LE, 15, "<20 00 00 00><20 00 00 00><20 00 00 00><20 00 00 00><20 00 00 00><20 00 00 00><20 00 00 00><20 00 00 00><20 00 00 00><20 00 00 00><20 00 00 00><20 00 00 00><20 00 00 00><20 00 00 00><20 00 00 00>")
WRITE_SPACE_TEST("(15) -> UTF-32BE", UTF32BE, 15, "<00 00 00 20><00 00 00 20><00 00 00 20><00 00 00 20><00 00 00 20><00 00 00 20><00 00 00 20><00 00 00 20><00 00 00 20><00 00 00 20><00 00 00 20><00 00 00 20><00 00 00 20><00 00 00 20><00 00 00 20>")

};

static void TestWriterWriteSpace(void)
{
    size_t i;
    for  (i = 0; i < sizeof(s_writeSpaceTests)/sizeof(s_writeSpaceTests[0]); i++)
    {
        RunWriteSpaceTest(&s_writeSpaceTests[i]);
    }
}

#define WRITE_NEWLINE_TEST(name, out_enc, crlf, output) { name, JSON_UnknownEncoding, JSON_##out_enc, NO_REPLACE, NO_ESCAPE_ALL, NULL, crlf, output },

#define NO_CRLF  JSON_False
#define CRLF     JSON_True

static void RunWriteNewLineTest(const WriteTest* pTest)
{
    JSON_Writer writer = NULL;
    WriterSettings settings;
    WriterState state;
    printf("Test writing newline %s ... ", pTest->pName);

    InitWriterSettings(&settings);
    settings.outputEncoding = pTest->outputEncoding;
    settings.useCRLF = (JSON_Boolean)pTest->length;

    InitWriterState(&state);
    ResetOutput();

    if (CheckWriterCreateWithCustomMemorySuite(&ReallocHandler, &FreeHandler, JSON_Success, &writer) &&
        CheckWriterSetOutputHandler(writer, &OutputHandler, JSON_Success) &&
        CheckWriterSetOutputEncoding(writer, settings.outputEncoding, JSON_Success) &&
        CheckWriterSetUseCRLF(writer, settings.useCRLF, JSON_Success))
    {
        if (JSON_Writer_WriteNewLine(writer) != JSON_Success)
        {
            state.error = JSON_Writer_GetError(writer);
            if (state.error != JSON_Error_None)
            {
                OutputSeparator();
                OutputFormatted("!(%s)", errorNames[state.error]);
            }
        }
        if (CheckWriterState(writer, &state) && CheckOutput(pTest->pOutput))
        {
            printf("OK\n");
        }
        else
        {
            s_failureCount++;
        }
    }
    else
    {
        s_failureCount++;
    }
    JSON_Writer_Free(writer);
    ResetOutput();
}

static const WriteTest s_writeNewLineTests[] =
{

WRITE_NEWLINE_TEST("-> UTF-8",    UTF8,    NO_CRLF, "<0A>")
WRITE_NEWLINE_TEST("-> UTF-16LE", UTF16LE, NO_CRLF, "<0A 00>")
WRITE_NEWLINE_TEST("-> UTF-16BE", UTF16BE, NO_CRLF, "<00 0A>")
WRITE_NEWLINE_TEST("-> UTF-32LE", UTF32LE, NO_CRLF, "<0A 00 00 00>")
WRITE_NEWLINE_TEST("-> UTF-32BE", UTF32BE, NO_CRLF, "<00 00 00 0A>")
WRITE_NEWLINE_TEST("-> UTF-8",    UTF8,    CRLF,    "<0D><0A>")
WRITE_NEWLINE_TEST("-> UTF-16LE", UTF16LE, CRLF,    "<0D 00><0A 00>")
WRITE_NEWLINE_TEST("-> UTF-16BE", UTF16BE, CRLF,    "<00 0D><00 0A>")
WRITE_NEWLINE_TEST("-> UTF-32LE", UTF32LE, CRLF,    "<0D 00 00 00><0A 00 00 00>")
WRITE_NEWLINE_TEST("-> UTF-32BE", UTF32BE, CRLF,    "<00 00 00 0D><00 00 00 0A>")

};

static void TestWriterWriteNewLine(void)
{
    size_t i;
    for  (i = 0; i < sizeof(s_writeNewLineTests)/sizeof(s_writeNewLineTests[0]); i++)
    {
        RunWriteNewLineTest(&s_writeNewLineTests[i]);
    }
}

#endif /* JSON_NO_WRITER */

static void TestLibraryVersion(void)
{
    const JSON_Version* pVersion = JSON_LibraryVersion();
    printf("Test library version ... ");
    if (pVersion->major == JSON_MAJOR_VERSION &&
        pVersion->minor == JSON_MINOR_VERSION &&
        pVersion->micro == JSON_MICRO_VERSION)
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
}

static int CheckErrorString(JSON_Error error, const char* pExpectedMessage)
{
    const char* pActualMessage = JSON_ErrorString(error);
    if (strcmp(pExpectedMessage, pActualMessage))
    {
        printf("FAILURE: expected JSON_ErrorString() to return \"%s\" instead of \"%s\"\n", pExpectedMessage, pActualMessage);
        return 0;
    }
    return 1;
}

static void TestErrorStrings(void)
{
    static const struct
    {
        int error;
        const char* message;
    } cases[] = {
        { -1000, "" },
        { -1, "" },

        { JSON_Error_None, "no error" },
        { JSON_Error_OutOfMemory, "could not allocate enough memory" },
        { JSON_Error_AbortedByHandler, "the operation was aborted by a handler" },
        { JSON_Error_BOMNotAllowed, "the input begins with a byte-order mark (BOM), which is not allowed by RFC 4627" },
        { JSON_Error_InvalidEncodingSequence, "the input contains a byte or sequence of bytes that is not valid for the input encoding" },
        { JSON_Error_UnknownToken, "the input contains an unknown token" },
        { JSON_Error_UnexpectedToken, "the input contains an unexpected token" },
        { JSON_Error_IncompleteToken,  "the input ends in the middle of a token" },
        { JSON_Error_ExpectedMoreTokens, "the input ends when more tokens are expected" },
        { JSON_Error_UnescapedControlCharacter, "the input contains a string containing an unescaped control character (U+0000 - U+001F)" },
        { JSON_Error_InvalidEscapeSequence, "the input contains a string containing an invalid escape sequence" },
        { JSON_Error_UnpairedSurrogateEscapeSequence, "the input contains a string containing an unmatched UTF-16 surrogate codepoint" },
        { JSON_Error_TooLongString, "the input contains a string that is too long" },
        { JSON_Error_InvalidNumber, "the input contains an invalid number" },
        { JSON_Error_TooLongNumber, "the input contains a number that is too long" },
        { JSON_Error_DuplicateObjectMember, "the input contains an object with duplicate members" },
        { JSON_Error_StoppedAfterEmbeddedDocument, "the end of the embedded document was reached"},

        { JSON_Error_StoppedAfterEmbeddedDocument + 1, "" },
        { 1000, "" }
    };

    size_t i;
    printf("Test error strings ... ");
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        if (!CheckErrorString((JSON_Error)cases[i].error, cases[i].message))
        {
            s_failureCount++;
            return;
        }
    }
    printf("OK\n");
}

static void TestNativeUTF16Encoding(void)
{
    static const int one = 1;
    JSON_Encoding expected = *(char*)&one ? JSON_UTF16LE : JSON_UTF16BE;
    JSON_Encoding actual = JSON_NativeUTF16Encoding();
    printf("Test native UTF-16 encoding ... ");
    if (actual == expected)
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
}

static void TestNativeUTF32Encoding(void)
{
    static const int one = 1;
    JSON_Encoding expected = *(char*)&one ? JSON_UTF32LE : JSON_UTF32BE;
    JSON_Encoding actual = JSON_NativeUTF32Encoding();
    printf("Test native UTF-32 encoding ... ");
    if (actual == expected)
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
}

static void TestNoLeaks(void)
{
    printf("Checking for memory leaks ... ");
    if (!s_blocksAllocated && !s_bytesAllocated)
    {
        printf("OK\n");
    }
    else
    {
        printf("FAILURE: %d blocks (%d bytes) leaked\n", (int)s_blocksAllocated, (int)s_bytesAllocated);
        s_failureCount++;
    }
}

int main(int argc, char* argv[])
{
    (void)argc; /* unused */
    (void)argv; /* unused */

#ifndef JSON_NO_PARSER
    TestParserCreate();
    TestParserCreateWithCustomMemorySuite();
    TestParserCreateMallocFailure();
    TestParserMissing();
    TestParserGetErrorLocationNullLocation();
    TestParserGetErrorLocationNoError();
    TestParserGetTokenLocationOutsideHandler();
    TestParserGetAfterTokenLocationOutsideHandler();
    TestParserSetSettings();
    TestParserSetInvalidSettings();
    TestParserSetHandlers();
    TestParserReset();
    TestParserMisbehaveInCallbacks();
    TestParserAbortInCallbacks();
    TestParserStringMallocFailure();
    TestParserStringReallocFailure();
    TestParserStackMallocFailure();
    TestParserStackReallocFailure();
    TestParserDuplicateMemberTrackingMallocFailure();
    TestParserParse();
#endif

#ifndef JSON_NO_WRITER
    TestWriterCreate();
    TestWriterCreateWithCustomMemorySuite();
    TestWriterCreateMallocFailure();
    TestWriterMissing();
    TestWriterSetSettings();
    TestWriterSetInvalidSettings();
    TestWriterSetHandlers();
    TestWriterReset();
    TestWriterMisbehaveInCallbacks();
    TestWriterAbortInCallbacks();
    TestWriterStackMallocFailure();
    TestWriterStackReallocFailure();
    TestWriterWriteNull();
    TestWriterWriteBoolean();
    TestWriterWriteString();
    TestWriterWriteStringWithInvalidParameters();
    TestWriterWriteNumber();
    TestWriterWriteNumberWithInvalidParameters();
    TestWriterWriteSpecialNumber();
    TestWriterWriteArray();
    TestWriterWriteObject();
    TestWriterWriteSpace();
    TestWriterWriteNewLine();
#endif

    TestLibraryVersion();
    TestErrorStrings();
    TestNativeUTF16Encoding();
    TestNativeUTF32Encoding();
    TestNoLeaks();

    if (s_failureCount)
    {
        printf("Error: %d failures.\n", s_failureCount);
    }
    else
    {
        printf("All tests passed.\n");
    }
    return s_failureCount;
}
