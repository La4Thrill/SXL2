#ifndef PTHREAD_COMPAT_H
#define PTHREAD_COMPAT_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>

typedef HANDLE pthread_t;
typedef CRITICAL_SECTION pthread_mutex_t;

static int pthread_create(pthread_t* thread, void* attr, void* (*start_routine)(void*), void* arg) {
    (void)attr;
    unsigned threadId;
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, (unsigned (__stdcall *)(void *))start_routine, arg, 0, &threadId);
    if (!h) return -1;
    *thread = h;
    return 0;
}

static int pthread_detach(pthread_t thread) {
    // Close handle to let thread run independently
    if (thread) {
        CloseHandle(thread);
    }
    return 0;
}

static int pthread_join(pthread_t thread, void** retval) {
    (void)retval;
    if (!thread) return -1;
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
}

static int pthread_mutex_init(pthread_mutex_t* m, void* attr) {
    (void)attr;
    InitializeCriticalSection(m);
    return 0;
}

static int pthread_mutex_lock(pthread_mutex_t* m) {
    EnterCriticalSection(m);
    return 0;
}

static int pthread_mutex_unlock(pthread_mutex_t* m) {
    LeaveCriticalSection(m);
    return 0;
}

static int pthread_mutex_destroy(pthread_mutex_t* m) {
    DeleteCriticalSection(m);
    return 0;
}

#else
#include <pthread.h>
#endif

#endif // PTHREAD_COMPAT_H
