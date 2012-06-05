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
static int s_failParseCallback = 0;
static int s_misbehaveInCallback = 0;
static size_t s_blocksAllocated = 0;
static size_t s_bytesAllocated = 0;

typedef struct tag_ParserState
{
    JSON_Error    error;
    JSON_Location errorLocation;
    JSON_Boolean  startedParsing;
    JSON_Boolean  finishedParsing;
    JSON_Encoding inputEncoding;
} ParserState;

static void InitParserState(ParserState* pState)
{
    pState->error = JSON_Error_None;
    pState->errorLocation.byte = 0;
    pState->errorLocation.line = 0;
    pState->errorLocation.column = 0;
    pState->errorLocation.depth = 0;
    pState->startedParsing = JSON_False;
    pState->finishedParsing = JSON_False;
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
    pState->startedParsing = JSON_Parser_StartedParsing(parser);
    pState->finishedParsing = JSON_Parser_FinishedParsing(parser);
    pState->inputEncoding = JSON_Parser_GetInputEncoding(parser);
}

static int ParserStatesAreIdentical(const ParserState* pState1, const ParserState* pState2)
{
    return (pState1->error == pState2->error &&
            pState1->errorLocation.byte == pState2->errorLocation.byte &&
            pState1->errorLocation.line == pState2->errorLocation.line &&
            pState1->errorLocation.column == pState2->errorLocation.column &&
            pState1->errorLocation.depth == pState2->errorLocation.depth &&
            pState1->startedParsing == pState2->startedParsing &&
            pState1->finishedParsing == pState2->finishedParsing &&
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
               "  JSON_Parser_GetErrorLocation().byte   %8d   %8d\n"
               "  JSON_Parser_GetErrorLocation().line   %8d   %8d\n"
               "  JSON_Parser_GetErrorLocation().column %8d   %8d\n"
               "  JSON_Parser_GetErrorLocation().depth  %8d   %8d\n"
               "  JSON_Parser_StartedParsing()          %8d   %8d\n"
               "  JSON_Parser_FinishedParsing()         %8d   %8d\n"
               "  JSON_Parser_GetInputEncoding()        %8d   %8d\n"
               ,
               (int)pExpectedState->error, (int)actualState.error,
               (int)pExpectedState->errorLocation.byte, (int)actualState.errorLocation.byte,
               (int)pExpectedState->errorLocation.line, (int)actualState.errorLocation.line,
               (int)pExpectedState->errorLocation.column, (int)actualState.errorLocation.column,
               (int)pExpectedState->errorLocation.depth, (int)actualState.errorLocation.depth,
               (int)pExpectedState->startedParsing, (int)actualState.startedParsing,
               (int)pExpectedState->finishedParsing, (int)actualState.finishedParsing,
               (int)pExpectedState->inputEncoding, (int)actualState.inputEncoding
            );
    }
    return isValid;
}

typedef struct tag_ParserSettings
{
    void*         userData;
    JSON_Encoding inputEncoding;
    JSON_Encoding outputEncoding;
    size_t        maxOutputStringLength;
    JSON_Boolean  allowBOM;
    JSON_Boolean  allowComments;
    JSON_Boolean  allowSpecialNumbers;
    JSON_Boolean  allowHexNumbers;
    JSON_Boolean  replaceInvalidEncodingSequences;
    JSON_Boolean  trackObjectMembers;
} ParserSettings;

static void InitParserSettings(ParserSettings* pSettings)
{
    pSettings->userData = NULL;
    pSettings->inputEncoding = JSON_UnknownEncoding;
    pSettings->outputEncoding = JSON_UTF8;
    pSettings->maxOutputStringLength = (size_t)-1;
    pSettings->allowBOM = JSON_False;
    pSettings->allowComments = JSON_False;
    pSettings->allowSpecialNumbers = JSON_False;
    pSettings->allowHexNumbers = JSON_False;
    pSettings->replaceInvalidEncodingSequences = JSON_False;
    pSettings->trackObjectMembers = JSON_False;
}

static void GetParserSettings(JSON_Parser parser, ParserSettings* pSettings)
{
    pSettings->userData = JSON_Parser_GetUserData(parser);
    pSettings->inputEncoding = JSON_Parser_GetInputEncoding(parser);
    pSettings->outputEncoding = JSON_Parser_GetOutputEncoding(parser);
    pSettings->maxOutputStringLength = JSON_Parser_GetMaxOutputStringLength(parser);
    pSettings->allowBOM = JSON_Parser_GetAllowBOM(parser);
    pSettings->allowComments = JSON_Parser_GetAllowComments(parser);
    pSettings->allowSpecialNumbers = JSON_Parser_GetAllowSpecialNumbers(parser);
    pSettings->allowHexNumbers = JSON_Parser_GetAllowHexNumbers(parser);
    pSettings->replaceInvalidEncodingSequences = JSON_Parser_GetReplaceInvalidEncodingSequences(parser);
    pSettings->trackObjectMembers = JSON_Parser_GetTrackObjectMembers(parser);
}

static int ParserSettingsAreIdentical(const ParserSettings* pSettings1, const ParserSettings* pSettings2)
{
    return (pSettings1->userData == pSettings2->userData &&
            pSettings1->inputEncoding == pSettings2->inputEncoding &&
            pSettings1->outputEncoding == pSettings2->outputEncoding &&
            pSettings1->maxOutputStringLength == pSettings2->maxOutputStringLength &&
            pSettings1->allowBOM == pSettings2->allowBOM &&
            pSettings1->allowComments == pSettings2->allowComments &&
            pSettings1->allowSpecialNumbers == pSettings2->allowSpecialNumbers &&
            pSettings1->allowHexNumbers == pSettings2->allowHexNumbers &&
            pSettings1->replaceInvalidEncodingSequences == pSettings2->replaceInvalidEncodingSequences &&
            pSettings1->trackObjectMembers == pSettings2->trackObjectMembers);
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
               "  JSON_Parser_GetOutputEncoding()                  %8d   %8d\n"
               "  JSON_Parser_GetMaxOutputStringLength()           %8d   %8d\n"
               ,
               pExpectedSettings->userData, actualSettings.userData,
               (int)pExpectedSettings->inputEncoding, (int)actualSettings.inputEncoding,
               (int)pExpectedSettings->outputEncoding, (int)actualSettings.outputEncoding,
               (int)pExpectedSettings->maxOutputStringLength, (int)actualSettings.maxOutputStringLength
            );
        printf("  JSON_Parser_GetAllowBOM()                        %8d   %8d\n"
               "  JSON_Parser_GetAllowComments()                   %8d   %8d\n"
               "  JSON_Parser_GetAllowSpecialNumbers()             %8d   %8d\n"
               "  JSON_Parser_GetAllowHexNumbers()                 %8d   %8d\n"
               "  JSON_Parser_GetReplaceInvalidEncodingSequences() %8d   %8d\n"
               "  JSON_Parser_GetTrackObjectMembers()              %8d   %8d\n"
               ,
               (int)pExpectedSettings->allowBOM, (int)actualSettings.allowBOM,
               (int)pExpectedSettings->allowComments, (int)actualSettings.allowComments,
               (int)pExpectedSettings->allowSpecialNumbers, (int)actualSettings.allowSpecialNumbers,
               (int)pExpectedSettings->allowHexNumbers, (int)actualSettings.allowHexNumbers,
               (int)pExpectedSettings->replaceInvalidEncodingSequences, (int)actualSettings.replaceInvalidEncodingSequences,
               (int)pExpectedSettings->trackObjectMembers, (int)actualSettings.trackObjectMembers
            );
    }
    return identical;
}

typedef struct tag_ParserHandlers
{
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
} ParserHandlers;

static void InitParserHandlers(ParserHandlers* pHandlers)
{
    pHandlers->nullHandler = NULL;
    pHandlers->booleanHandler = NULL;
    pHandlers->stringHandler = NULL;
    pHandlers->numberHandler = NULL;
    pHandlers->rawNumberHandler = NULL;
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
    pHandlers->nullHandler = JSON_Parser_GetNullHandler(parser);
    pHandlers->booleanHandler = JSON_Parser_GetBooleanHandler(parser);
    pHandlers->stringHandler = JSON_Parser_GetStringHandler(parser);
    pHandlers->numberHandler = JSON_Parser_GetNumberHandler(parser);
    pHandlers->rawNumberHandler = JSON_Parser_GetRawNumberHandler(parser);
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
    return (pHandlers1->nullHandler == pHandlers2->nullHandler &&
            pHandlers1->booleanHandler == pHandlers2->booleanHandler &&
            pHandlers1->stringHandler == pHandlers2->stringHandler &&
            pHandlers1->numberHandler == pHandlers2->numberHandler &&
            pHandlers1->rawNumberHandler == pHandlers2->rawNumberHandler &&
            pHandlers1->specialNumberHandler == pHandlers2->specialNumberHandler &&
            pHandlers1->startObjectHandler == pHandlers2->startObjectHandler &&
            pHandlers1->endObjectHandler == pHandlers2->endObjectHandler &&
            pHandlers1->objectMemberHandler == pHandlers2->objectMemberHandler &&
            pHandlers1->startArrayHandler == pHandlers2->startArrayHandler &&
            pHandlers1->endArrayHandler == pHandlers2->endArrayHandler &&
            pHandlers1->arrayItemHandler == pHandlers2->arrayItemHandler);
}

#define HANDLER_STRING(p) ((p) ? "non-NULL" : "NULL")

