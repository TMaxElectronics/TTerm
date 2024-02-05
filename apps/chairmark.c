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
#ifdef TERM_SUPPORT_CWD 
#include "ff.h"
#include "DMAconfig.h"
#endif

#define APP_NAME "chairMark"
#define APP_DESCRIPTION "almost a benchmark"
#define APP_STACK 550

static uint8_t CMD_main(TERMINAL_HANDLE * handle, uint8_t argCount, char ** args);
void TASK_main(void *pvParameters);
uint8_t INPUT_handler(TERMINAL_HANDLE * handle, uint16_t c);

static const char * AC_start_stop[] = {
    "-all",
    "-cpu",
    "-disp",
    "-fileIO",
    "-fpu",
    "-term",
    "fast"
};

#define CM_FILEIO_FILESIZE 15000

uint8_t REGISTER_chairMark(TermCommandDescriptor * desc){
    TERM_addCommandConstAC(CMD_main, APP_NAME, APP_DESCRIPTION, AC_start_stop,desc);
}

static uint8_t CMD_main(TERMINAL_HANDLE * handle, uint8_t argCount, char ** args){
    uint8_t currArg = 0;
    uint32_t CPUBenchmarkEnabled = 0;
    uint32_t FPUBenchmarkEnabled = 0;
    uint32_t FileIOBenchmarkEnabled = 0;
    uint32_t FileIOBenchmarkFastModeEnabled = 0;
    uint32_t TerminalBenchmarkEnabled = 0;
    uint32_t DisplaybufferBenchmarkEnabled = 0;
    
    for(;currArg<argCount; currArg++){
        if(strcmp(args[currArg], "-?") == 0){
            ttprintf("a utility for various benchmarks\r\n");
            ttprintf("usage:\r\n");
            ttprintf("\tchairMark [option]\r\n");
            ttprintf("\t\t options:\r\n");
            ttprintf("\t\t\t -cpu \t Tests raw instruction throughput\r\n");
            ttprintf("\t\t\t -fpu \t test fpu throughput\r\n");
            ttprintf("\t\t\t -fileIO [fileSize] [fast]\t tests external storage performance\r\n");
            ttprintf("\t\t\t -term \t tests terminal printing speed\r\n");
            ttprintf("\t\t\t -disp \t tests display buffer performance\r\n");
            ttprintf("\t\t\t -all \t tests everything\r\n");
    
            return TERM_CMD_EXIT_SUCCESS;
        }
        
        if(strcmp(args[currArg], "-all") == 0){
            CPUBenchmarkEnabled = 1;
            FPUBenchmarkEnabled = 1;
            FileIOBenchmarkEnabled = 1;
            TerminalBenchmarkEnabled = 1;
            DisplaybufferBenchmarkEnabled = 1;
        }
        
        if(strcmp(args[currArg], "-cpu") == 0){
            CPUBenchmarkEnabled = 1;
        }
        
        if(strcmp(args[currArg], "-fpu") == 0){
            FPUBenchmarkEnabled = 1;
        }
        
        if(strcmp(args[currArg], "-fileIO") == 0){
            FileIOBenchmarkEnabled = 1;
            if(currArg!=argCount-1){
                while(!strchr(args[currArg+1], '-')){
                    currArg++;  
                    if(strcmp(args[currArg], "fast") == 0){
                        FileIOBenchmarkFastModeEnabled = 1;
                    }else if(atoi(args[currArg]) != 0){
                        FileIOBenchmarkEnabled = atoi(args[currArg]);
                    }  
                    if(currArg==argCount-1) break;
                }
            }
        }
        
        if(strcmp(args[currArg], "-term") == 0){
            TerminalBenchmarkEnabled = 1;
        }
        
        if(strcmp(args[currArg], "-disp") == 0){
            DisplaybufferBenchmarkEnabled = 1;
        }
    }
    
#ifdef TERM_SUPPORT_CWD 
    if(FileIOBenchmarkEnabled){
        uint32_t bytesTransferred = 0;
        uint32_t count = 0;
        uint32_t bytesToWrite = (FileIOBenchmarkEnabled == 1) ? CM_FILEIO_FILESIZE : FileIOBenchmarkEnabled;
        char* data = pvPortMalloc(bytesToWrite);
        data = SYS_makeCoherent(data);
        
        //open file
        FIL* fp = f_open("/bMark.mark",FA_CREATE_ALWAYS);
            
        if(fp < 100){
            ttprintf("Error opening file for writing! (%d)", fp);
        }else{
            f_close(fp);
            fp = f_open("/bMark.mark",FA_WRITE);
        
            SYS_randFill(data, bytesToWrite);
            sprintf(data, "Hello World FUACK! :) I have some data for you: %d", rand());

            if(fp > 0xff){

                if(FileIOBenchmarkFastModeEnabled) ttprintf("Fast mode enabled! only writing entire sectors (512 bytes)\r\n");  

                ttprintf("Testing file write performance... file* = 0x%08x                                        ", fp);    
                FRESULT f_writeRes = FR_OK;
                
                uint32_t sysTime = portGET_INSTRUCTION_COUNTER_VALUE();
                for(bytesTransferred = 0; bytesTransferred < bytesToWrite && f_writeRes == FR_OK;){
                    if(FileIOBenchmarkFastModeEnabled){
                        f_writeRes = f_write(fp, &data[bytesTransferred], bytesToWrite - bytesTransferred, &count);
                        bytesTransferred += count;
                    }else{
                        f_writeRes = f_write(fp, &data[bytesTransferred], bytesToWrite - bytesTransferred, &count);
                        bytesTransferred += count;
                    }
                }
                uint32_t writeTime = portGET_INSTRUCTION_COUNTER_VALUE() - sysTime;
                
                uint32_t writeTimeMS = writeTime/(configCPU_CLOCK_HZ/10000);
                uint32_t transferSpeed = (writeTimeMS == 0) ? 0 : (100*bytesTransferred)/writeTimeMS;

                ttprintf("done (wrote %d bytes with code %d)!\t t_instr = %u <=> t = %u.%ums => %d.%dkB/s\r\n", bytesTransferred, f_writeRes, writeTime, writeTimeMS/10, writeTimeMS%10, transferSpeed/10, transferSpeed%10);
                ttprintf("Written = \"%.50s\"\r\n", data);
                
                f_writeRes = f_close(fp);
                if(f_writeRes != FR_OK) ttprintf("Error closing file! (%d)", f_writeRes);
            }else{
                ttprintf("Error opening File for writing! %d", fp);
            }
        }
        
        memset(data, 'a', bytesToWrite);
        fp = f_open("/bMark.mark",FA_READ);

        if(fp > 0xff){
            ttprintf("Testing file read performance... file* = 0x%08x                                        ", fp);
            uint32_t f_writeRes = FR_OK;
            
            uint32_t sysTime = portGET_INSTRUCTION_COUNTER_VALUE();
            for(bytesTransferred = 0; bytesTransferred < bytesToWrite && f_writeRes == FR_OK;){
                if(FileIOBenchmarkFastModeEnabled){
                    f_writeRes = f_fastRead(fp, data, bytesToWrite - bytesTransferred, &count);
                    bytesTransferred += count;
                }else{
                    f_writeRes = f_read(fp, data, bytesToWrite - bytesTransferred, &count);
                    bytesTransferred += count;
                }
            }
            uint32_t writeTime = portGET_INSTRUCTION_COUNTER_VALUE() - sysTime;
            
            uint32_t writeTimeMS = writeTime/(configCPU_CLOCK_HZ/10000);
            uint32_t transferSpeed = (writeTimeMS == 0) ? 0 : (100*bytesTransferred)/writeTimeMS;

            ttprintf("done (read %d bytes with code %d)!\t t_instr = %u <=> t = %u.%ums => %d.%dkB/s\r\n", bytesTransferred, f_writeRes, writeTime, writeTimeMS/10, writeTimeMS%10, transferSpeed/10, transferSpeed%10);
            ttprintf("Read result = \"%.50s\"\r\n", data);
        }else{
            ttprintf("Error opening File for reading! %d", fp);
        }
        
        data = SYS_makeNonCoherent(data);

        vPortFree(data);
    }
#endif
    
    return TERM_CMD_EXIT_SUCCESS;
}
