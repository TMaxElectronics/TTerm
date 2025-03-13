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
#if !__is_compiling || __has_include("FreeRTOS.h")

	#include "TTerm.h"
	#include "top.h"
	#include "string.h"

	#define APP_NAME "top"
	#define APP_DESCRIPTION "shows performance stats"
	#define APP_STACK 550

	#define TOP_SORT_NONE   0
	#define TOP_SORT_PID    1
	#define TOP_SORT_NAME   2
	#define TOP_SORT_LOAD   3
	#define TOP_SORT_RUNTIME 4
	#define TOP_SORT_STACK  5
	#define TOP_SORT_HEAP   6


	static const char top_sortingNamesPointers[][16] = {"none", "pid", "name", "CPU load", "total runtime", "stack usage", "heap usage"};
	static TaskStatus_t ** createSortedList(TaskStatus_t * pxTaskStatusArray, uint32_t taskCount, uint32_t mode);
	static int32_t compareTasks(TaskStatus_t * a, TaskStatus_t * b, uint32_t mode);

	static uint8_t CMD_main(TERMINAL_HANDLE * handle, uint8_t argCount, char ** args);

	uint8_t REGISTER_top(TermCommandDescriptor * desc){
		TERM_addCommand(CMD_main, APP_NAME, APP_DESCRIPTION, 0, desc);
	}

	static uint8_t CMD_main(TERMINAL_HANDLE * handle, uint8_t argCount, char ** args){
		uint8_t currArg = 0;
		uint8_t returnCode = TERM_CMD_EXIT_SUCCESS;

		TERM_sendVT100Code(handle, _VT100_RESET, 0); TERM_sendVT100Code(handle, _VT100_CURSOR_POS1, 0);

		uint32_t currSortingMode = 0;
		char c=0;
		do{

			TaskStatus_t * taskStats;
			uint32_t taskCount = uxTaskGetNumberOfTasks();
			uint32_t sysTime;

			taskStats = pvPortMalloc( taskCount * sizeof( TaskStatus_t ) );
			if(taskStats){
				taskCount = uxTaskGetSystemState(taskStats, taskCount, &sysTime);

				TERM_sendVT100Code(handle, _VT100_CURSOR_POS1, 0);

				uint32_t cpuLoad = SYS_getCPULoadFine(taskStats, taskCount, sysTime);
				ttprintf("%sbottom - %d\r\n%sTasks: \t%d\r\n%sCPU: \t%d,%d%%\r\n", TERM_getVT100Code(_VT100_ERASE_LINE_END, 0), xTaskGetTickCount(), TERM_getVT100Code(_VT100_ERASE_LINE_END, 0), taskCount, TERM_getVT100Code(_VT100_ERASE_LINE_END, 0), cpuLoad / 10, cpuLoad % 10);

				uint32_t heapRemaining = xPortGetFreeHeapSize();
				ttprintf("%sMem: \t%db total,\t %db free,\t %db used (%d%%)\r\n", TERM_getVT100Code(_VT100_ERASE_LINE_END, 0), configTOTAL_HEAP_SIZE, heapRemaining, configTOTAL_HEAP_SIZE - heapRemaining, ((configTOTAL_HEAP_SIZE - heapRemaining) * 100) / configTOTAL_HEAP_SIZE);

				ttprintf("%ssorting: %s (options: p(id), n(ame), l(oad), t(ime), s(tack), h(eap)) \r\n\n", TERM_getVT100Code(_VT100_ERASE_LINE_END, 0), top_sortingNamesPointers[currSortingMode]);

				//new CPU load test
				ttprintf("%s%s%s", TERM_getVT100Code(_VT100_BACKGROUND_COLOR, _VT100_WHITE), TERM_getVT100Code(_VT100_ERASE_LINE_END, 0), TERM_getVT100Code(_VT100_FOREGROUND_COLOR, _VT100_BLACK));
				ttprintf("PID \r\x1b[%dCName \r\x1b[%dCstate \r\x1b[%dC%%Cpu \r\x1b[%dCavg %%CPU  \r\x1b[%dCtime  \r\x1b[%dCStack \r\x1b[%dCHeap\r\n", 6
						, 7 + configMAX_TASK_NAME_LEN
						, 20 + configMAX_TASK_NAME_LEN
						, 27 + configMAX_TASK_NAME_LEN
						, 37 + configMAX_TASK_NAME_LEN
						, 47 + configMAX_TASK_NAME_LEN
						, 55 + configMAX_TASK_NAME_LEN);

				ttprintf("%s", TERM_getVT100Code(_VT100_RESET_ATTRIB, 0));

				uint32_t totalLoad = 0;

				TaskStatus_t ** sorted = createSortedList(taskStats, taskCount, currSortingMode);
				for(uint32_t currTask = 0; currTask < taskCount; currTask++){
					//make sure name is zero terminated
					char name[configMAX_TASK_NAME_LEN+1];
					strncpy(name, sorted[currTask]->pcTaskName, configMAX_TASK_NAME_LEN);

					ttprintf("%s%d\r\x1b[%dC%s\r\x1b[%dC%s\r\x1b[%dC%d,%d\r\x1b[%dC%d,%d\r\x1b[%dC%u\r\x1b[%dC%d\r\x1b[%dC%d\r\n", TERM_getVT100Code(_VT100_ERASE_LINE_END, 0)
														  , sorted[currTask]->xTaskNumber
							, 6                           , name
							, 7 + configMAX_TASK_NAME_LEN , SYS_getTaskStateString(sorted[currTask]->eCurrentState)
							, 20 + configMAX_TASK_NAME_LEN, sorted[currTask]->currCPULoad / 10, sorted[currTask]->currCPULoad % 10
							, 27 + configMAX_TASK_NAME_LEN, sorted[currTask]->avgCPULoad / 10, sorted[currTask]->avgCPULoad % 10
							, 37 + configMAX_TASK_NAME_LEN, sorted[currTask]->ulRunTimeCounter
							, 47 + configMAX_TASK_NAME_LEN, sorted[currTask]->usStackHighWaterMark
							, 55 + configMAX_TASK_NAME_LEN, sorted[currTask]->usedHeap);

					totalLoad += sorted[currTask]->currCPULoad;
				}

				uint32_t isrLoad = 1000-totalLoad;
				ttprintf("\n%s\r\x1b[%dC%s\r\x1b[%dC%s\r\x1b[%dC%d,%d\r\n", TERM_getVT100Code(_VT100_ERASE_LINE_END, 0)
							, 6                           , "Interrupts"
							, 7 + configMAX_TASK_NAME_LEN , "none"
							, 20 + configMAX_TASK_NAME_LEN, isrLoad / 10, isrLoad % 10);

				vPortFree(sorted);
				vPortFree(taskStats);
			}else{
				ttprintf("Malloc failed\r\n");
			}

			//wait 400ms and try to get a char while doing so
			for(uint32_t i = 0; i < 400; i++){
				c = ttgetc(pdMS_TO_TICKS(1));
				if(c != 0) break;
			}

			switch(c){
				case 'p':
				case 'P':
					currSortingMode = TOP_SORT_PID;
					break;

				case 'n':
				case 'N':
					currSortingMode = TOP_SORT_NAME;
					break;

				case 'l':
				case 'L':
					currSortingMode = TOP_SORT_LOAD;
					break;

				case 'r':
				case 'R':
				case 't':
				case 'T':
					currSortingMode = TOP_SORT_RUNTIME;
					break;

				case 's':
				case 'S':
					currSortingMode = TOP_SORT_STACK;
					break;

				case 'h':
				case 'H':
					currSortingMode = TOP_SORT_HEAP;
					break;

				case 0:
					break;

				default:
					currSortingMode = TOP_SORT_NONE;
					break;
			}

		}while(c!=CTRL_C);

		return returnCode;
	}

	/* sorting modes:
	 * 0: none
	 * 1: PID
	 * 2: Name
	 * 3: CPU Load
	 * 4: totalRuntime
	 * 5: Stack
	 * 6: Heap
	 *
	 * return: 0: <= 1:>
	 */

	static int32_t compareTasks(TaskStatus_t * a, TaskStatus_t * b, uint32_t mode){
		switch(mode){
			case TOP_SORT_NONE:
				return 0;

			case TOP_SORT_PID:
				return a->xTaskNumber < b->xTaskNumber;

			case TOP_SORT_NAME:
				for(uint32_t currChar = 0; currChar < configMAX_TASK_NAME_LEN; currChar++){
					if(b->pcTaskName[currChar] == 0) return 0;
					if(a->pcTaskName[currChar] == 0) return 1;
					if(toLowerCase(a->pcTaskName[currChar]) > toLowerCase(b->pcTaskName[currChar])) return 0;
					if(toLowerCase(a->pcTaskName[currChar]) < toLowerCase(b->pcTaskName[currChar])) return 1;
				}
				return compareTasks(a,b, 1);

			case TOP_SORT_LOAD:
				if(a->currCPULoad == b->currCPULoad) return compareTasks(a,b, 1);
				return a->currCPULoad > b->currCPULoad;

			case TOP_SORT_RUNTIME:
				if(a->ulRunTimeCounter == b->ulRunTimeCounter) return compareTasks(a,b, 1);
				return a->ulRunTimeCounter > b->ulRunTimeCounter;

			case TOP_SORT_STACK:
				if(a->usStackHighWaterMark == b->usStackHighWaterMark) return compareTasks(a,b, 1);
				return a->usStackHighWaterMark > b->usStackHighWaterMark;

			case TOP_SORT_HEAP:
				if(a->usedHeap == b->usedHeap) return compareTasks(a,b, 1);
				return a->usedHeap > b->usedHeap;

			default:
				return 0;
		}
	}

	static TaskStatus_t ** createSortedList(TaskStatus_t * pxTaskStatusArray, uint32_t taskCount, uint32_t mode){
		TaskStatus_t ** sortedList = pvPortMalloc(taskCount * sizeof(TaskStatus_t *));
		memset(sortedList, 0, taskCount * sizeof(TaskStatus_t *));

		for(uint32_t currTask = 0; currTask < taskCount; currTask++){
			int32_t newPos = taskCount-1;

			if(mode == TOP_SORT_NONE){
				//no sorting => find first empty spot
				while((uint32_t) sortedList[newPos] != 0) newPos--;
			}else{
				//sorting => move item back if we have a more important one
				for(uint32_t currCompTask = 0; currCompTask < taskCount; currCompTask++){
					if(currTask == currCompTask) continue;
					newPos -= compareTasks(&pxTaskStatusArray[currTask], &pxTaskStatusArray[currCompTask], mode);
				}
			}

			sortedList[newPos] = &pxTaskStatusArray[currTask];
		}
		return sortedList;
	}
#endif

