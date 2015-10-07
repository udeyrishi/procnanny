#include "Utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "memwatch.h"

Boolean checkMallocResult(void* pointer, LogReport* report)
{
    if (pointer == NULL)
    {
        report -> message = "Out of memory.";
        report -> type = FATAL;
        return FALSE;
    }
    return TRUE;
}

void safeFree(void* pointer)
{
	free(pointer);
	pointer = NULL;
}

void safeMwFree(void* pointer)
{
	mwFree_(pointer);
	pointer = NULL;
}

char* stringJoin(const char* first, const char* second)
{
    int len1 = strlen(first);
    int len2 = strlen(second);
    int sizeBuffer = len1 + len2 + 1;
    char* joined = (char*)malloc(sizeof(char)*sizeBuffer);
    LogReport report;
    if (!checkMallocResult(joined, &report))
    {
        return (char*)NULL;
    }
    
    char* temp = (char*)malloc(sizeof(char)*(len1 + 3));
    if (!checkMallocResult(temp, &report))
    {
    	safeFree(joined);
        return (char*)NULL;
    }

    strcpy(temp, first);
    temp[len1] = '%';
    temp[len1 + 1] = 's';
    temp[len1 + 2] = '\0';

    int n = sprintf(joined, temp, second);
    assert(n + 1 == sizeBuffer);
    safeFree(temp);
    return joined;
}

char* stringNumberJoin(const char* first, int second)
{
	char str[15]; // good enough
    sprintf(str, "%d", second);
    return stringJoin(first, str);
}

char* numberStrngJoin(int first, const char* second)
{
	char str[15]; // good enough
    sprintf(str, "%d", first);
    return stringJoin(str, second);
}