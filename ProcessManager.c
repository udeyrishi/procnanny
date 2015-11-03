#include "ProcessManager.h"
#include "Logging.h"
#include "Utils.h"
#include "ProgramIO.h"
#include <sys/types.h>
#include <signal.h>
#include <assert.h>
#include "memwatch.h"

const char* PROGRAM_NAME = "procnanny";

MonitorRequest** monitorRequests = NULL;
int configLength = 0;
RegisterEntry* root = NULL;

int writingToParent = 0;
int readingFromParent = 0;

void cleanupGlobals()
{
    destroyMonitorRequestArray(monitorRequests, configLength);
    destructChain(root);
}

Process** searchRunningProcesses(int* processesFound, const char* processName)
{
    int i = 0;
    LogReport report;
    
    char* command = stringJoin("ps -u | grep ", processName);
    char** lines = getOutputFromProgram(command, &i, &report);
    free(command);
    
    if (lines == NULL)
    {
        // LogReport has been filled with some error
        saveLogReport(report);
        *processesFound = -1;
        return (Process**)NULL;
    }

    if (i <= 2)
    {
        // Nothing's found. 2 because 2 internal processes are started
        freeOutputFromProgram(lines, i);
        *processesFound = 0;
        return (Process**)NULL;
    }

    // i  > 2
    Process** processes = (Process**)malloc(sizeof(Process*)*(i-2));
    if (!checkMallocResult(processes, &report))
    {
        saveLogReport(report);
        freeOutputFromProgram(lines, i);
        *processesFound = -1;
        return (Process**)NULL;
    }

    int source;
    int destination = 0;

    for (source = 0; source < i; ++source)
    {
        Process* p = processConstructor(lines[source]);
        if (p == NULL)
        {
            freeOutputFromProgram(lines, i);
            *processesFound = -1;
            return (Process**)NULL;
        }
        if (compareStrings(p->command, processName))
        {
            processes[destination++] = p;
        }
        else
        {
            processDestructor(p);
        }
    }

    freeOutputFromProgram(lines, i);

    *processesFound = destination;
    if (destination == 0)
    {
        destroyProcessArray(processes, destination);
        processes = NULL;
    }
    return processes;
}

int getProcessesToMonitor(int argc, char** argv, MonitorRequest*** monitorRequests)
{
    LogReport report;
    report.message = (char*)NULL;

    if (argc < 2)
    {
        report.message = "Config file path needed as argument.";
        report.type = ERROR;
        saveLogReport(report);
        printLogReport(report);
        return -1;
    }

    char* configPath = argv[1];
    int configLines = 0;
    char** config = readFile(configPath, &configLines, &report);
    
    if (report.message != NULL)
    {
        if (configLines > 0)
        {
            freeOutputFromProgram(config, configLines);
        }
        saveLogReport(report);
        return -1;
    }

    // Array of MonitorRequest pointers
    MonitorRequest** requests = (MonitorRequest**)malloc(configLines*sizeof(MonitorRequest*));
    if (!checkMallocResult(requests, &report))
    {
        freeOutputFromProgram(config, configLines);
        saveLogReport(report);
        return -1;
    }

    int i;
    for (i = 0; i < configLines; ++i) 
    {
        MonitorRequest* request = constructMonitorRequest(config[i]);

        if (request == NULL)
        {
            freeOutputFromProgram(config, configLines);
            destroyMonitorRequestArray(requests, configLines);
            return -1;
        }
        requests[i] = request;
    }

    freeOutputFromProgram(config, configLines);
    *monitorRequests = requests;
    return configLines;
}

bool killProcess(Process process)
{
    int result = kill(process.pid, SIGKILL);
    return (bool)(result == 0);
}

bool killOtherProcNannys()
{
    int num = 0;
    Process** procs = searchRunningProcesses(&num, PROGRAM_NAME);
    if (procs == NULL)
    {
        if (num == 0)
        {
            // Nothing found
            return true;
        }
        LogReport report;
        report.message = "Unexpected behaviour. Process** is NULL but count > 0";
        report.type = DEBUG;
        saveLogReport(report);
        return false;
    }

    int i;
    for(i = 0; i < num; ++i)
    {
        Process* p = procs[i];
        if (getpid() != p->pid)
        {
            LogReport report;
            report.message = stringNumberJoin("Another procnanny found. Killing it. PID: ", (int)p->pid);
            report.type = INFO;
            saveLogReport(report);
            free(report.message);
            
            if(!killProcess(*p))
            {
                report.message = stringNumberJoin("Failed to kill another procnanny. PID: ", (int)p->pid);
                report.type = ERROR;
                saveLogReport(report);
                free(report.message);
                return false;
            }
        }
    }

    destroyProcessArray(procs, num);
    procs = NULL;

    int verificationNum;
    Process** verificationProcs = searchRunningProcesses(&verificationNum, PROGRAM_NAME);
    if (verificationProcs == NULL)
    {
        if (verificationNum == 0)
        {
            // Nothing found
            return true;
        }
        LogReport report;
        report.message = "Unexpected behaviour. Process** is NULL but count > 0";
        report.type = DEBUG;
        saveLogReport(report);
        return false;
    }

    LogReport report;
    report.message = "Sent kill signal to other procnannys, but they didn't die.";
    report.type = ERROR;
    saveLogReport(report);
    destroyProcessArray(verificationProcs, verificationNum);
    return false;
}

