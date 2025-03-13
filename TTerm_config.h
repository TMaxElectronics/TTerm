//not-so example config...
//i guess it might be an example but there is hardly anything to configure yet lol

#ifndef TTERM_CONF
#define TTERM_CONF

//This is the name of the device stated in the command line as {user}@{TERM_NAME}>
//NOTE: if CWD support is enabled {TERM_NAME} will be replaced with the CWD
#define TERM_NAME "SmartBox"

//Enable to add void * port argument to printer function calls. This can be useful if you have multiple Terminals running and don't want to use multiple printer functions
#define EXTENDED_PRINTF 1

//Buffer sizes. If you don't have much ram to spare I recommend you decrease TERM_HISTORYSIZE, as every history position is the length of one input buffer
#define TERM_INPUTBUFFER_SIZE 128
#define TERM_HISTORYSIZE 16
#define TERM_PROG_BUFFER_SIZE 32

//Print a text when the terminal is started?
#define TERM_ENABLE_STARTUP_TEXT

//Do you want every command to run in its own task?
//NOTE: this requires FreeRTOS
#define TERM_startTaskPerCommand

//Should the terminal implement a working directory and include basic file commands?
//NOTE: this requires FatFS
//#define TERM_SUPPORT_CWD 1

//If you want to have the "reset" command available you can define what function should be called here
#define TERM_RESET_FUNCTION(X) SYS_softwareReset()

//Heap functions
#define TERM_MALLOC(X) pvPortMalloc(X)
#define TERM_FREE(X) vPortFree(X)

//What text should the terminal print at startup?
#ifdef TERM_ENABLE_STARTUP_TEXT
#define TERM_startupText "Welcome to a test terminal :)"
#endif   


#endif   
