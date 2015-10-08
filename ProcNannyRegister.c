#include "ProcNannyRegister.h"
#include <stdlib.h>
#include "memwatch.h"

RegisterEntry* constuctorRegisterEntry(pid_t monitoringProcess, Process* monitoredProcess, RegisterEntry* next)
{
	RegisterEntry* entry = (RegisterEntry*)malloc(sizeof(RegisterEntry));
    entry->monitoringProcess = monitoringProcess;
    if (monitoredProcess == NULL)
    {
    	entry->monitoredProcess = (pid_t)0;
    	entry->monitoredName = NULL;
    }
    else
    {
    	entry->monitoredProcess = monitoredProcess->pid;
   	    entry->monitoredName = copyString(monitoredProcess->command);
    }
    entry->next = next;
    return entry;
}

RegisterEntry* destructorRegisterEntry(RegisterEntry* this)
{
	if (this == NULL)
	{
		return NULL;
	}
	RegisterEntry* next = this->next;
	free(this->monitoredName);
	free(this);
	return next;
}

void destructChain(RegisterEntry* root)
{
	while (root != NULL)
	{
		root = destructorRegisterEntry(root);
	}
}

// Assumes same monitoring won't be called 2x. TODO: fix this
Process* findMonitoredProcess(pid_t monitoringProcess, RegisterEntry* reg)
{
	while (reg != NULL)
	{
		if (reg->monitoringProcess == monitoringProcess)
		{
			Process* found = (Process*)malloc(sizeof(Process));
			found->command = reg->monitoredName;
			found->pid = reg->monitoredProcess;
			return found;
		}
		else
		{
			reg = reg->next;
		}
	}
	return NULL;
}