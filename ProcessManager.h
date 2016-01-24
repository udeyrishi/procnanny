/*
Copyright 2015 Udey Rishi

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef __PROC_MANAGER__
#define __PROC_MANAGER__

#include <stdbool.h>
#include <sys/types.h>

#define CHILD (pid_t)0

typedef struct
{
	pid_t targetPid;
	unsigned long int monitorDuration;
} MonitorMessage;

bool killOtherProcNannys();
int monitor(int refreshRate, int argc, char** argv);
#endif