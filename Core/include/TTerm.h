/*
 * TTerm
 *
 * Copyright (c) 2020 Thorben Zethoff
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef TTerm_H
#define TTerm_H

#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"

#include "TTerm_VT100.h"
#include "TTerm_config.h"


#define EXTENDED_PRINTF 1
#define TERM_VERSION_STRING "V1.0"
#define TERM_PROG_BUFFER_SIZE 32

#define CTRL_C 0x03

#if PIC32 == 1 
    #define START_OF_FLASH  0xa0000000
    #define END_OF_FLASH    0xa000ffff
#else
    #define START_OF_FLASH  0x00000000
    #define END_OF_FLASH    0x1FFF8000
#endif

#define TERM_HISTORYSIZE 16
#define TERM_INPUTBUFFER_SIZE 128

                       
#define TERM_ARGS_ERROR_STRING_LITERAL 0xffff

#define TERM_CMD_EXIT_ERROR 0
#define TERM_CMD_EXIT_NOT_FOUND 1
#define TERM_CMD_EXIT_SUCCESS 0xff
#define TERM_CMD_EXIT_PROC_STARTED 0xfe
#define TERM_CMD_CONTINUE 0x80

#if TERM_SUPPORT_CWD == 1
    #define TERM_DEVICE_NAME handle->cwdPath
#else
    #define TERM_DEVICE_NAME "UD3"
#endif

#ifdef TERM_ENABLE_STARTUP_TEXT
const extern char TERM_startupText1[];
const extern char TERM_startupText2[];
const extern char TERM_startupText3[];
#endif


#if EXTENDED_PRINTF == 1
#define ttprintfEcho(format, ...) if(handle->echoEnabled) (*handle->print)(handle->port, format, ##__VA_ARGS__)
#else
#define ttprintfEcho(format, ...) if(echoEnabled) (*handle->print)(format, ##__VA_ARGS__)
#endif

#if EXTENDED_PRINTF == 1
#define ttprintf(format, ...) (*handle->print)(handle->port, format, ##__VA_ARGS__)
#else
#define ttprintf(format, ...) (*handle->print)(format, ##__VA_ARGS__)
#endif

//entity holding data of an open terminal
typedef struct __TERMINAL_HANDLE__ TERMINAL_HANDLE;


typedef struct __TermCommandDescriptor__ TermCommandDescriptor;

typedef uint8_t (* TermCommandFunction)(TERMINAL_HANDLE * handle, uint8_t argCount, char ** args);
typedef uint8_t (* TermCommandInputHandler)(TERMINAL_HANDLE * handle, uint16_t c);
typedef uint8_t (* TermErrorPrinter)(TERMINAL_HANDLE * handle, uint32_t retCode);

#if EXTENDED_PRINTF == 1
typedef uint32_t (* TermPrintHandler)(void * port, char * format, ...);
#else
typedef void (* TermPrintHandler)(char * format, ...);
#endif
typedef uint8_t (* TermAutoCompHandler)(TERMINAL_HANDLE * handle, void * params);

typedef struct{
    TaskHandle_t task;
    TermCommandInputHandler inputHandler;
    StreamBufferHandle_t inputStream;
    char ** args;
    uint8_t argCount;
} TermProgram;

struct __TermCommandDescriptor__{
    TermCommandFunction function;
    const char * command;
    const char * commandDescription;
    uint32_t commandLength;
    uint8_t minPermissionLevel;
    TermAutoCompHandler ACHandler;
    void * ACParams;
    
    TermCommandDescriptor * nextCmd;
};

struct __TERMINAL_HANDLE__{
    char * inputBuffer;
    #if EXTENDED_PRINTF == 1
    void * port;
    #endif        
    uint32_t currBufferPosition;
    uint32_t currBufferLength;
    uint32_t currAutocompleteCount;
    TermProgram * currProgram;
    char ** autocompleteBuffer;
    uint32_t autocompleteBufferLength;
    uint32_t autocompleteStart;    
    TermPrintHandler print;
    char * currUserName;
    char * historyBuffer[TERM_HISTORYSIZE];
    uint32_t currHistoryWritePosition;
    uint32_t currHistoryReadPosition;
    uint8_t currEscSeqPos;
    uint8_t escSeqBuff[16];
    unsigned echoEnabled;
    TermCommandDescriptor * cmdListHead;
    TermErrorPrinter errorPrinter;
//TODO actually finish implementing this...
#if TERM_SUPPORT_CWD == 1
    //DIR cwd;
    char * cwdPath;
#endif
};

typedef enum{
    TERM_CHECK_COMP_AND_HIST = 0b11, TERM_CHECK_COMP = 0b01, TERM_CHECK_HIST = 0b10, 
} COPYCHECK_MODE;

extern TermCommandDescriptor TERM_defaultList;

#if EXTENDED_PRINTF == 1
TERMINAL_HANDLE * TERM_createNewHandle(TermPrintHandler printFunction, void * port, unsigned echoEnabled, TermCommandDescriptor * cmdListHead, TermErrorPrinter errorPrinter, const char * usr);
#else
TERMINAL_HANDLE * TERM_createNewHandle(TermPrintHandler printFunction, unsigned echoEnabled, TermCommandDescriptor * cmdListHead, TermErrorPrinter errorPrinter, const char * usr);    
#endif    
void TERM_destroyHandle(TERMINAL_HANDLE * handle);
uint8_t TERM_processBuffer(uint8_t * data, uint16_t length, TERMINAL_HANDLE * handle);
unsigned isACIILetter(char c);
uint8_t TERM_handleInput(uint16_t c, TERMINAL_HANDLE * handle);
char * strnchr(char * str, char c, uint32_t length);
void strsft(char * src, int32_t startByte, int32_t offset);
void TERM_printBootMessage(TERMINAL_HANDLE * handle);
void TERM_freeCommandList(TermCommandDescriptor ** cl, uint16_t length);
uint8_t TERM_buildCMDList();
TermCommandDescriptor * TERM_addCommand(TermCommandFunction function, const char * command, const char * description, uint8_t minPermissionLevel, TermCommandDescriptor * head);
void TERM_addCommandAC(TermCommandDescriptor * cmd, TermAutoCompHandler ACH, void * ACParams);
unsigned TERM_isSorted(TermCommandDescriptor * a, TermCommandDescriptor * b);
char toLowerCase(char c);
void TERM_setCursorPos(TERMINAL_HANDLE * handle, uint16_t x, uint16_t y);
void TERM_sendVT100Code(TERMINAL_HANDLE * handle, uint16_t cmd, uint8_t var);
const char * TERM_getVT100Code(uint16_t cmd, uint8_t var);
uint16_t TERM_countArgs(const char * data, uint16_t dataLength);
uint8_t TERM_interpretCMD(char * data, uint16_t dataLength, TERMINAL_HANDLE * handle);
uint8_t TERM_seperateArgs(char * data, uint16_t dataLength, char ** buff);
void TERM_checkForCopy(TERMINAL_HANDLE * handle, COPYCHECK_MODE mode);
void TERM_printDebug(TERMINAL_HANDLE * handle, char * format, ...);
void TERM_removeProgramm(TERMINAL_HANDLE * handle);
void TERM_attachProgramm(TERMINAL_HANDLE * handle, TermProgram * prog);
void TERM_killProgramm(TERMINAL_HANDLE * handle);
uint8_t TERM_doAutoComplete(TERMINAL_HANDLE * handle);
uint8_t TERM_findMatchingCMDs(char * currInput, uint8_t length, char ** buff, TermCommandDescriptor * cmdListHead);
TermCommandDescriptor * TERM_findCMD(TERMINAL_HANDLE * handle);
uint8_t TERM_findLastArg(TERMINAL_HANDLE * handle, char * buff, uint8_t * lenBuff);
BaseType_t ptr_is_in_ram(void* ptr);
uint8_t TERM_defaultErrorPrinter(TERMINAL_HANDLE * handle, uint32_t retCode);
void TERM_LIST_add(TermCommandDescriptor * item, TermCommandDescriptor * head);

#include "TTerm_cwd.h"

#endif
