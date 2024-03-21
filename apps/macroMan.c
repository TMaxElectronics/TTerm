/*
 * TTerm
 *
 * Copyright (c) 2020 Thorben Zethoff, Jens Kerrinnes
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

#include <stdlib.h>

#include "TTerm.h"
#include "TTerm_AC.h"
#include "string.h"
#include "ConMan.h"
#include "System.h"

#define APP_NAME "macro"
#define APP_DESCRIPTION "allows one to create console macros"
#define APP_STACK 550

#define MacroMan_ListSize 4
#define MacroMan_Version 1
#define MacroMan_List 0
#define MacroMan_NameLength 16
#define MacroMan_MaxCommands 16

static uint8_t CMD_main(TERMINAL_HANDLE * handle, uint8_t argCount, char ** args);
static uint8_t MacroMan_macroCommand(TERMINAL_HANDLE * handle, uint8_t argCount, char ** args);
static uint32_t addMacroToList(char * macroName, char * macroDescription, char * macroLengthBytes);
static uint32_t getListSpaces();

static const char * AC_start_stop[] = {
    "-add",
    "-list",
    "-remove"
};

typedef struct{
    char name[MacroMan_NameLength];
    char description[MacroMan_NameLength];
    uint32_t commandLength;
    uint32_t reserved[3];
} MacroMan_ListItem_t;

static MacroMan_ListItem_t macroList[MacroMan_ListSize];
static char * currentMacroLines[MacroMan_MaxCommands] = {[0 ... (MacroMan_MaxCommands-1)] = NULL};
static uint32_t currentMacroLineCount = 0;
static uint32_t currentMactoLength = 0;

//#if __has_include("ConMan.h")

static ConMan_Result_t MacroMan_configCallback(ConMan_Result_t evt, ConMan_CallbackData_t * data){
    ConMan_CallbackData_t * cbd = (ConMan_CallbackData_t *) data;
            
    //check which event was sent to us
    if(evt == CONFIG_ENTRY_CREATED){
        //a configuration was just created, populate default values
        if((uint32_t) cbd->userData == MacroMan_List){
            ConMan_writeData(cbd->callbackData, 0, (uint8_t*) &macroList, sizeof(MacroMan_ListItem_t) * MacroMan_ListSize);
        }else{
            //a macro was just created, write the data from the line buffers
            if(currentMacroLineCount != 0){
                //a macro is actually ready to be written
                uint32_t currentPos = 0;
                for(uint32_t i = 0; i < currentMacroLineCount; i++){
                    if(currentMacroLines[i] == NULL) break;
                    //write the current line into NVM, replacing the null termination with a \n
                    
                    uint32_t currentLineLength = strlen(currentMacroLines[i]);
                    if(currentLineLength == 0) break;
                    
                    //TERM_printDebug(TERM_handle, "Writing \"%s\" to %d len=%d\r\n", currentMacroLines[i], currentPos, currentLineLength);
            
                    //replace null terminator with \n TODO evaluate risk of doing this. It _should_ be ok as no string operation follows after this point
                    //this byte is also always part of the buffer regardless of length as the null termination must have been too
                    currentMacroLines[i][currentLineLength] = '\n';
                    
                    //write the data. Length needs to be len+1 to also copy the \n terminator
                    if(ConMan_writeData(cbd->callbackData, currentPos, (uint8_t*) currentMacroLines[i], currentLineLength+1) != CONFIG_OK) TERM_printDebug(TERM_handle, "Write error\r\n");
                    
                    //count up the current position
                    currentPos += currentLineLength+1;
                    
                    //and finally free the line buffer
                    TERM_FREE(currentMacroLines[i]);
                }
                
                if(ConMan_writeData(cbd->callbackData, currentPos, (uint8_t*) "\0", 1) != CONFIG_OK) TERM_printDebug(TERM_handle, "Write error\r\n");
                
                //now that the macro has been written and all data free'd we can reset the line count
                currentMacroLineCount = 0;
            }else{
                return CONFIG_ERROR;
            }
        }
        
    }else if(evt == CONFIG_ENTRY_LOADED){
        //a configuration was just loaded
        if((uint32_t) cbd->userData == MacroMan_List){
            //load the macro list
            ConMan_readData(cbd->callbackData, 0, (uint8_t*) &macroList, sizeof(MacroMan_ListItem_t) * MacroMan_ListSize);
            
            //scan through the list and also add the command names to conman
            for(uint32_t currMacro = 0; currMacro < MacroMan_ListSize; currMacro ++){
                //check if the current list entry actually contains a string
                if(macroList[currMacro].name[0] != 0){
                    //yes! Tell conman that that parameter is ours too
                    ConMan_addParameter(macroList[currMacro].name, macroList[currMacro].commandLength, MacroMan_configCallback, (void*) (currMacro+1), MacroMan_Version);
                }
            }
        }else{
            //and then add the command to the terminal
            TERM_addCommand(MacroMan_macroCommand, macroList[(uint32_t) cbd->userData - 1].name, macroList[(uint32_t) cbd->userData - 1].description, 0, &TERM_defaultList);
            TERM_printDebug(TERM_handle, "Added a Macro to TTerm: \"%s\"\r\n", macroList[(uint32_t) cbd->userData - 1].name);
        }
        
    }else if(evt == CONFIG_ENTRY_UPDATED){
        //TODO
    
    }else if(evt == CONFIG_VERIFY_VALUE){
        //TODO (or leave unimplemented? we aren't using this yet anyway aren't we?)
    }else{
        configASSERT(0);
    }
    
    return CONFIG_OK;
}
//#endif

uint8_t REGISTER_macroMan(TermCommandDescriptor * desc){
#if __has_include("FreeRTOS.h") && __has_include("ConMan.h")
    TERM_addCommandConstAC(CMD_main, APP_NAME, APP_DESCRIPTION, AC_start_stop,desc);
    
    //we also need to add the macro list parameter
    ConMan_addParameter("MacroList", sizeof(MacroMan_ListItem_t) * MacroMan_ListSize, MacroMan_configCallback, (void*) MacroMan_List, MacroMan_Version);
#endif
}

//#if __has_include("ConMan.h")
static uint8_t CMD_main(TERMINAL_HANDLE * handle, uint8_t argCount, char ** args){
    uint32_t add = 0;
    uint32_t list = 0;
    uint32_t remove = 0;
    
    for(uint32_t currArg = 0;currArg<argCount; currArg++){
        if(strcmp(args[currArg], "-?") == 0){
            ttprintf("a utility for various benchmarks\r\n");
            ttprintf("usage:\r\n");
            ttprintf("\tmacro [option]\r\n");
            ttprintf("\t\t options:\r\n");
            ttprintf("\t\t\t -add \t Adds a new Macro (default option if nothing else is selected)\r\n");
            ttprintf("\t\t\t -list / -ls [name]\t Lists all macros or prints an existing one\r\n");
            ttprintf("\t\t\t -remove / -rem [name]\t removes a macro\r\n");
    
            return TERM_CMD_EXIT_SUCCESS;
        }else if(strcmp(args[currArg], "-add") == 0){
            add = 1;
            
        }else if(strcmp(args[currArg], "-list") == 0 || strcmp(args[currArg], "-ls") == 0){
            list = 1;
            
            //check if info on a specific command is requested (do we have enough args and is the next one not also a parameter)
            if(argCount > currArg + 1 && strchr(args[currArg+1], '-') == NULL){
                //yes, check if the macro exists
                ConMan_ParameterDescriptor_t * currentDescriptor = ConMan_getParameterDescriptor(args[currArg+1]);
                if(currentDescriptor != NULL){
                    //yes macro exists :) get the pointer to the data string
                    volatile char * macroString = (char*) ConMan_getDataPtr(currentDescriptor);
                    
                    ttprintf("at 0x%08x\r\n", macroString);
                    ttprintf("Contents of Macro \"%s\":\r\n\t", args[currArg+1]);
                    
                    //step through the data and print out whats in the buffer (do this manually even though its slow to allow for rewriting of the newline char)
                    for(uint32_t i = 0; i < currentDescriptor->dataSizeBytes; i++){
                        char currC = macroString[i];
                        //has the string ended? (might be before the end as given by dataSize due to rounding to word boundaries)
                        if(currC == 0) break;
                        
                        //print the char (TODO: maybe improve this? it is incredibly inefficient after all xD)
                        if(currC == '\n'){
                            //newline => add the \r to the string for correct printout in the console
                            ttprintf("\r\n\t");
                        }else{
                            ttprintf("%c", currC);
                        }
                    }
                    ttprintf("\r\n---end---\r\n");
                }else{
                        ttprintf("\r\n---end---\r\n");
                    ttprintf("MacroMan Error: Macro \"%s\" not found!\r\n", args[currArg+1]);
                }
            }else{
                //no, user wants to see a list with macros
                //scan through the list and print the macro
                ttprintf("MacroMan Macro List:\r\n");
                for(uint32_t currMacro = 0; currMacro < MacroMan_ListSize; currMacro ++){
                    if(macroList[currMacro].name[0] != 0){
                        ttprintf("\t%s\t%s\r\n", macroList[currMacro].name, macroList[currMacro].description);
                    }
                }
            }
            
        }else if(strcmp(args[currArg], "-remove") == 0 || strcmp(args[currArg], "-rem") == 0){
            remove = 1;
        }
    }
    
    if(add || (!list && ! remove)){
        //can we even add one right now or is a macro waiting to be written?
        if(currentMacroLineCount != 0){
            goto addError;
            ttprintf("Macro buffer is currently in use!");
        }
        
        //is there even space for another macro?
        if(getListSpaces() == 0){
            //no :(
            goto addError;
            ttprintf("Sorry but all macros spots are used up :(");
        }
        
        //add a macro
//enter macro name
        ttprintf("Enter new macro name: ");
        
        //let the user enter the name
        char * macroName = ttgetline(portMAX_DELAY);
        
        if(macroName == NULL || macroName[0] == 0){
            ttprintf("A name is required!");
            goto addError;
        }
        
        //check for name length < maxLength
        if(strlen(macroName)+1 >= MacroMan_NameLength){
            TERM_FREE(macroName);
            ttprintf("name is too long (max %d)!", MacroMan_NameLength);
            goto addError;
        }
        
        //check for invalid special characters
        char * c = macroName;
        do{
            if(*c == "-") break;
        }while(*(++c) != 0);

        if(*c != 0){
            TERM_FREE(macroName);
            ttprintf("That name is invalid :(\r\n");
            goto addError;
        }
        
        //check if the name is already an existing terminal command
        if(TERM_findCMDFromName(&TERM_defaultList, macroName, strlen(macroName)) != NULL){
            TERM_FREE(macroName);
            ttprintf("That name is already an existing TTerm Command\r\n");
            goto addError;
        }

        //check if the name is already used
        for(uint32_t currMacro = 0; currMacro < MacroMan_ListSize; currMacro ++){
            if(strcmp(macroList[currMacro].name, macroName) == 0){
                ttprintf("That Macro already exists!");
                TERM_FREE(macroName);
                goto addError;
            }
        }
        
        
//enter macro description
        
        ttprintf("Enter description for macro \"%s\": ", macroName);
        
        //name valid. Now enter the macro description
        char * macroDescription = ttgetline(portMAX_DELAY);
        
        //check for name length < maxLength
        if(macroDescription != NULL && strlen(macroName)+1 >= MacroMan_NameLength){
            TERM_FREE(macroName);
            TERM_FREE(macroDescription);
            ttprintf("description is too long (max %d)!", MacroMan_NameLength);
            goto addError;
        }
        
//enter macro content

        ttprintf("Enter macro commands and exit with ctrl+d:\r\n");

        currentMacroLineCount = 0;

        uint32_t success = 0;

        while(1){
            if(currentMacroLines == MacroMan_MaxCommands){ 
                ttprintf("---line entry error, too many lines entered (max %d)---\r\n", MacroMan_MaxCommands);
                break;
            }
            
            //check if by any chance the buffer remained allocated
            if(currentMacroLines[currentMacroLineCount] != NULL) TERM_FREE(currentMacroLines[currentMacroLineCount]);
            
            currentMacroLines[currentMacroLineCount] = ttgetlineSpecial(portMAX_DELAY, TERM_CONTROL_ENDLINE_KEEP);

            //check if we got a string back
            if(currentMacroLines[currentMacroLineCount] == NULL){
                //no... assume user aborted macro entry somehow
                ttprintf("---line entry error, no macro added---\r\n");
                
            }else{
                
                //yes, now check if it ended with a control char
                char * lastChar = &currentMacroLines[currentMacroLineCount][strlen(currentMacroLines[currentMacroLineCount]) - 1];
                currentMacroLineCount++;
                
                if(*lastChar == CTRL_D){
                    //ctrl + d ended the line => user finished macro entry
                    success = 1;
                    
                    //make sure to remove the control char from the string
                    *lastChar = 0;
                    
                    ttprintf("---end---\r\n");
                    break;

                }else if(lastChar == CTRL_C){
                    //ctrl + c ended the line => user cancelled macro entry
                    success = 0;
                    ttprintf("---cancelled, no macro added---\r\n");
                    break;

                }
            }
        }

        if(success){
            //entry completed

            //create a single string from all lines
            uint32_t macroLengthChars = 0;
            for(uint32_t i = 0; i < currentMacroLineCount; i++){
                //is there data in the current line?
                if(currentMacroLines[i] == NULL) break; //no => finished scanning macro

                //yes => add length of string including the required \n to the length
                macroLengthChars += strlen(currentMacroLines[i]) + 1;
            }

            //round length to word size for conman. We do potentially waste up to 4 bytes here due to lazy round up...
            uint32_t macroLengthBytes = ((macroLengthChars / sizeof(uint32_t))+1) * sizeof(uint32_t);

            //add macro to the list
            uint32_t id = addMacroToList(macroName, macroDescription, macroLengthBytes);
            
            if(id == 0){
                //there wasn't any space in the list :(
                ttprintf("Sorry but all macros spots are used up, despite being available before :(");
            }
            
            //and finally add the macro to the config data and update the list
            ConMan_addParameter(macroName, macroLengthBytes, MacroMan_configCallback, (void *) id, MacroMan_Version);
            ConMan_updateParameter("MacroList", 0, macroList, sizeof(MacroMan_ListItem_t) * MacroMan_ListSize, MacroMan_Version);
        }else{
            //no bueno => free everything
            for(uint32_t i = 0; i < currentMacroLineCount; i++){
                //is there data in the current line?
                if(currentMacroLines[i] == NULL) break; //no => finished scanning macro

                TERM_FREE(currentMacroLines[i]);
            }
            
            currentMacroLineCount = 0;
        }
            
        //free name and description in any case at this point
        TERM_FREE(macroName);
        if(macroDescription != NULL) TERM_FREE(macroDescription);
        
addError:
        ttprintf("\r\n");
        
    }
    
    if(list){
        
    }
    
    return TERM_CMD_EXIT_SUCCESS;
}

static uint32_t addMacroToList(char * macroName, char * macroDescription, char * macroLengthBytes){
    //find a free spot in the macro list
    for(uint32_t currMacro = 0; currMacro < MacroMan_ListSize; currMacro ++){
        if(macroList[currMacro].commandLength == 0){
            //found a free spot in the list, copy data
            macroList[currMacro].commandLength = macroLengthBytes;
            
            strncpy(macroList[currMacro].name, macroName, MacroMan_NameLength);
            
            if(macroDescription == NULL){
                strncpy(macroList[currMacro].description, "{empty}", MacroMan_NameLength);
            }else{
                strncpy(macroList[currMacro].description, macroDescription, MacroMan_NameLength);
            }
            return currMacro+1;
        }
    }
    
    return 0;
}

static uint32_t getListSpaces(){
    //find number of free spots in the macro list
    
    uint32_t spots = 0;
    for(uint32_t currMacro = 0; currMacro < MacroMan_ListSize; currMacro ++){
        if(macroList[currMacro].commandLength == 0){
            spots++;
        }
    }
    
    return spots;
}

static uint8_t MacroMan_macroCommand(TERMINAL_HANDLE * handle, uint8_t argCount, char ** args){
    ttprintf("a macro was executed! \"%s\"\r\n", TERM_getCommandString());
    
    char * command = TERM_getCommandString();
    
    char * currentLine = NULL;
    
    //try to find the macro
    ConMan_ParameterDescriptor_t * currentDescriptor = ConMan_getParameterDescriptor(command);
    if(currentDescriptor != NULL){
        //yes macro exists :) get the pointer to the data string
        volatile char * macroString = (char*) ConMan_getDataPtr(currentDescriptor);

        ttprintf("Contents of Macro \"%s\":\r\n\t", command);

        //step through the data and print out whats in the buffer (do this manually even though its slow to allow for rewriting of the newline char)
        for(uint32_t i = 0; i < currentDescriptor->dataSizeBytes; i++){
            char currC = macroString[i];
            //has the string ended? (might be before the end as given by dataSize due to rounding to word boundaries)
            if(currC == 0) break;

            //print the char (TODO: maybe improve this? it is incredibly inefficient after all xD)
            if(currC == '\n'){
                //newline => add the \r to the string for correct printout in the console
                ttprintf("\r\n\t");
            }else{
                ttprintf("%c", currC);
            }
        }
        ttprintf("\r\n---end---\r\n");
    }else{
            ttprintf("\r\n---end---\r\n");
        ttprintf("MacroMan Error: Macro \"%s\" not found!\r\n", command);
    }
}

//#endif