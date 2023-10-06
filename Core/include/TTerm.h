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

#define TERM_VERSION_STRING "V1.0"

#include <stdint.h>

//include freeRTOS if available
#if __has_include("FreeRTOS.h")
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stream_buffer.h"
#endif

#include "TTerm_VT100.h"
#include "TTerm_config.h"

#ifdef TERM_ENABLE_CWD
#include "TTerm_cwd.h"
#endif

//Return Code Defines
#define CTRL_C 							0x03

#define TERM_ARGS_ERROR_STRING_LITERAL 	0xffff

#define TERM_CMD_EXIT_ERROR 			0
#define TERM_CMD_EXIT_NOT_FOUND 		1
#define TERM_CMD_EXIT_SUCCESS 			0xff
#define TERM_CMD_EXIT_PROC_STARTED 		0xfe
#define TERM_CMD_PROC_RUNNING 			0x80

#if __has_include("FreeRTOS.h")
	#define TERM_DEFAULT_STACKSIZE 		configMINIMAL_STACK_SIZE + 100
#else
	#define TERM_DEFAULT_STACKSIZE 		0
#endif

//Terminal struct defines
typedef struct __TERMINAL_HANDLE__ TERMINAL_HANDLE;
typedef struct __TermCommandDescriptor__ TermCommandDescriptor;


//Function prototypes
typedef uint8_t (* TermCommandFunction)		(TERMINAL_HANDLE * handle, uint8_t argCount, char ** args);
typedef uint8_t (* TermCommandInputHandler)	(TERMINAL_HANDLE * handle, uint16_t c);		//TODO maybe remove this? shouldn't be required anymore
typedef uint8_t (* TermErrorPrinter)		(TERMINAL_HANDLE * handle, uint32_t retCode);
typedef uint8_t (* TermAutoCompHandler)		(TERMINAL_HANDLE * handle, void * params);


extern TermCommandDescriptor TERM_defaultList;

//CWD Defines
#if TERM_SUPPORT_CWD == 1
    #define TERM_DEVICE_NAME handle->cwdPath
#else
    #define TERM_DEVICE_NAME TERM_NAME
#endif

//defines for optional Handle extension on printf
#if EXTENDED_PRINTF == 1
	#define ttprintfEcho(format, ...) if(handle->currEchoEnabled) (*handle->print)(handle->port, format, ##__VA_ARGS__)
	#define ttprintf(format, ...) (*handle->print)(handle->port, format, ##__VA_ARGS__)
	typedef uint32_t (* TermPrintHandler)(void * port, char * format, ...);

#else
	#define ttprintfEcho(format, ...) if(echoEnabled) (*handle->print)(format, ##__VA_ARGS__)
	#define ttprintf(format, ...) (*handle->print)(format, ##__VA_ARGS__)
	typedef void (* TermPrintHandler)(char * format, ...);

#endif



//Defines for startTaskPerCommand. Make sure freeRTOS is available before actually including this
#if defined TERM_startTaskPerCommand
	#if __has_include("FreeRTOS.h")

		//function abbreviations
		#define ttgetline() TERM_getLine(handle, portMAX_DELAY)
		#define ttgetc(X) TERM_getChar(handle, X)

		//enums
		typedef enum {PROG_RETURN, PROG_SETINPUTMODE, PROG_ENTERFOREGROUND, PROG_EXITFOREGROUND, PROG_KILL} ProgCMDType_t;
		typedef enum {INPUTMODE_NONE, INPUTMODE_DIRECT, INPUTMODE_GET_LINE} InputMode_t;

		//structs
		typedef struct{
			TaskHandle_t 			task;
			TermCommandInputHandler inputHandler;
			StreamBufferHandle_t 	inputStream;
			QueueHandle_t 			cmdStream;

			TermCommandDescriptor 	* cmd;
			TERMINAL_HANDLE 	  	* handle;
			char 				  	* commandString;
			char 				  	** args;
			uint8_t argCount;
		} TermProgram;

		typedef struct{
			ProgCMDType_t   		cmd;
			uint32_t        		arg;
			TermProgram 		  	* src;
			void 				  	* data;
		} Term_progCMD_t;
	#else
		//TERM_startTaskPerCommand is set but freeRTOS is not available, throw an error so the user knows whats happening
		#error TERM_startTaskPerCommand requires FreeRTOS, but couldnt find it!
	#endif
#endif

struct __TermCommandDescriptor__{
	TermCommandFunction function;
	const char 			  * command;
	const char 			  * commandDescription;
	uint32_t 				commandLength;
	uint32_t 				stackSize;
	TermAutoCompHandler 	ACHandler;
	void 			 	  * ACParams;

	TermCommandDescriptor * nextCmd;
};

struct __TERMINAL_HANDLE__{

	//autocomplete stuff
    uint32_t 		currAutocompleteCount;
    char 		** 	autocompleteBuffer;
    uint32_t 		autocompleteBufferLength;
    uint32_t 		autocompleteStart;