static int CheckParserHandlers(JSON_Parser parser, const ParserHandlers* pExpectedHandlers)
{
    int identical;
    ParserHandlers actualHandlers;
    GetParserHandlers(parser, &actualHandlers);
    identical = ParserHandlersAreIdentical(pExpectedHandlers, &actualHandlers);
    if (!identical)
    {
        printf("FAILURE: parser handlers do not match\n"
               "  HANDLERS                             EXPECTED     ACTUAL\n"
               "  JSON_Parser_GetNullHandler()         %8s   %8s\n"
               "  JSON_Parser_GetBooleanHandler()      %8s   %8s\n"
               "  JSON_Parser_GetStringHandler()       %8s   %8s\n"
               "  JSON_Parser_GetNumberHandler()       %8s   %8s\n"
               "  JSON_Parser_GetRawNumberHandler()    %8s   %8s\n"
               "  JSON_Parser_GetSpecialNumberHandler()%8s   %8s\n"
               ,
               HANDLER_STRING(pExpectedHandlers->nullHandler), HANDLER_STRING(actualHandlers.nullHandler),
               HANDLER_STRING(pExpectedHandlers->booleanHandler), HANDLER_STRING(actualHandlers.booleanHandler),
               HANDLER_STRING(pExpectedHandlers->stringHandler), HANDLER_STRING(actualHandlers.stringHandler),
               HANDLER_STRING(pExpectedHandlers->numberHandler), HANDLER_STRING(actualHandlers.numberHandler),
               HANDLER_STRING(pExpectedHandlers->rawNumberHandler), HANDLER_STRING(actualHandlers.rawNumberHandler),
               HANDLER_STRING(pExpectedHandlers->specialNumberHandler), HANDLER_STRING(actualHandlers.specialNumberHandler)
            );
        printf("  JSON_Parser_GetStartObjectHandler()  %8s   %8s\n"
               "  JSON_Parser_GetEndObjectHandler()    %8s   %8s\n"
               "  JSON_Parser_GetObjectMemberHandler() %8s   %8s\n"
               "  JSON_Parser_GetStartArrayHandler()   %8s   %8s\n"
               "  JSON_Parser_GetEndArrayHandler()     %8s   %8s\n"
               "  JSON_Parser_GetArrayItemHandler()    %8s   %8s\n"
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

static int CheckCreateParser(const JSON_MemorySuite* pMemorySuite, JSON_Status expectedStatus, JSON_Parser* pParser)
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

static int CheckCreateParserWithCustomMemorySuite(JSON_MallocHandler m, JSON_ReallocHandler r, JSON_FreeHandler f, JSON_Status expectedStatus, JSON_Parser* pParser)
{
    JSON_MemorySuite memorySuite;
    memorySuite.userData = NULL;
    memorySuite.malloc = m;
    memorySuite.realloc = r;
    memorySuite.free = f;
    return CheckCreateParser(&memorySuite, expectedStatus, pParser);
}

static int CheckResetParser(JSON_Parser parser, JSON_Status expectedStatus)
{
    if (JSON_Parser_Reset(parser) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_Reset() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckFreeParser(JSON_Parser parser, JSON_Status expectedStatus)
{
    if (JSON_Parser_Free(parser) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_Free() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckGetErrorLocation(JSON_Parser parser, JSON_Location* pLocation, JSON_Status expectedStatus)
{
    if (JSON_Parser_GetErrorLocation(parser, pLocation) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_GetErrorLocation() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckGetTokenLocation(JSON_Parser parser, JSON_Location* pLocation, JSON_Status expectedStatus)
{
    if (JSON_Parser_GetTokenLocation(parser, pLocation) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_GetTokenLocation() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetUserData(JSON_Parser parser, void* userData, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetUserData(parser, userData) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetUserData() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetInputEncoding(JSON_Parser parser, JSON_Encoding encoding, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetInputEncoding(parser, encoding) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetInputEncoding() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetOutputEncoding(JSON_Parser parser, JSON_Encoding encoding, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetOutputEncoding(parser, encoding) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetOutputEncoding() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetMaxOutputStringLength(JSON_Parser parser, size_t maxLength, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetMaxOutputStringLength(parser, maxLength) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetMaxOutputStringLength() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetAllowBOM(JSON_Parser parser, JSON_Boolean allowBOM, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetAllowBOM(parser, allowBOM) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetAllowBOM() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetAllowComments(JSON_Parser parser, JSON_Boolean allowComments, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetAllowComments(parser, allowComments) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetAllowComments() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetAllowSpecialNumbers(JSON_Parser parser, JSON_Boolean allowSpecialNumbers, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetAllowSpecialNumbers(parser, allowSpecialNumbers) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetAllowSpecialNumbers() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetAllowHexNumbers(JSON_Parser parser, JSON_Boolean allowHexNumbers, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetAllowHexNumbers(parser, allowHexNumbers) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetAllowHexNumbers() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetReplaceInvalidEncodingSequences(JSON_Parser parser, JSON_Boolean replaceInvalidEncodingSequences, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetReplaceInvalidEncodingSequences(parser, replaceInvalidEncodingSequences) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetReplaceInvalidEncodingSequences() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetTrackObjectMembers(JSON_Parser parser, JSON_Boolean trackObjectMembers, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetTrackObjectMembers(parser, trackObjectMembers) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetTrackObjectMembers() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetNullHandler(JSON_Parser parser, JSON_Parser_NullHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetNullHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetNullHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetBooleanHandler(JSON_Parser parser, JSON_Parser_BooleanHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetBooleanHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetBooleanHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetStringHandler(JSON_Parser parser, JSON_Parser_StringHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetStringHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetStringHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetNumberHandler(JSON_Parser parser, JSON_Parser_NumberHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetNumberHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetNumberHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetRawNumberHandler(JSON_Parser parser, JSON_Parser_RawNumberHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetRawNumberHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetRawNumberHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetSpecialNumberHandler(JSON_Parser parser, JSON_Parser_SpecialNumberHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetSpecialNumberHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetSpecialNumberHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetStartObjectHandler(JSON_Parser parser, JSON_Parser_StartObjectHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetStartObjectHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetStartObjectHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetEndObjectHandler(JSON_Parser parser, JSON_Parser_EndObjectHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetEndObjectHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetEndObjectHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetObjectMemberHandler(JSON_Parser parser, JSON_Parser_ObjectMemberHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetObjectMemberHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetObjectMemberHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetStartArrayHandler(JSON_Parser parser, JSON_Parser_StartArrayHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetStartArrayHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetStartArrayHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetEndArrayHandler(JSON_Parser parser, JSON_Parser_EndArrayHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetEndArrayHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetEndArrayHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetArrayItemHandler(JSON_Parser parser, JSON_Parser_ArrayItemHandler handler, JSON_Status expectedStatus)
{
    if (JSON_Parser_SetArrayItemHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_SetArrayItemHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParse(JSON_Parser parser, const char* pBytes, size_t length, JSON_Boolean isFinal, JSON_Status expectedStatus)
{
    if (JSON_Parser_Parse(parser, pBytes, length, isFinal) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parser_Parse() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
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

static void* JSON_CALL MallocHandler(void* caller, size_t size)
{
    size_t* pBlock = NULL;
    (void)caller; /* unused */
    if (!s_failMalloc)
    {
        size_t blockSize = sizeof(size_t) + size;
        pBlock = (size_t*)malloc(blockSize);
        if (pBlock)
        {
            s_blocksAllocated++;
            s_bytesAllocated += blockSize;
            *pBlock = blockSize;
            pBlock++; /* return address to memory after block size */
        }
    }
    return pBlock;
}

static void* JSON_CALL ReallocHandler(void* caller, void* ptr, size_t size)
{
    size_t* pBlock = NULL;
    (void)caller; /* unused */
    if (!s_failRealloc)
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
size_t s_outputLength = 0;

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

static void OutputSeparator()
{
    if (s_outputLength && s_outputBuffer[s_outputLength] != ' ')
    {
        OutputCharacter(' ');
    }
}

static const char s_hexDigits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
static void OutputStringBytes(const char* pBytes, size_t length, JSON_StringAttributes attributes)
{
    size_t i;
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
    for (i = 0; i < length; i++)
    {
        unsigned char b = (unsigned char)pBytes[i];
        if (i)
        {
            OutputCharacter(' ');
        }
        OutputCharacter(s_hexDigits[b >> 4]);
        OutputCharacter(s_hexDigits[b & 0xF]);
    }
}

static void OutputLocation(const JSON_Location* pLocation)
{
    OutputFormatted("%d,%d,%d,%d", (int)pLocation->byte, (int)pLocation->line, (int)pLocation->column, (int)pLocation->depth);
}

static int CheckOutput(const char* pExpectedOutput)
{
    if (strcmp(pExpectedOutput, s_outputBuffer))
    {
        printf("FAILURE: parse output does not match expected\n"
               "  EXPECTED %s\n"
               "  ACTUAL   %s\n", pExpectedOutput, s_outputBuffer);
        return 0;
    }
    return 1;
}

static void ResetOutput()
{
    s_outputLength = 0;
    s_outputBuffer[0] = 0;
}

static int TryToMisbehaveInCallback(JSON_Parser parser)
{
    if (!CheckFreeParser(parser, JSON_Failure) ||
        !CheckResetParser(parser, JSON_Failure) ||
        !CheckGetTokenLocation(parser, NULL, JSON_Failure) ||
        !CheckSetInputEncoding(parser, JSON_UTF32LE, JSON_Failure) ||
        !CheckSetOutputEncoding(parser, JSON_UTF32LE, JSON_Failure) ||
        !CheckSetAllowBOM(parser, JSON_True, JSON_Failure) ||
        !CheckSetAllowComments(parser, JSON_True, JSON_Failure) ||
        !CheckSetAllowSpecialNumbers(parser, JSON_True, JSON_Failure) ||
        !CheckSetAllowHexNumbers(parser, JSON_True, JSON_Failure) ||
        !CheckSetReplaceInvalidEncodingSequences(parser, JSON_True, JSON_Failure) ||
        !CheckSetTrackObjectMembers(parser, JSON_True, JSON_Failure) ||
        !CheckParse(parser, " ", 1, JSON_False, JSON_Failure))
    {
        return 1;
    }
    return 0;
}

static JSON_Parser_HandlerResult JSON_CALL NullHandler(JSON_Parser parser)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_Parser_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_Parser_AbortParsing;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_Parser_AbortParsing;
    }
    OutputSeparator();
    OutputFormatted("n:");
    OutputLocation(&location);
    return JSON_Parser_ContinueParsing;
}

static JSON_Parser_HandlerResult JSON_CALL BooleanHandler(JSON_Parser parser, JSON_Boolean value)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_Parser_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_Parser_AbortParsing;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_Parser_AbortParsing;
    }
    OutputSeparator();
    OutputFormatted("%s:", (value == JSON_True) ? "t" : "f");
    OutputLocation(&location);
    return JSON_Parser_ContinueParsing;
}

static JSON_Parser_HandlerResult JSON_CALL StringHandler(JSON_Parser parser, const char* pBytes, size_t length, JSON_StringAttributes attributes)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_Parser_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_Parser_AbortParsing;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_Parser_AbortParsing;
    }
    OutputSeparator();
    OutputFormatted("s(");
    OutputStringBytes(pBytes, length, attributes);
    OutputFormatted("):");
    OutputLocation(&location);
    return JSON_Parser_ContinueParsing;
}

static JSON_Parser_HandlerResult JSON_CALL NumberHandler(JSON_Parser parser, double value)
{
    JSON_Location location;
    (void)value; /* unused */
    if (s_failParseCallback)
    {
        return JSON_Parser_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_Parser_AbortParsing;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_Parser_AbortParsing;
    }
    return JSON_Parser_ContinueParsing;
}

static JSON_Parser_HandlerResult JSON_CALL RawNumberHandler(JSON_Parser parser, const char* pValue, size_t length)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_Parser_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_Parser_AbortParsing;
    }
    if (strlen(pValue) != length)
    {
        return JSON_Parser_AbortParsing;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_Parser_AbortParsing;
    }
    OutputSeparator();
    OutputFormatted("#(%s):", pValue);
    OutputLocation(&location);
    return JSON_Parser_ContinueParsing;
}

static JSON_Parser_HandlerResult JSON_CALL SpecialNumberHandler(JSON_Parser parser, JSON_SpecialNumber value)
{
    JSON_Location location;
    const char* pValue;
    if (s_failParseCallback)
    {
        return JSON_Parser_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_Parser_AbortParsing;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_Parser_AbortParsing;
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
    return JSON_Parser_ContinueParsing;
}

static JSON_Parser_HandlerResult JSON_CALL StartObjectHandler(JSON_Parser parser)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_Parser_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_Parser_AbortParsing;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_Parser_AbortParsing;
    }
    OutputSeparator();
    OutputFormatted("{:");
    OutputLocation(&location);
    return JSON_Parser_ContinueParsing;
}

static JSON_Parser_HandlerResult JSON_CALL EndObjectHandler(JSON_Parser parser)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_Parser_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_Parser_AbortParsing;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_Parser_AbortParsing;
    }
    OutputSeparator();
    OutputFormatted("}:");
    OutputLocation(&location);
    return JSON_Parser_ContinueParsing;
}

static JSON_Parser_HandlerResult JSON_CALL ObjectMemberHandler(JSON_Parser parser, JSON_Boolean isFirstMember, const char* pBytes, size_t length, JSON_StringAttributes attributes)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_Parser_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_Parser_AbortParsing;
    }
    if (attributes == JSON_SimpleString && !strcmp(pBytes, "duplicate"))
    {
        return JSON_Parser_TreatAsDuplicateObjectMember;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_Parser_AbortParsing;
    }
    OutputSeparator();
    OutputFormatted("%s(", isFirstMember ? "M" : "m");
    OutputStringBytes(pBytes, length, attributes);
    OutputFormatted("):");
    OutputLocation(&location);
    return JSON_Parser_ContinueParsing;
}

static JSON_Parser_HandlerResult JSON_CALL StartArrayHandler(JSON_Parser parser)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_Parser_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_Parser_AbortParsing;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_Parser_AbortParsing;
    }
    OutputSeparator();
    OutputFormatted("[:");
    OutputLocation(&location);
    return JSON_Parser_ContinueParsing;
}

static JSON_Parser_HandlerResult JSON_CALL EndArrayHandler(JSON_Parser parser)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_Parser_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_Parser_AbortParsing;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_Parser_AbortParsing;
    }
    OutputSeparator();
    OutputFormatted("]:");
    OutputLocation(&location);
    return JSON_Parser_ContinueParsing;
}

static JSON_Parser_HandlerResult JSON_CALL ArrayItemHandler(JSON_Parser parser, JSON_Boolean isFirstItem)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_Parser_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_Parser_AbortParsing;
    }
    if (JSON_Parser_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_Parser_AbortParsing;
    }
    OutputSeparator();
    OutputFormatted("%s:", isFirstItem ? "I" : "i");
    OutputLocation(&location);
    return JSON_Parser_ContinueParsing;
}

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
    "DuplicateObjectMember"
};

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

    /* Rest of bits are settings. */
    AllowBOM                        = 1 << 10,
    AllowComments                   = 1 << 11,
    AllowSpecialNumbers             = 1 << 12,
    AllowHexNumbers                 = 1 << 13,
    ReplaceInvalidEncodingSequences = 1 << 14,
    TrackObjectMembers              = 1 << 15
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
    printf("Test %s ... ", pTest->pName);

    InitParserSettings(&settings);
    if ((pTest->parserParams & 0xF) != DefaultIn)
    {
        settings.inputEncoding = (JSON_Encoding)(pTest->parserParams & 0xF);
    }
    if ((pTest->parserParams & 0xF0) != DefaultOut)
    {
        settings.outputEncoding = (JSON_Encoding)((pTest->parserParams >> 4) & 0xF);
    }
    if ((pTest->parserParams & 0x300) != DefaultMaxStringLength)
    {
        settings.maxOutputStringLength = (size_t)((pTest->parserParams >> 8) & 0x3) - 1;
    }
    settings.allowBOM = (JSON_Boolean)((pTest->parserParams >> 10) & 0x1);
    settings.allowComments = (JSON_Boolean)((pTest->parserParams >> 11) & 0x1);
    settings.allowSpecialNumbers = (JSON_Boolean)((pTest->parserParams >> 12) & 0x1);
    settings.allowHexNumbers = (JSON_Boolean)((pTest->parserParams >> 13) & 0x1);
    settings.replaceInvalidEncodingSequences = (JSON_Boolean)((pTest->parserParams >> 14) & 0x1);
    settings.trackObjectMembers = (JSON_Boolean)((pTest->parserParams >> 15) & 0x1);

    InitParserState(&state);
    state.startedParsing = JSON_True;
    state.inputEncoding = pTest->inputEncoding;
    ResetOutput();

    if (CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckSetNullHandler(parser, &NullHandler, JSON_Success) &&
        CheckSetBooleanHandler(parser, &BooleanHandler, JSON_Success) &&
        CheckSetStringHandler(parser, &StringHandler, JSON_Success) &&
        CheckSetNumberHandler(parser, &NumberHandler, JSON_Success) &&
        CheckSetRawNumberHandler(parser, &RawNumberHandler, JSON_Success) &&
        CheckSetSpecialNumberHandler(parser, &SpecialNumberHandler, JSON_Success) &&
        CheckSetStartObjectHandler(parser, &StartObjectHandler, JSON_Success) &&
        CheckSetEndObjectHandler(parser, &EndObjectHandler, JSON_Success) &&
        CheckSetObjectMemberHandler(parser, &ObjectMemberHandler, JSON_Success) &&
        CheckSetStartArrayHandler(parser, &StartArrayHandler, JSON_Success) &&
        CheckSetEndArrayHandler(parser, &EndArrayHandler, JSON_Success) &&
        CheckSetArrayItemHandler(parser, &ArrayItemHandler, JSON_Success) &&
        CheckSetInputEncoding(parser, settings.inputEncoding, JSON_Success) &&
        CheckSetOutputEncoding(parser, settings.outputEncoding, JSON_Success) &&
        CheckSetMaxOutputStringLength(parser, settings.maxOutputStringLength, JSON_Success) &&
        CheckSetAllowBOM(parser, settings.allowBOM, JSON_Success) &&
        CheckSetAllowComments(parser, settings.allowComments, JSON_Success) &&
        CheckSetAllowSpecialNumbers(parser, settings.allowSpecialNumbers, JSON_Success) &&
        CheckSetAllowHexNumbers(parser, settings.allowHexNumbers, JSON_Success) &&
        CheckSetReplaceInvalidEncodingSequences(parser, settings.replaceInvalidEncodingSequences, JSON_Success) &&
        CheckSetTrackObjectMembers(parser, settings.trackObjectMembers, JSON_Success))
    {
        if (JSON_Parser_Parse(parser, pTest->pInput, pTest->length, pTest->isFinal) != JSON_Success ||
            pTest->isFinal)
        {
            state.finishedParsing = JSON_True;
        }
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

static void TestParserCreation()
{
    JSON_Parser parser = NULL;
    printf("Test creating parser ... ");
    if (CheckCreateParser(NULL, JSON_Success, &parser) &&
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

static void TestParserCreationWithCustomMemorySuite()
{
    JSON_Parser parser = NULL;
    printf("Test creating parser with custom memory suite ... ");
    if (CheckCreateParserWithCustomMemorySuite(NULL, NULL, NULL, JSON_Failure, &parser) &&
        CheckCreateParserWithCustomMemorySuite(&MallocHandler, NULL, NULL, JSON_Failure, &parser) &&
        CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, NULL, JSON_Failure, &parser) &
        CheckCreateParserWithCustomMemorySuite(&MallocHandler, NULL, &FreeHandler, JSON_Failure, &parser) &&
        CheckCreateParserWithCustomMemorySuite(NULL, &ReallocHandler, NULL, JSON_Failure, &parser) &&
        CheckCreateParserWithCustomMemorySuite(NULL, &ReallocHandler, &FreeHandler, JSON_Failure, &parser) &&
        CheckCreateParserWithCustomMemorySuite(NULL, NULL, &FreeHandler, JSON_Failure, &parser) &&
        CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
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

static void TestParserCreationMallocFailure()
{
    JSON_Parser parser = NULL;
    printf("Test creating parser malloc failure ... ");
    s_failMalloc = 1;
    if (CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, &FreeHandler, JSON_Failure, &parser))
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

static void TestSetParserSettings()
{
    JSON_Parser parser = NULL;
    ParserSettings settings;
    printf("Test setting parser settings ... ");
    InitParserSettings(&settings);
    settings.userData = (void*)1;
    settings.inputEncoding = JSON_UTF16LE;
    settings.outputEncoding = JSON_UTF16LE;
    settings.allowBOM = JSON_True;
    settings.allowComments = JSON_True;
    settings.allowSpecialNumbers = JSON_True;
    settings.allowHexNumbers = JSON_True;
    settings.replaceInvalidEncodingSequences = JSON_True;
    settings.trackObjectMembers = JSON_True;
    if (CheckCreateParser(NULL, JSON_Success, &parser) &&
        CheckSetUserData(parser, settings.userData, JSON_Success) &&
        CheckSetInputEncoding(parser, settings.inputEncoding, JSON_Success) &&
        CheckSetOutputEncoding(parser, settings.outputEncoding, JSON_Success) &&
        CheckSetMaxOutputStringLength(parser, settings.maxOutputStringLength, JSON_Success) &&
        CheckSetAllowBOM(parser, settings.allowBOM, JSON_Success) &&
        CheckSetAllowComments(parser, settings.allowComments, JSON_Success) &&
        CheckSetAllowSpecialNumbers(parser, settings.allowSpecialNumbers, JSON_Success) &&
        CheckSetAllowHexNumbers(parser, settings.allowHexNumbers, JSON_Success) &&
        CheckSetReplaceInvalidEncodingSequences(parser, settings.replaceInvalidEncodingSequences, JSON_Success) &&
        CheckSetTrackObjectMembers(parser, settings.trackObjectMembers, JSON_Success) &&
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

static void TestSetInvalidParserSettings()
{
    JSON_Parser parser = NULL;
    ParserSettings settings;
    printf("Test setting invalid parser settings ... ");
    InitParserSettings(&settings);
    if (CheckCreateParser(NULL, JSON_Success, &parser) &&
        CheckSetInputEncoding(parser, (JSON_Encoding)-1, JSON_Failure) &&
        CheckSetInputEncoding(parser, (JSON_Encoding)(JSON_UTF32BE + 1), JSON_Failure) &&
        CheckSetOutputEncoding(parser, (JSON_Encoding)-1, JSON_Failure) &&
        CheckSetOutputEncoding(parser, JSON_UnknownEncoding, JSON_Failure) &&
        CheckSetOutputEncoding(parser, (JSON_Encoding)(JSON_UTF32BE + 1), JSON_Failure) &&
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

static void TestSetParserHandlers()
{
    JSON_Parser parser = NULL;
    ParserHandlers handlers;
    printf("Test setting parser handlers ... ");
    InitParserHandlers(&handlers);
    handlers.nullHandler = &NullHandler;
    handlers.booleanHandler = &BooleanHandler;
    handlers.stringHandler = &StringHandler;
    handlers.numberHandler = &NumberHandler;
    handlers.rawNumberHandler = &RawNumberHandler;
    handlers.specialNumberHandler = &SpecialNumberHandler;
    handlers.startObjectHandler = &StartObjectHandler;
    handlers.endObjectHandler = &EndObjectHandler;
    handlers.objectMemberHandler = &ObjectMemberHandler;
    handlers.startArrayHandler = &StartArrayHandler;
    handlers.endArrayHandler = &EndArrayHandler;
    handlers.arrayItemHandler = &ArrayItemHandler;
    if (CheckCreateParser(NULL, JSON_Success, &parser) &&
        CheckSetNullHandler(parser, handlers.nullHandler, JSON_Success) &&
        CheckSetBooleanHandler(parser, handlers.booleanHandler, JSON_Success) &&
        CheckSetStringHandler(parser, handlers.stringHandler, JSON_Success) &&
        CheckSetNumberHandler(parser, handlers.numberHandler, JSON_Success) &&
        CheckSetRawNumberHandler(parser, handlers.rawNumberHandler, JSON_Success) &&
        CheckSetSpecialNumberHandler(parser, handlers.specialNumberHandler, JSON_Success) &&
        CheckSetStartObjectHandler(parser, handlers.startObjectHandler, JSON_Success) &&
        CheckSetEndObjectHandler(parser, handlers.endObjectHandler, JSON_Success) &&
        CheckSetObjectMemberHandler(parser, handlers.objectMemberHandler, JSON_Success) &&
        CheckSetStartArrayHandler(parser, handlers.startArrayHandler, JSON_Success) &&
        CheckSetEndArrayHandler(parser, handlers.endArrayHandler, JSON_Success) &&
        CheckSetArrayItemHandler(parser, handlers.arrayItemHandler, JSON_Success) &&
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

static void TestResetParser()
{
    JSON_Parser parser = NULL;
    ParserState state;
    ParserSettings settings;
    ParserHandlers handlers;
    printf("Test resetting parser ... ");
    InitParserState(&state);
    InitParserSettings(&settings);
    InitParserHandlers(&handlers);
    if (CheckCreateParser(NULL, JSON_Success, &parser) &&
        CheckSetUserData(parser, (void*)1, JSON_Success) &&
        CheckSetInputEncoding(parser, JSON_UTF16LE, JSON_Success) &&
        CheckSetOutputEncoding(parser, JSON_UTF16LE, JSON_Success) &&
        CheckSetMaxOutputStringLength(parser, 32, JSON_Success) &&
        CheckSetAllowBOM(parser, JSON_True, JSON_Success) &&
        CheckSetAllowComments(parser, JSON_True, JSON_Success) &&
        CheckSetAllowSpecialNumbers(parser, JSON_True, JSON_Success) &&
        CheckSetAllowHexNumbers(parser, JSON_True, JSON_Success) &&
        CheckSetReplaceInvalidEncodingSequences(parser, JSON_True, JSON_Success) &&
        CheckSetTrackObjectMembers(parser, JSON_True, JSON_Success) &&
        CheckSetNullHandler(parser, &NullHandler, JSON_Success) &&
        CheckSetBooleanHandler(parser, &BooleanHandler, JSON_Success) &&
        CheckSetStringHandler(parser, &StringHandler, JSON_Success) &&
        CheckSetNumberHandler(parser, &NumberHandler, JSON_Success) &&
        CheckSetRawNumberHandler(parser, &RawNumberHandler, JSON_Success) &&
        CheckSetSpecialNumberHandler(parser, &SpecialNumberHandler, JSON_Success) &&
        CheckSetStartObjectHandler(parser, &StartObjectHandler, JSON_Success) &&
        CheckSetEndObjectHandler(parser, &EndObjectHandler, JSON_Success) &&
        CheckSetObjectMemberHandler(parser, &ObjectMemberHandler, JSON_Success) &&
        CheckSetStartArrayHandler(parser, &StartArrayHandler, JSON_Success) &&
        CheckSetEndArrayHandler(parser, &EndArrayHandler, JSON_Success) &&
        CheckSetArrayItemHandler(parser, &ArrayItemHandler, JSON_Success) &&
        CheckParse(parser, "7\x00", 2, JSON_True, JSON_Success) &&
        CheckResetParser(parser, JSON_Success) &&
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

static void TestMisbehaveInCallbacks()
{
    JSON_Parser parser = NULL;
    printf("Test misbehaving in callbacks ... ");
    s_misbehaveInCallback = 1;
    if (CheckCreateParser(NULL, JSON_Success, &parser) &&
        CheckSetNullHandler(parser, &NullHandler, JSON_Success) &&
        CheckParse(parser, "null", 4, JSON_True, JSON_Success) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetBooleanHandler(parser, &BooleanHandler, JSON_Success) &&
        CheckParse(parser, "true", 4, JSON_True, JSON_Success) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetStringHandler(parser, &StringHandler, JSON_Success) &&
        CheckParse(parser, "\"\"", 2, JSON_True, JSON_Success) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetNumberHandler(parser, &NumberHandler, JSON_Success) &&
        CheckParse(parser, "7", 1, JSON_True, JSON_Success) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetRawNumberHandler(parser, &RawNumberHandler, JSON_Success) &&
        CheckParse(parser, "7", 1, JSON_True, JSON_Success) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetAllowSpecialNumbers(parser, JSON_True, JSON_Success) &&
        CheckSetSpecialNumberHandler(parser, &SpecialNumberHandler, JSON_Success) &&
        CheckParse(parser, "NaN", 3, JSON_True, JSON_Success) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetStartObjectHandler(parser, &StartObjectHandler, JSON_Success) &&
        CheckParse(parser, "{\"x\":0}", 7, JSON_True, JSON_Success) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetEndObjectHandler(parser, &EndObjectHandler, JSON_Success) &&
        CheckParse(parser, "{\"x\":0}", 7, JSON_True, JSON_Success) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetObjectMemberHandler(parser, &ObjectMemberHandler, JSON_Success) &&
        CheckParse(parser, "{\"x\":0}", 7, JSON_True, JSON_Success) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetStartArrayHandler(parser, &StartArrayHandler, JSON_Success) &&
        CheckParse(parser, "[0]", 3, JSON_True, JSON_Success) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetEndArrayHandler(parser, &EndArrayHandler, JSON_Success) &&
        CheckParse(parser, "[0]", 3, JSON_True, JSON_Success) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetArrayItemHandler(parser, &ArrayItemHandler, JSON_Success) &&
        CheckParse(parser, "[0]", 3, JSON_True, JSON_Success))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    s_misbehaveInCallback = 0;
    JSON_Parser_Free(parser);
}

static void TestAbortInCallbacks()
{
    JSON_Parser parser = NULL;
    ParserState state;
    printf("Test aborting in callbacks ... ");
    InitParserState(&state);
    state.error = JSON_Error_AbortedByHandler;
    state.errorLocation.byte = 1;
    state.errorLocation.line = 0;
    state.errorLocation.column = 1;
    state.errorLocation.depth = 0;
    state.startedParsing = JSON_True;
    state.finishedParsing = JSON_True;
    state.inputEncoding = JSON_UTF8;
    s_failParseCallback = 1;
    if (CheckCreateParser(NULL, JSON_Success, &parser) &&
        CheckSetNullHandler(parser, &NullHandler, JSON_Success) &&
        CheckParse(parser, " null", 6, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetBooleanHandler(parser, &BooleanHandler, JSON_Success) &&
        CheckParse(parser, " true", 6, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetStringHandler(parser, &StringHandler, JSON_Success) &&
        CheckParse(parser, " \"\"", 3, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetNumberHandler(parser, &NumberHandler, JSON_Success) &&
        CheckParse(parser, " 7", 2, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetRawNumberHandler(parser, &RawNumberHandler, JSON_Success) &&
        CheckParse(parser, " 7", 2, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetAllowSpecialNumbers(parser, JSON_True, JSON_Success) &&
        CheckSetSpecialNumberHandler(parser, &SpecialNumberHandler, JSON_Success) &&
        CheckParse(parser, " NaN", 4, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetStartObjectHandler(parser, &StartObjectHandler, JSON_Success) &&
        CheckParse(parser, " {}", 3, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetEndObjectHandler(parser, &EndObjectHandler, JSON_Success) &&
        CheckParse(parser, "{}", 2, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetStartArrayHandler(parser, &StartArrayHandler, JSON_Success) &&
        CheckParse(parser, " []", 3, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetEndArrayHandler(parser, &EndArrayHandler, JSON_Success) &&
        CheckParse(parser, "[]", 2, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        !!(state.errorLocation.depth = 1) && /* hacky! */

        CheckResetParser(parser, JSON_Success) &&
        CheckSetObjectMemberHandler(parser, &ObjectMemberHandler, JSON_Success) &&
        CheckParse(parser, "{\"x\":0}", 7, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetArrayItemHandler(parser, &ArrayItemHandler, JSON_Success) &&
        CheckParse(parser, "[0]", 3, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    s_failParseCallback = 0;
    JSON_Parser_Free(parser);
}

static void TestStringMallocFailure()
{
    int succeeded = 0;
    JSON_Parser parser = NULL;
    ParserState state;
    printf("Test string malloc failure ... ");
    InitParserState(&state);
    state.error = JSON_Error_OutOfMemory;
    state.startedParsing = JSON_True;
    state.finishedParsing = JSON_True;
    state.inputEncoding = JSON_UTF8;
    if (CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParse(parser, "\"", 1, JSON_False, JSON_Success))
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

static void TestStringReallocFailure()
{
    int succeeded = 0;
    JSON_Parser parser = NULL;
    ParserState state;
    printf("Test string realloc failure ... ");
    InitParserState(&state);
    state.error = JSON_Error_OutOfMemory;
    state.startedParsing = JSON_True;
    state.finishedParsing = JSON_True;
    state.inputEncoding = JSON_UTF8;
    if (CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParse(parser, "\"", 1, JSON_False, JSON_Success))
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

static void TestStackMallocFailure()
{
    int succeeded = 0;
    JSON_Parser parser = NULL;
    ParserState state;
    printf("Test stack malloc failure ... ");
    InitParserState(&state);
    state.error = JSON_Error_OutOfMemory;
    state.startedParsing = JSON_True;
    state.finishedParsing = JSON_True;
    state.inputEncoding = JSON_UTF8;
    if (CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, &FreeHandler, JSON_Success, &parser))
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

static void TestStackReallocFailure()
{
    int succeeded = 0;
    JSON_Parser parser = NULL;
    ParserState state;
    printf("Test stack realloc failure ... ");
    InitParserState(&state);
    state.error = JSON_Error_OutOfMemory;
    state.startedParsing = JSON_True;
    state.finishedParsing = JSON_True;
    state.inputEncoding = JSON_UTF8;
    if (CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, &FreeHandler, JSON_Success, &parser))
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

static void TestDuplicateMemberTrackingMallocFailure()
{
    int succeeded = 0;
    JSON_Parser parser = NULL;
    ParserState state;
    printf("Test duplicate member tracking malloc failure ... ");
    InitParserState(&state);
    state.error = JSON_Error_OutOfMemory;
    state.startedParsing = JSON_True;
    state.finishedParsing = JSON_True;
    state.inputEncoding = JSON_UTF8;
    if (CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckSetTrackObjectMembers(parser, JSON_True, JSON_Success))
    {
        s_failMalloc = 1;
        if (CheckParse(parser, "{\"a\":0}", 7, JSON_True, JSON_Failure) &&
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

static void TestMissingParser()
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
        CheckFreeParser(NULL, JSON_Failure) &&
        CheckResetParser(NULL, JSON_Failure) &&
        CheckSetUserData(NULL, (void*)1, JSON_Failure) &&
        CheckGetErrorLocation(NULL, &errorLocation, JSON_Failure) &&
        CheckSetInputEncoding(NULL, JSON_UTF16LE, JSON_Failure) &&
        CheckSetOutputEncoding(NULL, JSON_UTF16LE, JSON_Failure) &&
        CheckSetMaxOutputStringLength(NULL, 128, JSON_Failure) &&
        CheckSetNullHandler(NULL, &NullHandler, JSON_Failure) &&
        CheckSetBooleanHandler(NULL, &BooleanHandler, JSON_Failure) &&
        CheckSetStringHandler(NULL, &StringHandler, JSON_Failure) &&
        CheckSetNumberHandler(NULL, &NumberHandler, JSON_Failure) &&
        CheckSetRawNumberHandler(NULL, &RawNumberHandler, JSON_Failure) &&
        CheckSetSpecialNumberHandler(NULL, &SpecialNumberHandler, JSON_Failure) &&
        CheckSetStartObjectHandler(NULL, &StartObjectHandler, JSON_Failure) &&
        CheckSetEndObjectHandler(NULL, &EndObjectHandler, JSON_Failure) &&
        CheckSetObjectMemberHandler(NULL, &ObjectMemberHandler, JSON_Failure) &&
        CheckSetStartArrayHandler(NULL, &StartArrayHandler, JSON_Failure) &&
        CheckSetEndArrayHandler(NULL, &EndArrayHandler, JSON_Failure) &&
        CheckSetArrayItemHandler(NULL, &ArrayItemHandler, JSON_Failure) &&
        CheckParse(NULL, "7", 1, JSON_True, JSON_Failure))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
}

static void TestGetErrorLocationNullLocation()
{
    JSON_Parser parser = NULL;
    printf("Test get error location with NULL location ... ");
    if (CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParse(parser, "!", 1, JSON_True, JSON_Failure) &&
        CheckGetErrorLocation(parser, NULL, JSON_Failure))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_Parser_Free(parser);
}

static void TestGetErrorLocationNoError()
{
    JSON_Parser parser = NULL;
    JSON_Location location = { 100, 200, 300, 400 };
    printf("Test get error location when no error occurred ... ");
    if (CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParse(parser, "7", 1, JSON_True, JSON_Success) &&
        CheckGetErrorLocation(parser, &location, JSON_Failure))
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

static void TestGetTokenLocationOutsideHandler()
{
    JSON_Parser parser = NULL;
    JSON_Location location = { 100, 200, 300, 400 };
    printf("Test get token location when not in a parse handler  ... ");
    if (CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParse(parser, "7", 1, JSON_True, JSON_Success) &&
        CheckGetTokenLocation(parser, &location, JSON_Failure))
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

static void TestErrorStrings()
{
    printf("Test error strings ... ");
    if (CheckErrorString(JSON_Error_None, "no error") &&
        CheckErrorString(JSON_Error_OutOfMemory, "the parser could not allocate enough memory") &&
        CheckErrorString(JSON_Error_AbortedByHandler, "parsing was aborted by a handler") &&
        CheckErrorString(JSON_Error_BOMNotAllowed, "the input begins with a byte-order mark (BOM), which is not allowed by RFC 4627") &&
        CheckErrorString(JSON_Error_InvalidEncodingSequence, "the input contains a byte or sequence of bytes that is not valid for the input encoding") &&
        CheckErrorString(JSON_Error_UnknownToken, "the input contains an unknown token") &&
        CheckErrorString(JSON_Error_UnexpectedToken, "the input contains an unexpected token") &&
        CheckErrorString(JSON_Error_IncompleteToken,  "the input ends in the middle of a token") &&
        CheckErrorString(JSON_Error_ExpectedMoreTokens, "the input ends when more tokens are expected") &&
        CheckErrorString(JSON_Error_UnescapedControlCharacter, "the input contains a string containing an unescaped control character (U+0000 - U+001F)") &&
        CheckErrorString(JSON_Error_InvalidEscapeSequence, "the input contains a string containing an invalid escape sequence") &&
        CheckErrorString(JSON_Error_UnpairedSurrogateEscapeSequence, "the input contains a string containing an unmatched UTF-16 surrogate codepoint") &&
        CheckErrorString(JSON_Error_TooLongString, "the input contains a string that is too long") &&
        CheckErrorString(JSON_Error_InvalidNumber, "the input contains an invalid number") &&
        CheckErrorString(JSON_Error_TooLongNumber, "the input contains a number that is too long") &&
        CheckErrorString(JSON_Error_DuplicateObjectMember, "the input contains an object with duplicate members") &&
        CheckErrorString((JSON_Error)-1, "") &&
        CheckErrorString((JSON_Error)1000, ""))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
}

typedef struct tag_IEEE754Test
{
    const char* pInput;
    size_t      length;
    double      expectedValue;
} IEEE754Test;

#define IEEE754_TEST(input, value) { input, sizeof(input) - 1, value },

static const IEEE754Test s_IEEE754Tests[] =
{
    /* decimal */

    IEEE754_TEST("0", 0.0)
    IEEE754_TEST("0.0", 0.0)
    IEEE754_TEST("-0", -0.0)
    IEEE754_TEST("1", 1.0)
    IEEE754_TEST("1.0", 1.0)
    IEEE754_TEST("-1", -1.0)
    IEEE754_TEST("-1.0", -1.0)
    IEEE754_TEST("0.5", 0.5)
    IEEE754_TEST("-0.5", -0.5)
    IEEE754_TEST("12345", 12345.0)
    IEEE754_TEST("-12345", -12345.0)
    IEEE754_TEST("12345e2", 12345.0e2)
    IEEE754_TEST("12345e+2", 12345.0e2)
    IEEE754_TEST("0.5e-2", 0.005)

    /* hex */

    IEEE754_TEST("0x0", 0.0)
    IEEE754_TEST("0x1", 1.0)
    IEEE754_TEST("0x00000000000000000000000000000000000001", 1.0)
    IEEE754_TEST("0x00000000000000000000000000000000000000", 0.0)
    IEEE754_TEST("0xdeadBEEF", 3735928559.0)
    IEEE754_TEST("0xFFFFFFFF", 4294967295.0)

    IEEE754_TEST("0x20000000000000", 9007199254740992.0)    /* 10...00 | 0 */
    IEEE754_TEST("0x20000000000001", 9007199254740992.0)    /* 10...00 | 1 */
    IEEE754_TEST("0x20000000000002", 9007199254740994.0)    /* 10...01 | 0 */
    IEEE754_TEST("0x20000000000003", 9007199254740996.0)    /* 10...01 | 1 */

    IEEE754_TEST("0x40000000000000", 18014398509481984.0)   /* 10...00 | 00 */
    IEEE754_TEST("0x40000000000001", 18014398509481984.0)   /* 10...00 | 01 */
    IEEE754_TEST("0x40000000000002", 18014398509481984.0)   /* 10...00 | 10 */
    IEEE754_TEST("0x40000000000003", 18014398509481988.0)   /* 10...00 | 11 */
    IEEE754_TEST("0x40000000000004", 18014398509481988.0)   /* 10...01 | 00 */
    IEEE754_TEST("0x40000000000005", 18014398509481988.0)   /* 10...01 | 01 */
    IEEE754_TEST("0x40000000000006", 18014398509481992.0)   /* 10...01 | 10 */
    IEEE754_TEST("0x40000000000007", 18014398509481992.0)   /* 10...01 | 11 */

    IEEE754_TEST("0x800000000000000", 576460752303423490.0) /* 10...00 | 0000000 */
    IEEE754_TEST("0x80000000000000F", 576460752303423490.0) /* 10...00 | 0001111 */
    IEEE754_TEST("0x800000000000040", 576460752303423490.0) /* 10...00 | 1000000 */
    IEEE754_TEST("0x800000000000041", 576460752303423620.0) /* 10...00 | 1000001 */

    IEEE754_TEST("0x800000000000080", 576460752303423620.0) /* 10...01 | 0000000 */
    IEEE754_TEST("0x80000000000008F", 576460752303423620.0) /* 10...01 | 0001111 */
    IEEE754_TEST("0x8000000000000C0", 576460752303423740.0) /* 10...01 | 1000000 */
    IEEE754_TEST("0x8000000000000C1", 576460752303423740.0) /* 10...01 | 1000001 */

    IEEE754_TEST("0x1fffffffffffff", 9007199254740991.0)    /* 11...11 |      */
    IEEE754_TEST("0x3fffffffffffff", 18014398509481984.0)   /* 11...11 | 1    */
    IEEE754_TEST("0x7fffffffffffff", 36028797018963968.0)   /* 11...11 | 11   */
    IEEE754_TEST("0xffffffffffffff", 72057594037927936.0)   /* 11...11 | 111  */
    IEEE754_TEST("0x1ffffffffffffff", 144115188075855870.0) /* 11...11 | 1111 */
};

static JSON_Parser_HandlerResult JSON_CALL CheckIEEE754InterpretationNumberHandler(JSON_Parser parser, double value)
{
    const IEEE754Test* pTest = (const IEEE754Test*)JSON_Parser_GetUserData(parser);
    if (value != pTest->expectedValue)
    {
        printf("FAILURE: expected value to be %f instead of %f\n", pTest->expectedValue, value);
        s_failureCount++;
        return JSON_Parser_AbortParsing;
    }
    return JSON_Parser_ContinueParsing;
}

static void RunIEEE754Test(const IEEE754Test* pTest)
{
    JSON_Parser parser = NULL;
    printf("Test IEEE 754 interpretation of %s ... ", pTest->pInput);
    if (CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckSetNumberHandler(parser, &CheckIEEE754InterpretationNumberHandler, JSON_Success) &&
        CheckSetAllowHexNumbers(parser, JSON_True, JSON_Success) &&
        CheckSetUserData(parser, (void*)pTest, JSON_Success))
    {
        if (JSON_Parser_Parse(parser, pTest->pInput, pTest->length, JSON_True) == JSON_Success)
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

static void TestIEEE754NumberInterpretation()
{
    size_t i;
    for  (i = 0; i < sizeof(s_IEEE754Tests)/sizeof(s_IEEE754Tests[0]); i++)
    {
        RunIEEE754Test(&s_IEEE754Tests[i]);
    }
}

#define PARSE_TEST(name, params, input, final, enc, output) { name, params, input, sizeof(input) - 1, final, JSON_##enc, output },

#define FINAL   JSON_True
#define PARTIAL JSON_False

static const ParseTest s_parseTests[] =
{

/* input encoding detection */

PARSE_TEST("infer input encoding from 0 bytes", Standard, "", FINAL, UnknownEncoding, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("infer input encoding from 1 byte (1)", Standard, "7", FINAL, UTF8, "#(7):0,0,0,0")
PARSE_TEST("infer input encoding from 1 byte (2)", Standard, " ", FINAL, UTF8, "!(ExpectedMoreTokens):1,0,1,0")
PARSE_TEST("infer input encoding from 1 byte (3)", Standard, "\xFF", FINAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("infer input encoding from 2 bytes (1)", Standard, "{}", FINAL, UTF8, "{:0,0,0,0 }:1,0,1,0")
PARSE_TEST("infer input encoding from 2 bytes (2)", Standard, "7\x00", FINAL, UTF16LE, "#(7):0,0,0,0")
PARSE_TEST("infer input encoding from 2 bytes (3)", Standard, "\x00" "7", FINAL, UTF16BE, "#(7):0,0,0,0")
PARSE_TEST("infer input encoding from 2 bytes (4)", AllowBOM, "\xFF\xFE", FINAL, UTF16LE, "!(ExpectedMoreTokens):2,0,1,0")
PARSE_TEST("infer input encoding from 2 bytes (5)", AllowBOM, "\xFE\xFF", FINAL, UTF16BE, "!(ExpectedMoreTokens):2,0,1,0")
PARSE_TEST("infer input encoding from 2 bytes (6)", Standard, "\xFF\xFF", FINAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("infer input encoding from 3 bytes (1)", Standard, "{ }", FINAL, UTF8, "{:0,0,0,0 }:2,0,2,0")
PARSE_TEST("infer input encoding from 3 bytes (2)", AllowBOM, "\xEF\xBB\xBF", FINAL, UTF8, "!(ExpectedMoreTokens):3,0,1,0")
PARSE_TEST("infer input encoding from 3 bytes (3)", AllowBOM, "\xFF\xFF\xFF", FINAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("infer input encoding from 3 bytes (4)", Standard, "\xFF\xFF\xFF", FINAL, UTF8, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("infer input encoding from 4 bytes (1)", Standard, "1234", FINAL, UTF8, "#(1234):0,0,0,0")
PARSE_TEST("infer input encoding from 4 bytes (2)", Standard, "   7", FINAL, UTF8, "#(7):3,0,3,0")
PARSE_TEST("infer input encoding from 4 bytes (3)", Standard, "\x00 \x00" "7", FINAL, UTF16BE, "#(7):2,0,1,0")
PARSE_TEST("infer input encoding from 4 bytes (4)", Standard, " \x00" "7\x00", FINAL, UTF16LE, "#(7):2,0,1,0")
PARSE_TEST("infer input encoding from 4 bytes (5)", Standard, "\x00\x00\x00" "7", FINAL, UTF32BE, "#(7):0,0,0,0")
PARSE_TEST("infer input encoding from 4 bytes (6)", Standard, "7\x00\x00\x00", FINAL, UTF32LE, "#(7):0,0,0,0")
PARSE_TEST("no input encoding starts <00 00 00 00>", Standard, "\x00\x00\x00\x00", FINAL, UnknownEncoding, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("no input encoding starts <nz 00 00 nz>", Standard, " \x00\x00 ", FINAL, UnknownEncoding, "!(InvalidEncodingSequence):0,0,0,0")
PARSE_TEST("UTF-8 BOM not allowed", Standard, "\xEF\xBB\xBF" "7", PARTIAL, UTF8, "!(BOMNotAllowed):0,0,0,0")
PARSE_TEST("UTF-16LE BOM not allowed", Standard, "\xFF\xFE" "7\x00", PARTIAL, UTF16LE, "!(BOMNotAllowed):0,0,0,0")
PARSE_TEST("UTF-16BE BOM not allowed", Standard, "\xFE\xFF\x00" "7", PARTIAL, UTF16BE, "!(BOMNotAllowed):0,0,0,0")
PARSE_TEST("UTF-32LE BOM not allowed", Standard, "\xFF\xFE\x00\x00" "7\x00\x00\x00", PARTIAL, UTF32LE, "!(BOMNotAllowed):0,0,0,0")
PARSE_TEST("UTF-32BE BOM not allowed", Standard, "\x00\x00\xFE\xFF\x00\x00\x00" "7", PARTIAL, UTF32BE, "!(BOMNotAllowed):0,0,0,0")
PARSE_TEST("UTF-8 BOM allowed", AllowBOM, "\xEF\xBB\xBF" "7", FINAL, UTF8, "#(7):3,0,1,0")
PARSE_TEST("UTF-16LE BOM allowed", AllowBOM, "\xFF\xFE" "7\x00", FINAL, UTF16LE, "#(7):2,0,1,0")
PARSE_TEST("UTF-16BE BOM allowed", AllowBOM, "\xFE\xFF\x00" "7", FINAL, UTF16BE, "#(7):2,0,1,0")
PARSE_TEST("UTF-32LE BOM allowed", AllowBOM, "\xFF\xFE\x00\x00" "7\x00\x00\x00", FINAL, UTF32LE, "#(7):4,0,1,0")
PARSE_TEST("UTF-32BE BOM allowed", AllowBOM, "\x00\x00\xFE\xFF\x00\x00\x00" "7", FINAL, UTF32BE, "#(7):4,0,1,0")
PARSE_TEST("UTF-8 BOM allowed but no content", AllowBOM, "\xEF\xBB\xBF", FINAL, UTF8, "!(ExpectedMoreTokens):3,0,1,0")
PARSE_TEST("UTF-16LE BOM allowed but no content", AllowBOM, "\xFF\xFE", FINAL, UTF16LE, "!(ExpectedMoreTokens):2,0,1,0")
PARSE_TEST("UTF-16BE BOM allowed but no content", AllowBOM, "\xFE\xFF", FINAL, UTF16BE, "!(ExpectedMoreTokens):2,0,1,0")
PARSE_TEST("UTF-32LE BOM allowed but no content", AllowBOM, "\xFF\xFE\x00\x00", FINAL, UTF32LE, "!(ExpectedMoreTokens):4,0,1,0")
PARSE_TEST("UTF-32BE BOM allowed but no content", AllowBOM, "\x00\x00\xFE\xFF", FINAL, UTF32BE, "!(ExpectedMoreTokens):4,0,1,0")

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

PARSE_TEST("replace UTF-8 truncated 2-byte sequence (1)", ReplaceInvalidEncodingSequences, "\"abc\xC2\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 truncated 2-byte sequence (2)", ReplaceInvalidEncodingSequences, "\"abc\xC2\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 truncated 3-byte sequence (1)", ReplaceInvalidEncodingSequences, "\"abc\xE0\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 truncated 3-byte sequence (2)", ReplaceInvalidEncodingSequences, "\"abc\xE0\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 truncated 3-byte sequence (3)", ReplaceInvalidEncodingSequences, "\"abc\xE0\xBF\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 truncated 3-byte sequence (4)", ReplaceInvalidEncodingSequences, "\"abc\xE0\xBF\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0 !(UnknownToken):7,0,6,0")
PARSE_TEST("replace UTF-8 truncated 4-byte sequence (1)", ReplaceInvalidEncodingSequences, "\"abc\xF0\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 truncated 4-byte sequence (2)", ReplaceInvalidEncodingSequences, "\"abc\xF0\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 truncated 4-byte sequence (3)", ReplaceInvalidEncodingSequences, "\"abc\xF0\xBF\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 truncated 4-byte sequence (4)", ReplaceInvalidEncodingSequences, "\"abc\xF0\xBF\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0 !(UnknownToken):7,0,6,0")
PARSE_TEST("replace UTF-8 truncated 4-byte sequence (5)", ReplaceInvalidEncodingSequences, "\"abc\xF0\xBF\xBF\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 truncated 4-byte sequence (6)", ReplaceInvalidEncodingSequences, "\"abc\xF0\xBF\xBF\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0 !(UnknownToken):8,0,6,0")
PARSE_TEST("replace UTF-8 overlong 2-byte sequence (1)", ReplaceInvalidEncodingSequences, "\"abc\xC0\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 overlong 2-byte sequence (2)", ReplaceInvalidEncodingSequences, "\"abc\xC0\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 overlong 2-byte sequence (3)", ReplaceInvalidEncodingSequences, "\"abc\xC1\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 overlong 2-byte sequence (4)", ReplaceInvalidEncodingSequences, "\"abc\xC1\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 overlong 3-byte sequence (1)", ReplaceInvalidEncodingSequences, "\"abc\xE0\x80\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 overlong 3-byte sequence (2)", ReplaceInvalidEncodingSequences, "\"abc\xE0\x80\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 overlong 3-byte sequence (3)", ReplaceInvalidEncodingSequences, "\"abc\xE0\x9F\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 overlong 3-byte sequence (4)", ReplaceInvalidEncodingSequences, "\"abc\xE0\x9F\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 encoded surrogate (1)", ReplaceInvalidEncodingSequences, "\"abc\xED\xA0\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 encoded surrogate (2)", ReplaceInvalidEncodingSequences, "\"abc\xED\xA0\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 encoded surrogate (3)", ReplaceInvalidEncodingSequences, "\"abc\xED\xBF\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 encoded surrogate (4)", ReplaceInvalidEncodingSequences, "\"abc\xED\xBF\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 overlong 4-byte sequence (1)", ReplaceInvalidEncodingSequences, "\"abc\xF0\x80\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 overlong 4-byte sequence (2)", ReplaceInvalidEncodingSequences, "\"abc\xF0\x80\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 overlong 4-byte sequence (3)", ReplaceInvalidEncodingSequences, "\"abc\xF0\x8F\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 overlong 4-byte sequence (4)", ReplaceInvalidEncodingSequences, "\"abc\xF0\x8F\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 encoded out-of-range codepoint (1)", ReplaceInvalidEncodingSequences, "\"abc\xF4\x90\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 encoded out-of-range codepoint (2)", ReplaceInvalidEncodingSequences, "\"abc\xF4\x90\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 invalid leading byte (1)", ReplaceInvalidEncodingSequences, "\"abc\x80\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid leading byte (2)", ReplaceInvalidEncodingSequences, "\"abc\x80\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 invalid leading byte (3)", ReplaceInvalidEncodingSequences, "\"abc\xBF\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid leading byte (4)", ReplaceInvalidEncodingSequences, "\"abc\xBF\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 invalid leading byte (5)", ReplaceInvalidEncodingSequences, "\"abc\xF5\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid leading byte (6)", ReplaceInvalidEncodingSequences, "\"abc\xF5\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 invalid leading byte (7)", ReplaceInvalidEncodingSequences, "\"abc\xFF\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid leading byte (8)", ReplaceInvalidEncodingSequences, "\"abc\xFF\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD):0,0,0,0 !(UnknownToken):6,0,6,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (1)", ReplaceInvalidEncodingSequences, "\"abc\xC2\x7F\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD 7F):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (2)", ReplaceInvalidEncodingSequences, "\"abc\xC2\x7F\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD 7F):0,0,0,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (3)", ReplaceInvalidEncodingSequences, "\"abc\xC2\xC0\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (4)", ReplaceInvalidEncodingSequences, "\"abc\xC2\xC0\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (5)", ReplaceInvalidEncodingSequences, "\"abc\xE1\x7F\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD 7F):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (6)", ReplaceInvalidEncodingSequences, "\"abc\xE1\x7F\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD 7F):0,0,0,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (7)", ReplaceInvalidEncodingSequences, "\"abc\xE1\xC0\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (8)", ReplaceInvalidEncodingSequences, "\"abc\xE1\xC0\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (9)", ReplaceInvalidEncodingSequences, "\"abc\xE1\xBF\x7F\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD 7F):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (10)", ReplaceInvalidEncodingSequences, "\"abc\xE1\xBF\x7F\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD 7F):0,0,0,0 !(UnknownToken):8,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (11)", ReplaceInvalidEncodingSequences, "\"abc\xE1\xBF\xC0\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (12)", ReplaceInvalidEncodingSequences, "\"abc\xE1\xBF\xC0\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0 !(UnknownToken):8,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (13)", ReplaceInvalidEncodingSequences, "\"abc\xF1\x7F\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD 7F):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (14)", ReplaceInvalidEncodingSequences, "\"abc\xF1\x7F\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD 7F):0,0,0,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (15)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xC0\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (16)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xC0\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0 !(UnknownToken):7,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (17)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\x7F\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD 7F):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (18)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\x7F\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD 7F):0,0,0,0 !(UnknownToken):8,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (19)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\xC0\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (20)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\xC0\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0 !(UnknownToken):8,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (21)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\xBF\x7F\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD 7F):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (22)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\xBF\x7F\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD 7F):0,0,0,0 !(UnknownToken):9,0,7,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (23)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\xBF\xC0\"", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-8 invalid continuation byte (24)", ReplaceInvalidEncodingSequences, "\"abc\xF1\xBF\xBF\xC0\"!", FINAL, UTF8, "s(ar 61 62 63 EF BF BD EF BF BD):0,0,0,0 !(UnknownToken):9,0,7,0")
PARSE_TEST("Unicode 5.2.0 replacement example (1)", ReplaceInvalidEncodingSequences, "   \"\x61\xF1\x80\x80\xE1\x80\xC2\x62\x80\x63\x80\xBF\x64\"", FINAL, UTF8, "s(ar 61 EF BF BD EF BF BD EF BF BD 62 EF BF BD 63 EF BF BD EF BF BD 64):3,0,3,0")
PARSE_TEST("Unicode 5.2.0 replacement example (2)", ReplaceInvalidEncodingSequences, "   \"\x61\xF1\x80\x80\xE1\x80\xC2\x62\x80\x63\x80\xBF\x64\"!", FINAL, UTF8, "s(ar 61 EF BF BD EF BF BD EF BF BD 62 EF BF BD 63 EF BF BD EF BF BD 64):3,0,3,0 !(UnknownToken):18,0,15,0")
PARSE_TEST("replace UTF-16LE standalone trailing surrogate (1)", ReplaceInvalidEncodingSequences, "\"\x00" "_\x00" "\x00\xDC" "\"\x00", FINAL, UTF16LE, "s(ar 5F EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-16LE standalone trailing surrogate (2)", ReplaceInvalidEncodingSequences, "\"\x00" "_\x00" "\x00\xDC" "\"\x00" "!\x00", FINAL, UTF16LE, "s(ar 5F EF BF BD):0,0,0,0 !(UnknownToken):8,0,4,0")
PARSE_TEST("replace UTF-16LE standalone trailing surrogate (3)", ReplaceInvalidEncodingSequences, "\"\x00" "_\x00" "\xFF\xDF" "\"\x00", FINAL, UTF16LE, "s(ar 5F EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-16LE standalone trailing surrogate (4)", ReplaceInvalidEncodingSequences, "\"\x00" "_\x00" "\xFF\xDF" "\"\x00" "!\x00", FINAL, UTF16LE, "s(ar 5F EF BF BD):0,0,0,0 !(UnknownToken):8,0,4,0")
PARSE_TEST("replace UTF-16LE standalone leading surrogate (1)", ReplaceInvalidEncodingSequences,  "\"\x00" "_\x00" "\x00\xD8" "_\x00" "\"\x00", FINAL, UTF16LE, "s(ar 5F EF BF BD 5F):0,0,0,0")
PARSE_TEST("replace UTF-16LE standalone leading surrogate (2)", ReplaceInvalidEncodingSequences,  "\"\x00" "_\x00" "\x00\xD8" "_\x00" "\"\x00" "!\x00", FINAL, UTF16LE, "s(ar 5F EF BF BD 5F):0,0,0,0 !(UnknownToken):10,0,5,0")
PARSE_TEST("replace UTF-16LE standalone leading surrogate (3)", ReplaceInvalidEncodingSequences,  "\"\x00" "_\x00" "\xFF\xDB" "_\x00" "\"\x00", FINAL, UTF16LE, "s(ar 5F EF BF BD 5F):0,0,0,0")
PARSE_TEST("replace UTF-16LE standalone leading surrogate (4)", ReplaceInvalidEncodingSequences,  "\"\x00" "_\x00" "\xFF\xDB" "_\x00" "\"\x00" "!\x00", FINAL, UTF16LE, "s(ar 5F EF BF BD 5F):0,0,0,0 !(UnknownToken):10,0,5,0")
PARSE_TEST("replace UTF-16BE standalone trailing surrogate (1)", ReplaceInvalidEncodingSequences, "\x00\"" "\x00_" "\xDC\x00" "\x00\"", FINAL, UTF16BE, "s(ar 5F EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-16BE standalone trailing surrogate (2)", ReplaceInvalidEncodingSequences, "\x00\"" "\x00_" "\xDC\x00" "\x00\"" "\x00!", FINAL, UTF16BE, "s(ar 5F EF BF BD):0,0,0,0 !(UnknownToken):8,0,4,0")
PARSE_TEST("replace UTF-16BE standalone trailing surrogate (3)", ReplaceInvalidEncodingSequences, "\x00\"" "\x00_" "\xDF\xFF" "\x00\"", FINAL, UTF16BE, "s(ar 5F EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-16BE standalone trailing surrogate (4)", ReplaceInvalidEncodingSequences, "\x00\"" "\x00_" "\xDF\xFF" "\x00\"" "\x00!", FINAL, UTF16BE, "s(ar 5F EF BF BD):0,0,0,0 !(UnknownToken):8,0,4,0")
PARSE_TEST("replace UTF-16BE standalone leading surrogate (1)", ReplaceInvalidEncodingSequences,  "\x00\"" "\x00_" "\xD8\x00" "\x00_" "\x00\"", FINAL, UTF16BE, "s(ar 5F EF BF BD 5F):0,0,0,0")
PARSE_TEST("replace UTF-16BE standalone leading surrogate (2)", ReplaceInvalidEncodingSequences,  "\x00\"" "\x00_" "\xD8\x00" "\x00_" "\x00\"" "!\x00", FINAL, UTF16BE, "s(ar 5F EF BF BD 5F):0,0,0,0 !(UnknownToken):10,0,5,0")
PARSE_TEST("replace UTF-16BE standalone leading surrogate (3)", ReplaceInvalidEncodingSequences,  "\x00\"" "\x00_" "\xDB\xFF" "\x00_" "\x00\"", FINAL, UTF16BE, "s(ar 5F EF BF BD 5F):0,0,0,0")
PARSE_TEST("replace UTF-16BE standalone leading surrogate (4)", ReplaceInvalidEncodingSequences,  "\x00\"" "\x00_" "\xDB\xFF" "\x00_" "\x00\"" "!\x00", FINAL, UTF16BE, "s(ar 5F EF BF BD 5F):0,0,0,0 !(UnknownToken):10,0,5,0")
PARSE_TEST("replace UTF-32LE encoded surrogate (1)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\x00\xD8\x00\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "s(ar EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-32LE encoded surrogate (2)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\x00\xD8\x00\x00" "\"\x00\x00\x00" "!\x00\x00\x00", FINAL, UTF32LE, "s(ar EF BF BD):0,0,0,0 !(UnknownToken):12,0,3,0")
PARSE_TEST("replace UTF-32LE encoded surrogate (3)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\xFF\xDF\x00\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "s(ar EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-32LE encoded surrogate (4)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\xFF\xDF\x00\x00" "\"\x00\x00\x00" "!\x00\x00\x00", FINAL, UTF32LE, "s(ar EF BF BD):0,0,0,0 !(UnknownToken):12,0,3,0")
PARSE_TEST("replace UTF-32LE encoded out-of-range codepoint (1)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\x00\x00\x11\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "s(ar EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-32LE encoded out-of-range codepoint (2)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\x00\x00\x11\x00" "\"\x00\x00\x00" "!\x00\x00\x00", FINAL, UTF32LE, "s(ar EF BF BD):0,0,0,0 !(UnknownToken):12,0,3,0")
PARSE_TEST("replace UTF-32LE encoded out-of-range codepoint (3)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\x00\x00\x00\x01" "\"\x00\x00\x00", FINAL, UTF32LE, "s(ar EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-32LE encoded out-of-range codepoint (4)", ReplaceInvalidEncodingSequences, "\"\x00\x00\x00" "\x00\x00\x00\x01" "\"\x00\x00\x00" "!\x00\x00\x00", FINAL, UTF32LE, "s(ar EF BF BD):0,0,0,0 !(UnknownToken):12,0,3,0")
PARSE_TEST("replace UTF-32BE encoded surrogate (1)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x00\x00\xD8\x00" "\x00\x00\x00\"", FINAL, UTF32BE, "s(ar EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-32BE encoded surrogate (2)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x00\x00\xD8\x00" "\x00\x00\x00\"" "\x00\x00\x00!", FINAL, UTF32BE, "s(ar EF BF BD):0,0,0,0 !(UnknownToken):12,0,3,0")
PARSE_TEST("replace UTF-32BE encoded surrogate (3)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x00\x00\xDF\xFF" "\x00\x00\x00\"", FINAL, UTF32BE, "s(ar EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-32BE encoded surrogate (4)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x00\x00\xDF\xFF" "\x00\x00\x00\"" "\x00\x00\x00!", FINAL, UTF32BE, "s(ar EF BF BD):0,0,0,0 !(UnknownToken):12,0,3,0")
PARSE_TEST("replace UTF-32BE encoded out-of-range codepoint (1)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x00\x11\x00\x00" "\x00\x00\x00\"", FINAL, UTF32BE, "s(ar EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-32BE encoded out-of-range codepoint (2)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x00\x11\x00\x00" "\x00\x00\x00\"" "\x00\x00\x00!", FINAL, UTF32BE, "s(ar EF BF BD):0,0,0,0 !(UnknownToken):12,0,3,0")
PARSE_TEST("replace UTF-32BE encoded out-of-range codepoint (3)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x01\x00\x00\x00" "\x00\x00\x00\"", FINAL, UTF32BE, "s(ar EF BF BD):0,0,0,0")
PARSE_TEST("replace UTF-32BE encoded out-of-range codepoint (4)", ReplaceInvalidEncodingSequences, "\x00\x00\x00\"" "\x01\x00\x00\x00" "\x00\x00\x00\"" "\x00\x00\x00!", FINAL, UTF32BE, "s(ar EF BF BD):0,0,0,0 !(UnknownToken):12,0,3,0")

/* general */

PARSE_TEST("no input bytes (partial)", Standard, "", PARTIAL, UnknownEncoding, "")
PARSE_TEST("no input bytes", Standard, "", FINAL, UnknownEncoding, "!(ExpectedMoreTokens):0,0,0,0")
PARSE_TEST("all whitespace (partial) (1)", Standard, " ", PARTIAL, UnknownEncoding, "")
PARSE_TEST("all whitespace (partial) (2)", Standard, "\t", PARTIAL, UnknownEncoding, "")
PARSE_TEST("all whitespace (partial) (3)", Standard, "\r\n", PARTIAL, UnknownEncoding, "")
PARSE_TEST("all whitespace (partial) (4)", Standard, "\r\n\n\r ", PARTIAL, UTF8, "")
PARSE_TEST("all whitespace (1)", Standard, " ", FINAL, UTF8, "!(ExpectedMoreTokens):1,0,1,0")
PARSE_TEST("all whitespace (2)", Standard, "\t", FINAL, UTF8, "!(ExpectedMoreTokens):1,0,1,0")
PARSE_TEST("all whitespace (3)", Standard, "\r\n", FINAL, UTF8, "!(ExpectedMoreTokens):2,1,0,0")
PARSE_TEST("all whitespace (4)", Standard, "\r\n\n\r ", FINAL, UTF8, "!(ExpectedMoreTokens):5,3,1,0")
PARSE_TEST("trailing garbage (1)", Standard, "7 !", FINAL, UTF8, "#(7):0,0,0,0 !(UnknownToken):2,0,2,0")
PARSE_TEST("trailing garbage (2)", Standard, "7 {", FINAL, UTF8, "#(7):0,0,0,0 !(UnexpectedToken):2,0,2,0")
PARSE_TEST("trailing garbage (3)", Standard, "7 \xC0", FINAL, UTF8, "#(7):0,0,0,0 !(InvalidEncodingSequence):2,0,2,0")
PARSE_TEST("trailing garbage (4)", Standard, "7 \xC2", FINAL, UTF8, "#(7):0,0,0,0 !(InvalidEncodingSequence):2,0,2,0")
PARSE_TEST("trailing garbage (5)", Standard, "7 [", FINAL, UTF8, "#(7):0,0,0,0 !(UnexpectedToken):2,0,2,0")
PARSE_TEST("trailing garbage (6)", Standard, "7 ,", FINAL, UTF8, "#(7):0,0,0,0 !(UnexpectedToken):2,0,2,0")
PARSE_TEST("trailing garbage (7)", Standard, "7 8", FINAL, UTF8, "#(7):0,0,0,0 !(UnexpectedToken):2,0,2,0")
PARSE_TEST("trailing garbage (8)", Standard, "7 \"", FINAL, UTF8, "#(7):0,0,0,0 !(IncompleteToken):2,0,2,0")

/* null */

PARSE_TEST("null (1)", Standard, "null", FINAL, UTF8, "n:0,0,0,0")
PARSE_TEST("null (2)", Standard, " null ", FINAL, UTF8, "n:1,0,1,0")
PARSE_TEST("n is not a literal", Standard, "n ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("nu is not a literal", Standard, "nu ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("nul is not a literal", Standard, "nul ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("nullx is not a literal", Standard, "nullx", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("null0 is not a literal", Standard, "null0", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("null_ is not a literal", Standard, "null_", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("nullX is not a literal", Standard, "nullX", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("NULL is not a literal", Standard, "NULL", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("null truncated after n", Standard, "n", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("null truncated after nu", Standard, "nu", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("null truncated after nul", Standard, "nul", FINAL, UTF8, "!(UnknownToken):0,0,0,0")

/* true */

PARSE_TEST("true (1)", Standard, "true", FINAL, UTF8, "t:0,0,0,0")
PARSE_TEST("true (2)", Standard, " true ", FINAL, UTF8, "t:1,0,1,0")
PARSE_TEST("t is not a literal", Standard, "t ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("tr is not a literal", Standard, "tr ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("tru is not a literal", Standard, "tru ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("trux is not a literal", Standard, "trux", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("true0 is not a literal", Standard, "true0", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("true_ is not a literal", Standard, "true__", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("trueX is not a literal", Standard, "trueX", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("TRUE is not a literal", Standard, "TRUE", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("true truncated after t", Standard, "t", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("true truncated after tr", Standard, "tr", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("true truncated after tru", Standard, "tru", FINAL, UTF8, "!(UnknownToken):0,0,0,0")

/* false */

PARSE_TEST("false (1)", Standard, "false", FINAL, UTF8, "f:0,0,0,0")
PARSE_TEST("false (2)", Standard, " false ", FINAL, UTF8, "f:1,0,1,0")
PARSE_TEST("f is not a literal", Standard, "f ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("fa is not a literal", Standard, "fa ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("fal is not a literal", Standard, "fal ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("falx is not a literal", Standard, "falx", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("fals is not a literal", Standard, "fals", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("false0 is not a literal", Standard, "false0", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("false_ is not a literal", Standard, "false_", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("falseX is not a literal", Standard, "falseX", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("FALSE is not a literal", Standard, "FALSE", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("false truncated after f", Standard, "f", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("false truncated after fa", Standard, "fa", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("false truncated after fal", Standard, "fal", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("false truncated after fals", Standard, "fals", FINAL, UTF8, "!(UnknownToken):0,0,0,0")

/* NaN */

PARSE_TEST("NaN (1)", AllowSpecialNumbers, "NaN", FINAL, UTF8, "#(NaN):0,0,0,0")
PARSE_TEST("NaN (2)", AllowSpecialNumbers, " NaN ", FINAL, UTF8, "#(NaN):1,0,1,0")
PARSE_TEST("N is not a literal", AllowSpecialNumbers, "N ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Na is not a literal", AllowSpecialNumbers, "Na ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Nax is not a literal", AllowSpecialNumbers, "Nax", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Na0 is not a literal", AllowSpecialNumbers, "NaN0", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("NaN_ is not a literal", AllowSpecialNumbers, "NaN_", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("NaNX is not a literal", AllowSpecialNumbers, "NaNX", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("NAN is not a literal", AllowSpecialNumbers, "NAN", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("NaN truncated after N", AllowSpecialNumbers, "N", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("NaN truncated after Na", AllowSpecialNumbers, "Na", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("NaN not allowed", Standard, "NaN", FINAL, UTF8, "!(UnknownToken):0,0,0,0")

/* Infinity */

PARSE_TEST("Infinity (1)", AllowSpecialNumbers, "Infinity", FINAL, UTF8, "#(Infinity):0,0,0,0")
PARSE_TEST("Infinity (2)", AllowSpecialNumbers, " Infinity ", FINAL, UTF8, "#(Infinity):1,0,1,0")
PARSE_TEST("I is not a literal", AllowSpecialNumbers, "I ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("In is not a literal", AllowSpecialNumbers, "In ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Inf is not a literal", AllowSpecialNumbers, "Inf ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infi is not a literal", AllowSpecialNumbers, "Infi ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infin is not a literal", AllowSpecialNumbers, "Infin ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infini is not a literal", AllowSpecialNumbers, "Infini ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infinit is not a literal", AllowSpecialNumbers, "Infinit ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infinitx is not a literal", AllowSpecialNumbers, "Infinitx", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infinit0 is not a literal", AllowSpecialNumbers, "Infinit0", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity_ is not a literal", AllowSpecialNumbers, "Infinity_", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("InfinityX is not a literal", AllowSpecialNumbers, "InfinityX", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("INF is not a literal", AllowSpecialNumbers, "INF", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("INFINITY is not a literal", AllowSpecialNumbers, "INFINITY", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity truncated after I", AllowSpecialNumbers, "I", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity truncated after In", AllowSpecialNumbers, "In", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity truncated after Inf", AllowSpecialNumbers, "Inf", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity truncated after Infi", AllowSpecialNumbers, "Infi", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity truncated after Infin", AllowSpecialNumbers, "Infin", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity truncated after Infini", AllowSpecialNumbers, "Infini", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity truncated after Infinit", AllowSpecialNumbers, "Infinit", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("Infinity not allowed", Standard, "Infinity", FINAL, UTF8, "!(UnknownToken):0,0,0,0")

/* -Infinity */

PARSE_TEST("-Infinity (1)", AllowSpecialNumbers, "-Infinity", FINAL, UTF8, "#(-Infinity):0,0,0,0")
PARSE_TEST("-Infinity (2)", AllowSpecialNumbers, " -Infinity ", FINAL, UTF8, "#(-Infinity):1,0,1,0")
PARSE_TEST("-I is not a number", AllowSpecialNumbers, "-I ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-In is not a number", AllowSpecialNumbers, "-In ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Inf is not a number", AllowSpecialNumbers, "-Inf ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infi is not a number", AllowSpecialNumbers, "-Infi ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infin is not a number", AllowSpecialNumbers, "-Infin ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infini is not a number", AllowSpecialNumbers, "-Infini ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinit is not a number", AllowSpecialNumbers, "-Infinit ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinitx is not a number", AllowSpecialNumbers, "-Infinitx", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinit0 is not a number", AllowSpecialNumbers, "-Infinit0", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity_ is not a number", AllowSpecialNumbers, "-Infinity_", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-InfinityX is not a number", AllowSpecialNumbers, "-InfinityX", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-INF is not a number", AllowSpecialNumbers, "-INF", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-INFINITY is not a number", AllowSpecialNumbers, "-INFINITY", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity truncated after I", AllowSpecialNumbers, "-I", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity truncated after In", AllowSpecialNumbers, "-In", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity truncated after Inf", AllowSpecialNumbers, "-Inf", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity truncated after Infi", AllowSpecialNumbers, "-Infi", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity truncated after Infin", AllowSpecialNumbers, "-Infin", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity truncated after Infini", AllowSpecialNumbers, "-Infini", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity truncated after Infinit", AllowSpecialNumbers, "-Infinit", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("-Infinity not allowed", Standard, "-Infinity", FINAL, UTF8, "!(UnknownToken):0,0,0,0")

/* numbers */

PARSE_TEST("0 (1)", Standard, "0", FINAL, UTF8, "#(0):0,0,0,0")
PARSE_TEST("0 (2)", Standard, " 0 ", FINAL, UTF8, "#(0):1,0,1,0")
PARSE_TEST("-0 (1)", Standard, "-0", FINAL, UTF8, "#(-0):0,0,0,0")
PARSE_TEST("-0 (2)", Standard, " -0 ", FINAL, UTF8, "#(-0):1,0,1,0")
PARSE_TEST("7 (1)", Standard, "7", FINAL, UTF8, "#(7):0,0,0,0")
PARSE_TEST("7 (2)", Standard, " 7 ", FINAL, UTF8, "#(7):1,0,1,0")
PARSE_TEST("-7 (1)", Standard, "-7", FINAL, UTF8, "#(-7):0,0,0,0")
PARSE_TEST("-7 (2)", Standard, " -7 ", FINAL, UTF8, "#(-7):1,0,1,0")
PARSE_TEST("1234567890 (1)", Standard, "1234567890", FINAL, UTF8, "#(1234567890):0,0,0,0")
PARSE_TEST("1234567890 (2)", Standard, " 1234567890 ", FINAL, UTF8, "#(1234567890):1,0,1,0")
PARSE_TEST("-1234567890 (1)", Standard, "-1234567890", FINAL, UTF8, "#(-1234567890):0,0,0,0")
PARSE_TEST("-1234567890 (2)", Standard, " -1234567890 ", FINAL, UTF8, "#(-1234567890):1,0,1,0")
PARSE_TEST("0e1 (1)", Standard, "0e1", FINAL, UTF8, "#(0e1):0,0,0,0")
PARSE_TEST("0e1 (2)", Standard, " 0e1 ", FINAL, UTF8, "#(0e1):1,0,1,0")
PARSE_TEST("1e2 (1)", Standard, "1e2", FINAL, UTF8, "#(1e2):0,0,0,0")
PARSE_TEST("1e2 (2)", Standard, " 1e2 ", FINAL, UTF8, "#(1e2):1,0,1,0")
PARSE_TEST("0e+1 (1)", Standard, "0e+1", FINAL, UTF8, "#(0e+1):0,0,0,0")
PARSE_TEST("0e+1 (2)", Standard, " 0e+1 ", FINAL, UTF8, "#(0e+1):1,0,1,0")
PARSE_TEST("1e+2 (1)", Standard, "1e+2", FINAL, UTF8, "#(1e+2):0,0,0,0")
PARSE_TEST("1e+2 (2)", Standard, " 1e+2 ", FINAL, UTF8, "#(1e+2):1,0,1,0")
PARSE_TEST("0e-1 (1)", Standard, "0e-1", FINAL, UTF8, "#(0e-1):0,0,0,0")
PARSE_TEST("0e-1 (2)", Standard, " 0e-1 ", FINAL, UTF8, "#(0e-1):1,0,1,0")
PARSE_TEST("1e-2 (1)", Standard, "1e-2", FINAL, UTF8, "#(1e-2):0,0,0,0")
PARSE_TEST("1e-2 (2)", Standard, " 1e-2 ", FINAL, UTF8, "#(1e-2):1,0,1,0")
PARSE_TEST("1234567890E0987654321 (1)", Standard, "1234567890E0987654321", FINAL, UTF8, "#(1234567890E0987654321):0,0,0,0")
PARSE_TEST("1234567890E0987654321 (2)", Standard, " 1234567890E0987654321 ", FINAL, UTF8, "#(1234567890E0987654321):1,0,1,0")
PARSE_TEST("0.0 (1)", Standard, "0.0", FINAL, UTF8, "#(0.0):0,0,0,0")
PARSE_TEST("0.0 (2)", Standard, " 0.0 ", FINAL, UTF8, "#(0.0):1,0,1,0")
PARSE_TEST("0.12 (1)", Standard, "0.12", FINAL, UTF8, "#(0.12):0,0,0,0")
PARSE_TEST("0.12 (2)", Standard, " 0.12 ", FINAL, UTF8, "#(0.12):1,0,1,0")
PARSE_TEST("1.2 (1)", Standard, "1.2", FINAL, UTF8, "#(1.2):0,0,0,0")
PARSE_TEST("1.2 (2)", Standard, " 1.2 ", FINAL, UTF8, "#(1.2):1,0,1,0")
PARSE_TEST("1.23 (1)", Standard, "1.23", FINAL, UTF8, "#(1.23):0,0,0,0")
PARSE_TEST("1.23 (2)", Standard, " 1.23 ", FINAL, UTF8, "#(1.23):1,0,1,0")
PARSE_TEST("1.23e456 (1)", Standard, "1.23e456", FINAL, UTF8, "#(1.23e456):0,0,0,0")
PARSE_TEST("1.23e456 (2)", Standard, " 1.23e456 ", FINAL, UTF8, "#(1.23e456):1,0,1,0")
PARSE_TEST("1.23e+456 (1)", Standard, "1.23e+456", FINAL, UTF8, "#(1.23e+456):0,0,0,0")
PARSE_TEST("1.23e+456 (2)", Standard, " 1.23e+456 ", FINAL, UTF8, "#(1.23e+456):1,0,1,0")
PARSE_TEST("1.23e-456 (1)", Standard, "1.23e-456", FINAL, UTF8, "#(1.23e-456):0,0,0,0")
PARSE_TEST("1.23e-456 (2)", Standard, " 1.23e-456 ", FINAL, UTF8, "#(1.23e-456):1,0,1,0")
PARSE_TEST("maximum length number", Standard, "-123456789012345678901234567890.12345678901234567890e1234567890", FINAL, UTF8, "#(-123456789012345678901234567890.12345678901234567890e1234567890):0,0,0,0")
PARSE_TEST("number encoded in UTF-16LE (1)", UTF16LEIn | UTF16LEOut, "0\x00", FINAL, UTF16LE, "#(0):0,0,0,0")
PARSE_TEST("number encoded in UTF-16LE (2)", UTF16LEIn | UTF16LEOut, "-\x00" "1\x00" ".\x00" "2\x00" "3\x00" "e\x00" "-\x00" "4\x00" "5\x00" "6\x00", FINAL, UTF16LE, "#(-1.23e-456):0,0,0,0")
PARSE_TEST("number encoded in UTF-16BE (1)", UTF16BEIn | UTF16BEOut, "\x00" "0", FINAL, UTF16BE, "#(0):0,0,0,0")
PARSE_TEST("number encoded in UTF-16BE (2)", UTF16BEIn | UTF16BEOut, "\x00" "-\x00" "1\x00" ".\x00" "2\x00" "3\x00" "e\x00" "-\x00" "4\x00" "5\x00" "6", FINAL, UTF16BE, "#(-1.23e-456):0,0,0,0")
PARSE_TEST("number encoded in UTF-32LE (1)", UTF32LEIn | UTF32LEOut, "0\x00\x00\x00", FINAL, UTF32LE, "#(0):0,0,0,0")
PARSE_TEST("number encoded in UTF-32LE (2)", UTF32LEIn | UTF32LEOut, "-\x00\x00\x00" "1\x00\x00\x00" ".\x00\x00\x00" "2\x00\x00\x00" "3\x00\x00\x00" "e\x00\x00\x00" "-\x00\x00\x00" "4\x00\x00\x00" "5\x00\x00\x00" "6\x00\x00\x00", FINAL, UTF32LE, "#(-1.23e-456):0,0,0,0")
PARSE_TEST("number encoded in UTF-32BE (1)", UTF32BEIn | UTF32BEOut, "\x00\x00\x00" "0", FINAL, UTF32BE, "#(0):0,0,0,0")
PARSE_TEST("number encoded in UTF-32BE (2)", UTF32BEIn | UTF32BEOut, "\x00\x00\x00" "-\x00\x00\x00" "1\x00\x00\x00" ".\x00\x00\x00" "2\x00\x00\x00" "3\x00\x00\x00" "e\x00\x00\x00" "-\x00\x00\x00" "4\x00\x00\x00" "5\x00\x00\x00" "6", FINAL, UTF32BE, "#(-1.23e-456):0,0,0,0")
PARSE_TEST("number cannot have leading + sign", Standard, "+7", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("number cannot have digits after leading 0 (1)", Standard, "00", FINAL, UTF8, "!(InvalidNumber):0,0,0,0")
PARSE_TEST("number cannot have digits after leading 0 (2)", Standard, "01", FINAL, UTF8, "!(InvalidNumber):0,0,0,0")
PARSE_TEST("number cannot have digits after leading 0 (3)", Standard, "-00", FINAL, UTF8, "!(InvalidNumber):0,0,0,0")
PARSE_TEST("number cannot have digits after leading 0 (4)", Standard, "-01", FINAL, UTF8, "!(InvalidNumber):0,0,0,0")
PARSE_TEST("number requires digit after - sign", Standard, "-x", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("number truncated after - sign", Standard, "-", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("number requires digit after decimal point", Standard, "7.x", FINAL, UTF8, "!(InvalidNumber):0,0,0,0")
PARSE_TEST("number truncated after decimal point", Standard, "7.", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("number requires digit or sign after e", Standard, "7ex", FINAL, UTF8, "!(InvalidNumber):0,0,0,0")
PARSE_TEST("number truncated after e", Standard, "7e", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("number requires digit or sign after E", Standard, "7Ex", FINAL, UTF8, "!(InvalidNumber):0,0,0,0")
PARSE_TEST("number truncated after E", Standard, "7E", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("number requires digit after exponent + sign", Standard, "7e+x", FINAL, UTF8, "!(InvalidNumber):0,0,0,0")
PARSE_TEST("number truncated after exponent + sign", Standard, "7e+", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("number requires digit after exponent - sign", Standard, "7e-x", FINAL, UTF8, "!(InvalidNumber):0,0,0,0")
PARSE_TEST("number truncated after exponent - sign", Standard, "7e-", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("too long number", Standard, "-123456789012345678901234567890.12345678901234567890e12345678900", FINAL, UTF8, "!(TooLongNumber):0,0,0,0")

/* hex numbers */

PARSE_TEST("hex number not allowed (1)", Standard, "0x0", FINAL, UTF8, "#(0):0,0,0,0 !(UnknownToken):1,0,1,0")
PARSE_TEST("hex number not allowed (2)", Standard, "0X1", FINAL, UTF8, "#(0):0,0,0,0 !(UnknownToken):1,0,1,0")
PARSE_TEST("hex number not allowed (3)", Standard, "-0X1", FINAL, UTF8, "#(-0):0,0,0,0 !(UnknownToken):2,0,2,0")
PARSE_TEST("negative hex number not allowed", AllowHexNumbers, "-0X1", FINAL, UTF8, "#(-0):0,0,0,0 !(UnknownToken):2,0,2,0")
PARSE_TEST("hex number (1)", AllowHexNumbers, "0x0", FINAL, UTF8, "#(0x0):0,0,0,0")
PARSE_TEST("hex number (2)", AllowHexNumbers, "0x1", FINAL, UTF8, "#(0x1):0,0,0,0")
PARSE_TEST("hex number (3)", AllowHexNumbers, "0x0000", FINAL, UTF8, "#(0x0000):0,0,0,0")
PARSE_TEST("hex number (4)", AllowHexNumbers, "0x123456789abcdefABCDEF", FINAL, UTF8, "#(0x123456789abcdefABCDEF):0,0,0,0")
PARSE_TEST("maximum length hex number", AllowHexNumbers, "0x123456789a123456789a123456789a123456789a123456789a123456789a0", FINAL, UTF8, "#(0x123456789a123456789a123456789a123456789a123456789a123456789a0):0,0,0,0")
PARSE_TEST("hex number truncated after x", AllowHexNumbers, "0x", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("hex number requires  digit after x", AllowHexNumbers, "0xx", FINAL, UTF8, "!(InvalidNumber):0,0,0,0")
PARSE_TEST("hex number truncated after X", AllowHexNumbers, "0X", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("hex number requires  digit after X", AllowHexNumbers, "0Xx", FINAL, UTF8, "!(InvalidNumber):0,0,0,0")
PARSE_TEST("too long hex number", AllowHexNumbers, "0x123456789a123456789a123456789a123456789a123456789a123456789a00", FINAL, UTF8, "!(TooLongNumber):0,0,0,0")

/* strings */

PARSE_TEST("empty string", Standard, "\"\"", FINAL, UTF8, "s():0,0,0,0")
PARSE_TEST("UTF-8 -> UTF-8",    UTF8In | UTF8Out,    "\"" "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84" "\"", FINAL, UTF8, "s(ab 61 C2 A9 E4 B8 81 F0 9F 80 84):0,0,0,0")
PARSE_TEST("UTF-8 -> UTF-16LE", UTF8In | UTF16LEOut, "\"" "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84" "\"", FINAL, UTF8, "s(ab 61 00 A9 00 01 4E 3C D8 04 DC):0,0,0,0")
PARSE_TEST("UTF-8 -> UTF-16BE", UTF8In | UTF16BEOut, "\"" "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84" "\"", FINAL, UTF8, "s(ab 00 61 00 A9 4E 01 D8 3C DC 04):0,0,0,0")
PARSE_TEST("UTF-8 -> UTF-32LE", UTF8In | UTF32LEOut, "\"" "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84" "\"", FINAL, UTF8, "s(ab 61 00 00 00 A9 00 00 00 01 4E 00 00 04 F0 01 00):0,0,0,0")
PARSE_TEST("UTF-8 -> UTF-32BE", UTF8In | UTF32BEOut, "\"" "\x61\xC2\xA9\xE4\xB8\x81\xF0\x9F\x80\x84" "\"", FINAL, UTF8, "s(ab 00 00 00 61 00 00 00 A9 00 00 4E 01 00 01 F0 04):0,0,0,0")
PARSE_TEST("UTF-16LE -> UTF-8",    UTF16LEIn | UTF8Out,    "\"\x00" "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC" "\"\x00", FINAL, UTF16LE, "s(ab 61 C2 A9 E4 B8 81 F0 9F 80 84):0,0,0,0")
PARSE_TEST("UTF-16LE -> UTF-16LE", UTF16LEIn | UTF16LEOut, "\"\x00" "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC" "\"\x00", FINAL, UTF16LE, "s(ab 61 00 A9 00 01 4E 3C D8 04 DC):0,0,0,0")
PARSE_TEST("UTF-16LE -> UTF-16BE", UTF16LEIn | UTF16BEOut, "\"\x00" "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC" "\"\x00", FINAL, UTF16LE, "s(ab 00 61 00 A9 4E 01 D8 3C DC 04):0,0,0,0")
PARSE_TEST("UTF-16LE -> UTF-32LE", UTF16LEIn | UTF32LEOut, "\"\x00" "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC" "\"\x00", FINAL, UTF16LE, "s(ab 61 00 00 00 A9 00 00 00 01 4E 00 00 04 F0 01 00):0,0,0,0")
PARSE_TEST("UTF-16LE -> UTF-32BE", UTF16LEIn | UTF32BEOut, "\"\x00" "\x61\x00\xA9\x00\x01\x4E\x3C\xD8\x04\xDC" "\"\x00", FINAL, UTF16LE, "s(ab 00 00 00 61 00 00 00 A9 00 00 4E 01 00 01 F0 04):0,0,0,0")
PARSE_TEST("UTF-16BE -> UTF-8",    UTF16BEIn | UTF8Out,    "\x00\"" "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04" "\x00\"", FINAL, UTF16BE, "s(ab 61 C2 A9 E4 B8 81 F0 9F 80 84):0,0,0,0")
PARSE_TEST("UTF-16BE -> UTF-16LE", UTF16BEIn | UTF16LEOut, "\x00\"" "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04" "\x00\"", FINAL, UTF16BE, "s(ab 61 00 A9 00 01 4E 3C D8 04 DC):0,0,0,0")
PARSE_TEST("UTF-16BE -> UTF-16BE", UTF16BEIn | UTF16BEOut, "\x00\"" "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04" "\x00\"", FINAL, UTF16BE, "s(ab 00 61 00 A9 4E 01 D8 3C DC 04):0,0,0,0")
PARSE_TEST("UTF-16BE -> UTF-32LE", UTF16BEIn | UTF32LEOut, "\x00\"" "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04" "\x00\"", FINAL, UTF16BE, "s(ab 61 00 00 00 A9 00 00 00 01 4E 00 00 04 F0 01 00):0,0,0,0")
PARSE_TEST("UTF-16BE -> UTF-32BE", UTF16BEIn | UTF32BEOut, "\x00\"" "\x00\x61\x00\xA9\x4E\x01\xD8\x3C\xDC\x04" "\x00\"", FINAL, UTF16BE, "s(ab 00 00 00 61 00 00 00 A9 00 00 4E 01 00 01 F0 04):0,0,0,0")
PARSE_TEST("UTF-32LE -> UTF-8",    UTF32LEIn | UTF8Out,    "\"\x00\x00\x00" "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "s(ab 61 C2 A9 E4 B8 81 F0 9F 80 84):0,0,0,0")
PARSE_TEST("UTF-32LE -> UTF-16LE", UTF32LEIn | UTF16LEOut, "\"\x00\x00\x00" "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "s(ab 61 00 A9 00 01 4E 3C D8 04 DC):0,0,0,0")
PARSE_TEST("UTF-32LE -> UTF-16BE", UTF32LEIn | UTF16BEOut, "\"\x00\x00\x00" "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "s(ab 00 61 00 A9 4E 01 D8 3C DC 04):0,0,0,0")
PARSE_TEST("UTF-32LE -> UTF-32LE", UTF32LEIn | UTF32LEOut, "\"\x00\x00\x00" "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "s(ab 61 00 00 00 A9 00 00 00 01 4E 00 00 04 F0 01 00):0,0,0,0")
PARSE_TEST("UTF-32LE -> UTF-32BE", UTF32LEIn | UTF32BEOut, "\"\x00\x00\x00" "\x61\x00\x00\x00\xA9\x00\x00\x00\x01\x4E\x00\x00\x04\xF0\x01\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "s(ab 00 00 00 61 00 00 00 A9 00 00 4E 01 00 01 F0 04):0,0,0,0")
PARSE_TEST("UTF-32BE -> UTF-8",    UTF32BEIn | UTF8Out,    "\x00\x00\x00\"" "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04" "\x00\x00\x00\"", FINAL, UTF32BE, "s(ab 61 C2 A9 E4 B8 81 F0 9F 80 84):0,0,0,0")
PARSE_TEST("UTF-32BE -> UTF-16LE", UTF32BEIn | UTF16LEOut, "\x00\x00\x00\"" "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04" "\x00\x00\x00\"", FINAL, UTF32BE, "s(ab 61 00 A9 00 01 4E 3C D8 04 DC):0,0,0,0")
PARSE_TEST("UTF-32BE -> UTF-16BE", UTF32BEIn | UTF16BEOut, "\x00\x00\x00\"" "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04" "\x00\x00\x00\"", FINAL, UTF32BE, "s(ab 00 61 00 A9 4E 01 D8 3C DC 04):0,0,0,0")
PARSE_TEST("UTF-32BE -> UTF-32LE", UTF32BEIn | UTF32LEOut, "\x00\x00\x00\"" "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04" "\x00\x00\x00\"", FINAL, UTF32BE, "s(ab 61 00 00 00 A9 00 00 00 01 4E 00 00 04 F0 01 00):0,0,0,0")
PARSE_TEST("UTF-32BE -> UTF-32BE", UTF32BEIn | UTF32BEOut, "\x00\x00\x00\"" "\x00\x00\x00\x61\x00\x00\x00\xA9\x00\x00\x4E\x01\x00\x01\xF0\x04" "\x00\x00\x00\"", FINAL, UTF32BE, "s(ab 00 00 00 61 00 00 00 A9 00 00 4E 01 00 01 F0 04):0,0,0,0")
PARSE_TEST("all whitespace string", Standard, "\" \\r\\n\\t \"", FINAL, UTF8, "s(c 20 0D 0A 09 20):0,0,0,0")
PARSE_TEST("ASCII string", Standard, "\"abc DEF 123\"", FINAL, UTF8, "s(61 62 63 20 44 45 46 20 31 32 33):0,0,0,0")
PARSE_TEST("simple string escape sequences", Standard, "\"\\\"\\\\/\\t\\n\\r\\f\\b\"", FINAL, UTF8, "s(c 22 5C 2F 09 0A 0D 0C 08):0,0,0,0")
PARSE_TEST("string hex escape sequences", Standard, "\"\\u0000\\u0020\\u0aF9\\ufFfF\\uD834\\udd1e\"", FINAL, UTF8, "s(zcab 00 20 E0 AB B9 EF BF BF F0 9D 84 9E):0,0,0,0")
PARSE_TEST("string escaped control characters", Standard, "\""
                   "\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\u0008\\u0009\\u000A\\u000B\\u000C\\u000D\\u000E\\u000F"
                   "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017\\u0018\\u0019\\u001A\\u001B\\u001C\\u001D\\u001E\\u001F"
                   "\"", FINAL, UTF8, "s(zc 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F):0,0,0,0")
PARSE_TEST("non-control ASCII string", Standard, "\""
                   " !\\u0022#$%&'()+*,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\u005C]^_`abcdefghijklmnopqrstuvwxyz{|}~\\u007F"
                   "\"", FINAL, UTF8, "s(20 21 22 23 24 25 26 27 28 29 2B 2A 2C 2D 2E 2F 30 31 32 33 34 35 36 37 38 39 3A 3B 3C 3D 3E 3F 40 41 42 43 44 45 46 47 48 49 4A 4B 4C 4D 4E 4F 50 51 52 53 54 55 56 57 58 59 5A 5B 5C 5D 5E 5F 60 61 62 63 64 65 66 67 68 69 6A 6B 6C 6D 6E 6F 70 71 72 73 74 75 76 77 78 79 7A 7B 7C 7D 7E 7F):0,0,0,0")
PARSE_TEST("long string", Standard, "\""
                   "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
                   "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
                   "\"", FINAL, UTF8, "s(30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46):0,0,0,0")
PARSE_TEST("unterminated string (1)", Standard, "\"", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("unterminated string (2)", Standard, "\"abc", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("string cannot contain unescaped control character (1)", Standard, "\"abc\x00\"", FINAL, UTF8, "!(UnescapedControlCharacter):4,0,4,0")
PARSE_TEST("string cannot contain unescaped control character (2)", Standard, "\"abc\x09\"", FINAL, UTF8, "!(UnescapedControlCharacter):4,0,4,0")
PARSE_TEST("string cannot contain unescaped control character (3)", Standard, "\"abc\x0A\"", FINAL, UTF8, "!(UnescapedControlCharacter):4,0,4,0")
PARSE_TEST("string cannot contain unescaped control character (4)", Standard, "\"abc\x0D\"", FINAL, UTF8, "!(UnescapedControlCharacter):4,0,4,0")
PARSE_TEST("string cannot contain unescaped control character (5)", Standard, "\"abc\x1F\"", FINAL, UTF8, "!(UnescapedControlCharacter):4,0,4,0")
PARSE_TEST("string cannot contain invalid escape sequence (1)", Standard, "\"\\v\"", FINAL, UTF8, "!(InvalidEscapeSequence):1,0,1,0")
PARSE_TEST("string cannot contain invalid escape sequence (2)", Standard, "\"\\x0020\"", FINAL, UTF8, "!(InvalidEscapeSequence):1,0,1,0")
PARSE_TEST("string cannot contain invalid escape sequence (3)", Standard, "\"\\ \"", FINAL, UTF8, "!(InvalidEscapeSequence):1,0,1,0")
PARSE_TEST("string truncated after \\", Standard, "\"\\", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\u", Standard, "\"\\u", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\ux", Standard, "\"\\u0", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\uxx", Standard, "\"\\u01", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\uxxx", Standard, "\"\\u01a", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("string requires hex digit after \\u", Standard, "\"\\ux\"", FINAL, UTF8, "!(InvalidEscapeSequence):1,0,1,0")
PARSE_TEST("string requires hex digit after \\ux", Standard, "\"\\u0x\"", FINAL, UTF8, "!(InvalidEscapeSequence):1,0,1,0")
PARSE_TEST("string requires hex digit after \\uxx", Standard, "\"\\u01x\"", FINAL, UTF8, "!(InvalidEscapeSequence):1,0,1,0")
PARSE_TEST("string requires hex digit after \\uxxx", Standard, "\"\\u01ax\"", FINAL, UTF8, "!(InvalidEscapeSequence):1,0,1,0")
PARSE_TEST("string truncated after escaped leading surrogate", Standard, "\"\\uD800", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("string requires escaped surrogates to appear in valid pairs (1)", Standard, "\"\\uD834\"", FINAL, UTF8, "!(UnpairedSurrogateEscapeSequence):1,0,1,0")
PARSE_TEST("string requires escaped surrogates to appear in valid pairs (2)", Standard, "\"\\uD834x\"", FINAL, UTF8, "!(UnpairedSurrogateEscapeSequence):1,0,1,0")
PARSE_TEST("string requires escaped surrogates to appear in valid pairs (3)", Standard, "\"\\uD834\\n\"", FINAL, UTF8, "!(UnpairedSurrogateEscapeSequence):1,0,1,0")
PARSE_TEST("string requires escaped surrogates to appear in valid pairs (4)", Standard, "\"\\uD834\\u0020\"", FINAL, UTF8, "!(UnpairedSurrogateEscapeSequence):1,0,1,0")
PARSE_TEST("string requires escaped surrogates to appear in valid pairs (5)", Standard, "\"\\uD834\\uD834\"", FINAL, UTF8, "!(UnpairedSurrogateEscapeSequence):1,0,1,0")
PARSE_TEST("string requires escaped surrogates to appear in valid pairs (6)", Standard, "\"\\uDC00\"", FINAL, UTF8, "!(UnpairedSurrogateEscapeSequence):1,0,1,0")
PARSE_TEST("string truncated after \\ of trailing surrogate escape sequence", Standard, "\"\\uD834\\", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\u of trailing surrogate escape sequence", Standard, "\"\\uD834\\u", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\ux of trailing surrogate escape sequence", Standard, "\"\\uD834\\uD", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\uxx of trailing surrogate escape sequence", Standard, "\"\\uD834\\uDD", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("string truncated after \\uxxx of trailing surrogate escape sequence", Standard, "\"\\uD834\\uDD1", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("max length 0 string (1)", MaxStringLength0, "\"\"", FINAL, UTF8, "s():0,0,0,0")
PARSE_TEST("max length 0 string (2)", MaxStringLength0, "{\"\":0}", FINAL, UTF8, "{:0,0,0,0 M():1,0,1,1 #(0):4,0,4,1 }:5,0,5,0")
PARSE_TEST("max length 0 string (3)", MaxStringLength0, "\"a\"", FINAL, UTF8, "!(TooLongString):0,0,0,0")
PARSE_TEST("max length 0 string (4)", MaxStringLength0, "{\"a\":0}", FINAL, UTF8, "{:0,0,0,0 !(TooLongString):1,0,1,1")
PARSE_TEST("max length 1 string (1)", MaxStringLength1, "\"a\"", FINAL, UTF8, "s(61):0,0,0,0")
PARSE_TEST("max length 1 string (2)", MaxStringLength1, "{\"a\":0}", FINAL, UTF8, "{:0,0,0,0 M(61):1,0,1,1 #(0):5,0,5,1 }:6,0,6,0")
PARSE_TEST("max length 1 string (3)", MaxStringLength1, "\"ab\"", FINAL, UTF8, "!(TooLongString):0,0,0,0")
PARSE_TEST("max length 1 string (4)", MaxStringLength1, "{\"ab\":0}", FINAL, UTF8, "{:0,0,0,0 !(TooLongString):1,0,1,1")
PARSE_TEST("max length 1 string (5)", MaxStringLength1, "\"\xE0\xAB\xB9\"", FINAL, UTF8, "!(TooLongString):0,0,0,0")
PARSE_TEST("max length 2 string (1)", MaxStringLength2, "\"ab\"", FINAL, UTF8, "s(61 62):0,0,0,0")
PARSE_TEST("max length 2 string (2)", MaxStringLength2, "{\"ab\":0}", FINAL, UTF8, "{:0,0,0,0 M(61 62):1,0,1,1 #(0):6,0,6,1 }:7,0,7,0")
PARSE_TEST("max length 2 string (3)", MaxStringLength2, "\"abc\"", FINAL, UTF8, "!(TooLongString):0,0,0,0")
PARSE_TEST("max length 2 string (4)", MaxStringLength2, "{\"abc\":0}", FINAL, UTF8, "{:0,0,0,0 !(TooLongString):1,0,1,1")
PARSE_TEST("max length 2 string (5)", MaxStringLength2, "\"\xE0\xAB\xB9\"", FINAL, UTF8, "!(TooLongString):0,0,0,0")

/* objects */

PARSE_TEST("start object", UTF8In, "{", PARTIAL, UTF8, "{:0,0,0,0")
PARSE_TEST("empty object", Standard, "{}", FINAL, UTF8, "{:0,0,0,0 }:1,0,1,0")
PARSE_TEST("single-member object", Standard, "{ \"pi\" : 3.14159 }", FINAL, UTF8, "{:0,0,0,0 M(70 69):2,0,2,1 #(3.14159):9,0,9,1 }:17,0,17,0")
PARSE_TEST("multi-member object", Standard, "{ \"pi\" : 3.14159, \"e\" : 2.71828 }", FINAL, UTF8, "{:0,0,0,0 M(70 69):2,0,2,1 #(3.14159):9,0,9,1 m(65):18,0,18,1 #(2.71828):24,0,24,1 }:32,0,32,0")
PARSE_TEST("all types of object member values", AllowSpecialNumbers | AllowHexNumbers, "{ \"a\" : null, \"b\" : true, \"c\" : \"foo\", \"d\" : 17, \"e\" : NaN, \"f\": 0xbeef, \"g\" : {}, \"h\" : {}, \"i\" : [] }", FINAL, UTF8, "{:0,0,0,0 M(61):2,0,2,1 n:8,0,8,1 m(62):14,0,14,1 t:20,0,20,1 m(63):26,0,26,1 s(66 6F 6F):32,0,32,1 m(64):39,0,39,1 #(17):45,0,45,1 m(65):49,0,49,1 #(NaN):55,0,55,1 m(66):60,0,60,1 #(0xbeef):65,0,65,1 m(67):73,0,73,1 {:79,0,79,1 }:80,0,80,1 m(68):83,0,83,1 {:89,0,89,1 }:90,0,90,1 m(69):93,0,93,1 [:99,0,99,1 ]:100,0,100,1 }:102,0,102,0")
PARSE_TEST("nested objects", Standard, "{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{}}}}}}", FINAL, UTF8, "{:0,0,0,0 M(61):1,0,1,1 {:5,0,5,1 M(62):6,0,6,2 {:10,0,10,2 M(63):11,0,11,3 {:15,0,15,3 M(64):16,0,16,4 {:20,0,20,4 M(65):21,0,21,5 {:25,0,25,5 }:26,0,26,5 }:27,0,27,4 }:28,0,28,3 }:29,0,29,2 }:30,0,30,1 }:31,0,31,0")
PARSE_TEST("object members with similar names", Standard, "{\"\":null,\"\\u0000\":0,\"x\":1,\"X\":2,\"x2\":3,\"x\\u0000\":4,\"x\\u0000y\":5}", FINAL, UTF8, "{:0,0,0,0 M():1,0,1,1 n:4,0,4,1 m(zc 00):9,0,9,1 #(0):18,0,18,1 m(78):20,0,20,1 #(1):24,0,24,1 m(58):26,0,26,1 #(2):30,0,30,1 m(78 32):32,0,32,1 #(3):37,0,37,1 m(zc 78 00):39,0,39,1 #(4):49,0,49,1 m(zc 78 00 79):51,0,51,1 #(5):62,0,62,1 }:63,0,63,0")
PARSE_TEST("different objects with members with same names", Standard, "{\"foo\":{\"foo\":{\"foo\":3}}}", FINAL, UTF8, "{:0,0,0,0 M(66 6F 6F):1,0,1,1 {:7,0,7,1 M(66 6F 6F):8,0,8,2 {:14,0,14,2 M(66 6F 6F):15,0,15,3 #(3):21,0,21,3 }:22,0,22,2 }:23,0,23,1 }:24,0,24,0")
PARSE_TEST("object truncated after left curly brace", Standard, "{", FINAL, UTF8, "{:0,0,0,0 !(ExpectedMoreTokens):1,0,1,1")
PARSE_TEST("object truncated after member name (1)", Standard, "{\"x\"", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 !(ExpectedMoreTokens):4,0,4,1")
PARSE_TEST("object truncated after member name (2)", Standard, "{\"x\":1,\"y\"", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 m(79):7,0,7,1 !(ExpectedMoreTokens):10,0,10,1")
PARSE_TEST("object truncated after colon (1)", Standard, "{\"x\":", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 !(ExpectedMoreTokens):5,0,5,1")
PARSE_TEST("object truncated after colon (2)", Standard, "{\"x\":1,\"y\":", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 m(79):7,0,7,1 !(ExpectedMoreTokens):11,0,11,1")
PARSE_TEST("object truncated after member value (1)", Standard, "{\"x\":1", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 !(ExpectedMoreTokens):6,0,6,1")
PARSE_TEST("object truncated after member value (2)", Standard, "{\"x\":1,\"y\":2", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 m(79):7,0,7,1 #(2):11,0,11,1 !(ExpectedMoreTokens):12,0,12,1")
PARSE_TEST("object truncated after comma (1)", Standard, "{\"x\":1,", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 !(ExpectedMoreTokens):7,0,7,1")
PARSE_TEST("object truncated after comma (2)", Standard, "{\"x\":1,\"y\":2,", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 m(79):7,0,7,1 #(2):11,0,11,1 !(ExpectedMoreTokens):13,0,13,1")
PARSE_TEST("object requires string member names (1)", Standard, "{null:1}", FINAL, UTF8, "{:0,0,0,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("object requires string member names (2)", Standard, "{true:1}", FINAL, UTF8, "{:0,0,0,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("object requires string member names (3)", Standard, "{false:1}", FINAL, UTF8, "{:0,0,0,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("object requires string member names (4)", Standard, "{7:1}", FINAL, UTF8, "{:0,0,0,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("object requires string member names (5)", Standard, "{[]:1}", FINAL, UTF8, "{:0,0,0,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("object requires string member names (6)", Standard, "{{}:1}", FINAL, UTF8, "{:0,0,0,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("object member requires value (1)", Standard, "{\"x\"}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 !(UnexpectedToken):4,0,4,1")
PARSE_TEST("object member requires value (2)", Standard, "{\"x\":}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 !(UnexpectedToken):5,0,5,1")
PARSE_TEST("object member missing (1)", Standard, "{,\"y\":2}", FINAL, UTF8, "{:0,0,0,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("object member missing (2)", Standard, "{\"x\":1,,\"y\":2}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 !(UnexpectedToken):7,0,7,1")
PARSE_TEST("object member missing (3)", Standard, "{\"x\":1,}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 !(UnexpectedToken):7,0,7,1")
PARSE_TEST("object members require comma separator", Standard, "{\"x\":1 \"y\":2}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 !(UnexpectedToken):7,0,7,1")
PARSE_TEST("object members must be unique (1)", TrackObjectMembers, "{\"x\":1,\"x\":2}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 !(DuplicateObjectMember):7,0,7,1")
PARSE_TEST("object members must be unique (2)", TrackObjectMembers, "{\"x\":1,\"y\":2,\"x\":3}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 m(79):7,0,7,1 #(2):11,0,11,1 !(DuplicateObjectMember):13,0,13,1")
PARSE_TEST("object members must be unique (3)", TrackObjectMembers, "{\"x\":1,\"y\":{\"TRUE\":true,\"FALSE\":false},\"x\":3}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 m(79):7,0,7,1 {:11,0,11,1 M(54 52 55 45):12,0,12,2 t:19,0,19,2 m(46 41 4C 53 45):24,0,24,2 f:32,0,32,2 }:37,0,37,1 !(DuplicateObjectMember):39,0,39,1")
PARSE_TEST("object members must be unique (4)", TrackObjectMembers, "{\"x\":1,\"y\":{\"TRUE\":true,\"TRUE\":true},\"z\":3}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 m(79):7,0,7,1 {:11,0,11,1 M(54 52 55 45):12,0,12,2 t:19,0,19,2 !(DuplicateObjectMember):24,0,24,2")
PARSE_TEST("object members must be unique (5)", TrackObjectMembers, "{\"x\":1,\"y\":2,\"y\":3}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 m(79):7,0,7,1 #(2):11,0,11,1 !(DuplicateObjectMember):13,0,13,1")
PARSE_TEST("allow duplicate object members (1)", Standard, "{\"x\":1,\"x\":2}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 m(78):7,0,7,1 #(2):11,0,11,1 }:12,0,12,0")
PARSE_TEST("allow duplicate object members (2)", Standard, "{\"x\":1,\"y\":2,\"x\":3}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 m(79):7,0,7,1 #(2):11,0,11,1 m(78):13,0,13,1 #(3):17,0,17,1 }:18,0,18,0")
PARSE_TEST("allow duplicate object members (3)", Standard, "{\"x\":1,\"y\":{\"TRUE\":true,\"FALSE\":false},\"x\":3}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 m(79):7,0,7,1 {:11,0,11,1 M(54 52 55 45):12,0,12,2 t:19,0,19,2 m(46 41 4C 53 45):24,0,24,2 f:32,0,32,2 }:37,0,37,1 m(78):39,0,39,1 #(3):43,0,43,1 }:44,0,44,0")
PARSE_TEST("allow duplicate object members (4)", Standard, "{\"x\":1,\"y\":{\"TRUE\":true,\"TRUE\":true},\"z\":3}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 m(79):7,0,7,1 {:11,0,11,1 M(54 52 55 45):12,0,12,2 t:19,0,19,2 m(54 52 55 45):24,0,24,2 t:31,0,31,2 }:35,0,35,1 m(7A):37,0,37,1 #(3):41,0,41,1 }:42,0,42,0")
PARSE_TEST("allow duplicate object members (5)", Standard, "{\"x\":1,\"y\":2,\"y\":3}", FINAL, UTF8, "{:0,0,0,0 M(78):1,0,1,1 #(1):5,0,5,1 m(79):7,0,7,1 #(2):11,0,11,1 m(79):13,0,13,1 #(3):17,0,17,1 }:18,0,18,0")
PARSE_TEST("detect duplicate object member in callback", Standard, "{\"duplicate\":0}", FINAL, UTF8, "{:0,0,0,0 !(DuplicateObjectMember):1,0,1,1")
PARSE_TEST("empty string object member name (1)", Standard, "{\"\":0}", FINAL, UTF8, "{:0,0,0,0 M():1,0,1,1 #(0):4,0,4,1 }:5,0,5,0")
PARSE_TEST("empty string object member name (2)", TrackObjectMembers, "{\"\":0}", FINAL, UTF8, "{:0,0,0,0 M():1,0,1,1 #(0):4,0,4,1 }:5,0,5,0")
PARSE_TEST("empty string object member name (3)", TrackObjectMembers, "{\"\":0,\"x\":1}", FINAL, UTF8, "{:0,0,0,0 M():1,0,1,1 #(0):4,0,4,1 m(78):6,0,6,1 #(1):10,0,10,1 }:11,0,11,0")
PARSE_TEST("empty string object member name (4)", TrackObjectMembers, "{\"\":0,\"\":1}", FINAL, UTF8, "{:0,0,0,0 M():1,0,1,1 #(0):4,0,4,1 !(DuplicateObjectMember):6,0,6,1")

/* arrays */

PARSE_TEST("start array", UTF8In, "[", PARTIAL, UTF8, "[:0,0,0,0")
PARSE_TEST("empty array", Standard, "[]", FINAL, UTF8, "[:0,0,0,0 ]:1,0,1,0")
PARSE_TEST("single-item array", Standard, "[ 3.14159 ]", FINAL, UTF8, "[:0,0,0,0 I:2,0,2,1 #(3.14159):2,0,2,1 ]:10,0,10,0")
PARSE_TEST("multi-item array", Standard, "[ 3.14159, 2.71828 ]", FINAL, UTF8, "[:0,0,0,0 I:2,0,2,1 #(3.14159):2,0,2,1 i:11,0,11,1 #(2.71828):11,0,11,1 ]:19,0,19,0")
PARSE_TEST("all types of array items", AllowSpecialNumbers | AllowHexNumbers, "[ null, true, \"foo\", 17, NaN, 0xbeef, {}, [] ]", FINAL, UTF8, "[:0,0,0,0 I:2,0,2,1 n:2,0,2,1 i:8,0,8,1 t:8,0,8,1 i:14,0,14,1 s(66 6F 6F):14,0,14,1 i:21,0,21,1 #(17):21,0,21,1 i:25,0,25,1 #(NaN):25,0,25,1 i:30,0,30,1 #(0xbeef):30,0,30,1 i:38,0,38,1 {:38,0,38,1 }:39,0,39,1 i:42,0,42,1 [:42,0,42,1 ]:43,0,43,1 ]:45,0,45,0")
PARSE_TEST("nested arrays", Standard, "[[],[[],[[],[[],[[],[]]]]]]", FINAL, UTF8, "[:0,0,0,0 I:1,0,1,1 [:1,0,1,1 ]:2,0,2,1 i:4,0,4,1 [:4,0,4,1 I:5,0,5,2 [:5,0,5,2 ]:6,0,6,2 i:8,0,8,2 [:8,0,8,2 I:9,0,9,3 [:9,0,9,3 ]:10,0,10,3 i:12,0,12,3 [:12,0,12,3 I:13,0,13,4 [:13,0,13,4 ]:14,0,14,4 i:16,0,16,4 [:16,0,16,4 I:17,0,17,5 [:17,0,17,5 ]:18,0,18,5 i:20,0,20,5 [:20,0,20,5 ]:21,0,21,5 ]:22,0,22,4 ]:23,0,23,3 ]:24,0,24,2 ]:25,0,25,1 ]:26,0,26,0")
PARSE_TEST("array truncated after left square brace", Standard, "[", FINAL, UTF8, "[:0,0,0,0 !(ExpectedMoreTokens):1,0,1,1")
PARSE_TEST("array truncated after item value (1)", Standard, "[1", FINAL, UTF8, "[:0,0,0,0 I:1,0,1,1 #(1):1,0,1,1 !(ExpectedMoreTokens):2,0,2,1")
PARSE_TEST("array truncated after item value (2)", Standard, "[1,2", FINAL, UTF8, "[:0,0,0,0 I:1,0,1,1 #(1):1,0,1,1 i:3,0,3,1 #(2):3,0,3,1 !(ExpectedMoreTokens):4,0,4,1")
PARSE_TEST("array truncated after comma (1)", Standard, "[1,", FINAL, UTF8, "[:0,0,0,0 I:1,0,1,1 #(1):1,0,1,1 !(ExpectedMoreTokens):3,0,3,1")
PARSE_TEST("array truncated after comma (2)", Standard, "[1,2,", FINAL, UTF8, "[:0,0,0,0 I:1,0,1,1 #(1):1,0,1,1 i:3,0,3,1 #(2):3,0,3,1 !(ExpectedMoreTokens):5,0,5,1")
PARSE_TEST("array item missing (1)", Standard, "[,2]", FINAL, UTF8, "[:0,0,0,0 !(UnexpectedToken):1,0,1,1")
PARSE_TEST("array item missing (2)", Standard, "[1,,2]", FINAL, UTF8, "[:0,0,0,0 I:1,0,1,1 #(1):1,0,1,1 !(UnexpectedToken):3,0,3,1")
PARSE_TEST("array item missing (3)", Standard, "[1,]", FINAL, UTF8, "[:0,0,0,0 I:1,0,1,1 #(1):1,0,1,1 !(UnexpectedToken):3,0,3,1")
PARSE_TEST("array items require comma separator", Standard, "[1 2]", FINAL, UTF8, "[:0,0,0,0 I:1,0,1,1 #(1):1,0,1,1 !(UnexpectedToken):3,0,3,1")

/* comments */

PARSE_TEST("single-line comment not allowed (1)", Standard, "0 // comment", FINAL, UTF8, "#(0):0,0,0,0 !(UnknownToken):2,0,2,0")
PARSE_TEST("single-line comment not allowed (2)", Standard, "// comment\r\n0", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("multi-line comment not allowed (1)", Standard, "0 /* comment */", FINAL, UTF8, "#(0):0,0,0,0 !(UnknownToken):2,0,2,0")
PARSE_TEST("multi-line comment not allowed (2)", Standard, "/* comment */0", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("multi-line comment not allowed (3)", Standard, "/* comment \r\n * / * /*/0", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("multi-line comment not allowed (4)", Standard, "/* comment \r\n * / * /*/\r\n0", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("single-line comment (1)", AllowComments, "0 //", FINAL, UTF8, "#(0):0,0,0,0")
PARSE_TEST("single-line comment (2)", AllowComments, "0 // comment", FINAL, UTF8, "#(0):0,0,0,0")
PARSE_TEST("single-line comment (3)", AllowComments, "// comment\r\n0", FINAL, UTF8, "#(0):12,1,0,0")
PARSE_TEST("single-line comment with extra slashes", AllowComments, "0 ////////////", FINAL, UTF8, "#(0):0,0,0,0")
PARSE_TEST("single-line comment in object (1)", AllowComments, "{// comment\n\"a\":0}", FINAL, UTF8, "{:0,0,0,0 M(61):12,1,0,1 #(0):16,1,4,1 }:17,1,5,0")
PARSE_TEST("single-line comment in object (2)", AllowComments, "{\"a\"// comment\n:0}", FINAL, UTF8, "{:0,0,0,0 M(61):1,0,1,1 #(0):16,1,1,1 }:17,1,2,0")
PARSE_TEST("single-line comment in object (3)", AllowComments, "{\"a\":// comment\n0}", FINAL, UTF8, "{:0,0,0,0 M(61):1,0,1,1 #(0):16,1,0,1 }:17,1,1,0")
PARSE_TEST("single-line comment in object (4)", AllowComments, "{\"a\":0// comment\n}", FINAL, UTF8, "{:0,0,0,0 M(61):1,0,1,1 #(0):5,0,5,1 }:17,1,0,0")
PARSE_TEST("single-line comment in object (5)", AllowComments, "{\"a\":0// comment\n,\"b\":1}", FINAL, UTF8, "{:0,0,0,0 M(61):1,0,1,1 #(0):5,0,5,1 m(62):18,1,1,1 #(1):22,1,5,1 }:23,1,6,0")
PARSE_TEST("single-line comment in object (6)", AllowComments, "{\"a\":0,// comment\n\"b\":1}", FINAL, UTF8, "{:0,0,0,0 M(61):1,0,1,1 #(0):5,0,5,1 m(62):18,1,0,1 #(1):22,1,4,1 }:23,1,5,0")
PARSE_TEST("single-line comment in array (1)", AllowComments, "[// comment\n0]", FINAL, UTF8, "[:0,0,0,0 I:12,1,0,1 #(0):12,1,0,1 ]:13,1,1,0")
PARSE_TEST("single-line comment in array (2)", AllowComments, "[0// comment\n]", FINAL, UTF8, "[:0,0,0,0 I:1,0,1,1 #(0):1,0,1,1 ]:13,1,0,0")
PARSE_TEST("single-line comment in array (3)", AllowComments, "[0// comment\n,1]", FINAL, UTF8, "[:0,0,0,0 I:1,0,1,1 #(0):1,0,1,1 i:14,1,1,1 #(1):14,1,1,1 ]:15,1,2,0")
PARSE_TEST("single-line comment in array (4)", AllowComments, "[0,// comment\n1]", FINAL, UTF8, "[:0,0,0,0 I:1,0,1,1 #(0):1,0,1,1 i:14,1,0,1 #(1):14,1,0,1 ]:15,1,1,0")
PARSE_TEST("multi-line comment (1)", AllowComments, "0 /**/", FINAL, UTF8, "#(0):0,0,0,0")
PARSE_TEST("multi-line comment (2)", AllowComments, "0 /* comment */", FINAL, UTF8, "#(0):0,0,0,0")
PARSE_TEST("multi-line comment (3)", AllowComments, "/* comment */0", FINAL, UTF8, "#(0):13,0,13,0")
PARSE_TEST("multi-line comment (4)", AllowComments, "/* comment \r\n * / * /*/0", FINAL, UTF8, "#(0):23,1,10,0")
PARSE_TEST("multi-line comment (5)", AllowComments, "/* comment \r\n * / * /*/\r\n0", FINAL, UTF8, "#(0):25,2,0,0")
PARSE_TEST("multi-line comment with extra stars", AllowComments, "0 /************/", FINAL, UTF8, "#(0):0,0,0,0")
PARSE_TEST("multi-line comment in object (1)", AllowComments, "{/* comment */\"a\":0}", FINAL, UTF8, "{:0,0,0,0 M(61):14,0,14,1 #(0):18,0,18,1 }:19,0,19,0")
PARSE_TEST("multi-line comment in object (2)", AllowComments, "{\"a\"/* comment */:0}", FINAL, UTF8, "{:0,0,0,0 M(61):1,0,1,1 #(0):18,0,18,1 }:19,0,19,0")
PARSE_TEST("multi-line comment in object (3)", AllowComments, "{\"a\":/* comment */0}", FINAL, UTF8, "{:0,0,0,0 M(61):1,0,1,1 #(0):18,0,18,1 }:19,0,19,0")
PARSE_TEST("multi-line comment in object (4)", AllowComments, "{\"a\":0/* comment */}", FINAL, UTF8, "{:0,0,0,0 M(61):1,0,1,1 #(0):5,0,5,1 }:19,0,19,0")
PARSE_TEST("multi-line comment in object (5)", AllowComments, "{\"a\":0/* comment */,\"b\":1}", FINAL, UTF8, "{:0,0,0,0 M(61):1,0,1,1 #(0):5,0,5,1 m(62):20,0,20,1 #(1):24,0,24,1 }:25,0,25,0")
PARSE_TEST("multi-line comment in object (6)", AllowComments, "{\"a\":0,/* comment */\"b\":1}", FINAL, UTF8, "{:0,0,0,0 M(61):1,0,1,1 #(0):5,0,5,1 m(62):20,0,20,1 #(1):24,0,24,1 }:25,0,25,0")
PARSE_TEST("multi-line comment in array (1)", AllowComments, "[/* comment */0]", FINAL, UTF8, "[:0,0,0,0 I:14,0,14,1 #(0):14,0,14,1 ]:15,0,15,0")
PARSE_TEST("multi-line comment in array (2)", AllowComments, "[0/* comment */]", FINAL, UTF8, "[:0,0,0,0 I:1,0,1,1 #(0):1,0,1,1 ]:15,0,15,0")
PARSE_TEST("multi-line comment in array (3)", AllowComments, "[0/* comment */,1]", FINAL, UTF8, "[:0,0,0,0 I:1,0,1,1 #(0):1,0,1,1 i:16,0,16,1 #(1):16,0,16,1 ]:17,0,17,0")
PARSE_TEST("multi-line comment in array (4)", AllowComments, "[0,/* comment */1]", FINAL, UTF8, "[:0,0,0,0 I:1,0,1,1 #(0):1,0,1,1 i:16,0,16,1 #(1):16,0,16,1 ]:17,0,17,0")
PARSE_TEST("unclosed multi-line comment (1)", AllowComments, "/*", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("unclosed multi-line comment (2)", AllowComments, "/* comment", FINAL, UTF8, "!(IncompleteToken):0,0,0,0")
PARSE_TEST("just a comment (1)", AllowComments, "//", FINAL, UTF8, "!(ExpectedMoreTokens):2,0,2,0")
PARSE_TEST("just a comment (2)", AllowComments, "/**/", FINAL, UTF8, "!(ExpectedMoreTokens):4,0,4,0")
PARSE_TEST("comment between tokens (1)", AllowComments, "[//\n]", FINAL, UTF8, "[:0,0,0,0 ]:4,1,0,0")
PARSE_TEST("comment between tokens (2)", AllowComments, "[/**/]", FINAL, UTF8, "[:0,0,0,0 ]:5,0,5,0")
PARSE_TEST("lone forward slash (1)", AllowComments, "/", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("lone forward slash (2)", AllowComments, "/ ", FINAL, UTF8, "!(UnknownToken):0,0,0,0")

/* random tokens */

PARSE_TEST("random ]", Standard, "]", FINAL, UTF8, "!(UnexpectedToken):0,0,0,0")
PARSE_TEST("random }", Standard, "}", FINAL, UTF8, "!(UnexpectedToken):0,0,0,0")
PARSE_TEST("random :", Standard, ":", FINAL, UTF8, "!(UnexpectedToken):0,0,0,0")
PARSE_TEST("random ,", Standard, ",", FINAL, UTF8, "!(UnexpectedToken):0,0,0,0")
PARSE_TEST("single-quoted strings not allowed", Standard, "'abc'", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("random \\", Standard, "\\n", FINAL, UTF8, "!(UnknownToken):0,0,0,0")
PARSE_TEST("random /", Standard, "/", FINAL, UTF8, "!(UnknownToken):0,0,0,0")

/* multi-line input */

PARSE_TEST("multi-line input", Standard, "[\r 1,\n  2,\r\n\r\n   3]", FINAL, UTF8, "[:0,0,0,0 I:3,1,1,1 #(1):3,1,1,1 i:8,2,2,1 #(2):8,2,2,1 i:17,4,3,1 #(3):17,4,3,1 ]:18,4,4,0")
PARSE_TEST("multi-line input error (1)", Standard, "[\r1", FINAL, UTF8, "[:0,0,0,0 I:2,1,0,1 #(1):2,1,0,1 !(ExpectedMoreTokens):3,1,1,1")
PARSE_TEST("multi-line input error (2)", Standard, "[\n1", FINAL, UTF8, "[:0,0,0,0 I:2,1,0,1 #(1):2,1,0,1 !(ExpectedMoreTokens):3,1,1,1")
PARSE_TEST("multi-line input error (3)", Standard, "[\r\n1", FINAL, UTF8, "[:0,0,0,0 I:3,1,0,1 #(1):3,1,0,1 !(ExpectedMoreTokens):4,1,1,1")
PARSE_TEST("multi-line input error (4)", Standard, "[\r1,\n2\r\n", FINAL, UTF8, "[:0,0,0,0 I:2,1,0,1 #(1):2,1,0,1 i:5,2,0,1 #(2):5,2,0,1 !(ExpectedMoreTokens):8,3,0,1")
PARSE_TEST("multi-line input error (5)", Standard, "[\r\"x\n", FINAL, UTF8, "[:0,0,0,0 !(UnescapedControlCharacter):4,1,2,1")
PARSE_TEST("multi-line input error (6)", Standard, "[\n\"x\n", FINAL, UTF8, "[:0,0,0,0 !(UnescapedControlCharacter):4,1,2,1")
PARSE_TEST("multi-line input error (7)", Standard, "[\r\n\"x\r\n", FINAL, UTF8, "[:0,0,0,0 !(UnescapedControlCharacter):5,1,2,1")

};

static void TestParse()
{
    size_t i;
    for  (i = 0; i < sizeof(s_parseTests)/sizeof(s_parseTests[0]); i++)
    {
        RunParseTest(&s_parseTests[i]);
    }
}

static void TestNoLeaks()
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

    TestParserCreation();
    TestParserCreationWithCustomMemorySuite();
    TestParserCreationMallocFailure();
    TestMissingParser();
    TestGetErrorLocationNullLocation();
    TestGetErrorLocationNoError();
    TestGetTokenLocationOutsideHandler();
    TestSetParserSettings();
    TestSetInvalidParserSettings();
    TestSetParserHandlers();
    TestResetParser();
    TestMisbehaveInCallbacks();
    TestAbortInCallbacks();
    TestStringMallocFailure();
    TestStringReallocFailure();
    TestStackMallocFailure();
    TestStackReallocFailure();
    TestDuplicateMemberTrackingMallocFailure();
    TestErrorStrings();
    TestIEEE754NumberInterpretation();
    TestParse();
    TestNoLeaks();
    if (!s_failureCount)
    {
        printf("All tests passed.\n");
    }
    return s_failureCount;
}
