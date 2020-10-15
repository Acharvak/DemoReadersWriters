#include <windows.h>
#include <processthreadsapi.h>
#include <synchapi.h>
#include <stdio.h>
#include <stdlib.h>

#define READER_WAIT_MS 3000
#define WRITER_WAIT_MS 10000

#define NUMBER_OF_READERS 10
#define NUMBER_OF_WRITERS 3
#define MAXIMUM_VALUE 10

// Нормальные люди, конечно, берут готовый SRWLock из synchapi.h,
// но по заданию надо это делать вручную, причём с применением mutex
// и event. Поэтому вот вам вручную и на чистом C (должно компилироваться и как C++).

// Согласно присланным требованиям, надо выделять память в куче
typedef struct SharedData {
    int TheValue;
    DWORD LastWriter;
    
    HANDLE hEntryEvent;
    HANDLE hWaitingReadersEvent;
    HANDLE hWaitingWritersEvent;

    int ActiveReaders;
    int WaitingReaders;
    int WaitingWriters;

    unsigned long LastEvent;    // Для протоколирования
} SharedData;


typedef struct WriterLaunchInfo {
    int number;
    SharedData* ptrsdata;
} WriterLaunchInfo;


HANDLE hPrintMutex;     // Для непрерывных строк вывода


void handleFailure(const char* where) {
    fprintf(stderr, "%s failed, code %u\r\n", where, GetLastError());
    abort();
}


void _SetEvent(HANDLE hEvent) {
    if(!SetEvent(hEvent)) {
        handleFailure("SetEvent");
    }
}

void _WaitForSingleObject(HANDLE hObjectToWaitOn) {
    if(WaitForSingleObject(hObjectToWaitOn, INFINITE) != WAIT_OBJECT_0) {
        handleFailure("WaitForSingleObject");
    }
}

void _SignalObjectAndWait(HANDLE hObjectToSignal, HANDLE hObjectToWaitOn) {
    if(SignalObjectAndWait(hObjectToSignal, hObjectToWaitOn, INFINITE, FALSE) != WAIT_OBJECT_0) {
        handleFailure("SignalObjectAndWait");
    }
}

void _ReleaseMutex(HANDLE hMutex) {
    if(!ReleaseMutex(hMutex)) {
        handleFailure("ReleaseMutex");
    }
}

HANDLE _CreateEvent(BOOL bInitialState) {
    HANDLE result = CreateEvent(NULL, FALSE, bInitialState, NULL);
    if(!result) {
        handleFailure("CreateEvent");
    }
    return result;
}

HANDLE _CreateThread(LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter) {
    HANDLE result = CreateThread(NULL, 0, lpStartAddress, lpParameter, 0, 0);
    if(!result) {
        handleFailure("CreateThread");
    }
    return result;
}


DWORD WINAPI ReaderProc(_In_ LPVOID lpParameter) {
    SharedData* ptrsdata = (SharedData*) lpParameter;
    DWORD my_id = GetCurrentThreadId();
    BOOL must_continue = TRUE;

    while(must_continue) {
        Sleep(READER_WAIT_MS);
    
        unsigned long my_event;
        // Берём читательский захват
        _WaitForSingleObject(ptrsdata->hEntryEvent);
        if(ptrsdata->WaitingWriters) {
            // Надо ждать писателей
            ++ptrsdata->WaitingReaders;
            _SignalObjectAndWait(ptrsdata->hEntryEvent, ptrsdata->hWaitingReadersEvent);
            --ptrsdata->WaitingReaders;
        }
        ++ptrsdata->ActiveReaders;
        my_event = ++ptrsdata->LastEvent;
        
        // Выпускаем следующего читателя (или писателя на вход)
        if(ptrsdata->WaitingReaders) {
            _SetEvent(ptrsdata->hWaitingReadersEvent);
        } else {
            _SetEvent(ptrsdata->hEntryEvent);
        }
        
        // Считываем значение
        DWORD my_last_writer = ptrsdata->LastWriter;
        int my_value = ptrsdata->TheValue;

        // Отпускаем читательский захват
        _WaitForSingleObject(ptrsdata->hEntryEvent);
        --ptrsdata->ActiveReaders;
        if(!ptrsdata->ActiveReaders && ptrsdata->WaitingWriters) {
            _SetEvent(ptrsdata->hWaitingWritersEvent);
        } else {
            _SetEvent(ptrsdata->hEntryEvent);
        }

        // Выводим результат
        _WaitForSingleObject(hPrintMutex);
        if(my_value < MAXIMUM_VALUE) {
            printf("%08lu: Reader %d read %d left by writer %u\r\n", my_event, my_id, my_value, my_last_writer);
        } else {
            printf("%08lu: Reader %d read %d left by writer %u and exited\r\n", my_event, my_id, my_value, my_last_writer);
            must_continue = FALSE;
        }
        _ReleaseMutex(hPrintMutex);
    }
    
    return 0;
}