//private
bool isHeadNull(RegisterEntry* head)
{
    return (head == NULL || head->monitoringProcess == (pid_t)0);
}

//private 
bool isProcessAlreadyBeingMonitored(pid_t pid, RegisterEntry* reg)
{
    while (!isHeadNull(reg))
    {
        if (reg->monitoredProcess == pid)
        {
            return true;
        }
        else
        {
            reg = reg->next;
        }
    }
    return false;
}

int refreshRegisterEntries(RegisterEntry* head)
{
    int killed = 0;

    if (isHeadNull(head))
    {
        return killed;
    }

    time_t currentTime = time(NULL);
    while (head != NULL)
    {
        if (!(head->isAvailable) && (currentTime > head->startingTime + head->monitorDuration))
        {
            ProcessStatusCode message;
            assert(read(head->readFromChildFD, &message, 1) == 1);

            LogReport report;
            switch(message)
            {
                case DIED:
                    logSelfDying(head->monitoredProcess, head->monitoredName, head->monitorDuration);
                    break;

                case KILLED:
                    ++killed;
                    logProcessKill(head->monitoredProcess, head->monitoredName, head->monitorDuration);
                    break;

                case FAILED:
                    report.message = stringNumberJoin("Failed to kill process with PID: ", (int)head->monitoredProcess);
                    report.type = ERROR;
                    saveLogReport(report);
                    free(report.message);
                    break;

                default:
                    // TODO: UNEXPECTED
                    break;
            }

            head->isAvailable = true;
        }
        head = head->next;
    }

    return killed;
}

//private
RegisterEntry* getFirstFreeChild(RegisterEntry* head)
{
    while (!isHeadNull(head))
    {
        if (head->isAvailable)
        {
            return head;
        }
        else 
        {
            head = head->next;
        }
    }

    return NULL;
}

//private
ProcessStatusCode childMain(pid_t pid, int duration)
{
    sleep(duration);
    int result = kill(pid, SIGKILL);
    if (result == 0)
    {
        return KILLED;
    }
    else
    {
        return DIED;
    }
}

