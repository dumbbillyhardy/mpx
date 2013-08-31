#ifndef DATE_H
#define DATE_H

//std libraries and mpx
#include <string.h>
#include "mpx_supt.h"

//our h-files
#include "common.h"

//constants


//function prototypes
int date(int, char **);
int isLeapYear(int);
int checkDays(int, int, int);
#endif