DWORD WINAPI WriterProc(_In_ LPVOID lpParameter) {
    WriterLaunchInfo* ptrwli = (WriterLaunchInfo*) lpParameter;
    SharedData* ptrsdata = ptrwli->ptrsdata;
    DWORD my_id = GetCurrentThreadId();
    BOOL must_continue = TRUE;
    DWORD wait_ms = ptrwli->number * WRITER_WAIT_MS; // Раньше тут были некоторые оптимизации, но после переделок получилось так
    free(ptrwli);
    ptrwli = NULL;
	
    while(must_continue) {
        Sleep(wait_ms);
		wait_ms = WRITER_WAIT_MS * NUMBER_OF_WRITERS;
    
        unsigned long my_event;
        int written_value = 0;
        // Берём полный захват
        _WaitForSingleObject(ptrsdata->hEntryEvent);
        if(ptrsdata->ActiveReaders) {
            // Ждём читателей
            ++ptrsdata->WaitingWriters;
            _SignalObjectAndWait(ptrsdata->hEntryEvent, ptrsdata->hWaitingWritersEvent);
            --ptrsdata->WaitingWriters;
        }
        my_event = ++ptrsdata->LastEvent;
        
        // Записываем
        if(ptrsdata->TheValue < MAXIMUM_VALUE) {
            written_value = ++ptrsdata->TheValue;
            ptrsdata->LastWriter = my_id;
        } else {
            must_continue = FALSE;
        }
        
        // Выпускаем следующего
        if(ptrsdata->WaitingWriters) {
            _SetEvent(ptrsdata->hWaitingWritersEvent);
        } else if(ptrsdata->WaitingReaders) {
            _SetEvent(ptrsdata->hWaitingReadersEvent);
        } else {
            _SetEvent(ptrsdata->hEntryEvent);
        }
        
        // Выводим результат
        _WaitForSingleObject(hPrintMutex);
        if(must_continue) {
            printf("%08lu: Writer %u wrote %d\r\n", my_event, my_id, written_value);
        } else {
            printf("%08lu: Writer %u exits because the maximum value has been reached\r\n", my_event, my_id);
        }
        _ReleaseMutex(hPrintMutex);
    }
    
    return 0;
}


#define num_threads NUMBER_OF_WRITERS + NUMBER_OF_READERS
int main(int argc, char** argv) {
    HANDLE threads[num_threads];
    
    SharedData* ptrsdata = calloc(1, sizeof(SharedData));
    if(!ptrsdata) {
        handleFailure("calloc");
    }
    ptrsdata->hEntryEvent = _CreateEvent(TRUE);
    ptrsdata->hWaitingReadersEvent = _CreateEvent(FALSE);
    ptrsdata->hWaitingWritersEvent = _CreateEvent(FALSE);
    
    hPrintMutex = CreateMutex(NULL, FALSE, NULL);
    if(!hPrintMutex) {
        handleFailure("CreateMutex");
    }
    printf("The maximum value to write is %d\r\n", MAXIMUM_VALUE);
    printf("Launching %d writers\r\n", NUMBER_OF_WRITERS);
    for(int i = 1; i <= NUMBER_OF_WRITERS; ++i) {
        WriterLaunchInfo* ptrwli = malloc(sizeof(WriterLaunchInfo));
        ptrwli->number = i;
        ptrwli->ptrsdata = ptrsdata;
        threads[i - 1] = _CreateThread(&WriterProc, ptrwli);
        // ptrwli освобождается писателем
    }
    _WaitForSingleObject(hPrintMutex);
    printf("Launching %d readers\r\n", NUMBER_OF_READERS);
    _ReleaseMutex(hPrintMutex);
    for(int i = 1; i <= NUMBER_OF_READERS; ++i) {
        threads[NUMBER_OF_WRITERS + i - 1] = _CreateThread(&ReaderProc, ptrsdata);
    }
    
    // Ждём
    _WaitForSingleObject(hPrintMutex);
    printf("Waiting for Godot\r\n");
    _ReleaseMutex(hPrintMutex);
    if(WaitForMultipleObjects(num_threads, threads, TRUE, INFINITE) != WAIT_OBJECT_0) {
        handleFailure("Waiting for threads to finish");
    }
    
    // Для аккуратности
    HANDLE handles[] = {ptrsdata->hEntryEvent, ptrsdata->hWaitingReadersEvent, ptrsdata->hWaitingWritersEvent, hPrintMutex};
    for(size_t i = 0; i < sizeof(handles) / sizeof(HANDLE); ++i) {
        CloseHandle(handles[i]);
    }
    for(int i = 0; i < num_threads; ++i) {
        CloseHandle(threads[i]);
    }
    free(ptrsdata);
	return 0;
}
