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
    
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>


#if !__is_compiling || __has_include("FreeRTOS.h")
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stream_buffer.h"
#endif

#include "TTerm.h"
#include "TTerm_cmd.h"
#include "TTerm_AC.h"
#include "TTerm_cwd.h"

#include "apps.h"

TermCommandDescriptor TERM_defaultList = {.nextCmd = 0, .commandLength = 0};
unsigned TERM_baseCMDsAdded = 0;

static uint8_t TERM_handleInput(uint16_t c, TERMINAL_HANDLE * handle);


#if EXTENDED_PRINTF == 1
TERMINAL_HANDLE * TERM_createNewHandle(TermPrintHandler printFunction, void * port, unsigned echoEnabled, TermCommandDescriptor * cmdListHead, TermErrorPrinter errorPrinter, const char * usr){
#else
TERMINAL_HANDLE * TERM_createNewHandle(TermPrintHandler printFunction, unsigned echoEnabled, TermCommandDescriptor * cmdListHead, TermErrorPrinter errorPrinter, const char * usr){    
#endif    
    
    //reserve memory
    TERMINAL_HANDLE * newHandle = TERM_MALLOC(sizeof(TERMINAL_HANDLE));
    memset(newHandle, 0, sizeof(TERMINAL_HANDLE));
    
    newHandle->inputBuffer = TERM_MALLOC(TERM_INPUTBUFFER_SIZE);
    newHandle->currUserName = TERM_MALLOC(strlen(usr) + 1 + strlen(TERM_getVT100Code(_VT100_FOREGROUND_COLOR, _VT100_YELLOW)) + strlen(TERM_getVT100Code(_VT100_RESET_ATTRIB, 0)));
    
    //initialise function pointers
    newHandle->print = printFunction;  
    
    if(errorPrinter == 0){
        newHandle->errorPrinter = TERM_defaultErrorPrinter;
    }else{
        newHandle->errorPrinter = errorPrinter;
    }
    
    //initialise data
#if EXTENDED_PRINTF == 1
    newHandle->port = port;
#endif

#ifdef TERM_startTaskPerCommand
    newHandle->cmdStream = xQueueCreate(16, sizeof(Term_progCMD_t));
#endif
    
    newHandle->echoEnabled = echoEnabled;
    newHandle->currEchoEnabled = echoEnabled;
    newHandle->cmdListHead = cmdListHead;
    sprintf(newHandle->currUserName, "%s%s%s", TERM_getVT100Code(_VT100_FOREGROUND_COLOR, _VT100_BLUE), usr, TERM_getVT100Code(_VT100_RESET_ATTRIB, 0));
    
    //reset pointers
    newHandle->currEscSeqPos = 0xff;
    
#if TERM_SUPPORT_CWD == 1
    newHandle->cwdPath = TERM_MALLOC(2);
    strcpy(newHandle->cwdPath, "/");
#endif

    //if this is the first console we initialize we need to add the static commands
    if(!TERM_baseCMDsAdded){
        TERM_baseCMDsAdded = 1;
        
        TERM_addCommand(CMD_help, "help", "Displays this help message", TERM_DEFAULT_STACKSIZE, &TERM_defaultList);
        TERM_addCommand(CMD_cls, "cls", "Clears the screen", TERM_DEFAULT_STACKSIZE, &TERM_defaultList);

#ifdef TERM_RESET_FUNCTION
        TERM_addCommand(CMD_reset, "reset", "resets the fibernet", TERM_DEFAULT_STACKSIZE, &TERM_defaultList);
#endif
        
#if TERM_SUPPORT_CWD == 1
        TERM_addCommand(CMD_cat, "cat", "a cat in the terminal?", 0, &TERM_defaultList);
        TERM_addCommand(CMD_ls, "ls", "List directory", 0, &TERM_defaultList);
        TERM_addCommand(CMD_cd, "cd", "Change directory", 0, &TERM_defaultList);
        TERM_addCommand(CMD_mkdir, "mkdir", "Make directory", 0, &TERM_defaultList);
#endif  
        
      
        TermCommandDescriptor * test = TERM_addCommand(CMD_testCommandHandler, "test", "tests stuff", TERM_DEFAULT_STACKSIZE+500, &TERM_defaultList);
        head = ACL_create();
        ACL_add(head, "-ra");
        ACL_add(head, "-r");
        ACL_add(head, "-aa");
        TERM_addCommandAC(test, ACL_defaultCompleter, head);  
        
        REGISTER_apps(&TERM_defaultList);
    }
    
#ifdef TERM_ENABLE_STARTUP_TEXT
    //TODO VT100 reset at boot
    //TODO add min start frame to signal that debugging started and print this again
    //TODO colors in the boot message
    TERM_printBootMessage(newHandle);
#endif
    return newHandle;
}

void TERM_destroyHandle(TERMINAL_HANDLE * handle){
#ifdef TERM_startTaskPerCommand
    if(handle->currProgram != NULL){
        //try to gracefully shutdown the running program
        (*handle->currProgram->inputHandler)(handle, 0x03);
    }
#endif
    
    TERM_FREE(handle->inputBuffer);
    TERM_FREE(handle->currUserName);
    
    uint8_t currHistoryPos = 0;
    for(;currHistoryPos < TERM_HISTORYSIZE; currHistoryPos++){
        if(handle->historyBuffer[currHistoryPos] != 0){
            TERM_FREE(handle->historyBuffer[currHistoryPos]);
            handle->historyBuffer[currHistoryPos] = 0;
        }
    }
    
    TERM_FREE(handle->autocompleteBuffer);
    handle->autocompleteBuffer = NULL;
    TERM_FREE(handle);
}

void TERM_printDebug(TERMINAL_HANDLE * handle, char * format, ...){
    //is handle valid?
    if(handle == NULL) return;
    
    //TODO make this nicer... we don't need a double buffer allocation, we should instead send the va_list to the print function. But it is way to late at night for me to code this now...
    //TODO implement a debug level control in the terminal handle (permission level?)
    va_list arg;
    va_start(arg, format);
    
    char * buff = (char*) TERM_MALLOC(256);
    vsnprintf(buff, 256,format, arg);
    
    ttprintfEcho("\r\n%s", buff);
    
    if(handle->currBufferLength == 0){
        ttprintfEcho("%s@%s>", handle->currUserName, TERM_DEVICE_NAME);
    }else{
        ttprintfEcho("%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->inputBuffer);
        if(handle->inputBuffer[handle->currBufferPosition] != 0) TERM_sendVT100Code(handle, _VT100_CURSOR_BACK_BY, handle->currBufferLength - handle->currBufferPosition);
    }
    
    TERM_FREE(buff);
    
    va_end(arg);
}

uint8_t TERM_processBuffer(uint8_t * data, uint16_t length, TERMINAL_HANDLE * handle){
    uint16_t currPos = 0;
    for(;currPos < length; currPos++){
        //ttprintfEcho("checking 0x%02x\r\n", data[currPos]);
        if(handle->currEscSeqPos != 0xff){
            if(handle->currEscSeqPos == 0){
                if(data[currPos] == '['){
                    handle->escSeqBuff[handle->currEscSeqPos++] = data[currPos];
                }else{
                    switch(data[currPos]){
                        case 'c':
                            handle->currEscSeqPos = 0xff;
                            TERM_handleInput(_VT100_RESET, handle);
                            break;
                        case 0x1b:
                            handle->currEscSeqPos = 0;
                            break;
                        default:
                            handle->currEscSeqPos = 0xff;
                            TERM_handleInput(0x1b, handle);
                            TERM_handleInput(data[currPos], handle);
                            break;
                    }
                }
            }else{
                if(isACIILetter(data[currPos])){
                    if(data[currPos] == 'n'){
                        if(handle->currEscSeqPos == 2){
                            if(handle->escSeqBuff[0] == '5'){        //Query device status
                            }else if(handle->escSeqBuff[0] == '6'){  //Query cursor position
                            }
                        }
                    }else if(data[currPos] == 'c'){
                        if(handle->currEscSeqPos == 1){              //Query device code
                        }
                    }else if(data[currPos] == 'F'){
                        if(handle->currEscSeqPos == 1){              //end
                            TERM_handleInput(_VT100_KEY_END, handle);
                        }
                    }else if(data[currPos] == 'H'){
                        if(handle->currEscSeqPos == 1){              //pos1
                            TERM_handleInput(_VT100_KEY_POS1, handle);
                        }
                    }else if(data[currPos] == 'C'){                      //cursor forward
                        if(handle->currEscSeqPos > 1){              
                            handle->escSeqBuff[handle->currEscSeqPos] = 0;
                        }else{
                            TERM_handleInput(_VT100_CURSOR_FORWARD, handle);
                        }
                    }else if(data[currPos] == 'D'){                      //cursor backward
                        if(handle->currEscSeqPos > 1){                 
                            handle->escSeqBuff[handle->currEscSeqPos] = 0;
                        }else{
                            TERM_handleInput(_VT100_CURSOR_BACK, handle);
                        }
                    }else if(data[currPos] == 'A'){                      //cursor up
                        if(handle->currEscSeqPos > 1){                 
                            handle->escSeqBuff[handle->currEscSeqPos] = 0;
                        }else{
                            TERM_handleInput(_VT100_CURSOR_UP, handle);
                        }
                    }else if(data[currPos] == 'B'){                      //cursor down
                        if(handle->currEscSeqPos > 1){                 
                            handle->escSeqBuff[handle->currEscSeqPos] = 0;
                        }else{
                            TERM_handleInput(_VT100_CURSOR_DOWN, handle);
                        }
                    }else if(data[currPos] == 'Z'){                      //shift tab or ident request(at least from exp.; didn't find any official spec containing this)
                        TERM_handleInput(_VT100_BACKWARDS_TAB, handle);
                        
                    }else if(data[currPos] == '~'){                      //Tilde the end of a special key command (Esc[{keyID}~)
                        if(handle->escSeqBuff[1] == 0x32){            //insert       
                            TERM_handleInput(_VT100_KEY_INS, handle);
                        }else if(handle->escSeqBuff[1] == 0x33){      //delete     
                            TERM_handleInput(_VT100_KEY_DEL, handle);
                        }else if(handle->escSeqBuff[1] == 0x35){      //page up      
                            TERM_handleInput(_VT100_KEY_PAGE_UP, handle);
                        }else if(handle->escSeqBuff[1] == 0x36){      //page down 
                            TERM_handleInput(_VT100_KEY_PAGE_DOWN, handle);
                        }else{
                            TERM_handleInput(_VT100_INVALID, handle);
                        }
                    }else{                      //others
                        handle->escSeqBuff[handle->currEscSeqPos+1] = 0;
                    }
                    handle->currEscSeqPos = 0xff;
                }else{
                    handle->escSeqBuff[handle->currEscSeqPos++] = data[currPos];
                }
            }
        }else{
            if(data[currPos] == 0x1B){     //ESC for VT100 control sequences
                handle->currEscSeqPos = 0;
            }else{
                TERM_handleInput(data[currPos], handle);
            }
        }
    }
}

unsigned isACIILetter(char c){
    return (c > 64 && c < 91) || (c > 96 && c < 122) || c == '~';
}

void TERM_printBootMessage(TERMINAL_HANDLE * handle){
    TERM_sendVT100Code(handle, _VT100_RESET, 0); TERM_sendVT100Code(handle, _VT100_CURSOR_POS1, 0); TERM_sendVT100Code(handle, _VT100_WRAP_OFF, 0);
    ttprintfEcho("\r\n\n\n%s\r\n", TERM_startupText);
    
    if(handle->currBufferLength == 0){
        ttprintfEcho("\r\n\r\n%s@%s>", handle->currUserName, TERM_DEVICE_NAME);
    }else{
        ttprintfEcho("\r\n\r\n%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->inputBuffer);
        if(handle->inputBuffer[handle->currBufferPosition] != 0) TERM_sendVT100Code(handle, _VT100_CURSOR_BACK_BY, handle->currBufferLength - handle->currBufferPosition);
    }
}

uint8_t TERM_defaultErrorPrinter(TERMINAL_HANDLE * handle, uint32_t retCode){
    switch(retCode){
        case TERM_CMD_EXIT_SUCCESS:
            ttprintfEcho("\r\n%s@%s>", handle->currUserName, TERM_DEVICE_NAME);
            break;

        case TERM_CMD_EXIT_ERROR:
            ttprintfEcho("\r\nTask returned with error code %d\r\n%s@%s>", retCode, handle->currUserName, TERM_DEVICE_NAME);
            break;

        case TERM_CMD_EXIT_NOT_FOUND:
            ttprintfEcho("\"%s\" is not a valid command. Type \"help\" to see a list of available ones\r\n%s@%s>", handle->inputBuffer, handle->currUserName, TERM_DEVICE_NAME);
            break;
    }
    return 0;
}

static void resetInputBuffer(TERMINAL_HANDLE * handle){//reset inputbuffer
    handle->currBufferPosition = 0;
    handle->currBufferLength = 0;
    memset(handle->inputBuffer, 0, TERM_INPUTBUFFER_SIZE);
    handle->currBufferPosition = 0;
}

static uint8_t TERM_handleInput(uint16_t c, TERMINAL_HANDLE * handle){
#if defined TERM_startTaskPerCommand && (!__is_compiling || __has_include("FreeRTOS.h"))
    //check if we have any program commands to process (that could be enterForeground, exitForeground, return etc.)
    Term_progCMD_t currProgCMD;
    while(xQueueReceive(handle->cmdStream, &currProgCMD, 0)){
        //weeee goooot ooneee ;)
        
        //which command did we get?
        switch(currProgCMD.cmd){
            case PROG_RETURN:
                //a program wants to return
                
                //is it currently in the foreground?
                if(handle->currProgram == currProgCMD.src){
                    //yes, we'll have to remove it from the foreground
                    
                    //reenable echo if it was on before
                    handle->currEchoEnabled = handle->echoEnabled;
                    
                    //remove currProgram
                    handle->currProgram = NULL;
                    
                    //reset buffer
                    resetInputBuffer(handle);
                }else{
                    //no it wasn't, user might have been typing something. Is this the case?
                    if(handle->currBufferLength != 0){
                        //erase the line
                        TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                        
                        //write the identifier and buffer
                        ttprintfEcho("\r\n\r\n%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->inputBuffer);
                        if(handle->inputBuffer[handle->currBufferPosition] != 0) TERM_sendVT100Code(handle, _VT100_CURSOR_BACK_BY, handle->currBufferLength - handle->currBufferPosition);
                    }
                }
                
                //free the data. This needs to happen here, as this is the last place in the code the data is accessed after program exit
                vStreamBufferDelete(currProgCMD.src->inputStream);
                vQueueDelete(currProgCMD.src->cmdStream);
                if(currProgCMD.src->argCount != 0) TERM_FREE(currProgCMD.src->args);
                TERM_FREE(currProgCMD.src->commandString);
                TERM_FREE(currProgCMD.src);

                break;
                
            case PROG_SETINPUTMODE:
                //input mode change requested.
                
                //is the program even in the foreground?
                if(handle->currProgram == currProgCMD.src){
                    //yes, oblige the request
                    
                    //is the new inputmode getLine?
                    if(currProgCMD.arg == INPUTMODE_GET_LINE){
                        //yes, empty out the line buffer
                        resetInputBuffer(handle);
                    }

                    //assign new inputmode
                    handle->currProgramInputMode = currProgCMD.arg;
                } else{
                    //lol no, it doesn't have a say in the matter...
                    
                    //is the reuested inputmode getLine?
                    if(currProgCMD.arg == INPUTMODE_GET_LINE){
                        //yes, task might be waiting for a line of data in the inputBuffer, Send an empty one to make sure it won't get stuck doing nothing
                        xStreamBufferSend(currProgCMD.src->inputStream, "\n", sizeof(char), 0);
                    }
                }
                
                break;
                
            case PROG_ENTERFOREGROUND:
                //program wants to go into the foreground
                
                //is another program already in the foreground?
                if(handle->currProgram != NULL){
                    //yes, we can't put another one there
                }else{
                    //no, all good
                    //set currProgram
                    handle->currProgram = currProgCMD.src;
                    
                    //reset inputmode
                    handle->currProgramInputMode = INPUTMODE_DIRECT;
                }
                
                break;
                
            case PROG_EXITFOREGROUND:
                //reenable echo
                handle->currEchoEnabled = handle->echoEnabled;
                
                //remove program
                handle->currProgram = NULL;
                
                break;
                
            case PROG_KILL:
                //do nothing, this isn't a valid command for the interpreter
                break;
        }
    }
    
    //is a program currently in the foreground
    if(handle->currProgram != NULL){
        //does the input mode require any immediate action?
        if(handle->currProgramInputMode == INPUTMODE_DIRECT){
            //yes => send data to the queue
            xStreamBufferSend(handle->currProgram->inputStream, &c, sizeof(c), 0);

            //check for ctrl+c (kill program)
            if(c == 0x03){
                //jep got that, kill it
                //TODO, maybe also add hardkill if ctrl+c is pressed multiple times
                ttprintfEcho("^C");
            }
            return 1;
        }else if(handle->currProgramInputMode == INPUTMODE_NONE){
            //yes => do nothing
            return 1;
        }
    }
#endif
    
    switch(c){
        case '\r':      //enter
            //are we currently looking at a history entry?
            TERM_checkForCopy(handle, TERM_CHECK_COMP_AND_HIST);
            
            //is there any data in the buffer?
            if(handle->currBufferLength != 0){
                //send newline
                ttprintfEcho("\r\n");
                uint8_t retCode = TERM_CMD_EXIT_ERROR;
                

#if defined TERM_startTaskPerCommand && (!__is_compiling || __has_include("FreeRTOS.h"))
                //is a program currently active?
                if(handle->currProgram != NULL){
                    //yes, don't interpret any commands or add anything to the history
                    
                    //what action does the program want us to do?
                    if(handle->currProgramInputMode == INPUTMODE_GET_LINE){
                        //send data to the stream
                        xStreamBufferSend(handle->currProgram->inputStream, handle->inputBuffer, sizeof(char) * handle->currBufferLength, 0);
                        xStreamBufferSend(handle->currProgram->inputStream, "\n", sizeof(char), 0);
                    }
                    
                    retCode = TERM_CMD_EXIT_SUCCESS;
#else
				if(0){
#endif
                }else{
                //copy command into history
                    //is the next position free?
                    if(handle->historyBuffer[handle->currHistoryWritePosition] != 0){
                        //no it already has a command in it, free that
                        TERM_FREE(handle->historyBuffer[handle->currHistoryWritePosition]);
                        handle->historyBuffer[handle->currHistoryWritePosition] = 0;
                    }

                    //allocate memory for the entry and copy the command
                    handle->historyBuffer[handle->currHistoryWritePosition] = TERM_MALLOC(handle->currBufferLength + 1);
                    memcpy(handle->historyBuffer[handle->currHistoryWritePosition], handle->inputBuffer, handle->currBufferLength + 1);

                    //increment history pointer
                    if(++handle->currHistoryWritePosition >= TERM_HISTORYSIZE) handle->currHistoryWritePosition = 0;

                    //reset history read pointer (make sure the next entry the history will show is the one just added)
                    handle->currHistoryReadPosition = handle->currHistoryWritePosition;

                //interpret and run the command
                    retCode = TERM_interpretCMD(handle->inputBuffer, handle->currBufferLength, handle);
                    (*handle->errorPrinter)(handle, retCode);
                }

                handle->currBufferPosition = 0;
                handle->currBufferLength = 0;
                handle->inputBuffer[handle->currBufferPosition] = 0;
                
                return retCode;
            }else{

                //no data in the buffer, just send an empty line if no program is active, and a newline into the buffer otherwise
#if defined TERM_startTaskPerCommand && (!__is_compiling || __has_include("FreeRTOS.h"))
            	if(handle->currProgram != NULL){
					xStreamBufferSend(handle->currProgram->inputStream, "\n", sizeof(char), 0);
#else
				if(0){
#endif
                }else{
                    ttprintfEcho("\r\n%s@%s>", handle->currUserName, TERM_DEVICE_NAME);
                }
            }       
            break;
            
        case 0x03:      //CTRL+c
            //TODO reset current buffer
            
            ttprintfEcho("\n\r^C");

#if defined TERM_startTaskPerCommand && (!__is_compiling || __has_include("FreeRTOS.h"))
            //is there a program in the foreground?
            if(handle->currProgram != NULL){
                //yes :) we need to send the kill char to it
                xStreamBufferSend(handle->currProgram->inputStream, &c, sizeof(char), 0);
#else
			if(0){
#endif
            }else{
                handle->currBufferPosition = 0;
                handle->currBufferLength = 0;
                memset(handle->inputBuffer, 0, TERM_INPUTBUFFER_SIZE);
                handle->currBufferPosition = 0;
                ttprintfEcho("\r\n%s@%s>", handle->currUserName, TERM_DEVICE_NAME);
            }
            
            break;
            
        case 0x08:      //backspace (used by xTerm)
        case 0x7f:      //DEL       (used by hTerm)
            TERM_checkForCopy(handle, TERM_CHECK_COMP_AND_HIST);
            if(handle->currBufferPosition == 0) break;
            
            if(handle->inputBuffer[handle->currBufferPosition] != 0){      //check if we are at the end of our command
                //we are somewhere in the middle -> move back existing characters
                strsft(handle->inputBuffer, handle->currBufferPosition - 1, -1);    
                ttprintfEcho("\x08");   
                TERM_sendVT100Code(handle, _VT100_ERASE_LINE_END, 0);
                ttprintfEcho("%s", &handle->inputBuffer[handle->currBufferPosition - 1]);
                TERM_sendVT100Code(handle, _VT100_CURSOR_BACK_BY, handle->currBufferLength - handle->currBufferPosition);
                handle->currBufferPosition --;
                handle->currBufferLength --;
            }else{
                //we are somewhere at the end -> just delete the current one
                handle->inputBuffer[--handle->currBufferPosition] = 0;
                ttprintfEcho("\x08 \x08");           
                handle->currBufferLength --;
            }
            break;
            
        case _VT100_KEY_END:
            TERM_checkForCopy(handle, TERM_CHECK_COMP_AND_HIST); //is the user browsing the history right now? if so copy whatever ehs looking at to the entry buffer
            
            if(handle->currBufferLength > handle->currBufferPosition){
                //move the cursor to the right position
                TERM_sendVT100Code(handle, _VT100_CURSOR_FORWARD_BY, handle->currBufferLength - handle->currBufferPosition);
                handle->currBufferPosition = handle->currBufferLength;
            }
            break;
            
        case _VT100_KEY_POS1:
            TERM_checkForCopy(handle, TERM_CHECK_COMP_AND_HIST); //is the user browsing the history right now? if so copy whatever ehs looking at to the entry buffer
            
            if(handle->currBufferPosition > 0){
                //move the cursor to the right position
                TERM_sendVT100Code(handle, _VT100_CURSOR_BACK_BY, handle->currBufferPosition);
                handle->currBufferPosition = 0;
            }
            break;
            
        case _VT100_CURSOR_FORWARD:
            TERM_checkForCopy(handle, TERM_CHECK_COMP_AND_HIST);
            
            if(handle->currBufferPosition < handle->currBufferLength){
                handle->currBufferPosition ++;
                TERM_sendVT100Code(handle, _VT100_CURSOR_FORWARD, 0);
            }
            break;
            
        case _VT100_CURSOR_BACK:
            TERM_checkForCopy(handle, TERM_CHECK_COMP_AND_HIST);
            
            if(handle->currBufferPosition > 0){
                handle->currBufferPosition --;
                TERM_sendVT100Code(handle, _VT100_CURSOR_BACK, 0);
            }
            break;
            
        case _VT100_CURSOR_UP:
            TERM_checkForCopy(handle, TERM_CHECK_COMP);

#if defined TERM_startTaskPerCommand && (!__is_compiling || __has_include("FreeRTOS.h"))
            //is there a program in the foreground?
            if(handle->currProgram == NULL){
#else
			if(1){
#endif
                //no, do history lookup
                do{
                    if(--handle->currHistoryReadPosition >= TERM_HISTORYSIZE) handle->currHistoryReadPosition = TERM_HISTORYSIZE - 1;

                    if(handle->historyBuffer[handle->currHistoryReadPosition] != 0){
                        break;
                    }
                }while(handle->currHistoryReadPosition != handle->currHistoryWritePosition);

                //print out the command at the current history read position
                if(handle->currHistoryReadPosition == handle->currHistoryWritePosition){
                    ttprintfEcho("\x07");   //rings a bell doesn't it?                                                      
                    TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                    ttprintfEcho("\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->inputBuffer);
                }else{
                    TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                    ttprintfEcho("\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->historyBuffer[handle->currHistoryReadPosition]);
                }
            }
                
            break;
            
        case _VT100_CURSOR_DOWN:
            TERM_checkForCopy(handle, TERM_CHECK_COMP);

#if defined TERM_startTaskPerCommand && (!__is_compiling || __has_include("FreeRTOS.h"))
            //is there a program in the foreground?
            if(handle->currProgram == NULL){
#else
			if(1){
#endif
                //no, do history lookup
                while(handle->currHistoryReadPosition != handle->currHistoryWritePosition){
                    if(++handle->currHistoryReadPosition >= TERM_HISTORYSIZE) handle->currHistoryReadPosition = 0;

                    if(handle->historyBuffer[handle->currHistoryReadPosition] != 0){
                        break;
                    }
                }

                //print out the command at the current history read position
                if(handle->currHistoryReadPosition == handle->currHistoryWritePosition){
                    ttprintfEcho("\x07");   //rings a bell doesn't it?                                                      
                    TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                    ttprintfEcho("\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->inputBuffer);
                }else{
                    TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                    ttprintfEcho("\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->historyBuffer[handle->currHistoryReadPosition]);
                }
            }
            
            break;
            
        case '\t':      //tab
            TERM_checkForCopy(handle, TERM_CHECK_HIST);

#if defined TERM_startTaskPerCommand && (!__is_compiling || __has_include("FreeRTOS.h"))
            //is there a program in the foreground?
            if(handle->currProgram == NULL){
                //no, do autocomplete
#else
			if(1){
#endif
            
                if(handle->autocompleteBuffer == NULL){ 
                    TERM_doAutoComplete(handle);
                }

                if(++handle->currAutocompleteCount > handle->autocompleteBufferLength) handle->currAutocompleteCount = 0;

                if(handle->currAutocompleteCount == 0){
                    ttprintfEcho("\x07");
                    TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                    ttprintfEcho("\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->inputBuffer);
                }else{
                    TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                    unsigned printQuotationMarks = strchr(handle->autocompleteBuffer[handle->currAutocompleteCount - 1], ' ') != 0;
                    volatile TERMINAL_HANDLE * temp = handle;

                    //workaround for inconsistent printf behaviour when using %.*s with length 0
                    if(handle->autocompleteStart == 0){
                        ttprintfEcho(printQuotationMarks ? "\r%s@%s>\"%s\"" : "\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->autocompleteBuffer[handle->currAutocompleteCount - 1]);
                    }else{
                        ttprintfEcho(printQuotationMarks ? "\r%s@%s>%.*s\"%s\"" : "\r%s@%s>%.*s%s", handle->currUserName, TERM_DEVICE_NAME, handle->autocompleteStart, handle->inputBuffer, handle->autocompleteBuffer[handle->currAutocompleteCount - 1]);
                    }
                }
            }
            break;
            
        case _VT100_BACKWARDS_TAB:
            TERM_checkForCopy(handle, TERM_CHECK_HIST);

#if defined TERM_startTaskPerCommand && (!__is_compiling || __has_include("FreeRTOS.h"))
            //is there a program in the foreground?
            if(handle->currProgram == NULL){
                //no, do autocomplete
#else
			if(1){
#endif
                if(handle->autocompleteBuffer == NULL){ 
                    TERM_doAutoComplete(handle);
                }

                if(--handle->currAutocompleteCount > handle->autocompleteBufferLength - 1) handle->currAutocompleteCount = handle->autocompleteBufferLength - 1;

                if(handle->currAutocompleteCount == 0){
                    ttprintfEcho("\x07");
                    TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                    ttprintfEcho("\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->inputBuffer);
                }else{
                    TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                    unsigned printQuotationMarks = strchr(handle->autocompleteBuffer[handle->currAutocompleteCount - 1], ' ') != 0;
                    ttprintfEcho(printQuotationMarks ? "\r%s@%s>%.*s\"%s\"" : "\r%s@%s>%.*s%s", handle->currUserName, TERM_DEVICE_NAME, handle->autocompleteStart, handle->inputBuffer, handle->autocompleteBuffer[handle->currAutocompleteCount - 1]);
                }
            }
            break;
            
        case _VT100_RESET:

#if defined TERM_startTaskPerCommand && (!__is_compiling || __has_include("FreeRTOS.h"))
            //is there a program in the foreground?
            if(handle->currProgram != NULL){
                //yes :) we need to kill it to reset the terminal to its default state
                //TODO
#else
			if(0){
#endif
            }else{
                TERM_printBootMessage(handle);
            }
            break;
           
        case 32 ... 126: //normal letter
            TERM_checkForCopy(handle, TERM_CHECK_COMP_AND_HIST);
            
            //this needs to happen even when a program is active
            
            //check if there is still space in the buffer
            if(handle->currBufferLength+1 < TERM_INPUTBUFFER_SIZE){
                if(handle->inputBuffer[handle->currBufferPosition] != 0){      //check if we are at the end of our command
                    strsft(handle->inputBuffer, handle->currBufferPosition, 1);   
                    handle->inputBuffer[handle->currBufferPosition] = c; 
                    TERM_sendVT100Code(handle, _VT100_ERASE_LINE_END, 0);
                    ttprintfEcho("%s", &handle->inputBuffer[handle->currBufferPosition]);
                    TERM_sendVT100Code(handle, _VT100_CURSOR_BACK_BY, handle->currBufferLength - handle->currBufferPosition);
                    handle->currBufferLength ++;
                    handle->currBufferPosition ++;

                }else{
                    //we are at the end -> just delete the current character

                    //check if we still have space in the buffer
                    handle->inputBuffer[handle->currBufferPosition++] = c;
                    handle->inputBuffer[handle->currBufferPosition] = 0;
                    handle->currBufferLength ++;
                    ttprintfEcho("%c", c);
                }
            }else{
                TERM_printDebug(handle, "ERROR: input buffer overflow!\r\n");
            }
            break;
            
        case 0:
            break;

        //check for control chars

#if defined TERM_startTaskPerCommand && (!__is_compiling || __has_include("FreeRTOS.h"))
        case 0x04:  //ctrl-d
        case 0x19:  //ctrl-y
        case 0x18:  //ctrl-x
        case 0x17:  //ctrl-w
        case 0x16:  //ctrl-v
        case 0x13:  //ctrl-s
            
            //is there a program in the foreground?
            if(handle->currProgram != NULL){
                //a programm is currently running in the foreground => send any control chars to it directly
                xStreamBufferSend(handle->currProgram->inputStream, &c, sizeof(char), 0);
            }else{
                if(c == 0x13){ 
                    TERM_printDebug(handle, "Stop your ctrl-s autism please, nothing to save here\r\n");
                }else{
                    TERM_printDebug(handle, "unknown code received: 0x%02x\r\n", c);
                }
            }
            break;
#else
        
        case 0x13:  //ctrl-s
            TERM_printDebug(handle, "Stop your ctrl-s autism please, nothing to save here\r\n");
            break;
#endif
            
        default:
            TERM_printDebug(handle, "unknown code received: 0x%02x\r\n", c);
            break;
    }

    return TERM_CMD_EXIT_SUCCESS;
}

void TERM_checkForCopy(TERMINAL_HANDLE * handle, COPYCHECK_MODE mode){
    if((mode & TERM_CHECK_COMP) && handle->autocompleteBuffer != NULL){ 
        if(handle->currAutocompleteCount != 0){
            char * dst = (char *) ((uint32_t) handle->inputBuffer + handle->autocompleteStart);
            if(strchr(handle->autocompleteBuffer[handle->currAutocompleteCount - 1], ' ') != 0){
                sprintf(dst, "\"%s\"", handle->autocompleteBuffer[handle->currAutocompleteCount - 1]);
            }else{
                strcpy(dst, handle->autocompleteBuffer[handle->currAutocompleteCount - 1]);
            }
            handle->currBufferLength = strlen(handle->inputBuffer);
            handle->currBufferPosition = handle->currBufferLength;
            handle->inputBuffer[handle->currBufferPosition] = 0;
        }
        TERM_FREE(handle->autocompleteBuffer);
        handle->autocompleteBuffer = NULL;
    }
    
    if((mode & TERM_CHECK_HIST) && handle->currHistoryWritePosition != handle->currHistoryReadPosition){
        strcpy(handle->inputBuffer, handle->historyBuffer[handle->currHistoryReadPosition]);
        handle->currBufferLength = strlen(handle->inputBuffer);
        handle->currBufferPosition = handle->currBufferLength;
        handle->currHistoryReadPosition = handle->currHistoryWritePosition;
    }
}

char * strnchr(char * str, char c, uint32_t length){
    uint32_t currPos = 0;
    for(;currPos < length && str[currPos] != 0; currPos++){
        if(str[currPos] == c) return &str[currPos];
    }
    return NULL;
}

void strsft(char * src, int32_t startByte, int32_t offset){
    if(offset == 0) return;
    
    if(offset > 0){     //shift forward
        uint32_t currPos = strlen(src) + offset;
        src[currPos--] = 0;
        for(; currPos >= startByte; currPos--){
            if(currPos == 0){
                src[currPos] = ' ';
                break;
            }
            src[currPos] = src[currPos - offset];
        }
        return;
    }else{              //shift backward
        uint32_t currPos = startByte;
        for(; src[currPos - offset] != 0; currPos++){
            src[currPos] = src[currPos - offset];
        }
        src[currPos] = src[currPos - offset];
        return;
    }
}

TermCommandDescriptor * TERM_findCMD(TERMINAL_HANDLE * handle){
    uint16_t cmdLength = handle->currBufferLength;
    
    char * firstSpace = strchr(handle->inputBuffer, ' ');
    if(firstSpace != 0){
        cmdLength = (uint16_t) ((uint32_t) firstSpace - (uint32_t) handle->inputBuffer);
    }
    
    return TERM_findCMDFromName(handle->cmdListHead, handle->inputBuffer, cmdLength);
}

TermCommandDescriptor * TERM_findCMDFromName(TermCommandDescriptor * list, char * name, uint32_t length){
    uint32_t currPos = 0;
    TermCommandDescriptor * currCmd = list->nextCmd;
    
    for(;currPos < list->commandLength; currPos++){
        if(currCmd->commandLength == length && strncmp(name, currCmd->command, length) == 0) return currCmd;
        currCmd = currCmd->nextCmd;
    }
    
    return NULL;
}

#if defined TERM_startTaskPerCommand && (!__is_compiling || __has_include("FreeRTOS.h"))
//sends a program command to the interpreter
static uint32_t TERM_sendProgCMD(TermProgram * prog, ProgCMDType_t cmd, uint32_t arg, void * data){
    Term_progCMD_t cmdStruct = {.cmd = cmd, .arg = arg, .data = data, .src = prog};
    return xQueueSend(prog->handle->cmdStream, &cmdStruct, 0);
}

//also sends a program command to the interpreter, but waits indefinetely until it fit into the queue isntead of discarding it if the queue is full
static uint32_t TERM_sendCriticalProgCMD(TermProgram * prog, ProgCMDType_t cmd, uint32_t arg, void * data){
    Term_progCMD_t cmdStruct = {.cmd = cmd, .arg = arg, .data = data, .src = prog};
    return xQueueSend(prog->handle->cmdStream, &cmdStruct, portMAX_DELAY);
}

static void TERM_programEnterForeground(TermProgram * prog){
    TERM_sendProgCMD(prog, PROG_ENTERFOREGROUND, 0, 0);
}

static uint32_t TERM_programExitForeground(TermProgram * prog){
    TERM_sendProgCMD(prog, PROG_EXITFOREGROUND, 0, 0);
}

//TODO: make this thread safe! Its likely to cause problems as is
static void TERM_programReturn(TermProgram * prog, uint8_t retCode){
    //print return string and inputbuffer if its not empty
    TERMINAL_HANDLE * handle = prog->handle;
    
    //print exit code if its not success
    if(retCode != TERM_CMD_EXIT_SUCCESS) ttprintfEcho("\r\n\nCommand \"%s\" exited with code %d\r\n", prog->commandString, retCode);
    
    //also print a new input line
    ttprintfEcho("\r\n\r\n%s@%s>", handle->currUserName, TERM_DEVICE_NAME);
    
    //return terminal (automatically frees memory and exits foreground if needed)
    TERM_sendProgCMD(prog, PROG_RETURN, retCode, 0);
}

void TERM_killProgramm(TERMINAL_HANDLE * handle){
	//TODO re-implement! Not used at the moment. Function to kill the program currently in the foreground (f.E. due to pressing ctrl+c multiple times)
}

static void TERM_cmdTask(void * pvData){
    //prepare data
    TermProgram *prog = (TermProgram *) pvData;
    uint8_t retCode = TERM_CMD_EXIT_ERROR;
    
    TERM_programEnterForeground(prog);
    
    //start with direct input mode
    TERM_sendProgCMD(prog, PROG_SETINPUTMODE, INPUTMODE_DIRECT, NULL);
    
    //run command
    if(prog->cmd->function != 0){
        retCode = (*prog->cmd->function)(prog->handle, prog->argCount, prog->args);
    }
              
    TERM_programReturn(prog, retCode);
    
    //remove task
    vTaskDelete(NULL);
    while(1);
}

char * TERM_getCommandString(){
    //get prog pointer
    TermProgram *prog = (TermProgram *) pvTaskGetCurrentTaskParameters();
    
    return prog->commandString;
}

uint16_t TERM_getChar(TERMINAL_HANDLE * handle, uint32_t timeout){
    //get prog pointer
    TermProgram *prog = (TermProgram *) pvTaskGetCurrentTaskParameters();
    uint16_t c = 0;
    
    //try to receive a character from the buffer, if we get nothing c will remain NULL
    //as we are in input mode direct we need to read 16bits from the buffer 
    if(xStreamBufferReceive(prog->inputStream, &c, sizeof(c), timeout) != sizeof(c)){
        xStreamBufferReset(prog->inputStream);
        return 0;
    }
    
    //return what we got, or NULL if we didn't get anything
    return c;
}

char * TERM_getLine(TERMINAL_HANDLE * handle, uint32_t timeout, uint32_t controlBehaviour){
    //get prog pointer
    TermProgram *prog = (TermProgram *) pvTaskGetCurrentTaskParameters();
    
    //empty out the input buffer
    xStreamBufferReset(prog->inputStream);
    
    //request input mode of GETLINE from the interpreter
    if(!TERM_sendProgCMD(prog, PROG_SETINPUTMODE, INPUTMODE_GET_LINE, NULL)){
        return NULL;
    }
    
    //now wait for the string to be read. Terminal will dump the string including a "\n" termination into the input stream
    char * ret = TERM_MALLOC(sizeof(char) * TERM_INPUTBUFFER_SIZE);
    char c = 0;
    
    //contains the reason we broke the loop. If this is 0 the loop exited normally and the string needs to be returned
    uint32_t breakCause = 0;
    
    uint32_t currPos = 0;
    while(1){
        //why do we read 8bit words from the buffer? In INPUTMODE_GET_LINE the input parser runs and deals with all vt100 character, so no 16bit words would ever be in the buffer. 
        //Plus all writes in this mode are limited to 8bit only
        if(xStreamBufferReceive(prog->inputStream, &c, sizeof(c), timeout) == 0){
            breakCause = 0xff;
            break;
        }else{
            if(c == '\n'){ 
                //terminate string
                ret[currPos] = 0;
                break;
                
            //is c a control character?
            }else if(c != 0 && c < 32){
                //yes => check what we need to do
                if(controlBehaviour == TERM_CONTROL_CANCEL){
                    //cancel input. To tell the task what happened we write the char to the beginning and terminate the string afterwards
                    ret[0] = c;
                    ret[1] = 0;
                    currPos = 1;
                    break;
                    
                }else if(controlBehaviour == TERM_CONTROL_ENDLINE_KEEP){
                    //end the line upon writing of a control character but write the control character into the buffer
                    
                    if(currPos == TERM_INPUTBUFFER_SIZE-1){
                        //not enough space in the buffer for two chars :(
                        
                        //just terminate the string here
                        ret[currPos] = 0;
                    }else{
                        //write the cc into the buffer
                        ret[currPos] = c;
                        currPos++;
                        //terminate the string
                        ret[currPos] = 0;
                    }
                    
                    //break the loop
                    break;
                        
                }else if(controlBehaviour == TERM_CONTROL_ENDLINE_DISCARD){
                    //end the line upon writing of a control character and end the line like usually
                    ret[currPos] = 0;
                    break;
                        
                }else{
                    //default / TERM_CONTROL_IGNORE
                    
                    //is c "ctrl+c"?
                    if(c == CTRL_C){ 
                        //yes => since the program wouldn't recognise this we need to stop input ourselves
                        ret[currPos] = 0;
                        breakCause = 0xff;
                        break;
                    }

                    //treat the character like a normal char and write it to the buffer
                    ret[currPos++] = c;
                }
            }else{
                ret[currPos++] = c;
            }
            
            if(currPos == TERM_INPUTBUFFER_SIZE){ 
                //terminate string
                ret[currPos-1] = 0;
                
                break;
            }
        }
    }    
   
    //reset inputmode
    TERM_sendProgCMD(prog, PROG_SETINPUTMODE, INPUTMODE_DIRECT, NULL);
	
    if(breakCause == 0xff){
        //timeout or other error
        
        //free buffer
        TERM_FREE(ret);
        
        return NULL;
    }
    
    return ret;
}
#endif

uint8_t TERM_interpretCMD(char * data, uint16_t dataLength, TERMINAL_HANDLE * handle){
    
    TermCommandDescriptor * cmd = TERM_findCMD(handle);
    
    if(cmd != 0){
        uint16_t argCount = TERM_countArgs(data, dataLength);
        if(argCount == TERM_ARGS_ERROR_STRING_LITERAL){
            ttprintfEcho("\r\nError: unclosed string literal in command\r\n");
            return TERM_CMD_EXIT_ERROR;
        }
        
        char * dataPtr;

#if defined TERM_startTaskPerCommand && (!__is_compiling || __has_include("FreeRTOS.h"))
        //allocate persistent memory for args and copy them
        dataPtr = TERM_MALLOC(dataLength + 1);
        dataPtr[dataLength] = 0; //we only need to set the string terminator to 0, the rest will be set by memcpy
        memcpy(dataPtr, data, dataLength);
#else
        //just assign the data pointer to the data
        dataPtr = data;
#endif
        
        //seperate the arguments. The pointer returned is going to contain pointers to parts of the dataPrt array
        char ** args = 0;
        if(argCount != 0){
            args = TERM_MALLOC(sizeof(char*) * argCount);
            TERM_seperateArgs(dataPtr, dataLength, args);
        }

#if defined TERM_startTaskPerCommand && (!__is_compiling || __has_include("FreeRTOS.h"))
        TermProgram * program = TERM_MALLOC(sizeof(TermProgram));
        memset(program, 0, sizeof(TermProgram));
        
        //assign data pointers
        program->argCount = argCount;
        program->commandString = dataPtr;
        program->args = args;
        
        //assign command info
        program->cmd = cmd;
        program->handle = handle;
        
        //program->returnCode = TERM_CMD_PROC_RUNNING;
        
        program->inputStream = xStreamBufferCreate(TERM_PROG_BUFFER_SIZE,1);
        program->cmdStream = xQueueCreate(5, sizeof(Term_progCMD_t));
        
        if(xTaskCreate(TERM_cmdTask, cmd->command, cmd->stackSize, (void*) program, tskIDLE_PRIORITY + 1, &program->task) == pdPASS){
            //also send the programm into the foreground, to make sure no other one will be started until it is done
            TERM_sendProgCMD(program, PROG_ENTERFOREGROUND, 0, 0);
            return TERM_CMD_EXIT_PROC_STARTED;
        }else{
            return TERM_CMD_EXIT_ERROR;
        }
#else
        uint8_t retCode = TERM_CMD_EXIT_ERROR;
        if(cmd->function != 0){
            retCode = (*cmd->function)(handle, argCount, args);
        }

        if(argCount != 0) TERM_FREE(args);
        return retCode;
#endif      
    }
    
    return TERM_CMD_EXIT_NOT_FOUND;
}

uint8_t TERM_seperateArgs(char * data, uint16_t dataLength, char ** buff){
    uint8_t count = 0;
    uint8_t currPos = 0;
    unsigned quoteMark = 0;
    char * currStringStart = 0;
    char * lastSpace = 0;
    for(;currPos<dataLength; currPos++){
        switch(data[currPos]){
            case ' ':
                if(!quoteMark){
                    data[currPos] = 0;
                    lastSpace = &data[currPos + 1];
                }
                break;
                
            case '"':
                if(quoteMark){
                    quoteMark = 0;
                    if(currStringStart){
                        data[currPos] = 0;
                        buff[count++] = currStringStart;
                    }
                }else{
                    quoteMark = 1;
                    currStringStart = &data[currPos+1];
                }
                        
                break;
                
            default:
                if(!quoteMark){
                    if(lastSpace != 0){
                        buff[count++] = lastSpace;
                        lastSpace = 0;
                    }
                }
                break;
        }
    }
    if(quoteMark) TERM_ARGS_ERROR_STRING_LITERAL;
    return count;
}

uint16_t TERM_countArgs(const char * data, uint16_t dataLength){
    uint8_t count = 0;
    uint8_t currPos = 0;
    unsigned quoteMark = 0;
    const char * currStringStart = NULL;
    const char * lastSpace = NULL;
    for(;currPos<dataLength; currPos++){
        switch(data[currPos]){
            case ' ':
                if(!quoteMark){
                    lastSpace = &data[currPos + 1];
                }
                break;
                
            case '"':
                if(quoteMark){
                    quoteMark = 0;
                    if(currStringStart){
                        count ++;
                    }
                }else{
                    quoteMark = 1;
                    currStringStart = &data[currPos+1];
                }
                        
                break;
                
            default:
                if(!quoteMark){
                    if(lastSpace){
                        count ++;
                        lastSpace = NULL;
                    }
                }
                break;
        }
    }
    if(quoteMark) TERM_ARGS_ERROR_STRING_LITERAL;
    return count;
}

void TERM_freeCommandList(TermCommandDescriptor ** cl, uint16_t length){
    /*uint8_t currPos = 0;
    for(;currPos < length; currPos++){
        TERM_FREE(cl[currPos]);
    }*/
    TERM_FREE(cl);
}

TermCommandDescriptor * TERM_addCommand(TermCommandFunction function, const char * command, const char * description, uint32_t stackSize, TermCommandDescriptor * head){
    //if(head == NULL) head = TERM_defaultList;
        
    if(head->commandLength == 0xff) return 0;
    
    TermCommandDescriptor * newCMD = TERM_MALLOC(sizeof(TermCommandDescriptor));
    
    newCMD->command = command;
    newCMD->commandDescription = description;
    newCMD->commandLength = strlen(command);
    newCMD->function = function;
    newCMD->ACHandler = 0;
    
#ifdef TERM_startTaskPerCommand
    
    //TODO add default stack size define
    newCMD->stackSize = (stackSize == 0) ? configMINIMAL_STACK_SIZE + 500 : stackSize;
    
#endif
    
    TERM_LIST_add(newCMD, head);
    return newCMD;
}

void TERM_LIST_add(TermCommandDescriptor * item, TermCommandDescriptor * head){
    uint32_t currPos = 0;
    TermCommandDescriptor ** lastComp = &head->nextCmd;
    TermCommandDescriptor * currComp = head->nextCmd;
    
    while(currPos < head->commandLength){
        if(TERM_isSorted(currComp, item)){
            *lastComp = item;
            item->nextCmd = currComp;
            head->commandLength ++;
            
            return;
        }
        if(currComp->nextCmd == 0){
            item->nextCmd = currComp->nextCmd;
            currComp->nextCmd = item;
            head->commandLength ++;
            return;
        }
        lastComp = &currComp->nextCmd;
        currComp = currComp->nextCmd;
    }
    
    item->nextCmd = 0;
    *lastComp = item;
    head->commandLength ++;
}
/*
void ACL_remove(AC_LIST_HEAD * head, char * string){
    if(head->isConst || head->elementCount == 0) return;
    uint32_t currPos = 0;
    AC_LIST_ELEMENT ** lastComp = &head->first;
    AC_LIST_ELEMENT * currComp = head->first;
    
    for(;currPos < head->elementCount; currPos++){
        if((strlen(currComp->string) == strlen(string)) && (strcmp(currComp->string, string) == 0)){
            *lastComp = currComp->next;
            
            //TODO make this portable
            if(ptr_is_in_ram(currComp->string)){
                TERM_FREE(currComp->string);
            }
            
            TERM_FREE(currComp);
            head->elementCount --;
            return;
        }
        if(currComp->next == 0) break;
        lastComp = &currComp->next;
        currComp = currComp->next;
    }
}
*/

unsigned TERM_isSorted(TermCommandDescriptor * a, TermCommandDescriptor * b){
    uint8_t currPos = 0;
    //compare the lowercase ASCII values of each character in the command (They are alphabetically sorted)
    for(;currPos < a->commandLength && currPos < b->commandLength; currPos++){
        char letterA = toLowerCase(a->command[currPos]);
        char letterB = toLowerCase(b->command[currPos]);
        //if the letters are different we return 1 if a is smaller than b (they are correctly sorted) or zero if its the other way around
        if(letterA > letterB){
            return 1;
        }else if(letterB > letterA){
            return 0;
        }
    }
    
    //the two commands have identical letters for their entire length we check which one is longer (the shortest should come first)
    if(a->commandLength > b->commandLength){
        return 1;
    }else if(b->commandLength > a->commandLength){
        return 0;
    }else{
        //it might happen that a command is added twice (or two identical ones are added), in which case we just say they are sorted correctly and print an error in the console
        //TODO implement an alarm here
        //UART_print("WARNING: Found identical commands: \"%S\" and \"%S\"\r\n", a->command, b->command);        
        return 1;
    }
}

char toLowerCase(char c){
    if(c > 65 && c < 90){
        return c + 32;
    }
    
    //TODO: eeeehhhm wtf git? I know encoding is hard but upper and lower case umlauts should definetely be handled xD
    switch(c){
        case 'Ä':
            return 'ä';
        case 'Ö':
            return 'ö';
        case 'Ü':
            return 'ü';
        default:
            return c;
    }
}

void TERM_setCursorPos(TERMINAL_HANDLE * handle, uint16_t x, uint16_t y){
    
}

void TERM_sendVT100Code(TERMINAL_HANDLE * handle, uint16_t cmd, uint8_t var){
    switch(cmd){
        case _VT100_RESET:
            ttprintfEcho("%cc", 0x1b);
            break;
        case _VT100_CURSOR_BACK:
            ttprintfEcho("\x1b[D");
            break;
        case _VT100_CURSOR_FORWARD:
            ttprintfEcho("\x1b[C");
            break;
        case _VT100_CURSOR_POS1:
            ttprintfEcho("\x1b[H");
            break;
        case _VT100_CURSOR_END:
            ttprintfEcho("\x1b[F");
            break;
        case _VT100_FOREGROUND_COLOR:
            ttprintfEcho("\x1b[%dm", var+30);
            break;
        case _VT100_BACKGROUND_COLOR:
            ttprintfEcho("\x1b[%dm", var+40);
            break;
        case _VT100_RESET_ATTRIB:
            ttprintfEcho("\x1b[0m");
            break;
        case _VT100_BRIGHT:
            ttprintfEcho("\x1b[1m");
            break;
        case _VT100_DIM:
            ttprintfEcho("\x1b[2m");
            break;
        case _VT100_UNDERSCORE:
            ttprintfEcho("\x1b[4m");
            break;
        case _VT100_BLINK:
            ttprintfEcho("\x1b[5m");
            break;
        case _VT100_REVERSE:
            ttprintfEcho("\x1b[7m");
            break;
        case _VT100_HIDDEN:
            ttprintfEcho("\x1b[8m");
            break;
        case _VT100_ERASE_SCREEN:
            ttprintfEcho("\x1b[2J");
            break;
        case _VT100_ERASE_LINE:
            ttprintfEcho("\x1b[2K");
            break;
        case _VT100_FONT_G0:
            ttprintfEcho("\x1b(");
            break;
        case _VT100_FONT_G1:
            ttprintfEcho("\x1b)");
            break;
        case _VT100_WRAP_ON:
            ttprintfEcho("\x1b[7h");
            break;
        case _VT100_WRAP_OFF:
            ttprintfEcho("\x1b[7l");
            break;
        case _VT100_ERASE_LINE_END:
            ttprintfEcho("\x1b[K");
            break;
        case _VT100_CURSOR_BACK_BY:
            ttprintfEcho("\x1b[%dD", var);
            break;
        case _VT100_CURSOR_FORWARD_BY:
            ttprintfEcho("\x1b[%dC", var);
            break;
        case _VT100_CURSOR_SAVE_POSITION:
            ttprintfEcho("\x1b" "7");
            break;
        case _VT100_CURSOR_RESTORE_POSITION:
            ttprintfEcho("\x1b" "8");
            break;
        case _VT100_CURSOR_ENABLE:
            ttprintfEcho("\x1b[?25h");
            break;
        case _VT100_CURSOR_DISABLE:
            ttprintfEcho("\x1b[?25l");
            break;
        case _VT100_CLS:
            ttprintfEcho("\x1b[2J\033[1;1H");
            break;
        case _VT100_CURSOR_DOWN_BY:
            ttprintfEcho("\x1b[%dB", var);
            break;
        case _VT100_CURSOR_UP_BY:
            ttprintfEcho("\x1b[%dA", var);
            break;
            
    }
}

//returns a static pointer to the requested VT100 code, so it can be used in printf without needing any free() call afterwards
const char * TERM_getVT100Code(uint16_t cmd, uint8_t var){
    //INFO this is deprecated since all of the VT100 functions are handled by the terminal now
    switch(cmd){
        case _VT100_RESET:
            return "\x1b" "C";
            
        case _VT100_CURSOR_BACK:
            return "\x1b[D";
            
        case _VT100_CURSOR_FORWARD:
            return "\x1b[C";
            
        case _VT100_CURSOR_POS1:
            return "\x1b[H";
            
        case _VT100_CURSOR_END:
            return "\x1b[F";
        case _VT100_FOREGROUND_COLOR:
            switch(var){
                case 0:
                    return "\x1b[30m";
                case 1:
                    return "\x1b[31m";
                case 2:
                    return "\x1b[32m";
                case 3:
                    return "\x1b[33m";
                case 4:
                    return "\x1b[34m";
                case 5:
                    return "\x1b[35m";
                case 6:
                    return "\x1b[36m";
                case 7:
                    return "\x1b[37m";
                default:
                    return "\x1b[30m";
            }
            
        case _VT100_BACKGROUND_COLOR:
            switch(var){
                case 0:
                    return "\x1b[40m";
                case 1:
                    return "\x1b[41m";
                case 2:
                    return "\x1b[42m";
                case 3:
                    return "\x1b[43m";
                case 4:
                    return "\x1b[44m";
                case 5:
                    return "\x1b[45m";
                case 6:
                    return "\x1b[46m";
                case 7:
                    return "\x1b[47m";
                default:
                    return "\x1b[40m";
            }
            
        case _VT100_RESET_ATTRIB:
            return "\x1b[0m";
            
        case _VT100_BRIGHT:
            return "\x1b[1m";
            
        case _VT100_DIM:
            return "\x1b[2m";
            
        case _VT100_UNDERSCORE:
            return "\x1b[4m";
            
        case _VT100_BLINK:
            return "\x1b[5m";
            
        case _VT100_REVERSE:
            return "\x1b[7m";
            
        case _VT100_HIDDEN:
            return "\x1b[8m";
            
        case _VT100_ERASE_SCREEN:
            return "\x1b[2J";
            
        case _VT100_ERASE_LINE:
            return "\x1b[2K";
            
        case _VT100_FONT_G0:
            return "\x1b(";
            
        case _VT100_FONT_G1:
            return "\x1b)";
            
        case _VT100_WRAP_ON:
            return "\x1b[7h";
            
        case _VT100_WRAP_OFF:
            return "\x1b[71";
            
        case _VT100_ERASE_LINE_END:
            return "\x1b[K";
            
        case _VT100_CURSOR_ENABLE:
            return "\x1b[?25h";
            
        case _VT100_CURSOR_DISABLE:
            return "\x1b[?25l";
            
        case _VT100_CLS:
            return "\x1b[2J\033[1;1H";
            
            
    }
    return "";
}
/*
void TERM_attachProgramm(TERMINAL_HANDLE * handle, TermProgram * prog){
    //TODO re-implement this for terminals without taskPerCommand
	ttprintf("INVALD SHIT CALLED OIufhglifdoifd :(\r\n\n");
    //handle->currProgram = prog;
    //handle->currProgram->inputStream = xStreamBufferCreate(TERM_PROG_BUFFER_SIZE,1);
}

void TERM_removeProgramm(TERMINAL_HANDLE * handle){
    handle->currProgram = NULL;
}
*/

