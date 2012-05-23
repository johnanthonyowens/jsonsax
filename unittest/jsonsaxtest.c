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
    pState->startedParsing = JSON_False;
    pState->finishedParsing = JSON_False;
    pState->inputEncoding = JSON_UnknownEncoding;
}

static void GetParserState(JSON_Parser parser, ParserState* pState)
{
    pState->error = JSON_GetError(parser);
    if (JSON_GetErrorLocation(parser, &pState->errorLocation) != JSON_Success)
    {
        pState->errorLocation.byte = 0;
        pState->errorLocation.line = 0;
        pState->errorLocation.column = 0;
    }
    pState->startedParsing = JSON_StartedParsing(parser);
    pState->finishedParsing = JSON_FinishedParsing(parser);
    pState->inputEncoding = JSON_GetInputEncoding(parser);
}

static int ParserStatesAreIdentical(const ParserState* pState1, const ParserState* pState2)
{
    return (pState1->error == pState2->error &&
            pState1->errorLocation.byte == pState2->errorLocation.byte &&
            pState1->errorLocation.line == pState2->errorLocation.line &&
            pState1->errorLocation.column == pState2->errorLocation.column &&
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
               "  STATE                          EXPECTED     ACTUAL\n"
               "  JSON_GetError()                %8d   %8d\n"
               "  JSON_GetErrorLocation().byte   %8d   %8d\n"
               "  JSON_GetErrorLocation().line   %8d   %8d\n"
               "  JSON_GetErrorLocation().column %8d   %8d\n"
               "  JSON_StartedParsing()          %8d   %8d\n"
               "  JSON_FinishedParsing()         %8d   %8d\n"
               "  JSON_GetInputEncoding()        %8d   %8d\n"
               ,
               (int)pExpectedState->error, (int)actualState.error,
               (int)pExpectedState->errorLocation.byte, (int)actualState.errorLocation.byte,
               (int)pExpectedState->errorLocation.line, (int)actualState.errorLocation.line,
               (int)pExpectedState->errorLocation.column, (int)actualState.errorLocation.column,
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
    JSON_Boolean  allowTrailingCommas;
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
    pSettings->allowTrailingCommas = JSON_False;
    pSettings->allowSpecialNumbers = JSON_False;
    pSettings->allowHexNumbers = JSON_False;
    pSettings->replaceInvalidEncodingSequences = JSON_False;
    pSettings->trackObjectMembers = JSON_False;
}

static void GetParserSettings(JSON_Parser parser, ParserSettings* pSettings)
{
    pSettings->userData = JSON_GetUserData(parser);
    pSettings->inputEncoding = JSON_GetInputEncoding(parser);
    pSettings->outputEncoding = JSON_GetOutputEncoding(parser);
    pSettings->maxOutputStringLength = JSON_GetMaxOutputStringLength(parser);
    pSettings->allowBOM = JSON_GetAllowBOM(parser);
    pSettings->allowComments = JSON_GetAllowComments(parser);
    pSettings->allowTrailingCommas = JSON_GetAllowTrailingCommas(parser);
    pSettings->allowSpecialNumbers = JSON_GetAllowSpecialNumbers(parser);
    pSettings->allowHexNumbers = JSON_GetAllowHexNumbers(parser);
    pSettings->replaceInvalidEncodingSequences = JSON_GetReplaceInvalidEncodingSequences(parser);
    pSettings->trackObjectMembers = JSON_GetTrackObjectMembers(parser);
}

static int ParserSettingsAreIdentical(const ParserSettings* pSettings1, const ParserSettings* pSettings2)
{
    return (pSettings1->userData == pSettings2->userData &&
            pSettings1->inputEncoding == pSettings2->inputEncoding &&
            pSettings1->outputEncoding == pSettings2->outputEncoding &&
            pSettings1->maxOutputStringLength == pSettings2->maxOutputStringLength &&
            pSettings1->allowBOM == pSettings2->allowBOM &&
            pSettings1->allowComments == pSettings2->allowComments &&
            pSettings1->allowTrailingCommas == pSettings2->allowTrailingCommas &&
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
               "  SETTINGS                               EXPECTED     ACTUAL\n"
               "  JSON_GetUserData()                     %8p   %8p\n"
               "  JSON_GetInputEncoding()                %8d   %8d\n"
               "  JSON_GetOutputEncoding()               %8d   %8d\n"
               "  JSON_GetMaxOutputStringLength()        %8d   %8d\n"
               ,
               pExpectedSettings->userData, actualSettings.userData,
               (int)pExpectedSettings->inputEncoding, (int)actualSettings.inputEncoding,
               (int)pExpectedSettings->outputEncoding, (int)actualSettings.outputEncoding,
               (int)pExpectedSettings->maxOutputStringLength, (int)actualSettings.maxOutputStringLength
            );
        printf("  JSON_GetAllowBOM()                     %8d   %8d\n"
               "  JSON_GetAllowComments()                %8d   %8d\n"
               "  JSON_GetAllowTrailingCommas()          %8d   %8d\n"
               "  JSON_GetAllowSpecialNumbers()          %8d   %8d\n"
               "  JSON_GetAllowHexNumbers()              %8d   %8d\n"
               "  JSON_ReplaceInvalidEncodingSequences() %8d   %8d\n"
               "  JSON_GetTrackObjectMembers()           %8d   %8d\n"
               ,
               (int)pExpectedSettings->allowBOM, (int)actualSettings.allowBOM,
               (int)pExpectedSettings->allowComments, (int)actualSettings.allowComments,
               (int)pExpectedSettings->allowTrailingCommas, (int)actualSettings.allowTrailingCommas,
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
    JSON_NullHandler          nullHandler;
    JSON_BooleanHandler       booleanHandler;
    JSON_StringHandler        stringHandler;
    JSON_NumberHandler        numberHandler;
    JSON_RawNumberHandler     rawNumberHandler;
    JSON_SpecialNumberHandler specialNumberHandler;
    JSON_StartObjectHandler   startObjectHandler;
    JSON_EndObjectHandler     endObjectHandler;
    JSON_ObjectMemberHandler  objectMemberHandler;
    JSON_StartArrayHandler    startArrayHandler;
    JSON_EndArrayHandler      endArrayHandler;
    JSON_ArrayItemHandler     arrayItemHandler;
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
    pHandlers->nullHandler = JSON_GetNullHandler(parser);
    pHandlers->booleanHandler = JSON_GetBooleanHandler(parser);
    pHandlers->stringHandler = JSON_GetStringHandler(parser);
    pHandlers->numberHandler = JSON_GetNumberHandler(parser);
    pHandlers->rawNumberHandler = JSON_GetRawNumberHandler(parser);
    pHandlers->specialNumberHandler = JSON_GetSpecialNumberHandler(parser);
    pHandlers->startObjectHandler = JSON_GetStartObjectHandler(parser);
    pHandlers->endObjectHandler = JSON_GetEndObjectHandler(parser);
    pHandlers->objectMemberHandler = JSON_GetObjectMemberHandler(parser);
    pHandlers->startArrayHandler = JSON_GetStartArrayHandler(parser);
    pHandlers->endArrayHandler = JSON_GetEndArrayHandler(parser);
    pHandlers->arrayItemHandler = JSON_GetArrayItemHandler(parser);
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
               "  HANDLERS                      EXPECTED     ACTUAL\n"
               "  JSON_GetNullHandler()         %8s   %8s\n"
               "  JSON_GetBooleanHandler()      %8s   %8s\n"
               "  JSON_GetStringHandler()       %8s   %8s\n"
               "  JSON_GetNumberHandler()       %8s   %8s\n"
               "  JSON_GetRawNumberHandler()    %8s   %8s\n"
               "  JSON_GetSpecialNumberHandler()%8s   %8s\n"
               ,
               HANDLER_STRING(pExpectedHandlers->nullHandler), HANDLER_STRING(actualHandlers.nullHandler),
               HANDLER_STRING(pExpectedHandlers->booleanHandler), HANDLER_STRING(actualHandlers.booleanHandler),
               HANDLER_STRING(pExpectedHandlers->stringHandler), HANDLER_STRING(actualHandlers.stringHandler),
               HANDLER_STRING(pExpectedHandlers->numberHandler), HANDLER_STRING(actualHandlers.numberHandler),
               HANDLER_STRING(pExpectedHandlers->rawNumberHandler), HANDLER_STRING(actualHandlers.rawNumberHandler),
               HANDLER_STRING(pExpectedHandlers->specialNumberHandler), HANDLER_STRING(actualHandlers.specialNumberHandler)
            );
        printf("  JSON_GetStartObjectHandler()  %8s   %8s\n"
               "  JSON_GetEndObjectHandler()    %8s   %8s\n"
               "  JSON_GetObjectMemberHandler() %8s   %8s\n"
               "  JSON_GetStartArrayHandler()   %8s   %8s\n"
               "  JSON_GetEndArrayHandler()     %8s   %8s\n"
               "  JSON_GetArrayItemHandler()    %8s   %8s\n"
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
    *pParser = JSON_CreateParser(pMemorySuite);
    if (expectedStatus == JSON_Success && !*pParser)
    {
        printf("FAILURE: expected JSON_CreateParser() to return a parser instance\n");
        return 0;
    }
    if (expectedStatus == JSON_Failure && *pParser)
    {
        printf("FAILURE: expected JSON_CreateParser() to return NULL\n");
        JSON_FreeParser(*pParser);
        *pParser = NULL;
        return 0;
    }
    return 1;
}

static int CheckCreateParserWithCustomMemorySuite(JSON_MallocHandler m, JSON_ReallocHandler r, JSON_FreeHandler f, JSON_Status expectedStatus, JSON_Parser* pParser)
{
    JSON_MemorySuite memorySuite;
    memorySuite.malloc = m;
    memorySuite.realloc = r;
    memorySuite.free = f;
    return CheckCreateParser(&memorySuite, expectedStatus, pParser);
}

static int CheckResetParser(JSON_Parser parser, JSON_Status expectedStatus)
{
    if (JSON_ResetParser(parser) != expectedStatus)
    {
        printf("FAILURE: expected JSON_ResetParser() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckFreeParser(JSON_Parser parser, JSON_Status expectedStatus)
{
    if (JSON_FreeParser(parser) != expectedStatus)
    {
        printf("FAILURE: expected JSON_FreeParser() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckGetErrorLocation(JSON_Parser parser, JSON_Location* pLocation, JSON_Status expectedStatus)
{
    if (JSON_GetErrorLocation(parser, pLocation) != expectedStatus)
    {
        printf("FAILURE: expected JSON_GetErrorLocation() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckGetTokenLocation(JSON_Parser parser, JSON_Location* pLocation, JSON_Status expectedStatus)
{
    if (JSON_GetTokenLocation(parser, pLocation) != expectedStatus)
    {
        printf("FAILURE: expected JSON_GetTokenLocation() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetUserData(JSON_Parser parser, void* userData, JSON_Status expectedStatus)
{
    if (JSON_SetUserData(parser, userData) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetUserData() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetInputEncoding(JSON_Parser parser, JSON_Encoding encoding, JSON_Status expectedStatus)
{
    if (JSON_SetInputEncoding(parser, encoding) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetInputEncoding() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetOutputEncoding(JSON_Parser parser, JSON_Encoding encoding, JSON_Status expectedStatus)
{
    if (JSON_SetOutputEncoding(parser, encoding) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetOutputEncoding() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetMaxOutputStringLength(JSON_Parser parser, size_t maxLength, JSON_Status expectedStatus)
{
    if (JSON_SetMaxOutputStringLength(parser, maxLength) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetMaxOutputStringLength() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetAllowBOM(JSON_Parser parser, JSON_Boolean allowBOM, JSON_Status expectedStatus)
{
    if (JSON_SetAllowBOM(parser, allowBOM) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetAllowBOM() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetAllowComments(JSON_Parser parser, JSON_Boolean allowComments, JSON_Status expectedStatus)
{
    if (JSON_SetAllowComments(parser, allowComments) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetAllowComments() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetAllowTrailingCommas(JSON_Parser parser, JSON_Boolean allowTrailingCommas, JSON_Status expectedStatus)
{
    if (JSON_SetAllowTrailingCommas(parser, allowTrailingCommas) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetAllowTrailingCommas() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetAllowSpecialNumbers(JSON_Parser parser, JSON_Boolean allowSpecialNumbers, JSON_Status expectedStatus)
{
    if (JSON_SetAllowSpecialNumbers(parser, allowSpecialNumbers) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetAllowSpecialNumbers() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetAllowHexNumbers(JSON_Parser parser, JSON_Boolean allowHexNumbers, JSON_Status expectedStatus)
{
    if (JSON_SetAllowHexNumbers(parser, allowHexNumbers) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetAllowHexNumbers() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetReplaceInvalidEncodingSequences(JSON_Parser parser, JSON_Boolean replaceInvalidEncodingSequences, JSON_Status expectedStatus)
{
    if (JSON_SetReplaceInvalidEncodingSequences(parser, replaceInvalidEncodingSequences) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetReplaceInvalidEncodingSequences() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetTrackObjectMembers(JSON_Parser parser, JSON_Boolean trackObjectMembers, JSON_Status expectedStatus)
{
    if (JSON_SetTrackObjectMembers(parser, trackObjectMembers) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetTrackObjectMembers() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetNullHandler(JSON_Parser parser, JSON_NullHandler handler, JSON_Status expectedStatus)
{
    if (JSON_SetNullHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetNullHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetBooleanHandler(JSON_Parser parser, JSON_BooleanHandler handler, JSON_Status expectedStatus)
{
    if (JSON_SetBooleanHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetBooleanHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetStringHandler(JSON_Parser parser, JSON_StringHandler handler, JSON_Status expectedStatus)
{
    if (JSON_SetStringHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetStringHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetNumberHandler(JSON_Parser parser, JSON_NumberHandler handler, JSON_Status expectedStatus)
{
    if (JSON_SetNumberHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetNumberHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetRawNumberHandler(JSON_Parser parser, JSON_RawNumberHandler handler, JSON_Status expectedStatus)
{
    if (JSON_SetRawNumberHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetRawNumberHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetSpecialNumberHandler(JSON_Parser parser, JSON_SpecialNumberHandler handler, JSON_Status expectedStatus)
{
    if (JSON_SetSpecialNumberHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetSpecialNumberHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetStartObjectHandler(JSON_Parser parser, JSON_StartObjectHandler handler, JSON_Status expectedStatus)
{
    if (JSON_SetStartObjectHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetStartObjectHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetEndObjectHandler(JSON_Parser parser, JSON_EndObjectHandler handler, JSON_Status expectedStatus)
{
    if (JSON_SetEndObjectHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetEndObjectHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetObjectMemberHandler(JSON_Parser parser, JSON_ObjectMemberHandler handler, JSON_Status expectedStatus)
{
    if (JSON_SetObjectMemberHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetObjectMemberHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetStartArrayHandler(JSON_Parser parser, JSON_StartArrayHandler handler, JSON_Status expectedStatus)
{
    if (JSON_SetStartArrayHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetStartArrayHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetEndArrayHandler(JSON_Parser parser, JSON_EndArrayHandler handler, JSON_Status expectedStatus)
{
    if (JSON_SetEndArrayHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetEndArrayHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckSetArrayItemHandler(JSON_Parser parser, JSON_ArrayItemHandler handler, JSON_Status expectedStatus)
{
    if (JSON_SetArrayItemHandler(parser, handler) != expectedStatus)
    {
        printf("FAILURE: expected JSON_SetArrayItemHandler() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
        return 0;
    }
    return 1;
}

static int CheckParse(JSON_Parser parser, const char* pBytes, size_t length, JSON_Boolean isFinal, JSON_Status expectedStatus)
{
    if (JSON_Parse(parser, pBytes, length, isFinal) != expectedStatus)
    {
        printf("FAILURE: expected JSON_Parse() to return %s\n", (expectedStatus == JSON_Success) ? "JSON_Success" : "JSON_Failure");
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

static void* JSON_CALL MallocHandler(JSON_Parser parser, size_t size)
{
    size_t* pBlock = NULL;
    (void)parser; /* unused */
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

static void* JSON_CALL ReallocHandler(JSON_Parser parser, void* ptr, size_t size)
{
    size_t* pBlock = NULL;
    (void)parser; /* unused */
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

static void JSON_CALL FreeHandler(JSON_Parser parser, void* ptr)
{
    (void)parser; /* unused */
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

static const char s_hexDigits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
static void OutputBytes(const char* pBytes, size_t length)
{
    size_t i;
    for (i = 0; i < length; i++)
    {
        unsigned char b = (unsigned char)pBytes[i];
        if (i)
        {
            s_outputBuffer[s_outputLength] = ' ';
            s_outputLength++;
        }
        s_outputBuffer[s_outputLength] = s_hexDigits[b >> 4];
        s_outputBuffer[s_outputLength + 1] = s_hexDigits[b & 0xF];
        s_outputLength += 2;
    }
    s_outputBuffer[s_outputLength] = 0;
}

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
        !CheckSetAllowTrailingCommas(parser, JSON_True, JSON_Failure) ||
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

static JSON_HandlerResult JSON_CALL NullHandler(JSON_Parser parser)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_AbortParsing;
    }
    if (JSON_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_AbortParsing;
    }
    OutputFormatted("null:%d,%d,%d;", (int)location.byte, (int)location.line, (int)location.column);
    return JSON_ContinueParsing;
}

static JSON_HandlerResult JSON_CALL BooleanHandler(JSON_Parser parser, JSON_Boolean value)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_AbortParsing;
    }
    if (JSON_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_AbortParsing;
    }
    OutputFormatted("%s:%d,%d,%d;", (value == JSON_True) ? "true" : "false", (int)location.byte, (int)location.line, (int)location.column);
    return JSON_ContinueParsing;
}

static JSON_HandlerResult JSON_CALL StringHandler(JSON_Parser parser, const char* pBytes, size_t length, JSON_StringAttributes attributes)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_AbortParsing;
    }
    if (JSON_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_AbortParsing;
    }
    OutputFormatted("string(0x%X, ", attributes);
    OutputBytes(pBytes, length);
    OutputFormatted("):%d,%d,%d;", (int)location.byte, (int)location.line, (int)location.column);
    return JSON_ContinueParsing;
}

static JSON_HandlerResult JSON_CALL NumberHandler(JSON_Parser parser, double value)
{
    JSON_Location location;
    (void)value; /* unused */
    if (s_failParseCallback)
    {
        return JSON_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_AbortParsing;
    }
    if (JSON_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_AbortParsing;
    }
    return JSON_ContinueParsing;
}

static JSON_HandlerResult JSON_CALL RawNumberHandler(JSON_Parser parser, const char* pValue, size_t length)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_AbortParsing;
    }
    if (strlen(pValue) != length)
    {
        return JSON_AbortParsing;
    }
    if (JSON_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_AbortParsing;
    }
    OutputFormatted("#(%s):%d,%d,%d;", pValue, (int)location.byte, (int)location.line, (int)location.column);
    return JSON_ContinueParsing;
}

static JSON_HandlerResult JSON_CALL SpecialNumberHandler(JSON_Parser parser, JSON_SpecialNumber value)
{
    JSON_Location location;
    const char* pValue;
    if (s_failParseCallback)
    {
        return JSON_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_AbortParsing;
    }
    if (JSON_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_AbortParsing;
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
    OutputFormatted("%s:%d,%d,%d;", pValue, (int)location.byte, (int)location.line, (int)location.column);
    return JSON_ContinueParsing;
}

static JSON_HandlerResult JSON_CALL StartObjectHandler(JSON_Parser parser)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_AbortParsing;
    }
    if (JSON_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_AbortParsing;
    }
    OutputFormatted("{:%d,%d,%d;", (int)location.byte, (int)location.line, (int)location.column);
    return JSON_ContinueParsing;
}

static JSON_HandlerResult JSON_CALL EndObjectHandler(JSON_Parser parser)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_AbortParsing;
    }
    if (JSON_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_AbortParsing;
    }
    OutputFormatted("}:%d,%d,%d;", (int)location.byte, (int)location.line, (int)location.column);
    return JSON_ContinueParsing;
}

static JSON_HandlerResult JSON_CALL ObjectMemberHandler(JSON_Parser parser, const char* pBytes, size_t length, JSON_StringAttributes attributes)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_AbortParsing;
    }
    if (attributes == JSON_SimpleString && !strcmp(pBytes, "duplicate"))
    {
        return JSON_TreatAsDuplicateObjectMember;
    }
    if (JSON_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_AbortParsing;
    }
    OutputFormatted("member(0x%X, ", attributes);
    OutputBytes(pBytes, length);
    OutputFormatted("):%d,%d,%d;", (int)location.byte, (int)location.line, (int)location.column);
    return JSON_ContinueParsing;
}

static JSON_HandlerResult JSON_CALL StartArrayHandler(JSON_Parser parser)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_AbortParsing;
    }
    if (JSON_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_AbortParsing;
    }
    OutputFormatted("[:%d,%d,%d;", (int)location.byte, (int)location.line, (int)location.column);
    return JSON_ContinueParsing;
}

static JSON_HandlerResult JSON_CALL EndArrayHandler(JSON_Parser parser)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_AbortParsing;
    }
    if (JSON_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_AbortParsing;
    }
    OutputFormatted("]:%d,%d,%d;", (int)location.byte, (int)location.line, (int)location.column);
    return JSON_ContinueParsing;
}

static JSON_HandlerResult JSON_CALL ArrayItemHandler(JSON_Parser parser)
{
    JSON_Location location;
    if (s_failParseCallback)
    {
        return JSON_AbortParsing;
    }
    if (s_misbehaveInCallback && TryToMisbehaveInCallback(parser))
    {
        return JSON_AbortParsing;
    }
    if (JSON_GetTokenLocation(parser, &location) != JSON_Success)
    {
        return JSON_AbortParsing;
    }
    OutputFormatted("item:%d,%d,%d;", (int)location.byte, (int)location.line, (int)location.column);
    return JSON_ContinueParsing;
}

typedef JSON_Parser (*ParserFactory)();

typedef struct tag_ParseTest
{
    const char*   pName;
    ParserFactory createParser;
    const char*   pInput;
    size_t        length;
    JSON_Boolean  isFinal;
    JSON_Error    error;
    size_t        errorLocationByte;
    size_t        errorLocationLine;
    size_t        errorLocationColumn;
    JSON_Encoding inputEncoding;
    const char*   pOutput;
} ParseTest;

static void RunParseTest(const ParseTest* pTest)
{
    JSON_Parser parser = NULL;
    ParserState state;
    printf("Test %s ... ", pTest->pName);
    InitParserState(&state);
    state.error = pTest->error;
    state.errorLocation.byte = pTest->errorLocationByte;
    state.errorLocation.line = pTest->errorLocationLine;
    state.errorLocation.column = pTest->errorLocationColumn;
    state.startedParsing = JSON_True;
    state.finishedParsing = (pTest->isFinal || pTest->error != JSON_Error_None) ? JSON_True : JSON_False;
    state.inputEncoding = pTest->inputEncoding;
    parser = pTest->createParser();
    ResetOutput();
    if (parser &&
        CheckParse(parser, pTest->pInput, pTest->length, pTest->isFinal, (pTest->error == JSON_Error_None) ? JSON_Success : JSON_Failure) &&
        CheckParserState(parser, &state) &&
        (!pTest->pOutput || CheckOutput(pTest->pOutput)))
    {
        printf("OK\n");
    }
    else
    {
        s_failureCount++;
    }
    JSON_FreeParser(parser);
    ResetOutput();
}

static JSON_Parser DefaultParserFactory()
{
    JSON_Parser parser = NULL;
    JSON_MemorySuite memorySuite;
    memorySuite.malloc = &MallocHandler;
    memorySuite.realloc = &ReallocHandler;
    memorySuite.free = &FreeHandler;
    parser = JSON_CreateParser(&memorySuite);
    if (parser)
    {
        JSON_SetNullHandler(parser, &NullHandler);
        JSON_SetBooleanHandler(parser, &BooleanHandler);
        JSON_SetStringHandler(parser, &StringHandler);
        JSON_SetNumberHandler(parser, &NumberHandler);
        JSON_SetRawNumberHandler(parser, &RawNumberHandler);
        JSON_SetSpecialNumberHandler(parser, &SpecialNumberHandler);
        JSON_SetStartObjectHandler(parser, &StartObjectHandler);
        JSON_SetEndObjectHandler(parser, &EndObjectHandler);
        JSON_SetObjectMemberHandler(parser, &ObjectMemberHandler);
        JSON_SetStartArrayHandler(parser, &StartArrayHandler);
        JSON_SetEndArrayHandler(parser, &EndArrayHandler);
        JSON_SetArrayItemHandler(parser, &ArrayItemHandler);
    }
    return parser;
}

static JSON_Parser AllowBOMParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetAllowBOM(parser, JSON_True);
    return parser;
}

static JSON_Parser AllowCommentsParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetAllowComments(parser, JSON_True);
    return parser;
}

static JSON_Parser AllowTrailingCommasParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetAllowTrailingCommas(parser, JSON_True);
    return parser;
}

static JSON_Parser AllowSpecialNumbersParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetAllowSpecialNumbers(parser, JSON_True);
    return parser;
}

static JSON_Parser AllowHexNumbersParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetAllowHexNumbers(parser, JSON_True);
    return parser;
}

static JSON_Parser ReplaceInvalidEncodingSequencesParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetReplaceInvalidEncodingSequences(parser, JSON_True);
    return parser;
}

static JSON_Parser TrackObjectMembersParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetTrackObjectMembers(parser, JSON_True);
    return parser;
}

static JSON_Parser UTF8ParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetInputEncoding(parser, JSON_UTF8);
    JSON_SetOutputEncoding(parser, JSON_UTF8);
    return parser;
}

static JSON_Parser UTF16LEParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetInputEncoding(parser, JSON_UTF16LE);
    JSON_SetOutputEncoding(parser, JSON_UTF16LE);
    return parser;
}

static JSON_Parser UTF16BEParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetInputEncoding(parser, JSON_UTF16BE);
    JSON_SetOutputEncoding(parser, JSON_UTF16BE);
    return parser;
}

static JSON_Parser UTF32LEParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetInputEncoding(parser, JSON_UTF32LE);
    JSON_SetOutputEncoding(parser, JSON_UTF32LE);
    return parser;
}

static JSON_Parser UTF32BEParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetInputEncoding(parser, JSON_UTF32BE);
    JSON_SetOutputEncoding(parser, JSON_UTF32BE);
    return parser;
}

static JSON_Parser MaxStringLength0ParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetMaxOutputStringLength(parser, 0);
    return parser;
}

static JSON_Parser MaxStringLength1ParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetMaxOutputStringLength(parser, 1);
    return parser;
}

static JSON_Parser MaxStringLength2ParserFactory()
{
    JSON_Parser parser = DefaultParserFactory();
    JSON_SetMaxOutputStringLength(parser, 2);
    return parser;
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
    JSON_FreeParser(parser);
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
    JSON_FreeParser(parser);
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
    JSON_FreeParser(parser);
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
    settings.allowTrailingCommas = JSON_True;
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
        CheckSetAllowTrailingCommas(parser, settings.allowTrailingCommas, JSON_Success) &&
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
    JSON_FreeParser(parser);
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
    JSON_FreeParser(parser);
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
    JSON_FreeParser(parser);
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
        CheckSetAllowTrailingCommas(parser, JSON_True, JSON_Success) &&
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
    JSON_FreeParser(parser);
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
    JSON_FreeParser(parser);
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
        CheckSetObjectMemberHandler(parser, &ObjectMemberHandler, JSON_Success) &&
        CheckParse(parser, "{\"x\":0}", 7, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetStartArrayHandler(parser, &StartArrayHandler, JSON_Success) &&
        CheckParse(parser, " []", 3, JSON_True, JSON_Failure) &&
        CheckParserState(parser, &state) &&

        CheckResetParser(parser, JSON_Success) &&
        CheckSetEndArrayHandler(parser, &EndArrayHandler, JSON_Success) &&
        CheckParse(parser, "[]", 2, JSON_True, JSON_Failure) &&
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
    JSON_FreeParser(parser);
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
            if (JSON_Parse(parser, "a", 1, JSON_False) == JSON_Failure)
            {
                break;
            }
        }
        JSON_GetErrorLocation(parser, &state.errorLocation);
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
    JSON_FreeParser(parser);
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
            if (JSON_Parse(parser, "a", 1, JSON_False) == JSON_Failure)
            {
                break;
            }
        }
        JSON_GetErrorLocation(parser, &state.errorLocation);
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
    JSON_FreeParser(parser);
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
            if (JSON_Parse(parser, "{\"a\":", 5, JSON_False) == JSON_Failure)
            {
                break;
            }
        }
        JSON_GetErrorLocation(parser, &state.errorLocation);
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
    JSON_FreeParser(parser);
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
            if (JSON_Parse(parser, "{\"a\":", 5, JSON_False) == JSON_Failure)
            {
                break;
            }
        }
        JSON_GetErrorLocation(parser, &state.errorLocation);
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
    JSON_FreeParser(parser);
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
    JSON_FreeParser(parser);
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
    JSON_FreeParser(parser);
}

static void TestGetErrorLocationNoError()
{
    JSON_Parser parser = NULL;
    JSON_Location location = { 100, 200, 300 };
    printf("Test get error location when no error occurred ... ");
    if (CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParse(parser, "7", 1, JSON_True, JSON_Success) &&
        CheckGetErrorLocation(parser, &location, JSON_Failure))
    {
        if (location.byte != 100 || location.line != 200 || location.column != 300)
        {
            printf("FAILURE: JSON_GetErrorLocation() modified the location when it shouldn't have\n");
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
    JSON_FreeParser(parser);
}

static void TestGetTokenLocationOutsideHandler()
{
    JSON_Parser parser = NULL;
    JSON_Location location = { 100, 200, 300 };
    printf("Test get token location when not in a parse handler  ... ");
    if (CheckCreateParserWithCustomMemorySuite(&MallocHandler, &ReallocHandler, &FreeHandler, JSON_Success, &parser) &&
        CheckParse(parser, "7", 1, JSON_True, JSON_Success) &&
        CheckGetTokenLocation(parser, &location, JSON_Failure))
    {
        if (location.byte != 100 || location.line != 200 || location.column != 300)
        {
            printf("FAILURE: JSON_GetTokenLocation() modified the location when it shouldn't have\n");
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
    JSON_FreeParser(parser);
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

static JSON_HandlerResult JSON_CALL CheckIEEE754InterpretationNumberHandler(JSON_Parser parser, double value)
{
    const IEEE754Test* pTest = (const IEEE754Test*)JSON_GetUserData(parser);
    if (value != pTest->expectedValue)
    {
        printf("FAILURE: expected value to be %f instead of %f\n", pTest->expectedValue, value);
        s_failureCount++;
        return JSON_AbortParsing;
    }
    return JSON_ContinueParsing;
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
        if (JSON_Parse(parser, pTest->pInput, pTest->length, JSON_True) == JSON_Success)
        {
            printf("OK\n");
        }
    }
    else
    {
        s_failureCount++;
    }
    JSON_FreeParser(parser);
}

static void TestIEEE754NumberInterpretation()
{
    size_t i;
    for  (i = 0; i < sizeof(s_IEEE754Tests)/sizeof(s_IEEE754Tests[0]); i++)
    {
        RunIEEE754Test(&s_IEEE754Tests[i]);
    }
}

#define PARSE_SUCCESS_TEST(name, create, input, final, enc, output) { name, create, input, sizeof(input) - 1, final, JSON_Error_None, 0, 0, 0, JSON_##enc, output },
#define PARSE_FAILURE_TEST(name, create, input, final, err, byte, line, col, enc) { name, create, input, sizeof(input) - 1, final, JSON_Error_##err, byte, line, col, JSON_##enc, NULL },

#define FINAL   JSON_True
#define PARTIAL JSON_False

static const ParseTest s_parseTests[] =
{

/* input encoding detection */

PARSE_FAILURE_TEST("infer input encoding from 0 bytes", DefaultParserFactory, "", FINAL, ExpectedMoreTokens, 0, 0, 0, UnknownEncoding)
PARSE_SUCCESS_TEST("infer input encoding from 1 byte (1)", DefaultParserFactory, "7", FINAL, UTF8, "#(7):0,0,0;")
PARSE_FAILURE_TEST("infer input encoding from 1 byte (2)", DefaultParserFactory, " ", FINAL, ExpectedMoreTokens, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("infer input encoding from 1 byte (3)", DefaultParserFactory, "\xFF", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_SUCCESS_TEST("infer input encoding from 2 bytes (1)", DefaultParserFactory, "{}", FINAL, UTF8, "{:0,0,0;}:1,0,1;")
PARSE_SUCCESS_TEST("infer input encoding from 2 bytes (2)", DefaultParserFactory, "7\x00", FINAL, UTF16LE, "#(7):0,0,0;")
PARSE_SUCCESS_TEST("infer input encoding from 2 bytes (3)", DefaultParserFactory, "\x00" "7", FINAL, UTF16BE, "#(7):0,0,0;")
PARSE_FAILURE_TEST("infer input encoding from 2 bytes (4)", AllowBOMParserFactory, "\xFF\xFE", FINAL, ExpectedMoreTokens, 2, 0, 1, UTF16LE)
PARSE_FAILURE_TEST("infer input encoding from 2 bytes (5)", AllowBOMParserFactory, "\xFE\xFF", FINAL, ExpectedMoreTokens, 2, 0, 1, UTF16BE)
PARSE_FAILURE_TEST("infer input encoding from 2 bytes (6)", DefaultParserFactory, "\xFF\xFF", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_SUCCESS_TEST("infer input encoding from 3 bytes (1)", DefaultParserFactory, "{ }", FINAL, UTF8, "{:0,0,0;}:2,0,2;")
PARSE_FAILURE_TEST("infer input encoding from 3 bytes (2)", AllowBOMParserFactory, "\xEF\xBB\xBF", FINAL, ExpectedMoreTokens, 3, 0, 1, UTF8)
PARSE_FAILURE_TEST("infer input encoding from 3 bytes (3)", AllowBOMParserFactory, "\xFF\xFF\xFF", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("infer input encoding from 3 bytes (4)", DefaultParserFactory, "\xFF\xFF\xFF", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_SUCCESS_TEST("infer input encoding from 4 bytes (1)", DefaultParserFactory, "1234", FINAL, UTF8, "#(1234):0,0,0;")
PARSE_SUCCESS_TEST("infer input encoding from 4 bytes (2)", DefaultParserFactory, "   7", FINAL, UTF8, "#(7):3,0,3;")
PARSE_SUCCESS_TEST("infer input encoding from 4 bytes (3)", DefaultParserFactory, "\x00 \x00" "7", FINAL, UTF16BE, "#(7):2,0,1;")
PARSE_SUCCESS_TEST("infer input encoding from 4 bytes (4)", DefaultParserFactory, " \x00" "7\x00", FINAL, UTF16LE, "#(7):2,0,1;")
PARSE_SUCCESS_TEST("infer input encoding from 4 bytes (5)", DefaultParserFactory, "\x00\x00\x00" "7", FINAL, UTF32BE, "#(7):0,0,0;")
PARSE_SUCCESS_TEST("infer input encoding from 4 bytes (6)", DefaultParserFactory, "7\x00\x00\x00", FINAL, UTF32LE, "#(7):0,0,0;")
PARSE_FAILURE_TEST("no input encoding starts <00 00 00 00>", DefaultParserFactory, "\x00\x00\x00\x00", FINAL, InvalidEncodingSequence, 0, 0, 0, UnknownEncoding)
PARSE_FAILURE_TEST("no input encoding starts <nz 00 00 nz>", DefaultParserFactory, " \x00\x00 ", FINAL, InvalidEncodingSequence, 0, 0, 0, UnknownEncoding)
PARSE_FAILURE_TEST("UTF-8 BOM not allowed", DefaultParserFactory, "\xEF\xBB\xBF" "7", PARTIAL, BOMNotAllowed, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-16LE BOM not allowed", DefaultParserFactory, "\xFF\xFE" "7\x00", PARTIAL, BOMNotAllowed, 0, 0, 0, UTF16LE)
PARSE_FAILURE_TEST("UTF-16BE BOM not allowed", DefaultParserFactory, "\xFE\xFF\x00" "7", PARTIAL, BOMNotAllowed, 0, 0, 0, UTF16BE)
PARSE_FAILURE_TEST("UTF-32LE BOM not allowed", DefaultParserFactory, "\xFF\xFE\x00\x00" "7\x00\x00\x00", PARTIAL, BOMNotAllowed, 0, 0, 0, UTF32LE)
PARSE_FAILURE_TEST("UTF-32BE BOM not allowed", DefaultParserFactory, "\x00\x00\xFE\xFF\x00\x00\x00" "7", PARTIAL, BOMNotAllowed, 0, 0, 0, UTF32BE)
PARSE_SUCCESS_TEST("UTF-8 BOM allowed", AllowBOMParserFactory, "\xEF\xBB\xBF" "7", FINAL, UTF8, "#(7):3,0,1;")
PARSE_SUCCESS_TEST("UTF-16LE BOM allowed", AllowBOMParserFactory, "\xFF\xFE" "7\x00", FINAL, UTF16LE, "#(7):2,0,1;")
PARSE_SUCCESS_TEST("UTF-16BE BOM allowed", AllowBOMParserFactory, "\xFE\xFF\x00" "7", FINAL, UTF16BE, "#(7):2,0,1;")
PARSE_SUCCESS_TEST("UTF-32LE BOM allowed", AllowBOMParserFactory, "\xFF\xFE\x00\x00" "7\x00\x00\x00", FINAL, UTF32LE, "#(7):4,0,1;")
PARSE_SUCCESS_TEST("UTF-32BE BOM allowed", AllowBOMParserFactory, "\x00\x00\xFE\xFF\x00\x00\x00" "7", FINAL, UTF32BE, "#(7):4,0,1;")
PARSE_FAILURE_TEST("UTF-8 BOM allowed but no content", AllowBOMParserFactory, "\xEF\xBB\xBF", FINAL, ExpectedMoreTokens, 3, 0, 1, UTF8)
PARSE_FAILURE_TEST("UTF-16LE BOM allowed but no content", AllowBOMParserFactory, "\xFF\xFE", FINAL, ExpectedMoreTokens, 2, 0, 1, UTF16LE)
PARSE_FAILURE_TEST("UTF-16BE BOM allowed but no content", AllowBOMParserFactory, "\xFE\xFF", FINAL, ExpectedMoreTokens, 2, 0, 1, UTF16BE)
PARSE_FAILURE_TEST("UTF-32LE BOM allowed but no content", AllowBOMParserFactory, "\xFF\xFE\x00\x00", FINAL, ExpectedMoreTokens, 4, 0, 1, UTF32LE)
PARSE_FAILURE_TEST("UTF-32BE BOM allowed but no content", AllowBOMParserFactory, "\x00\x00\xFE\xFF", FINAL, ExpectedMoreTokens, 4, 0, 1, UTF32BE)

/* invalid input encoding sequences */

PARSE_FAILURE_TEST("UTF-8 truncated sequence (1)", UTF8ParserFactory, "\xC2", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 truncated sequence (2)", UTF8ParserFactory, "\xE0", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 truncated sequence (3)", UTF8ParserFactory, "\xE0\xBF", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 truncated sequence (4)", UTF8ParserFactory, "\xF0\xBF", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 truncated sequence (5)", UTF8ParserFactory, "\xF0\xBF\xBF", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 overlong 2-byte sequence not allowed (1)", UTF8ParserFactory, "\xC0", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 overlong 2-byte sequence not allowed (2)", UTF8ParserFactory, "\xC1", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 overlong 3-byte sequence not allowed (1)", UTF8ParserFactory, "\xE0\x80", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 overlong 3-byte sequence not allowed (2)", UTF8ParserFactory, "\xE0\x9F", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 encoded surrogate not allowed (1)", UTF8ParserFactory, "\xED\xA0", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 encoded surrogate not allowed (2)", UTF8ParserFactory, "\xED\xBF", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 overlong 4-byte sequence not allowed (1)", UTF8ParserFactory, "\xF0\x80", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 overlong 4-byte sequence not allowed (2)", UTF8ParserFactory, "\xF0\x8F", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 encoded out-of-range codepoint not allowed (1)", UTF8ParserFactory, "\xF4\x90", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid leading byte not allowed (1)", UTF8ParserFactory, "\x80", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid leading byte not allowed (2)", UTF8ParserFactory, "\xBF", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid leading byte not allowed (3)", UTF8ParserFactory, "\xF5", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid leading byte not allowed (4)", UTF8ParserFactory, "\xFF", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid continuation byte not allowed (1)", UTF8ParserFactory, "\xC2\x7F", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid continuation byte not allowed (2)", UTF8ParserFactory, "\xC2\xC0", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid continuation byte not allowed (3)", UTF8ParserFactory, "\xE1\x7F", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid continuation byte not allowed (4)", UTF8ParserFactory, "\xE1\xC0", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid continuation byte not allowed (5)", UTF8ParserFactory, "\xE1\xBF\x7F", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid continuation byte not allowed (6)", UTF8ParserFactory, "\xE1\xBF\xC0", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid continuation byte not allowed (7)", UTF8ParserFactory, "\xF1\x7F", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid continuation byte not allowed (8)", UTF8ParserFactory, "\xF1\xC0", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid continuation byte not allowed (9)", UTF8ParserFactory, "\xF1\xBF\x7F", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid continuation byte not allowed (10)", UTF8ParserFactory, "\xF1\xBF\xC0", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid continuation byte not allowed (11)", UTF8ParserFactory, "\xF1\xBF\xBF\x7F", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-8 invalid continuation byte not allowed (12)", UTF8ParserFactory, "\xF1\xBF\xBF\xC0", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("UTF-16LE truncated sequence", UTF16LEParserFactory, " ", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF16LE)
PARSE_FAILURE_TEST("UTF-16LE standalone trailing surrogate not allowed (1)", UTF16LEParserFactory, "\x00\xDC", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF16LE)
PARSE_FAILURE_TEST("UTF-16LE standalone trailing surrogate not allowed (2)", UTF16LEParserFactory, "\xFF\xDF", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF16LE)
PARSE_FAILURE_TEST("UTF-16LE standalone leading surrogate not allowed (1)", UTF16LEParserFactory, "\x00\xD8\x00_", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF16LE)
PARSE_FAILURE_TEST("UTF-16LE standalone leading surrogate not allowed (2)", UTF16LEParserFactory, "\xFF\xDB\x00_", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF16LE)
PARSE_FAILURE_TEST("UTF-16LE standalone leading surrogate not allowed (3)", UTF16LEParserFactory, "\xFF\xDB_", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF16LE)
PARSE_FAILURE_TEST("UTF-16BE truncated sequence", UTF16BEParserFactory, "\x00", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF16BE)
PARSE_FAILURE_TEST("UTF-16BE standalone trailing surrogate not allowed (1)", UTF16BEParserFactory, "\xDC\x00", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF16BE)
PARSE_FAILURE_TEST("UTF-16BE standalone trailing surrogate not allowed (2)", UTF16BEParserFactory, "\xDF\xFF", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF16BE)
PARSE_FAILURE_TEST("UTF-16BE standalone leading surrogate not allowed (1)", UTF16BEParserFactory, "\xD8\x00\x00_", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF16BE)
PARSE_FAILURE_TEST("UTF-16BE standalone leading surrogate not allowed (2)", UTF16BEParserFactory, "\xDB\xFF\x00_", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF16BE)
PARSE_FAILURE_TEST("UTF-16BE standalone leading surrogate not allowed (3)", UTF16BEParserFactory, "\xDB\xFF", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF16BE)
PARSE_FAILURE_TEST("UTF-32LE truncated sequence (1)", UTF32LEParserFactory, " ", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF32LE)
PARSE_FAILURE_TEST("UTF-32LE truncated sequence (2)", UTF32LEParserFactory, " \x00", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF32LE)
PARSE_FAILURE_TEST("UTF-32LE truncated sequence (3)", UTF32LEParserFactory, " \x00\x00", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF32LE)
PARSE_FAILURE_TEST("UTF-32LE encoded surrogate not allowed (1)", UTF32LEParserFactory, "\x00\xD8\x00\x00", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF32LE)
PARSE_FAILURE_TEST("UTF-32LE encoded surrogate not allowed (2)", UTF32LEParserFactory, "\x00\xDF\x00\x00", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF32LE)
PARSE_FAILURE_TEST("UTF-32LE encoded out-of-range codepoint not allowed (1)", UTF32LEParserFactory, "\x00\x00\x11\x00", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF32LE)
PARSE_FAILURE_TEST("UTF-32LE encoded out-of-range codepoint not allowed (2)", UTF32LEParserFactory, "\xFF\xFF\xFF\xFF", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF32LE)
PARSE_FAILURE_TEST("UTF-32BE truncated sequence (1)", UTF32BEParserFactory, "\x00", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF32BE)
PARSE_FAILURE_TEST("UTF-32BE truncated sequence (2)", UTF32BEParserFactory, "\x00\x00", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF32BE)
PARSE_FAILURE_TEST("UTF-32BE truncated sequence (3)", UTF32BEParserFactory, "\x00\x00\x00", FINAL, InvalidEncodingSequence, 0, 0, 0, UTF32BE)
PARSE_FAILURE_TEST("UTF-32BE encoded surrogate not allowed (1)", UTF32BEParserFactory, "\x00\x00\xD8\x00", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF32BE)
PARSE_FAILURE_TEST("UTF-32BE encoded surrogate not allowed (2)", UTF32BEParserFactory, "\x00\x00\xDF\xFF", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF32BE)
PARSE_FAILURE_TEST("UTF-32BE encoded out-of-range codepoint not allowed (1)", UTF32BEParserFactory, "\x00\x11\x00\x00", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF32BE)
PARSE_FAILURE_TEST("UTF-32BE encoded out-of-range codepoint not allowed (2)", UTF32BEParserFactory, "\xFF\xFF\xFF\xFF", PARTIAL, InvalidEncodingSequence, 0, 0, 0, UTF32BE)

/* replace invalid input encoding sequences */

PARSE_SUCCESS_TEST("replace UTF-8 truncated 2-byte sequence (1)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xC2\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 truncated 2-byte sequence (2)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xC2\"!", FINAL, UnknownToken, 6, 0, 6, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 truncated 3-byte sequence (1)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE0\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 truncated 3-byte sequence (2)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE0\"!", FINAL, UnknownToken, 6, 0, 6, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 truncated 3-byte sequence (3)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE0\xBF\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 truncated 3-byte sequence (4)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE0\xBF\"!", FINAL, UnknownToken, 7, 0, 6, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 truncated 4-byte sequence (1)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF0\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 truncated 4-byte sequence (2)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF0\"!", FINAL, UnknownToken, 6, 0, 6, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 truncated 4-byte sequence (3)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF0\xBF\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 truncated 4-byte sequence (4)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF0\xBF\"!", FINAL, UnknownToken, 7, 0, 6, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 truncated 4-byte sequence (5)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF0\xBF\xBF\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 truncated 4-byte sequence (6)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF0\xBF\xBF\"!", FINAL, UnknownToken, 8, 0, 6, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 overlong 2-byte sequence (1)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xC0\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 overlong 2-byte sequence (2)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xC0\"!", FINAL, UnknownToken, 6, 0, 6, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 overlong 2-byte sequence (3)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xC1\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 overlong 2-byte sequence (4)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xC1\"!", FINAL, UnknownToken, 6, 0, 6, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 overlong 3-byte sequence (1)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE0\x80\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 overlong 3-byte sequence (2)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE0\x80\"!", FINAL, UnknownToken, 7, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 overlong 3-byte sequence (3)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE0\x9F\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 overlong 3-byte sequence (4)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE0\x9F\"!", FINAL, UnknownToken, 7, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 encoded surrogate (1)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xED\xA0\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 encoded surrogate (2)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xED\xA0\"!", FINAL, UnknownToken, 7, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 encoded surrogate (3)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xED\xBF\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 encoded surrogate (4)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xED\xBF\"!", FINAL, UnknownToken, 7, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 overlong 4-byte sequence (1)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF0\x80\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 overlong 4-byte sequence (2)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF0\x80\"!", FINAL, UnknownToken, 7, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 overlong 4-byte sequence (3)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF0\x8F\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 overlong 4-byte sequence (4)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF0\x8F\"!", FINAL, UnknownToken, 7, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 encoded out-of-range codepoint (1)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF4\x90\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 encoded out-of-range codepoint (2)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF4\x90\"!", FINAL, UnknownToken, 7, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid leading byte (1)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\x80\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid leading byte (2)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\x80\"!", FINAL, UnknownToken, 6, 0, 6, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid leading byte (3)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xBF\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid leading byte (4)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xBF\"!", FINAL, UnknownToken, 6, 0, 6, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid leading byte (5)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF5\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid leading byte (6)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF5\"!", FINAL, UnknownToken, 6, 0, 6, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid leading byte (7)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xFF\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid leading byte (8)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xFF\"!", FINAL, UnknownToken, 6, 0, 6, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid continuation byte (1)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xC2\x7F\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD 7F):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid continuation byte (2)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xC2\x7F\"!", FINAL, UnknownToken, 7, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid continuation byte (3)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xC2\xC0\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid continuation byte (4)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xC2\xC0\"!", FINAL, UnknownToken, 7, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid continuation byte (5)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE1\x7F\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD 7F):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid continuation byte (6)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE1\x7F\"!", FINAL, UnknownToken, 7, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid continuation byte (7)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE1\xC0\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid continuation byte (8)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE1\xC0\"!", FINAL, UnknownToken, 7, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid continuation byte (9)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE1\xBF\x7F\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD 7F):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid continuation byte (10)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE1\xBF\x7F\"!", FINAL, UnknownToken, 8, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid continuation byte (11)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE1\xBF\xC0\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid continuation byte (12)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xE1\xBF\xC0\"!", FINAL, UnknownToken, 8, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid continuation byte (13)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF1\x7F\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD 7F):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid continuation byte (14)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF1\x7F\"!", FINAL, UnknownToken, 7, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid continuation byte (15)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF1\xC0\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid continuation byte (16)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF1\xC0\"!", FINAL, UnknownToken, 7, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid continuation byte (17)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF1\xBF\x7F\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD 7F):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid continuation byte (18)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF1\xBF\x7F\"!", FINAL, UnknownToken, 8, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid continuation byte (19)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF1\xBF\xC0\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid continuation byte (20)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF1\xBF\xC0\"!", FINAL, UnknownToken, 8, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid continuation byte (21)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF1\xBF\xBF\x7F\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD 7F):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid continuation byte (22)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF1\xBF\xBF\x7F\"!", FINAL, UnknownToken, 9, 0, 7, UTF8)
PARSE_SUCCESS_TEST("replace UTF-8 invalid continuation byte (23)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF1\xBF\xBF\xC0\"", FINAL, UTF8, "string(0x14, 61 62 63 EF BF BD EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-8 invalid continuation byte (24)", ReplaceInvalidEncodingSequencesParserFactory, "\"abc\xF1\xBF\xBF\xC0\"!", FINAL, UnknownToken, 9, 0, 7, UTF8)
PARSE_SUCCESS_TEST("Unicode 5.2.0 replacement example (1)", ReplaceInvalidEncodingSequencesParserFactory, "   \"\x61\xF1\x80\x80\xE1\x80\xC2\x62\x80\x63\x80\xBF\x64\"", FINAL, UTF8, "string(0x14, 61 EF BF BD EF BF BD EF BF BD 62 EF BF BD 63 EF BF BD EF BF BD 64):3,0,3;")
PARSE_FAILURE_TEST("Unicode 5.2.0 replacement example (2)", ReplaceInvalidEncodingSequencesParserFactory, "   \"\x61\xF1\x80\x80\xE1\x80\xC2\x62\x80\x63\x80\xBF\x64\"!", FINAL, UnknownToken, 18, 0, 15, UTF8)
PARSE_SUCCESS_TEST("replace UTF-16LE standalone trailing surrogate (1)", ReplaceInvalidEncodingSequencesParserFactory, "\"\x00" "_\x00" "\x00\xDC" "\"\x00", FINAL, UTF16LE, "string(0x14, 5F EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-16LE standalone trailing surrogate (2)", ReplaceInvalidEncodingSequencesParserFactory, "\"\x00" "_\x00" "\x00\xDC" "\"\x00" "!\x00", FINAL, UnknownToken, 8, 0, 4, UTF16LE)
PARSE_SUCCESS_TEST("replace UTF-16LE standalone trailing surrogate (3)", ReplaceInvalidEncodingSequencesParserFactory, "\"\x00" "_\x00" "\xFF\xDF" "\"\x00", FINAL, UTF16LE, "string(0x14, 5F EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-16LE standalone trailing surrogate (4)", ReplaceInvalidEncodingSequencesParserFactory, "\"\x00" "_\x00" "\xFF\xDF" "\"\x00" "!\x00", FINAL, UnknownToken, 8, 0, 4, UTF16LE)
PARSE_SUCCESS_TEST("replace UTF-16LE standalone leading surrogate (1)", ReplaceInvalidEncodingSequencesParserFactory,  "\"\x00" "_\x00" "\x00\xD8" "_\x00" "\"\x00", FINAL, UTF16LE, "string(0x14, 5F EF BF BD 5F):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-16LE standalone leading surrogate (2)", ReplaceInvalidEncodingSequencesParserFactory,  "\"\x00" "_\x00" "\x00\xD8" "_\x00" "\"\x00" "!\x00", FINAL, UnknownToken, 10, 0, 5, UTF16LE)
PARSE_SUCCESS_TEST("replace UTF-16LE standalone leading surrogate (3)", ReplaceInvalidEncodingSequencesParserFactory,  "\"\x00" "_\x00" "\xFF\xDB" "_\x00" "\"\x00", FINAL, UTF16LE, "string(0x14, 5F EF BF BD 5F):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-16LE standalone leading surrogate (4)", ReplaceInvalidEncodingSequencesParserFactory,  "\"\x00" "_\x00" "\xFF\xDB" "_\x00" "\"\x00" "!\x00", FINAL, UnknownToken, 10, 0, 5, UTF16LE)
PARSE_SUCCESS_TEST("replace UTF-16BE standalone trailing surrogate (1)", ReplaceInvalidEncodingSequencesParserFactory, "\x00\"" "\x00_" "\xDC\x00" "\x00\"", FINAL, UTF16BE, "string(0x14, 5F EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-16BE standalone trailing surrogate (2)", ReplaceInvalidEncodingSequencesParserFactory, "\x00\"" "\x00_" "\xDC\x00" "\x00\"" "\x00!", FINAL, UnknownToken, 8, 0, 4, UTF16BE)
PARSE_SUCCESS_TEST("replace UTF-16BE standalone trailing surrogate (3)", ReplaceInvalidEncodingSequencesParserFactory, "\x00\"" "\x00_" "\xDF\xFF" "\x00\"", FINAL, UTF16BE, "string(0x14, 5F EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-16BE standalone trailing surrogate (4)", ReplaceInvalidEncodingSequencesParserFactory, "\x00\"" "\x00_" "\xDF\xFF" "\x00\"" "\x00!", FINAL, UnknownToken, 8, 0, 4, UTF16BE)
PARSE_SUCCESS_TEST("replace UTF-16BE standalone leading surrogate (1)", ReplaceInvalidEncodingSequencesParserFactory,  "\x00\"" "\x00_" "\xD8\x00" "\x00_" "\x00\"", FINAL, UTF16BE, "string(0x14, 5F EF BF BD 5F):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-16BE standalone leading surrogate (2)", ReplaceInvalidEncodingSequencesParserFactory,  "\x00\"" "\x00_" "\xD8\x00" "\x00_" "\x00\"" "!\x00", FINAL, UnknownToken, 10, 0, 5, UTF16BE)
PARSE_SUCCESS_TEST("replace UTF-16BE standalone leading surrogate (3)", ReplaceInvalidEncodingSequencesParserFactory,  "\x00\"" "\x00_" "\xDB\xFF" "\x00_" "\x00\"", FINAL, UTF16BE, "string(0x14, 5F EF BF BD 5F):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-16BE standalone leading surrogate (4)", ReplaceInvalidEncodingSequencesParserFactory,  "\x00\"" "\x00_" "\xDB\xFF" "\x00_" "\x00\"" "!\x00", FINAL, UnknownToken, 10, 0, 5, UTF16BE)
PARSE_SUCCESS_TEST("replace UTF-32LE encoded surrogate (1)", ReplaceInvalidEncodingSequencesParserFactory, "\"\x00\x00\x00" "\x00\xD8\x00\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "string(0x14, EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-32LE encoded surrogate (2)", ReplaceInvalidEncodingSequencesParserFactory, "\"\x00\x00\x00" "\x00\xD8\x00\x00" "\"\x00\x00\x00" "!\x00\x00\x00", FINAL, UnknownToken, 12, 0, 3, UTF32LE)
PARSE_SUCCESS_TEST("replace UTF-32LE encoded surrogate (3)", ReplaceInvalidEncodingSequencesParserFactory, "\"\x00\x00\x00" "\xFF\xDF\x00\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "string(0x14, EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-32LE encoded surrogate (4)", ReplaceInvalidEncodingSequencesParserFactory, "\"\x00\x00\x00" "\xFF\xDF\x00\x00" "\"\x00\x00\x00" "!\x00\x00\x00", FINAL, UnknownToken, 12, 0, 3, UTF32LE)
PARSE_SUCCESS_TEST("replace UTF-32LE encoded out-of-range codepoint (1)", ReplaceInvalidEncodingSequencesParserFactory, "\"\x00\x00\x00" "\x00\x00\x11\x00" "\"\x00\x00\x00", FINAL, UTF32LE, "string(0x14, EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-32LE encoded out-of-range codepoint (2)", ReplaceInvalidEncodingSequencesParserFactory, "\"\x00\x00\x00" "\x00\x00\x11\x00" "\"\x00\x00\x00" "!\x00\x00\x00", FINAL, UnknownToken, 12, 0, 3, UTF32LE)
PARSE_SUCCESS_TEST("replace UTF-32LE encoded out-of-range codepoint (3)", ReplaceInvalidEncodingSequencesParserFactory, "\"\x00\x00\x00" "\x00\x00\x00\x01" "\"\x00\x00\x00", FINAL, UTF32LE, "string(0x14, EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-32LE encoded out-of-range codepoint (4)", ReplaceInvalidEncodingSequencesParserFactory, "\"\x00\x00\x00" "\x00\x00\x00\x01" "\"\x00\x00\x00" "!\x00\x00\x00", FINAL, UnknownToken, 12, 0, 3, UTF32LE)
PARSE_SUCCESS_TEST("replace UTF-32BE encoded surrogate (1)", ReplaceInvalidEncodingSequencesParserFactory, "\x00\x00\x00\"" "\x00\x00\xD8\x00" "\x00\x00\x00\"", FINAL, UTF32BE, "string(0x14, EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-32BE encoded surrogate (2)", ReplaceInvalidEncodingSequencesParserFactory, "\x00\x00\x00\"" "\x00\x00\xD8\x00" "\x00\x00\x00\"" "\x00\x00\x00!", FINAL, UnknownToken, 12, 0, 3, UTF32BE)
PARSE_SUCCESS_TEST("replace UTF-32BE encoded surrogate (3)", ReplaceInvalidEncodingSequencesParserFactory, "\x00\x00\x00\"" "\x00\x00\xDF\xFF" "\x00\x00\x00\"", FINAL, UTF32BE, "string(0x14, EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-32BE encoded surrogate (4)", ReplaceInvalidEncodingSequencesParserFactory, "\x00\x00\x00\"" "\x00\x00\xDF\xFF" "\x00\x00\x00\"" "\x00\x00\x00!", FINAL, UnknownToken, 12, 0, 3, UTF32BE)
PARSE_SUCCESS_TEST("replace UTF-32BE encoded out-of-range codepoint (1)", ReplaceInvalidEncodingSequencesParserFactory, "\x00\x00\x00\"" "\x00\x11\x00\x00" "\x00\x00\x00\"", FINAL, UTF32BE, "string(0x14, EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-32BE encoded out-of-range codepoint (2)", ReplaceInvalidEncodingSequencesParserFactory, "\x00\x00\x00\"" "\x00\x11\x00\x00" "\x00\x00\x00\"" "\x00\x00\x00!", FINAL, UnknownToken, 12, 0, 3, UTF32BE)
PARSE_SUCCESS_TEST("replace UTF-32BE encoded out-of-range codepoint (3)", ReplaceInvalidEncodingSequencesParserFactory, "\x00\x00\x00\"" "\x01\x00\x00\x00" "\x00\x00\x00\"", FINAL, UTF32BE, "string(0x14, EF BF BD):0,0,0;")
PARSE_FAILURE_TEST("replace UTF-32BE encoded out-of-range codepoint (4)", ReplaceInvalidEncodingSequencesParserFactory, "\x00\x00\x00\"" "\x01\x00\x00\x00" "\x00\x00\x00\"" "\x00\x00\x00!", FINAL, UnknownToken, 12, 0, 3, UTF32BE)

/* general */

PARSE_SUCCESS_TEST("no input bytes (partial)", DefaultParserFactory, "", PARTIAL, UnknownEncoding, "")
PARSE_FAILURE_TEST("no input bytes", DefaultParserFactory, "", FINAL, ExpectedMoreTokens, 0, 0, 0, UnknownEncoding)
PARSE_SUCCESS_TEST("all whitespace (partial) (1)", DefaultParserFactory, " ", PARTIAL, UnknownEncoding, "")
PARSE_SUCCESS_TEST("all whitespace (partial) (2)", DefaultParserFactory, "\t", PARTIAL, UnknownEncoding, "")
PARSE_SUCCESS_TEST("all whitespace (partial) (3)", DefaultParserFactory, "\r\n", PARTIAL, UnknownEncoding, "")
PARSE_SUCCESS_TEST("all whitespace (partial) (4)", DefaultParserFactory, "\r\n\n\r ", PARTIAL, UTF8, "")
PARSE_FAILURE_TEST("all whitespace (1)", DefaultParserFactory, " ", FINAL, ExpectedMoreTokens, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("all whitespace (2)", DefaultParserFactory, "\t", FINAL, ExpectedMoreTokens, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("all whitespace (3)", DefaultParserFactory, "\r\n", FINAL, ExpectedMoreTokens, 2, 1, 0, UTF8)
PARSE_FAILURE_TEST("all whitespace (4)", DefaultParserFactory, "\r\n\n\r ", FINAL, ExpectedMoreTokens, 5, 3, 1, UTF8)
PARSE_FAILURE_TEST("trailing garbage (1)", DefaultParserFactory, "7 !", FINAL, UnknownToken, 2, 0, 2, UTF8)
PARSE_FAILURE_TEST("trailing garbage (2)", DefaultParserFactory, "7 {", FINAL, UnexpectedToken, 2, 0, 2, UTF8)
PARSE_FAILURE_TEST("trailing garbage (3)", DefaultParserFactory, "7 \xC0", FINAL, InvalidEncodingSequence, 2, 0, 2, UTF8)
PARSE_FAILURE_TEST("trailing garbage (4)", DefaultParserFactory, "7 \xC2", FINAL, InvalidEncodingSequence, 2, 0, 2, UTF8)
PARSE_FAILURE_TEST("trailing garbage (5)", DefaultParserFactory, "7 [", FINAL, UnexpectedToken, 2, 0, 2, UTF8)
PARSE_FAILURE_TEST("trailing garbage (6)", DefaultParserFactory, "7 ,", FINAL, UnexpectedToken, 2, 0, 2, UTF8)
PARSE_FAILURE_TEST("trailing garbage (7)", DefaultParserFactory, "7 8", FINAL, UnexpectedToken, 2, 0, 2, UTF8)
PARSE_FAILURE_TEST("trailing garbage (8)", DefaultParserFactory, "7 \"", FINAL, IncompleteToken, 2, 0, 2, UTF8)

/* null */

PARSE_SUCCESS_TEST("null (1)", DefaultParserFactory, "null", FINAL, UTF8, "null:0,0,0;")
PARSE_SUCCESS_TEST("null (2)", DefaultParserFactory, " null ", FINAL, UTF8, "null:1,0,1;")
PARSE_FAILURE_TEST("n is not a literal", DefaultParserFactory, "n ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("nu is not a literal", DefaultParserFactory, "nu ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("nul is not a literal", DefaultParserFactory, "nul ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("nullx is not a literal", DefaultParserFactory, "nullx", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("null0 is not a literal", DefaultParserFactory, "null0", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("null_ is not a literal", DefaultParserFactory, "null_", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("nullX is not a literal", DefaultParserFactory, "nullX", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("NULL is not a literal", DefaultParserFactory, "NULL", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("null truncated after n", DefaultParserFactory, "n", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("null truncated after nu", DefaultParserFactory, "nu", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("null truncated after nul", DefaultParserFactory, "nul", FINAL, UnknownToken, 0, 0, 0, UTF8)

/* true */

PARSE_SUCCESS_TEST("true (1)", DefaultParserFactory, "true", FINAL, UTF8, "true:0,0,0;")
PARSE_SUCCESS_TEST("true (2)", DefaultParserFactory, " true ", FINAL, UTF8, "true:1,0,1;")
PARSE_FAILURE_TEST("t is not a literal", DefaultParserFactory, "t ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("tr is not a literal", DefaultParserFactory, "tr ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("tru is not a literal", DefaultParserFactory, "tru ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("trux is not a literal", DefaultParserFactory, "trux", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("true0 is not a literal", DefaultParserFactory, "true0", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("true_ is not a literal", DefaultParserFactory, "true__", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("trueX is not a literal", DefaultParserFactory, "trueX", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("TRUE is not a literal", DefaultParserFactory, "TRUE", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("true truncated after t", DefaultParserFactory, "t", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("true truncated after tr", DefaultParserFactory, "tr", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("true truncated after tru", DefaultParserFactory, "tru", FINAL, UnknownToken, 0, 0, 0, UTF8)

/* false */

PARSE_SUCCESS_TEST("false (1)", DefaultParserFactory, "false", FINAL, UTF8, "false:0,0,0;")
PARSE_SUCCESS_TEST("false (2)", DefaultParserFactory, " false ", FINAL, UTF8, "false:1,0,1;")
PARSE_FAILURE_TEST("f is not a literal", DefaultParserFactory, "f ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("fa is not a literal", DefaultParserFactory, "fa ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("fal is not a literal", DefaultParserFactory, "fal ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("falx is not a literal", DefaultParserFactory, "falx", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("fals is not a literal", DefaultParserFactory, "fals", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("false0 is not a literal", DefaultParserFactory, "false0", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("false_ is not a literal", DefaultParserFactory, "false_", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("falseX is not a literal", DefaultParserFactory, "falseX", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("FALSE is not a literal", DefaultParserFactory, "FALSE", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("false truncated after f", DefaultParserFactory, "f", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("false truncated after fa", DefaultParserFactory, "fa", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("false truncated after fal", DefaultParserFactory, "fal", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("false truncated after fals", DefaultParserFactory, "fals", FINAL, UnknownToken, 0, 0, 0, UTF8)

/* NaN */

PARSE_SUCCESS_TEST("NaN (1)", AllowSpecialNumbersParserFactory, "NaN", FINAL, UTF8, "NaN:0,0,0;")
PARSE_SUCCESS_TEST("NaN (2)", AllowSpecialNumbersParserFactory, " NaN ", FINAL, UTF8, "NaN:1,0,1;")
PARSE_FAILURE_TEST("N is not a literal", AllowSpecialNumbersParserFactory, "N ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Na is not a literal", AllowSpecialNumbersParserFactory, "Na ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Nax is not a literal", AllowSpecialNumbersParserFactory, "Nax", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Na0 is not a literal", AllowSpecialNumbersParserFactory, "NaN0", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("NaN_ is not a literal", AllowSpecialNumbersParserFactory, "NaN_", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("NaNX is not a literal", AllowSpecialNumbersParserFactory, "NaNX", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("NAN is not a literal", AllowSpecialNumbersParserFactory, "NAN", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("NaN truncated after N", AllowSpecialNumbersParserFactory, "N", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("NaN truncated after Na", AllowSpecialNumbersParserFactory, "Na", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("NaN not allowed", DefaultParserFactory, "NaN", FINAL, UnknownToken, 0, 0, 0, UTF8)

/* Infinity */

PARSE_SUCCESS_TEST("Infinity (1)", AllowSpecialNumbersParserFactory, "Infinity", FINAL, UTF8, "Infinity:0,0,0;")
PARSE_SUCCESS_TEST("Infinity (2)", AllowSpecialNumbersParserFactory, " Infinity ", FINAL, UTF8, "Infinity:1,0,1;")
PARSE_FAILURE_TEST("I is not a literal", AllowSpecialNumbersParserFactory, "I ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("In is not a literal", AllowSpecialNumbersParserFactory, "In ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Inf is not a literal", AllowSpecialNumbersParserFactory, "Inf ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infi is not a literal", AllowSpecialNumbersParserFactory, "Infi ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infin is not a literal", AllowSpecialNumbersParserFactory, "Infin ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infini is not a literal", AllowSpecialNumbersParserFactory, "Infini ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infinit is not a literal", AllowSpecialNumbersParserFactory, "Infinit ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infinitx is not a literal", AllowSpecialNumbersParserFactory, "Infinitx", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infinit0 is not a literal", AllowSpecialNumbersParserFactory, "Infinit0", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infinity_ is not a literal", AllowSpecialNumbersParserFactory, "Infinity_", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("InfinityX is not a literal", AllowSpecialNumbersParserFactory, "InfinityX", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("INF is not a literal", AllowSpecialNumbersParserFactory, "INF", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("INFINITY is not a literal", AllowSpecialNumbersParserFactory, "INFINITY", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infinity truncated after I", AllowSpecialNumbersParserFactory, "I", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infinity truncated after In", AllowSpecialNumbersParserFactory, "In", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infinity truncated after Inf", AllowSpecialNumbersParserFactory, "Inf", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infinity truncated after Infi", AllowSpecialNumbersParserFactory, "Infi", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infinity truncated after Infin", AllowSpecialNumbersParserFactory, "Infin", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infinity truncated after Infini", AllowSpecialNumbersParserFactory, "Infini", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infinity truncated after Infinit", AllowSpecialNumbersParserFactory, "Infinit", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("Infinity not allowed", DefaultParserFactory, "Infinity", FINAL, UnknownToken, 0, 0, 0, UTF8)

/* -Infinity */

PARSE_SUCCESS_TEST("-Infinity (1)", AllowSpecialNumbersParserFactory, "-Infinity", FINAL, UTF8, "-Infinity:0,0,0;")
PARSE_SUCCESS_TEST("-Infinity (2)", AllowSpecialNumbersParserFactory, " -Infinity ", FINAL, UTF8, "-Infinity:1,0,1;")
PARSE_FAILURE_TEST("-I is not a number", AllowSpecialNumbersParserFactory, "-I ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-In is not a number", AllowSpecialNumbersParserFactory, "-In ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Inf is not a number", AllowSpecialNumbersParserFactory, "-Inf ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infi is not a number", AllowSpecialNumbersParserFactory, "-Infi ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infin is not a number", AllowSpecialNumbersParserFactory, "-Infin ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infini is not a number", AllowSpecialNumbersParserFactory, "-Infini ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infinit is not a number", AllowSpecialNumbersParserFactory, "-Infinit ", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infinitx is not a number", AllowSpecialNumbersParserFactory, "-Infinitx", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infinit0 is not a number", AllowSpecialNumbersParserFactory, "-Infinit0", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infinity_ is not a number", AllowSpecialNumbersParserFactory, "-Infinity_", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-InfinityX is not a number", AllowSpecialNumbersParserFactory, "-InfinityX", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-INF is not a number", AllowSpecialNumbersParserFactory, "-INF", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-INFINITY is not a number", AllowSpecialNumbersParserFactory, "-INFINITY", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infinity truncated after I", AllowSpecialNumbersParserFactory, "-I", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infinity truncated after In", AllowSpecialNumbersParserFactory, "-In", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infinity truncated after Inf", AllowSpecialNumbersParserFactory, "-Inf", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infinity truncated after Infi", AllowSpecialNumbersParserFactory, "-Infi", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infinity truncated after Infin", AllowSpecialNumbersParserFactory, "-Infin", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infinity truncated after Infini", AllowSpecialNumbersParserFactory, "-Infini", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infinity truncated after Infinit", AllowSpecialNumbersParserFactory, "-Infinit", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("-Infinity not allowed", DefaultParserFactory, "-Infinity", FINAL, UnknownToken, 0, 0, 0, UTF8)

/* numbers */

PARSE_SUCCESS_TEST("0 (1)", DefaultParserFactory, "0", FINAL, UTF8, "#(0):0,0,0;")
PARSE_SUCCESS_TEST("0 (2)", DefaultParserFactory, " 0 ", FINAL, UTF8, "#(0):1,0,1;")
PARSE_SUCCESS_TEST("-0 (1)", DefaultParserFactory, "-0", FINAL, UTF8, "#(-0):0,0,0;")
PARSE_SUCCESS_TEST("-0 (2)", DefaultParserFactory, " -0 ", FINAL, UTF8, "#(-0):1,0,1;")
PARSE_SUCCESS_TEST("7 (1)", DefaultParserFactory, "7", FINAL, UTF8, "#(7):0,0,0;")
PARSE_SUCCESS_TEST("7 (2)", DefaultParserFactory, " 7 ", FINAL, UTF8, "#(7):1,0,1;")
PARSE_SUCCESS_TEST("-7 (1)", DefaultParserFactory, "-7", FINAL, UTF8, "#(-7):0,0,0;")
PARSE_SUCCESS_TEST("-7 (2)", DefaultParserFactory, " -7 ", FINAL, UTF8, "#(-7):1,0,1;")
PARSE_SUCCESS_TEST("1234567890 (1)", DefaultParserFactory, "1234567890", FINAL, UTF8, "#(1234567890):0,0,0;")
PARSE_SUCCESS_TEST("1234567890 (2)", DefaultParserFactory, " 1234567890 ", FINAL, UTF8, "#(1234567890):1,0,1;")
PARSE_SUCCESS_TEST("-1234567890 (1)", DefaultParserFactory, "-1234567890", FINAL, UTF8, "#(-1234567890):0,0,0;")
PARSE_SUCCESS_TEST("-1234567890 (2)", DefaultParserFactory, " -1234567890 ", FINAL, UTF8, "#(-1234567890):1,0,1;")
PARSE_SUCCESS_TEST("0e1 (1)", DefaultParserFactory, "0e1", FINAL, UTF8, "#(0e1):0,0,0;")
PARSE_SUCCESS_TEST("0e1 (2)", DefaultParserFactory, " 0e1 ", FINAL, UTF8, "#(0e1):1,0,1;")
PARSE_SUCCESS_TEST("1e2 (1)", DefaultParserFactory, "1e2", FINAL, UTF8, "#(1e2):0,0,0;")
PARSE_SUCCESS_TEST("1e2 (2)", DefaultParserFactory, " 1e2 ", FINAL, UTF8, "#(1e2):1,0,1;")
PARSE_SUCCESS_TEST("0e+1 (1)", DefaultParserFactory, "0e+1", FINAL, UTF8, "#(0e+1):0,0,0;")
PARSE_SUCCESS_TEST("0e+1 (2)", DefaultParserFactory, " 0e+1 ", FINAL, UTF8, "#(0e+1):1,0,1;")
PARSE_SUCCESS_TEST("1e+2 (1)", DefaultParserFactory, "1e+2", FINAL, UTF8, "#(1e+2):0,0,0;")
PARSE_SUCCESS_TEST("1e+2 (2)", DefaultParserFactory, " 1e+2 ", FINAL, UTF8, "#(1e+2):1,0,1;")
PARSE_SUCCESS_TEST("0e-1 (1)", DefaultParserFactory, "0e-1", FINAL, UTF8, "#(0e-1):0,0,0;")
PARSE_SUCCESS_TEST("0e-1 (2)", DefaultParserFactory, " 0e-1 ", FINAL, UTF8, "#(0e-1):1,0,1;")
PARSE_SUCCESS_TEST("1e-2 (1)", DefaultParserFactory, "1e-2", FINAL, UTF8, "#(1e-2):0,0,0;")
PARSE_SUCCESS_TEST("1e-2 (2)", DefaultParserFactory, " 1e-2 ", FINAL, UTF8, "#(1e-2):1,0,1;")
PARSE_SUCCESS_TEST("1234567890E0987654321 (1)", DefaultParserFactory, "1234567890E0987654321", FINAL, UTF8, "#(1234567890E0987654321):0,0,0;")
PARSE_SUCCESS_TEST("1234567890E0987654321 (2)", DefaultParserFactory, " 1234567890E0987654321 ", FINAL, UTF8, "#(1234567890E0987654321):1,0,1;")
PARSE_SUCCESS_TEST("0.0 (1)", DefaultParserFactory, "0.0", FINAL, UTF8, "#(0.0):0,0,0;")
PARSE_SUCCESS_TEST("0.0 (2)", DefaultParserFactory, " 0.0 ", FINAL, UTF8, "#(0.0):1,0,1;")
PARSE_SUCCESS_TEST("0.12 (1)", DefaultParserFactory, "0.12", FINAL, UTF8, "#(0.12):0,0,0;")
PARSE_SUCCESS_TEST("0.12 (2)", DefaultParserFactory, " 0.12 ", FINAL, UTF8, "#(0.12):1,0,1;")
PARSE_SUCCESS_TEST("1.2 (1)", DefaultParserFactory, "1.2", FINAL, UTF8, "#(1.2):0,0,0;")
PARSE_SUCCESS_TEST("1.2 (2)", DefaultParserFactory, " 1.2 ", FINAL, UTF8, "#(1.2):1,0,1;")
PARSE_SUCCESS_TEST("1.23 (1)", DefaultParserFactory, "1.23", FINAL, UTF8, "#(1.23):0,0,0;")
PARSE_SUCCESS_TEST("1.23 (2)", DefaultParserFactory, " 1.23 ", FINAL, UTF8, "#(1.23):1,0,1;")
PARSE_SUCCESS_TEST("1.23e456 (1)", DefaultParserFactory, "1.23e456", FINAL, UTF8, "#(1.23e456):0,0,0;")
PARSE_SUCCESS_TEST("1.23e456 (2)", DefaultParserFactory, " 1.23e456 ", FINAL, UTF8, "#(1.23e456):1,0,1;")
PARSE_SUCCESS_TEST("1.23e+456 (1)", DefaultParserFactory, "1.23e+456", FINAL, UTF8, "#(1.23e+456):0,0,0;")
PARSE_SUCCESS_TEST("1.23e+456 (2)", DefaultParserFactory, " 1.23e+456 ", FINAL, UTF8, "#(1.23e+456):1,0,1;")
PARSE_SUCCESS_TEST("1.23e-456 (1)", DefaultParserFactory, "1.23e-456", FINAL, UTF8, "#(1.23e-456):0,0,0;")
PARSE_SUCCESS_TEST("1.23e-456 (2)", DefaultParserFactory, " 1.23e-456 ", FINAL, UTF8, "#(1.23e-456):1,0,1;")
PARSE_SUCCESS_TEST("maximum length number", DefaultParserFactory, "-123456789012345678901234567890.12345678901234567890e1234567890", FINAL, UTF8, "#(-123456789012345678901234567890.12345678901234567890e1234567890):0,0,0;")
PARSE_SUCCESS_TEST("number encoded in UTF-16LE (1)", UTF16LEParserFactory, "0\x00", FINAL, UTF16LE, "#(0):0,0,0;")
PARSE_SUCCESS_TEST("number encoded in UTF-16LE (2)", UTF16LEParserFactory, "-\x00" "1\x00" ".\x00" "2\x00" "3\x00" "e\x00" "-\x00" "4\x00" "5\x00" "6\x00", FINAL, UTF16LE, "#(-1.23e-456):0,0,0;")
PARSE_SUCCESS_TEST("number encoded in UTF-16BE (1)", UTF16BEParserFactory, "\x00" "0", FINAL, UTF16BE, "#(0):0,0,0;")
PARSE_SUCCESS_TEST("number encoded in UTF-16BE (2)", UTF16BEParserFactory, "\x00" "-\x00" "1\x00" ".\x00" "2\x00" "3\x00" "e\x00" "-\x00" "4\x00" "5\x00" "6", FINAL, UTF16BE, "#(-1.23e-456):0,0,0;")
PARSE_SUCCESS_TEST("number encoded in UTF-32LE (1)", UTF32LEParserFactory, "0\x00\x00\x00", FINAL, UTF32LE, "#(0):0,0,0;")
PARSE_SUCCESS_TEST("number encoded in UTF-32LE (2)", UTF32LEParserFactory, "-\x00\x00\x00" "1\x00\x00\x00" ".\x00\x00\x00" "2\x00\x00\x00" "3\x00\x00\x00" "e\x00\x00\x00" "-\x00\x00\x00" "4\x00\x00\x00" "5\x00\x00\x00" "6\x00\x00\x00", FINAL, UTF32LE, "#(-1.23e-456):0,0,0;")
PARSE_SUCCESS_TEST("number encoded in UTF-32BE (1)", UTF32BEParserFactory, "\x00\x00\x00" "0", FINAL, UTF32BE, "#(0):0,0,0;")
PARSE_SUCCESS_TEST("number encoded in UTF-32BE (2)", UTF32BEParserFactory, "\x00\x00\x00" "-\x00\x00\x00" "1\x00\x00\x00" ".\x00\x00\x00" "2\x00\x00\x00" "3\x00\x00\x00" "e\x00\x00\x00" "-\x00\x00\x00" "4\x00\x00\x00" "5\x00\x00\x00" "6", FINAL, UTF32BE, "#(-1.23e-456):0,0,0;")
PARSE_FAILURE_TEST("number cannot have leading + sign", DefaultParserFactory, "+7", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number cannot have digits after leading 0 (1)", DefaultParserFactory, "00", FINAL, InvalidNumber, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number cannot have digits after leading 0 (2)", DefaultParserFactory, "01", FINAL, InvalidNumber, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number cannot have digits after leading 0 (3)", DefaultParserFactory, "-00", FINAL, InvalidNumber, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number cannot have digits after leading 0 (4)", DefaultParserFactory, "-01", FINAL, InvalidNumber, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number requires digit after - sign", DefaultParserFactory, "-x", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number truncated after - sign", DefaultParserFactory, "-", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number requires digit after decimal point", DefaultParserFactory, "7.x", FINAL, InvalidNumber, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number truncated after decimal point", DefaultParserFactory, "7.", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number requires digit or sign after e", DefaultParserFactory, "7ex", FINAL, InvalidNumber, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number truncated after e", DefaultParserFactory, "7e", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number requires digit or sign after E", DefaultParserFactory, "7Ex", FINAL, InvalidNumber, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number truncated after E", DefaultParserFactory, "7E", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number requires digit after exponent + sign", DefaultParserFactory, "7e+x", FINAL, InvalidNumber, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number truncated after exponent + sign", DefaultParserFactory, "7e+", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number requires digit after exponent - sign", DefaultParserFactory, "7e-x", FINAL, InvalidNumber, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("number truncated after exponent - sign", DefaultParserFactory, "7e-", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("too long number", DefaultParserFactory, "-123456789012345678901234567890.12345678901234567890e12345678900", FINAL, TooLongNumber, 0, 0, 0, UTF8)

/* hex numbers */

PARSE_FAILURE_TEST("hex number not allowed (1)", DefaultParserFactory, "0x0", FINAL, UnknownToken, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("hex number not allowed (2)", DefaultParserFactory, "0X1", FINAL, UnknownToken, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("hex number not allowed (3)", DefaultParserFactory, "-0X1", FINAL, UnknownToken, 2, 0, 2, UTF8)

PARSE_FAILURE_TEST("negative hex number not allowed", AllowHexNumbersParserFactory, "-0X1", FINAL, UnknownToken, 2, 0, 2, UTF8)

PARSE_SUCCESS_TEST("hex number (1)", AllowHexNumbersParserFactory, "0x0", FINAL, UTF8, "#(0x0):0,0,0;")
PARSE_SUCCESS_TEST("hex number (2)", AllowHexNumbersParserFactory, "0x1", FINAL, UTF8, "#(0x1):0,0,0;")
PARSE_SUCCESS_TEST("hex number (3)", AllowHexNumbersParserFactory, "0x0000", FINAL, UTF8, "#(0x0000):0,0,0;")
PARSE_SUCCESS_TEST("hex number (4)", AllowHexNumbersParserFactory, "0x123456789abcdefABCDEF", FINAL, UTF8, "#(0x123456789abcdefABCDEF):0,0,0;")

PARSE_SUCCESS_TEST("maximum length hex number", AllowHexNumbersParserFactory, "0x123456789a123456789a123456789a123456789a123456789a123456789a0", FINAL, UTF8, "#(0x123456789a123456789a123456789a123456789a123456789a123456789a0):0,0,0;")

PARSE_FAILURE_TEST("hex number truncated after x", AllowHexNumbersParserFactory, "0x", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("hex number requires  digit after x", AllowHexNumbersParserFactory, "0xx", FINAL, InvalidNumber, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("hex number truncated after X", AllowHexNumbersParserFactory, "0X", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("hex number requires  digit after X", AllowHexNumbersParserFactory, "0Xx", FINAL, InvalidNumber, 0, 0, 0, UTF8)

PARSE_FAILURE_TEST("too long hex number", AllowHexNumbersParserFactory, "0x123456789a123456789a123456789a123456789a123456789a123456789a00", FINAL, TooLongNumber, 0, 0, 0, UTF8)

/* strings */

PARSE_SUCCESS_TEST("all whitespace string", DefaultParserFactory, "\" \\r\\n\\t \"", FINAL, UTF8, "string(0x2, 20 0D 0A 09 20):0,0,0;")
PARSE_SUCCESS_TEST("ASCII string", DefaultParserFactory, "\"abc DEF 123\"", FINAL, UTF8, "string(0x0, 61 62 63 20 44 45 46 20 31 32 33):0,0,0;")
PARSE_SUCCESS_TEST("simple string escape sequences", DefaultParserFactory, "\"\\\"\\\\/\\t\\n\\r\\f\\b\"", FINAL, UTF8, "string(0x2, 22 5C 2F 09 0A 0D 0C 08):0,0,0;")
PARSE_SUCCESS_TEST("string hex escape sequences", DefaultParserFactory, "\"\\u0000\\u0020\\u0aF9\\ufFfF\\uD834\\udd1e\"", FINAL, UTF8, "string(0xF, 00 20 E0 AB B9 EF BF BF F0 9D 84 9E):0,0,0;")
PARSE_SUCCESS_TEST("string escaped control characters", DefaultParserFactory, "\""
                   "\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\u0008\\u0009\\u000A\\u000B\\u000C\\u000D\\u000E\\u000F"
                   "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017\\u0018\\u0019\\u001A\\u001B\\u001C\\u001D\\u001E\\u001F"
                   "\"", FINAL, UTF8, "string(0x3, 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F):0,0,0;")
PARSE_SUCCESS_TEST("non-control ASCII string", DefaultParserFactory, "\""
                   " !\\u0022#$%&'()+*,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\u005C]^_`abcdefghijklmnopqrstuvwxyz{|}~\\u007F"
                   "\"", FINAL, UTF8, "string(0x0, 20 21 22 23 24 25 26 27 28 29 2B 2A 2C 2D 2E 2F 30 31 32 33 34 35 36 37 38 39 3A 3B 3C 3D 3E 3F 40 41 42 43 44 45 46 47 48 49 4A 4B 4C 4D 4E 4F 50 51 52 53 54 55 56 57 58 59 5A 5B 5C 5D 5E 5F 60 61 62 63 64 65 66 67 68 69 6A 6B 6C 6D 6E 6F 70 71 72 73 74 75 76 77 78 79 7A 7B 7C 7D 7E 7F):0,0,0;")
PARSE_SUCCESS_TEST("long string", DefaultParserFactory, "\""
                   "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
                   "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
                   "\"", FINAL, UTF8, "string(0x0, 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46 30 31 32 33 34 35 36 37 38 39 41 42 43 44 45 46):0,0,0;")
PARSE_SUCCESS_TEST("UTF-8 output empty string", UTF8ParserFactory, "\"\"", FINAL, UTF8, "string(0x0, ):0,0,0;")
PARSE_SUCCESS_TEST("UTF-8 output ASCII string", UTF8ParserFactory, "\"\x61\"", FINAL, UTF8, "string(0x0, 61):0,0,0;")
PARSE_SUCCESS_TEST("UTF-8 output non-ASCII string", UTF8ParserFactory, "\"\xE0\xAB\xB9\"", FINAL, UTF8, "string(0x4, E0 AB B9):0,0,0;")
PARSE_SUCCESS_TEST("UTF-8 output non-BMP string", UTF8ParserFactory, "\"\xF0\x9D\x84\x9E\"", FINAL, UTF8, "string(0xC, F0 9D 84 9E):0,0,0;")
PARSE_SUCCESS_TEST("UTF-16LE output empty string", UTF16LEParserFactory, "\"\x00\"\x00", FINAL, UTF16LE, "string(0x0, ):0,0,0;")
PARSE_SUCCESS_TEST("UTF-16LE output ASCII string", UTF16LEParserFactory, "\"\x00\x61\x00\"\x00", FINAL, UTF16LE, "string(0x0, 61 00):0,0,0;")
PARSE_SUCCESS_TEST("UTF-16LE output non-ASCII string", UTF16LEParserFactory, "\"\x00\xF9\x0A\"\x00", FINAL, UTF16LE, "string(0x4, F9 0A):0,0,0;")
PARSE_SUCCESS_TEST("UTF-16LE output non-BMP string", UTF16LEParserFactory, "\"\x00\x34\xD8\x1E\xDD\"\x00", FINAL, UTF16LE, "string(0xC, 34 D8 1E DD):0,0,0;")
PARSE_SUCCESS_TEST("UTF-16BE output empty string", UTF16BEParserFactory, "\x00\"\x00\"", FINAL, UTF16BE, "string(0x0, ):0,0,0;")
PARSE_SUCCESS_TEST("UTF-16BE output ASCII string", UTF16BEParserFactory, "\x00\"\x00\x61\x00\"", FINAL, UTF16BE, "string(0x0, 00 61):0,0,0;")
PARSE_SUCCESS_TEST("UTF-16BE output non-ASCII string", UTF16BEParserFactory, "\x00\"\x0A\xF9\x00\"", FINAL, UTF16BE, "string(0x4, 0A F9):0,0,0;")
PARSE_SUCCESS_TEST("UTF-16BE output non-BMP string", UTF16BEParserFactory, "\x00\"\xD8\x34\xDD\x1E\x00\"", FINAL, UTF16BE, "string(0xC, D8 34 DD 1E):0,0,0;")
PARSE_SUCCESS_TEST("UTF-32LE output empty string", UTF32LEParserFactory, "\"\x00\x00\x00\"\x00\x00\x00", FINAL, UTF32LE, "string(0x0, ):0,0,0;")
PARSE_SUCCESS_TEST("UTF-32LE output ASCII string", UTF32LEParserFactory, "\"\x00\x00\x00\x61\x00\x00\x00\"\x00\x00\x00", FINAL, UTF32LE, "string(0x0, 61 00 00 00):0,0,0;")
PARSE_SUCCESS_TEST("UTF-32LE output non-ASCII string", UTF32LEParserFactory, "\"\x00\x00\x00\xF9\x0A\x00\x00\"\x00\x00\x00", FINAL, UTF32LE, "string(0x4, F9 0A 00 00):0,0,0;")
PARSE_SUCCESS_TEST("UTF-32LE output non-BMP string", UTF32LEParserFactory, "\"\x00\x00\x00\x1E\xD1\x01\x00\"\x00\x00\x00", FINAL, UTF32LE, "string(0xC, 1E D1 01 00):0,0,0;")
PARSE_SUCCESS_TEST("UTF-32BE output empty string", UTF32BEParserFactory, "\x00\x00\x00\"\x00\x00\x00\"", FINAL, UTF32BE, "string(0x0, ):0,0,0;")
PARSE_SUCCESS_TEST("UTF-32BE output ASCII string", UTF32BEParserFactory, "\x00\x00\x00\"\x00\x00\x00\x61\x00\x00\x00\"", FINAL, UTF32BE, "string(0x0, 00 00 00 61):0,0,0;")
PARSE_SUCCESS_TEST("UTF-32BE output non-ASCII string", UTF32BEParserFactory, "\x00\x00\x00\"\x00\x00\x0A\xF9\x00\x00\x00\"", FINAL, UTF32BE, "string(0x4, 00 00 0A F9):0,0,0;")
PARSE_SUCCESS_TEST("UTF-32BE output non-BMP string", UTF32BEParserFactory, "\x00\x00\x00\"\x00\x01\xD1\x1E\x00\x00\x00\"", FINAL, UTF32BE, "string(0xC, 00 01 D1 1E):0,0,0;")
PARSE_FAILURE_TEST("unterminated string (1)", DefaultParserFactory, "\"", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("unterminated string (2)", DefaultParserFactory, "\"abc", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("string cannot contain unescaped control character (1)", DefaultParserFactory, "\"abc\x00\"", FINAL, UnescapedControlCharacter, 4, 0, 4, UTF8)
PARSE_FAILURE_TEST("string cannot contain unescaped control character (2)", DefaultParserFactory, "\"abc\x09\"", FINAL, UnescapedControlCharacter, 4, 0, 4, UTF8)
PARSE_FAILURE_TEST("string cannot contain unescaped control character (3)", DefaultParserFactory, "\"abc\x0A\"", FINAL, UnescapedControlCharacter, 4, 0, 4, UTF8)
PARSE_FAILURE_TEST("string cannot contain unescaped control character (4)", DefaultParserFactory, "\"abc\x0D\"", FINAL, UnescapedControlCharacter, 4, 0, 4, UTF8)
PARSE_FAILURE_TEST("string cannot contain unescaped control character (5)", DefaultParserFactory, "\"abc\x1F\"", FINAL, UnescapedControlCharacter, 4, 0, 4, UTF8)
PARSE_FAILURE_TEST("string cannot contain invalid escape sequence (1)", DefaultParserFactory, "\"\\v\"", FINAL, InvalidEscapeSequence, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("string cannot contain invalid escape sequence (2)", DefaultParserFactory, "\"\\x0020\"", FINAL, InvalidEscapeSequence, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("string cannot contain invalid escape sequence (3)", DefaultParserFactory, "\"\\ \"", FINAL, InvalidEscapeSequence, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("string truncated after \\", DefaultParserFactory, "\"\\", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("string truncated after \\u", DefaultParserFactory, "\"\\u", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("string truncated after \\ux", DefaultParserFactory, "\"\\u0", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("string truncated after \\uxx", DefaultParserFactory, "\"\\u01", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("string truncated after \\uxxx", DefaultParserFactory, "\"\\u01a", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("string requires hex digit after \\u", DefaultParserFactory, "\"\\ux\"", FINAL, InvalidEscapeSequence, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("string requires hex digit after \\ux", DefaultParserFactory, "\"\\u0x\"", FINAL, InvalidEscapeSequence, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("string requires hex digit after \\uxx", DefaultParserFactory, "\"\\u01x\"", FINAL, InvalidEscapeSequence, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("string requires hex digit after \\uxxx", DefaultParserFactory, "\"\\u01ax\"", FINAL, InvalidEscapeSequence, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("string truncated after escaped leading surrogate", DefaultParserFactory, "\"\\uD800", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("string requires escaped surrogates to appear in valid pairs (1)", DefaultParserFactory, "\"\\uD834\"", FINAL, UnpairedSurrogateEscapeSequence, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("string requires escaped surrogates to appear in valid pairs (2)", DefaultParserFactory, "\"\\uD834x\"", FINAL, UnpairedSurrogateEscapeSequence, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("string requires escaped surrogates to appear in valid pairs (3)", DefaultParserFactory, "\"\\uD834\\n\"", FINAL, UnpairedSurrogateEscapeSequence, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("string requires escaped surrogates to appear in valid pairs (4)", DefaultParserFactory, "\"\\uD834\\u0020\"", FINAL, UnpairedSurrogateEscapeSequence, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("string requires escaped surrogates to appear in valid pairs (5)", DefaultParserFactory, "\"\\uD834\\uD834\"", FINAL, UnpairedSurrogateEscapeSequence, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("string requires escaped surrogates to appear in valid pairs (6)", DefaultParserFactory, "\"\\uDC00\"", FINAL, UnpairedSurrogateEscapeSequence, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("string truncated after \\ of trailing surrogate escape sequence", DefaultParserFactory, "\"\\uD834\\", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("string truncated after \\u of trailing surrogate escape sequence", DefaultParserFactory, "\"\\uD834\\u", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("string truncated after \\ux of trailing surrogate escape sequence", DefaultParserFactory, "\"\\uD834\\uD", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("string truncated after \\uxx of trailing surrogate escape sequence", DefaultParserFactory, "\"\\uD834\\uDD", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("string truncated after \\uxxx of trailing surrogate escape sequence", DefaultParserFactory, "\"\\uD834\\uDD1", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_SUCCESS_TEST("max length 0 string (1)", MaxStringLength0ParserFactory, "\"\"", FINAL, UTF8, "string(0x0, ):0,0,0;")
PARSE_SUCCESS_TEST("max length 0 string (2)", MaxStringLength0ParserFactory, "{\"\":0}", FINAL, UTF8, "{:0,0,0;member(0x0, ):1,0,1;#(0):4,0,4;}:5,0,5;")
PARSE_FAILURE_TEST("max length 0 string (3)", MaxStringLength0ParserFactory, "\"a\"", FINAL, TooLongString, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("max length 0 string (4)", MaxStringLength0ParserFactory, "{\"a\":0}", FINAL, TooLongString, 1, 0, 1, UTF8)
PARSE_SUCCESS_TEST("max length 1 string (1)", MaxStringLength1ParserFactory, "\"a\"", FINAL, UTF8, "string(0x0, 61):0,0,0;")
PARSE_SUCCESS_TEST("max length 1 string (2)", MaxStringLength1ParserFactory, "{\"a\":0}", FINAL, UTF8, "{:0,0,0;member(0x0, 61):1,0,1;#(0):5,0,5;}:6,0,6;")
PARSE_FAILURE_TEST("max length 1 string (3)", MaxStringLength1ParserFactory, "\"ab\"", FINAL, TooLongString, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("max length 1 string (4)", MaxStringLength1ParserFactory, "{\"ab\":0}", FINAL, TooLongString, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("max length 1 string (5)", MaxStringLength1ParserFactory, "\"\xE0\xAB\xB9\"", FINAL, TooLongString, 0, 0, 0, UTF8)
PARSE_SUCCESS_TEST("max length 2 string (1)", MaxStringLength2ParserFactory, "\"ab\"", FINAL, UTF8, "string(0x0, 61 62):0,0,0;")
PARSE_SUCCESS_TEST("max length 2 string (2)", MaxStringLength2ParserFactory, "{\"ab\":0}", FINAL, UTF8, "{:0,0,0;member(0x0, 61 62):1,0,1;#(0):6,0,6;}:7,0,7;")
PARSE_FAILURE_TEST("max length 2 string (3)", MaxStringLength2ParserFactory, "\"abc\"", FINAL, TooLongString, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("max length 2 string (4)", MaxStringLength2ParserFactory, "{\"abc\":0}", FINAL, TooLongString, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("max length 2 string (5)", MaxStringLength2ParserFactory, "\"\xE0\xAB\xB9\"", FINAL, TooLongString, 0, 0, 0, UTF8)

/* objects */

PARSE_SUCCESS_TEST("start object", UTF8ParserFactory, "{", PARTIAL, UTF8, "{:0,0,0;")
PARSE_SUCCESS_TEST("empty object (1)", DefaultParserFactory, "{}", FINAL, UTF8, "{:0,0,0;}:1,0,1;")
PARSE_SUCCESS_TEST("empty object (2)", AllowTrailingCommasParserFactory, "{}", FINAL, UTF8, "{:0,0,0;}:1,0,1;")
PARSE_SUCCESS_TEST("single-member object (1)", DefaultParserFactory, "{ \"pi\" : 3.14159 }", FINAL, UTF8, "{:0,0,0;member(0x0, 70 69):2,0,2;#(3.14159):9,0,9;}:17,0,17;")
PARSE_SUCCESS_TEST("single-member object (2)", AllowTrailingCommasParserFactory, "{ \"pi\" : 3.14159 }", FINAL, UTF8, "{:0,0,0;member(0x0, 70 69):2,0,2;#(3.14159):9,0,9;}:17,0,17;")
PARSE_SUCCESS_TEST("multi-member object (1)", DefaultParserFactory, "{ \"pi\" : 3.14159, \"e\" : 2.71828 }", FINAL, UTF8, "{:0,0,0;member(0x0, 70 69):2,0,2;#(3.14159):9,0,9;member(0x0, 65):18,0,18;#(2.71828):24,0,24;}:32,0,32;")
PARSE_SUCCESS_TEST("multi-member object (2)", AllowTrailingCommasParserFactory, "{ \"pi\" : 3.14159, \"e\" : 2.71828 }", FINAL, UTF8, "{:0,0,0;member(0x0, 70 69):2,0,2;#(3.14159):9,0,9;member(0x0, 65):18,0,18;#(2.71828):24,0,24;}:32,0,32;")
PARSE_SUCCESS_TEST("all types of object member values", DefaultParserFactory, "{ \"a\" : null, \"b\" : true, \"c\" : false, \"d\" :\"foo\", \"e\" : 17, \"f\" : {}, \"g\" : { \"bar\" : 0 }, \"h\" : [], \"i\" : [ true, false ] }", FINAL, UTF8, "{:0,0,0;member(0x0, 61):2,0,2;null:8,0,8;member(0x0, 62):14,0,14;true:20,0,20;member(0x0, 63):26,0,26;false:32,0,32;member(0x0, 64):39,0,39;string(0x0, 66 6F 6F):44,0,44;member(0x0, 65):51,0,51;#(17):57,0,57;member(0x0, 66):61,0,61;{:67,0,67;}:68,0,68;member(0x0, 67):71,0,71;{:77,0,77;member(0x0, 62 61 72):79,0,79;#(0):87,0,87;}:89,0,89;member(0x0, 68):92,0,92;[:98,0,98;]:99,0,99;member(0x0, 69):102,0,102;[:108,0,108;item:110,0,110;true:110,0,110;item:116,0,116;false:116,0,116;]:122,0,122;}:124,0,124;")
PARSE_SUCCESS_TEST("nested objects", DefaultParserFactory, "{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{}}}}}}", FINAL, UTF8, "{:0,0,0;member(0x0, 61):1,0,1;{:5,0,5;member(0x0, 62):6,0,6;{:10,0,10;member(0x0, 63):11,0,11;{:15,0,15;member(0x0, 64):16,0,16;{:20,0,20;member(0x0, 65):21,0,21;{:25,0,25;}:26,0,26;}:27,0,27;}:28,0,28;}:29,0,29;}:30,0,30;}:31,0,31;")
PARSE_SUCCESS_TEST("object members with similar names", DefaultParserFactory, "{\"\":null,\"\\u0000\":0,\"x\":1,\"X\":2,\"x2\":3,\"x\\u0000\":4,\"x\\u0000y\":5}", FINAL, UTF8, "{:0,0,0;member(0x0, ):1,0,1;null:4,0,4;member(0x3, 00):9,0,9;#(0):18,0,18;member(0x0, 78):20,0,20;#(1):24,0,24;member(0x0, 58):26,0,26;#(2):30,0,30;member(0x0, 78 32):32,0,32;#(3):37,0,37;member(0x3, 78 00):39,0,39;#(4):49,0,49;member(0x3, 78 00 79):51,0,51;#(5):62,0,62;}:63,0,63;")
PARSE_SUCCESS_TEST("different objects with members with same names", DefaultParserFactory, "{\"foo\":{\"foo\":{\"foo\":3}}}", FINAL, UTF8, "{:0,0,0;member(0x0, 66 6F 6F):1,0,1;{:7,0,7;member(0x0, 66 6F 6F):8,0,8;{:14,0,14;member(0x0, 66 6F 6F):15,0,15;#(3):21,0,21;}:22,0,22;}:23,0,23;}:24,0,24;")
PARSE_FAILURE_TEST("object truncated after left curly brace", DefaultParserFactory, "{", FINAL, ExpectedMoreTokens, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("object truncated after member name (1)", DefaultParserFactory, "{\"x\"", FINAL, ExpectedMoreTokens, 4, 0, 4, UTF8)
PARSE_FAILURE_TEST("object truncated after member name (2)", DefaultParserFactory, "{\"x\":1,\"y\"", FINAL, ExpectedMoreTokens, 10, 0, 10, UTF8)
PARSE_FAILURE_TEST("object truncated after colon (1)", DefaultParserFactory, "{\"x\":", FINAL, ExpectedMoreTokens, 5, 0, 5, UTF8)
PARSE_FAILURE_TEST("object truncated after colon (2)", DefaultParserFactory, "{\"x\":1,\"y\":", FINAL, ExpectedMoreTokens, 11, 0, 11, UTF8)
PARSE_FAILURE_TEST("object truncated after member value (1)", DefaultParserFactory, "{\"x\":1", FINAL, ExpectedMoreTokens, 6, 0, 6, UTF8)
PARSE_FAILURE_TEST("object truncated after member value (2)", DefaultParserFactory, "{\"x\":1,\"y\":2", FINAL, ExpectedMoreTokens, 12, 0, 12, UTF8)
PARSE_FAILURE_TEST("object truncated after comma (1)", DefaultParserFactory, "{\"x\":1,", FINAL, ExpectedMoreTokens, 7, 0, 7, UTF8)
PARSE_FAILURE_TEST("object truncated after comma (2)", DefaultParserFactory, "{\"x\":1,\"y\":2,", FINAL, ExpectedMoreTokens, 13, 0, 13, UTF8)
PARSE_FAILURE_TEST("object requires string member names (1)", DefaultParserFactory, "{null:1}", FINAL, UnexpectedToken, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("object requires string member names (2)", DefaultParserFactory, "{true:1}", FINAL, UnexpectedToken, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("object requires string member names (3)", DefaultParserFactory, "{false:1}", FINAL, UnexpectedToken, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("object requires string member names (4)", DefaultParserFactory, "{7:1}", FINAL, UnexpectedToken, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("object requires string member names (5)", DefaultParserFactory, "{[]:1}", FINAL, UnexpectedToken, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("object requires string member names (6)", DefaultParserFactory, "{{}:1}", FINAL, UnexpectedToken, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("object member requires value (1)", DefaultParserFactory, "{\"x\"}", FINAL, UnexpectedToken, 4, 0, 4, UTF8)
PARSE_FAILURE_TEST("object member requires value (2)", DefaultParserFactory, "{\"x\":}", FINAL, UnexpectedToken, 5, 0, 5, UTF8)
PARSE_FAILURE_TEST("object member missing (1)", DefaultParserFactory, "{,\"y\":2}", FINAL, UnexpectedToken, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("object member missing (2)", DefaultParserFactory, "{\"x\":1,,\"y\":2}", FINAL, UnexpectedToken, 7, 0, 7, UTF8)
PARSE_FAILURE_TEST("object member missing (3)", DefaultParserFactory, "{\"x\":1,}", FINAL, UnexpectedToken, 7, 0, 7, UTF8)
PARSE_SUCCESS_TEST("allow trailing comma after last object member (1)", AllowTrailingCommasParserFactory, "{\"x\":0,}", FINAL, UTF8, "{:0,0,0;member(0x0, 78):1,0,1;#(0):5,0,5;}:7,0,7;")
PARSE_SUCCESS_TEST("allow trailing comma after last object member (2)", AllowTrailingCommasParserFactory, "{\"x\":0,\"y\":1}", FINAL, UTF8, "{:0,0,0;member(0x0, 78):1,0,1;#(0):5,0,5;member(0x0, 79):7,0,7;#(1):11,0,11;}:12,0,12;")
PARSE_FAILURE_TEST("object members require comma separator", DefaultParserFactory, "{\"x\":1 \"y\":2}", FINAL, UnexpectedToken, 7, 0, 7, UTF8)
PARSE_FAILURE_TEST("object members must be unique (1)", TrackObjectMembersParserFactory, "{\"x\":1,\"x\":2}", FINAL, DuplicateObjectMember, 7, 0, 7, UTF8)
PARSE_FAILURE_TEST("object members must be unique (2)", TrackObjectMembersParserFactory, "{\"x\":1,\"y\":2,\"x\":3}", FINAL, DuplicateObjectMember, 13, 0, 13, UTF8)
PARSE_FAILURE_TEST("object members must be unique (3)", TrackObjectMembersParserFactory, "{\"x\":1,\"y\":{\"TRUE\":true,\"FALSE\":false},\"x\":3}", FINAL, DuplicateObjectMember, 39, 0, 39, UTF8)
PARSE_FAILURE_TEST("object members must be unique (4)", TrackObjectMembersParserFactory, "{\"x\":1,\"y\":{\"TRUE\":true,\"TRUE\":true},\"z\":3}", FINAL, DuplicateObjectMember, 24, 0, 24, UTF8)
PARSE_FAILURE_TEST("object members must be unique (5)", TrackObjectMembersParserFactory, "{\"x\":1,\"y\":2,\"y\":3}", FINAL, DuplicateObjectMember, 13, 0, 13, UTF8)
PARSE_SUCCESS_TEST("allow duplicate object members (1)", DefaultParserFactory, "{\"x\":1,\"x\":2}", FINAL, UTF8, "{:0,0,0;member(0x0, 78):1,0,1;#(1):5,0,5;member(0x0, 78):7,0,7;#(2):11,0,11;}:12,0,12;")
PARSE_SUCCESS_TEST("allow duplicate object members (2)", DefaultParserFactory, "{\"x\":1,\"y\":2,\"x\":3}", FINAL, UTF8, "{:0,0,0;member(0x0, 78):1,0,1;#(1):5,0,5;member(0x0, 79):7,0,7;#(2):11,0,11;member(0x0, 78):13,0,13;#(3):17,0,17;}:18,0,18;")
PARSE_SUCCESS_TEST("allow duplicate object members (3)", DefaultParserFactory, "{\"x\":1,\"y\":{\"TRUE\":true,\"FALSE\":false},\"x\":3}", FINAL, UTF8, "{:0,0,0;member(0x0, 78):1,0,1;#(1):5,0,5;member(0x0, 79):7,0,7;{:11,0,11;member(0x0, 54 52 55 45):12,0,12;true:19,0,19;member(0x0, 46 41 4C 53 45):24,0,24;false:32,0,32;}:37,0,37;member(0x0, 78):39,0,39;#(3):43,0,43;}:44,0,44;")
PARSE_SUCCESS_TEST("allow duplicate object members (4)", DefaultParserFactory, "{\"x\":1,\"y\":{\"TRUE\":true,\"TRUE\":true},\"z\":3}", FINAL, UTF8, "{:0,0,0;member(0x0, 78):1,0,1;#(1):5,0,5;member(0x0, 79):7,0,7;{:11,0,11;member(0x0, 54 52 55 45):12,0,12;true:19,0,19;member(0x0, 54 52 55 45):24,0,24;true:31,0,31;}:35,0,35;member(0x0, 7A):37,0,37;#(3):41,0,41;}:42,0,42;")
PARSE_SUCCESS_TEST("allow duplicate object members (5)", DefaultParserFactory, "{\"x\":1,\"y\":2,\"y\":3}", FINAL, UTF8, "{:0,0,0;member(0x0, 78):1,0,1;#(1):5,0,5;member(0x0, 79):7,0,7;#(2):11,0,11;member(0x0, 79):13,0,13;#(3):17,0,17;}:18,0,18;")
PARSE_FAILURE_TEST("detect duplicate object member in callback", DefaultParserFactory, "{\"duplicate\":0}", FINAL, DuplicateObjectMember, 1, 0, 1, UTF8)
PARSE_SUCCESS_TEST("empty string object member name (1)", DefaultParserFactory, "{\"\":0}", FINAL, UTF8, "{:0,0,0;member(0x0, ):1,0,1;#(0):4,0,4;}:5,0,5;")
PARSE_SUCCESS_TEST("empty string object member name (2)", TrackObjectMembersParserFactory, "{\"\":0}", FINAL, UTF8, "{:0,0,0;member(0x0, ):1,0,1;#(0):4,0,4;}:5,0,5;")
PARSE_SUCCESS_TEST("empty string object member name (3)", TrackObjectMembersParserFactory, "{\"\":0,\"x\":1}", FINAL, UTF8, "{:0,0,0;member(0x0, ):1,0,1;#(0):4,0,4;member(0x0, 78):6,0,6;#(1):10,0,10;}:11,0,11;")
PARSE_FAILURE_TEST("empty string object member name (4)", TrackObjectMembersParserFactory, "{\"\":0,\"\":1}", FINAL, DuplicateObjectMember, 6,0,6, UTF8)

/* arrays */

PARSE_SUCCESS_TEST("start array", UTF8ParserFactory, "[", PARTIAL, UTF8, "[:0,0,0;")
PARSE_SUCCESS_TEST("empty array (1)", DefaultParserFactory, "[]", FINAL, UTF8, "[:0,0,0;]:1,0,1;")
PARSE_SUCCESS_TEST("empty array (2)", AllowTrailingCommasParserFactory, "[]", FINAL, UTF8, "[:0,0,0;]:1,0,1;")
PARSE_SUCCESS_TEST("single-item array (1)", DefaultParserFactory, "[ 3.14159 ]", FINAL, UTF8, "[:0,0,0;item:2,0,2;#(3.14159):2,0,2;]:10,0,10;")
PARSE_SUCCESS_TEST("single-item array (2)", AllowTrailingCommasParserFactory, "[ 3.14159 ]", FINAL, UTF8, "[:0,0,0;item:2,0,2;#(3.14159):2,0,2;]:10,0,10;")
PARSE_SUCCESS_TEST("multi-item array (1)", DefaultParserFactory, "[ 3.14159, 2.71828 ]", FINAL, UTF8, "[:0,0,0;item:2,0,2;#(3.14159):2,0,2;item:11,0,11;#(2.71828):11,0,11;]:19,0,19;")
PARSE_SUCCESS_TEST("multi-item array (2)", AllowTrailingCommasParserFactory, "[ 3.14159, 2.71828 ]", FINAL, UTF8, "[:0,0,0;item:2,0,2;#(3.14159):2,0,2;item:11,0,11;#(2.71828):11,0,11;]:19,0,19;")
PARSE_SUCCESS_TEST("all types of array items", DefaultParserFactory, "[ null, true, false, \"foo\", 17, {}, { \"bar\" : 0 }, [], [ true, false ] ]", FINAL, UTF8, "[:0,0,0;item:2,0,2;null:2,0,2;item:8,0,8;true:8,0,8;item:14,0,14;false:14,0,14;item:21,0,21;string(0x0, 66 6F 6F):21,0,21;item:28,0,28;#(17):28,0,28;item:32,0,32;{:32,0,32;}:33,0,33;item:36,0,36;{:36,0,36;member(0x0, 62 61 72):38,0,38;#(0):46,0,46;}:48,0,48;item:51,0,51;[:51,0,51;]:52,0,52;item:55,0,55;[:55,0,55;item:57,0,57;true:57,0,57;item:63,0,63;false:63,0,63;]:69,0,69;]:71,0,71;")
PARSE_SUCCESS_TEST("nested arrays", DefaultParserFactory, "[[],[[],[[],[[],[[],[]]]]]]", FINAL, UTF8, "[:0,0,0;item:1,0,1;[:1,0,1;]:2,0,2;item:4,0,4;[:4,0,4;item:5,0,5;[:5,0,5;]:6,0,6;item:8,0,8;[:8,0,8;item:9,0,9;[:9,0,9;]:10,0,10;item:12,0,12;[:12,0,12;item:13,0,13;[:13,0,13;]:14,0,14;item:16,0,16;[:16,0,16;item:17,0,17;[:17,0,17;]:18,0,18;item:20,0,20;[:20,0,20;]:21,0,21;]:22,0,22;]:23,0,23;]:24,0,24;]:25,0,25;]:26,0,26;")
PARSE_FAILURE_TEST("array truncated after left square brace", DefaultParserFactory, "[", FINAL, ExpectedMoreTokens, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("array truncated after item value (1)", DefaultParserFactory, "[1", FINAL, ExpectedMoreTokens, 2, 0, 2, UTF8)
PARSE_FAILURE_TEST("array truncated after item value (2)", DefaultParserFactory, "[1,2", FINAL, ExpectedMoreTokens, 4, 0, 4, UTF8)
PARSE_FAILURE_TEST("array truncated after comma (1)", DefaultParserFactory, "[1,", FINAL, ExpectedMoreTokens, 3, 0, 3, UTF8)
PARSE_FAILURE_TEST("array truncated after comma (2)", DefaultParserFactory, "[1,2,", FINAL, ExpectedMoreTokens, 5, 0, 5, UTF8)
PARSE_FAILURE_TEST("array item missing (1)", DefaultParserFactory, "[,2]", FINAL, UnexpectedToken, 1, 0, 1, UTF8)
PARSE_FAILURE_TEST("array item missing (2)", DefaultParserFactory, "[1,,2]", FINAL, UnexpectedToken, 3, 0, 3, UTF8)
PARSE_FAILURE_TEST("array item missing (3)", DefaultParserFactory, "[1,]", FINAL, UnexpectedToken, 3, 0, 3, UTF8)
PARSE_SUCCESS_TEST("allow trailing comma after last array item (1)", AllowTrailingCommasParserFactory, "[1,]", FINAL, UTF8, "[:0,0,0;item:1,0,1;#(1):1,0,1;]:3,0,3;")
PARSE_SUCCESS_TEST("allow trailing comma after last array item (2)", AllowTrailingCommasParserFactory, "[1,2,]", FINAL, UTF8, "[:0,0,0;item:1,0,1;#(1):1,0,1;item:3,0,3;#(2):3,0,3;]:5,0,5;")
PARSE_FAILURE_TEST("array items require comma separator", DefaultParserFactory, "[1 2]", FINAL, UnexpectedToken, 3, 0, 3, UTF8)

/* comments */

PARSE_FAILURE_TEST("single-line comment not allowed (1)", DefaultParserFactory, "0 // comment", FINAL, UnknownToken, 2, 0, 2, UTF8)
PARSE_FAILURE_TEST("single-line comment not allowed (2)", DefaultParserFactory, "// comment\r\n0", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("multi-line comment not allowed (1)", DefaultParserFactory, "0 /* comment */", FINAL, UnknownToken, 2, 0, 2, UTF8)
PARSE_FAILURE_TEST("multi-line comment not allowed (2)", DefaultParserFactory, "/* comment */0", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("multi-line comment not allowed (3)", DefaultParserFactory, "/* comment \r\n * / * /*/0", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("multi-line comment not allowed (4)", DefaultParserFactory, "/* comment \r\n * / * /*/\r\n0", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_SUCCESS_TEST("single-line comment (1)", AllowCommentsParserFactory, "0 //", FINAL, UTF8, "#(0):0,0,0;")
PARSE_SUCCESS_TEST("single-line comment (2)", AllowCommentsParserFactory, "0 // comment", FINAL, UTF8, "#(0):0,0,0;")
PARSE_SUCCESS_TEST("single-line comment (3)", AllowCommentsParserFactory, "// comment\r\n0", FINAL, UTF8, "#(0):12,1,0;")
PARSE_SUCCESS_TEST("single-line comment with extra slashes", AllowCommentsParserFactory, "0 ////////////", FINAL, UTF8, "#(0):0,0,0;")
PARSE_SUCCESS_TEST("multi-line comment (1)", AllowCommentsParserFactory, "0 /**/", FINAL, UTF8, "#(0):0,0,0;")
PARSE_SUCCESS_TEST("multi-line comment (2)", AllowCommentsParserFactory, "0 /* comment */", FINAL, UTF8, "#(0):0,0,0;")
PARSE_SUCCESS_TEST("multi-line comment (3)", AllowCommentsParserFactory, "/* comment */0", FINAL, UTF8, "#(0):13,0,13;")
PARSE_SUCCESS_TEST("multi-line comment (4)", AllowCommentsParserFactory, "/* comment \r\n * / * /*/0", FINAL, UTF8, "#(0):23,1,10;")
PARSE_SUCCESS_TEST("multi-line comment (5)", AllowCommentsParserFactory, "/* comment \r\n * / * /*/\r\n0", FINAL, UTF8, "#(0):25,2,0;")
PARSE_SUCCESS_TEST("multi-line comment with extra stars", AllowCommentsParserFactory, "0 /************/", FINAL, UTF8, "#(0):0,0,0;")
PARSE_FAILURE_TEST("unclosed multi-line comment (1)", AllowCommentsParserFactory, "/*", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("unclosed multi-line comment (2)", AllowCommentsParserFactory, "/* comment", FINAL, IncompleteToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("just a comment (1)", AllowCommentsParserFactory, "//", FINAL, ExpectedMoreTokens, 2, 0, 2, UTF8)
PARSE_FAILURE_TEST("just a comment (2)", AllowCommentsParserFactory, "/**/", FINAL, ExpectedMoreTokens, 4, 0, 4, UTF8)
PARSE_SUCCESS_TEST("comment between tokens (1)", AllowCommentsParserFactory, "[//\n]", FINAL, UTF8, "[:0,0,0;]:4,1,0;")
PARSE_SUCCESS_TEST("comment between tokens (2)", AllowCommentsParserFactory, "[/**/]", FINAL, UTF8, "[:0,0,0;]:5,0,5;")
PARSE_FAILURE_TEST("lone forward slash (1)", AllowCommentsParserFactory, "/", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("lone forward slash (2)", AllowCommentsParserFactory, "/ ", FINAL, UnknownToken, 0, 0, 0, UTF8)

/* random tokens */

PARSE_FAILURE_TEST("random ]", DefaultParserFactory, "]", FINAL, UnexpectedToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("random }", DefaultParserFactory, "}", FINAL, UnexpectedToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("random :", DefaultParserFactory, ":", FINAL, UnexpectedToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("random ,", DefaultParserFactory, ",", FINAL, UnexpectedToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("single-quoted strings not allowed", DefaultParserFactory, "'abc'", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("random \\", DefaultParserFactory, "\\n", FINAL, UnknownToken, 0, 0, 0, UTF8)
PARSE_FAILURE_TEST("random /", DefaultParserFactory, "/", FINAL, UnknownToken, 0, 0, 0, UTF8)

/* multi-line input */

PARSE_SUCCESS_TEST("multi-line input", DefaultParserFactory, "[\r 1,\n  2,\r\n\r\n   3]", FINAL, UTF8, "[:0,0,0;item:3,1,1;#(1):3,1,1;item:8,2,2;#(2):8,2,2;item:17,4,3;#(3):17,4,3;]:18,4,4;")
PARSE_FAILURE_TEST("multi-line input error (1)", DefaultParserFactory, "[\r1", FINAL, ExpectedMoreTokens, 3, 1, 1, UTF8)
PARSE_FAILURE_TEST("multi-line input error (2)", DefaultParserFactory, "[\n1", FINAL, ExpectedMoreTokens, 3, 1, 1, UTF8)
PARSE_FAILURE_TEST("multi-line input error (3)", DefaultParserFactory, "[\r\n1", FINAL, ExpectedMoreTokens, 4, 1, 1, UTF8)
PARSE_FAILURE_TEST("multi-line input error (4)", DefaultParserFactory, "[\r1,\n2\r\n", FINAL, ExpectedMoreTokens, 8, 3, 0, UTF8)
PARSE_FAILURE_TEST("multi-line input error (5)", DefaultParserFactory, "[\r\"x\n", FINAL, UnescapedControlCharacter, 4, 1, 2, UTF8)
PARSE_FAILURE_TEST("multi-line input error (6)", DefaultParserFactory, "[\n\"x\n", FINAL, UnescapedControlCharacter, 4, 1, 2, UTF8)
PARSE_FAILURE_TEST("multi-line input error (7)", DefaultParserFactory, "[\r\n\"x\r\n", FINAL, UnescapedControlCharacter, 5, 1, 2, UTF8)

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