void setupMonitoring(bool isRetry, char* processName, unsigned long int duration, RegisterEntry* head, RegisterEntry* tail)
{
    int num = 0;
    Process** runningProcesses = searchRunningProcesses(&num, processName);
    if (runningProcesses == NULL)
    {
        // Nothing to be done
        if (num == 0)
        {
            if (!isRetry)
            {
                LogReport report;
                report.message = stringJoin("No process found with name: ", processName);
                report.type = INFO;
                saveLogReport(report);
                free(report.message);
            }
            
            return;
        }
        exit(-1);
    }

    int i;
    for(i = 0; i < num; ++i)
    {
        Process* p = runningProcesses[i];

        if (p -> pid == getpid())
        {
            // If procnannys were killed in the beginning, but a new one was started in between and the user expects to track that. 
            // Should never happen/be done.
            LogReport report;
            report.message = "Config file had procnanny as one of the entries. It will be ignored if no other procnanny is found.";
            report.type = WARNING;
            saveLogReport(report);
            continue;
        }

        if (isProcessAlreadyBeingMonitored(p->pid, head))
        {
            continue;
        }

        logProcessMonitoringInit(processName, p->pid);

        RegisterEntry* freeChild = getFirstFreeChild(head);
        if (freeChild == NULL)
        {
            // fork a new child
            int writeToChildFD[2];
            int readFromChildFD[2];
            if (pipe(writeToChildFD) < 0)
            {
                destroyProcessArray(runningProcesses, num);
                LogReport report;
                report.message = "Pipe creation error when trying to monitor new process.";
                report.type = ERROR;
                saveLogReport(report);
                printLogReport(report);
                // TODO: Kill all children
                // TODO: Log all final kill count
                exit(-1);
            }

            if (pipe(readFromChildFD) < 0)
            {
                destroyProcessArray(runningProcesses, num);
                LogReport report;
                report.message = "Pipe creation error when trying to monitor new process.";
                report.type = ERROR;
                saveLogReport(report);
                printLogReport(report);
                // TODO: Kill all children
                // TODO: Log all final kill count
                exit(-1);
            }

            pid_t forkPid = fork();
            switch (forkPid)
            {
                case -1:
                    destroyProcessArray(runningProcesses, num);
                    exit(-1);

                case CHILD:
                    close(writeToChildFD[1]);
                    close(readFromChildFD[0]);
                    writingToParent = readFromChildFD[1];
                    readingFromParent = writeToChildFD[0];
                    pid_t targetPid = p->pid;
                    destroyProcessArray(runningProcesses, num);
                    cleanupGlobals();

                    while (true)
                    {
                        ProcessStatusCode childStatus = childMain(targetPid, duration);
                        write(writingToParent, &childStatus, 1);

                        MonitorMessage message;
                        assert(read(readingFromParent, &message, sizeof(MonitorMessage)) == sizeof(MonitorMessage));
                        targetPid = message.targetPid;
                        duration = message.monitorDuration;
                    }
                    
                    break;

                default:
                     // parent
                    close(writeToChildFD[0]);
                    close(readFromChildFD[1]);
                    tail->monitoringProcess = forkPid;
                    tail->monitoredProcess = p->pid;
                    tail->monitorDuration = duration;
                    tail->monitoredName = copyString(p->command);
                    tail->startingTime = time(NULL);
                    tail->isAvailable = false;
                    tail->writeToChildFD = writeToChildFD[1];
                    tail->readFromChildFD = readFromChildFD[0];
                    tail->next = constuctorRegisterEntry((pid_t)0, NULL, NULL);
                    tail = tail->next;
                    break;
            }

        }
        else
        {
            // use freeChild
            freeChild->isAvailable = false;
            freeChild->monitoredProcess = p->pid;
            free(freeChild->monitoredName);
            freeChild->monitoredName = copyString(p->command);
            freeChild->monitorDuration = duration;
            freeChild->startingTime = time(NULL);
            MonitorMessage message;
            message.targetPid = p->pid;
            message.monitorDuration = duration;
            write(freeChild->writeToChildFD, &message, sizeof(MonitorMessage));
        }
    }

    destroyProcessArray(runningProcesses, num);
}

// private
void rereadConfig(int argc, char** argv)
{
    destroyMonitorRequestArray(monitorRequests, configLength);
    monitorRequests = NULL;
    configLength = 0;
    configLength = getProcessesToMonitor(argc, argv, &monitorRequests);

    if (configLength == -1)
    {
        // Already logged
        exit(-1);
    }
}


bool sigintReceived = false;
bool readConfig = false;

void sigintHandler(int signum)
{
    if (signum == SIGINT)
    {
        sigintReceived = true;
    }
}

void sighupHandler(int signum)
{
    if (signum == SIGHUP)
    {
        readConfig = true;
    }
}

int monitor(int refreshRate, int argc, char** argv)
{
    signal(SIGINT, sigintHandler);
    signal(SIGHUP, sighupHandler);

    root = constuctorRegisterEntry((pid_t)0, NULL, NULL);
    RegisterEntry* tail = root;
    
    int killCount = 0;
    rereadConfig(argc, argv);
    bool isRetry = false;

    while (!sigintReceived)
    {
        if (readConfig)
        {
            logSighupCatch(argv[1]);
            readConfig = false;
            isRetry = false;
            rereadConfig(argc, argv);
        }

        killCount += refreshRegisterEntries(root);
        int i;
        for (i = 0; i < configLength; ++i)
        {
            setupMonitoring(isRetry, monitorRequests[i]->processName, monitorRequests[i]->monitorDuration, root, tail);
            while(tail->next != NULL)
            {
                // Refresh tail
                tail = tail->next;
            }
        }
        sleep(refreshRate);
        isRetry = true;
    }

    killAllChildren(root);
    cleanupGlobals();
    return killCount;
}

void killAllChildren(RegisterEntry* root)
{
    while (!isHeadNull(root))
    {
        close(root->writeToChildFD);
        close(root->readFromChildFD);
        kill(root->monitoringProcess, SIGKILL_CHILD);
        root = root->next;
    }
}

void closeChildEndsOfPipes()
{
    close(writingToParent);
    close(readingFromParent);
}