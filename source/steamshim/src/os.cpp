#include "os.h"
#include "parent/parent_private.h"
#include <cstring>

#ifdef _WIN32
void fail(const char *err)
{
    MessageBoxA(NULL, err, "ERROR", MB_ICONERROR | MB_OK);
    ExitProcess(1);
} // fail

bool createPipes(PipeType *pPipeParentRead, PipeType *pPipeParentWrite,
                        PipeType *pPipeChildRead, PipeType *pPipeChildWrite)
{
    SECURITY_ATTRIBUTES pipeAttr;

    pipeAttr.nLength = sizeof (pipeAttr);
    pipeAttr.lpSecurityDescriptor = NULL;
    pipeAttr.bInheritHandle = TRUE;
    if (!CreatePipe(pPipeParentRead, pPipeChildWrite, &pipeAttr, 0))
        return 0;

    pipeAttr.nLength = sizeof (pipeAttr);
    pipeAttr.lpSecurityDescriptor = NULL;
    pipeAttr.bInheritHandle = TRUE;
    if (!CreatePipe(pPipeChildRead, pPipeParentWrite, &pipeAttr, 0))
    {
        CloseHandle(*pPipeParentRead);
        CloseHandle(*pPipeChildWrite);
        return 0;
    } // if

    return 1;
} // createPipes

bool setEnvVar(const char *key, const char *val)
{
    return (SetEnvironmentVariableA(key, val) != 0);
} // setEnvVar

bool launchChild(ProcessType *pid)
{
    LPWSTR str = _wcsdup( GetCommandLineW() );
    STARTUPINFOW si = { sizeof( si ) };

    memset( pid, 0, sizeof( *pid ) );
    bool bResult = ( CreateProcessW( TEXT( ".\\" ) TEXT( GAME_CLIENT_LAUNCH_NAME ), str, NULL, NULL, TRUE, 0, NULL,
                              NULL, &si, pid) != 0);
    free( str );
    return bResult;
} // launchChild

int closeProcess(ProcessType *pid)
{
    CloseHandle(pid->hProcess);
    CloseHandle(pid->hThread);
    return 0;
} // closeProcess

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine, int nCmdShow)
{
    mainline();
    ExitProcess(0);
    return 0;  // just in case.
} // WinMain


#else  // everyone else that isn't Windows.

bool launchChild(ProcessType *pid)
{
    *pid = fork();
    if (*pid == -1)   // failed
        return false;
    else if (*pid != 0)  // we're the parent
        return true;  // we'll let the pipe fail if this didn't work.

    // we're the child.
    if (strstr(*GArgv,"warfork_steam"))
        GArgv[0] = strdup("./" GAME_CLIENT_LAUNCH_NAME);
    else
        GArgv[0] = strdup("./" GAME_SERVER_LAUNCH_NAME);
    execvp(GArgv[0], GArgv);
    // still here? It failed! Terminate, closing child's ends of the pipes.
    _exit(1);
} // launchChild

int closeProcess(ProcessType *pid)
{
    int rc = 0;
    while ((waitpid(*pid, &rc, 0) == -1) && (errno == EINTR)) { /*spin*/ }
    if (!WIFEXITED(rc))
        return 1;  // oh well.
    return WEXITSTATUS(rc);
} // closeProcess



void fail(const char *err)
{
    // !!! FIXME: zenity or something.
    fprintf(stderr, "%s\n", err);
    _exit(1);
} // fail

bool createPipes(PipeType *pPipeParentRead, PipeType *pPipeParentWrite,
                        PipeType *pPipeChildRead, PipeType *pPipeChildWrite)
{
    int fds[2];
    if (pipe(fds) == -1)
        return 0;
    *pPipeParentRead = fds[0];
    *pPipeChildWrite = fds[1];

    if (pipe(fds) == -1)
    {
        close(*pPipeParentRead);
        close(*pPipeChildWrite);
        return 0;
    } // if

    *pPipeChildRead = fds[0];
    *pPipeParentWrite = fds[1];

    return 1;
} // createPipes

bool setEnvVar(const char *key, const char *val)
{
    return (setenv(key, val, 1) != -1);
} // setEnvVar

#endif

#ifdef _WIN32

int pipeReady(PipeType fd)
{
    DWORD avail = 0;
    return (PeekNamedPipe(fd, NULL, 0, NULL, &avail, NULL) && (avail > 0));
} /* pipeReady */

int writePipe(PipeType fd, const void *buf, const unsigned int _len)
{
    const DWORD len = (DWORD) _len;
    DWORD bw = 0;
    return ((WriteFile(fd, buf, len, &bw, NULL) != 0) && (bw == len));
} /* writePipe */

int readPipe(PipeType fd, void *buf, const unsigned int _len)
{
    const DWORD len = (DWORD) _len;
    DWORD br = 0;
    return ReadFile(fd, buf, len, &br, NULL) ? (int) br : -1;
} /* readPipe */

void closePipe(PipeType fd)
{
    CloseHandle(fd);
} /* closePipe */

char *getEnvVar(const char *key, char *buf, const size_t _buflen)
{
    const DWORD buflen = (DWORD) _buflen;
    const DWORD rc = GetEnvironmentVariableA(key, buf, buflen);
    /* rc doesn't count null char, hence "<". */
    return ((rc > 0) && (rc < buflen)) ? buf : NULL;
} /* getEnvVar */

#else

int pipeReady(PipeType fd)
{
    int rc;
    struct pollfd pfd = { fd, POLLIN | POLLERR | POLLHUP, 0 };
    while (((rc = poll(&pfd, 1, 0)) == -1) && (errno == EINTR)) { /*spin*/ }
    return (rc == 1);
} /* pipeReady */

int writePipe(PipeType fd, const void *buf, const unsigned int _len)
{
    const ssize_t len = (ssize_t) _len;
    ssize_t bw;
    while (((bw = write(fd, buf, len)) == -1) && (errno == EINTR)) { /*spin*/ }
    return (bw == len);
} /* writePipe */

int readPipe(PipeType fd, void *buf, const unsigned int _len)
{
    const ssize_t len = (ssize_t) _len;
    ssize_t br;
    while (((br = read(fd, buf, len)) == -1) && (errno == EINTR)) { /*spin*/ }
    return (int) br;
} /* readPipe */

void closePipe(PipeType fd)
{
    close(fd);
} /* closePipe */

char *getEnvVar(const char *key, char *buf, const size_t buflen)
{
    const char *envr = getenv(key);
    if (!envr || (strlen(envr) >= buflen))
        return NULL;
    strcpy(buf, envr);
    return buf;
} /* getEnvVar */

#endif