    //buffers
    char 		* 	inputBuffer;
    char 		* 	historyBuffer[TERM_HISTORYSIZE];
    uint8_t 		escSeqBuff[16];

    //position pointers
    uint32_t 		currBufferPosition;
    uint32_t 		currBufferLength;
    uint32_t 		currHistoryWritePosition;
    uint32_t 		currHistoryReadPosition;
    uint8_t 		currEscSeqPos;

    //enable flags
    unsigned 		echoEnabled;
    unsigned 		currEchoEnabled;

    //constants
    char 		* 	currUserName;
    TermCommandDescriptor * cmdListHead;
    TermPrintHandler print;
    TermErrorPrinter errorPrinter;

#if defined TERM_startTaskPerCommand && __has_include("FreeRTOS.h")
    TermProgram * nextProgram;
    TermProgram * currProgram;

    InputMode_t * currProgramInputMode;

    QueueHandle_t cmdStream;
#endif
    
#if EXTENDED_PRINTF == 1
    void * port;
#endif
    
#if TERM_SUPPORT_CWD == 1
    char * cwdPath;
#endif
};

typedef enum{TERM_CHECK_COMP_AND_HIST = 0b11, TERM_CHECK_COMP = 0b01, TERM_CHECK_HIST = 0b10} COPYCHECK_MODE;


//(De-) initializers
#if EXTENDED_PRINTF == 1
TERMINAL_HANDLE * TERM_createNewHandle(TermPrintHandler printFunction, void * port, unsigned echoEnabled, TermCommandDescriptor * cmdListHead, TermErrorPrinter errorPrinter, const char * usr);
#else
TERMINAL_HANDLE * TERM_createNewHandle(TermPrintHandler printFunction, unsigned echoEnabled, TermCommandDescriptor * cmdListHead, TermErrorPrinter errorPrinter, const char * usr);    
#endif    

void TERM_destroyHandle(TERMINAL_HANDLE * handle);

//String utilities
unsigned 		isACIILetter(char c);
char 			toLowerCase(char c);
void 			strsft(char * src, int32_t startByte, int32_t offset);
char 		* 	strnchr(char * str, char c, uint32_t length);

//other utilities
void 			TERM_setCursorPos(TERMINAL_HANDLE * handle, uint16_t x, uint16_t y);

//VT100 Support TODO improve this?
void 			TERM_sendVT100Code(TERMINAL_HANDLE * handle, uint16_t cmd, uint8_t var);
const char 	* 	TERM_getVT100Code(uint16_t cmd, uint8_t var);

//Input processing
uint8_t 		TERM_processBuffer(uint8_t * data, uint16_t length, TERMINAL_HANDLE * handle);
void 			TERM_checkForCopy(TERMINAL_HANDLE * handle, COPYCHECK_MODE mode);

//Command list functions
TermCommandDescriptor * TERM_addCommand(TermCommandFunction function, const char * command, const char * description, uint32_t stackSize, TermCommandDescriptor * head);
void 			TERM_LIST_add(TermCommandDescriptor * item, TermCommandDescriptor * head); //TODO refactor this to align with naming convention
void 			TERM_addCommandAC(TermCommandDescriptor * cmd, TermAutoCompHandler ACH, void * ACParams);
unsigned 		TERM_isSorted(TermCommandDescriptor * a, TermCommandDescriptor * b);
void 			TERM_freeCommandList(TermCommandDescriptor ** cl, uint16_t length);
uint8_t 		TERM_buildCMDList();

//command interpreter
uint16_t 		TERM_countArgs(const char * data, uint16_t dataLength);
TermCommandDescriptor * TERM_findCMD(TERMINAL_HANDLE * handle);
uint8_t 		TERM_interpretCMD(char * data, uint16_t dataLength, TERMINAL_HANDLE * handle);
uint8_t 		TERM_seperateArgs(char * data, uint16_t dataLength, char ** buff);
uint8_t 		TERM_findLastArg(TERMINAL_HANDLE * handle, char * buff, uint8_t * lenBuff);

//autocomplete handlers
uint8_t TERM_findMatchingCMDs(char * currInput, uint8_t length, char ** buff, TermCommandDescriptor * cmdListHead);
uint8_t TERM_doAutoComplete(TERMINAL_HANDLE * handle);


//default printer functions
void 			TERM_printBootMessage(TERMINAL_HANDLE * handle);
uint8_t 		TERM_defaultErrorPrinter(TERMINAL_HANDLE * handle, uint32_t retCode);
void 			TERM_printDebug(TERMINAL_HANDLE * handle, char * format, ...);

//Programm functions TODO evaluate usage and remove. Perhaps still required without taskPerCommand?
#if defined TERM_startTaskPerCommand && __has_include("FreeRTOS.h")
void 			TERM_removeProgramm(TERMINAL_HANDLE * handle);
void 			TERM_attachProgramm(TERMINAL_HANDLE * handle, TermProgram * prog);
void 			TERM_killProgramm(TERMINAL_HANDLE * handle);
#endif


#endif
