/*

    Name: Xi-Ping Xu
    Date: April 20,2017
    Last Update: July 19,2017
    Program Statement:
    We define print type and error reporting functions here
    In addition, we should add error numbers at here

*/

//printf with colors
//Example printf(RED"Error\n"NONE);

#ifndef COLORS

#define COLORS

#define NONE "\033[m"
#define RED "\033[0;32;31m"
#define LIGHT_RED "\033[1;31m"
#define GREEN "\033[0;32;32m"
#define LIGHT_GREEN "\033[1;32m"
#define BLUE "\033[0;32;34m"
#define LIGHT_BLUE "\033[1;34m"
#define DARY_GRAY "\033[1;30m"
#define CYAN "\033[0;36m"
#define LIGHT_CYAN "\033[1;36m"
#define PURPLE "\033[0;35m"
#define LIGHT_PURPLE "\033[1;35m"
#define BROWN "\033[0;33m"
#define YELLOW "\033[1;33m"
#define LIGHT_GRAY "\033[0;37m"
#define WHITE "\033[1;37m"

#endif

#ifndef LIB_CURL

#define LIB_CURL

#endif

unsigned int sgsErrNum;
char sgsErrMsg[1024];

//Intent : Send e-mail via gmail relay and libcurl
//Pre    : Error messages
//Post   : Nothing
/*

    Get receiver,
    Get CC list,
    Form email content
    invoke curl email process (fork())
    return

*/
#ifdef LIB_CURL

void sgsSendEmail(char *message);

#endif

//Intent : print out the stored the error message and the error flag.
//Pre    : Nothing
//Post   : Nothing
/*

    retrieve sgsErrMsg
    print it out
    return

*/

void sgsShowErrMsg();

//Intent : get a pointer to error message
//Pre    : Nothing
//Post   : pointer to sgsErrMsg

char* sgsGetErrMsg();

//Intent : Set err num
//pre    : Specific errNum
//Post   : Nothing

void sgsSetErrNum(unsigned int errNum);


//Intent : Set ErrMsg
//Pre    : Specific message
//Post   : Nothing

void sgsSetErrMsg(char *message);