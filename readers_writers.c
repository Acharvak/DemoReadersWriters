#include <windows.h>
#include <synchapi.h>
#include <stdio.h>
#include <stdlib.h>

#define READER_WAIT_MS 3000
#define WRITER_WAIT_MS 10000

#define NUMBER_OF_READERS 10
#define NUMBER_OF_WRITERS 3
#define MAXIMUM_VALUE 10

// Я кастую между int и LPVOID очень аккуратно. Достаточно аккуратно для тестового задания, по крайней мере.
#pragma warning(disable : 4311)
#pragma warning(disable : 4312)

// Нормальные люди, конечно, берут готовый SRWLock из synchapi.h,
// но по заданию надо это делать вручную, причём с применением mutex
// и event. Поэтому вот вам вручную и на чистом C (должно компилироваться и как C++).
HANDLE hEntryEvent;
HANDLE hWaitingReadersEvent;
HANDLE hWaitingWritersEvent;

int ActiveReaders = 0;
int WaitingReaders = 0;
int WaitingWriters = 0;

int LastWriter = 0;
int TheValue = 0;

unsigned long LastEvent = 0;    // Для протоколирования

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
    int my_number = (int) lpParameter;
    BOOL must_continue = TRUE;

    while(must_continue) {
        Sleep(READER_WAIT_MS);
    
        unsigned long my_event;
        // Берём читательский захват
        _WaitForSingleObject(hEntryEvent);
        if(WaitingWriters) {
            // Надо ждать писателей
            ++WaitingReaders;
            _SignalObjectAndWait(hEntryEvent, hWaitingReadersEvent);
            --WaitingReaders;
        }
        ++ActiveReaders;
        my_event = ++LastEvent;
        
        // Выпускаем следующего читателя (или писателя на вход)
        if(WaitingReaders) {
            _SetEvent(hWaitingReadersEvent);
        } else {
            _SetEvent(hEntryEvent);
        }
        
        // Считываем значение
        int my_last_writer = LastWriter;
        int my_value = TheValue;

        // Отпускаем читательский захват
        _WaitForSingleObject(hEntryEvent);
        --ActiveReaders;
        if(!ActiveReaders && WaitingWriters) {
            _SetEvent(hWaitingWritersEvent);
        } else {
            _SetEvent(hEntryEvent);
        }

        // Выводим результат
        _WaitForSingleObject(hPrintMutex);
        if(my_value < MAXIMUM_VALUE) {
            printf("%08lu: Reader %d read %d left by writer %d\r\n", my_event, my_number, my_value, my_last_writer);
        } else {
            printf("%08lu: Reader %d read %d left by writer %d and exited\r\n", my_event, my_number, my_value, my_last_writer);
            must_continue = FALSE;
        }
        _ReleaseMutex(hPrintMutex);
    }
    
    return 0;
}


DWORD WINAPI WriterProc(_In_ LPVOID lpParameter) {
    int my_number = (int) lpParameter;
    BOOL must_continue = TRUE;
    DWORD wait_ms = my_number * WRITER_WAIT_MS;
	
    while(must_continue) {
        Sleep(wait_ms);
		wait_ms = WRITER_WAIT_MS * NUMBER_OF_WRITERS;
    
        unsigned long my_event;
        int written_value = 0;
        // Берём полный захват
        _WaitForSingleObject(hEntryEvent);
        if(ActiveReaders) {
            // Ждём читателей
            ++WaitingWriters;
            _SignalObjectAndWait(hEntryEvent, hWaitingWritersEvent);
            --WaitingWriters;
        }
        my_event = ++LastEvent;
        
        // Записываем
        if(TheValue < MAXIMUM_VALUE) {
            written_value = ++TheValue;
            LastWriter = my_number;
        } else {
            must_continue = FALSE;
        }
        
        // Выпускаем следующего
        if(WaitingWriters) {
            _SetEvent(hWaitingWritersEvent);
        } else if(WaitingReaders) {
            _SetEvent(hWaitingReadersEvent);
        } else {
            _SetEvent(hEntryEvent);
        }
        
        // Выводим результат
        _WaitForSingleObject(hPrintMutex);
        if(must_continue) {
            printf("%08lu: Writer %d wrote %d\r\n", my_event, my_number, written_value);
        } else {
            printf("%08lu: Writer %d exits because the maximum value has been reached\r\n", my_event, my_number);
        }
        _ReleaseMutex(hPrintMutex);
    }
    
    return 0;
}


#define num_threads NUMBER_OF_WRITERS + NUMBER_OF_READERS
int main(int argc, char** argv) {
    HANDLE threads[num_threads];
    hEntryEvent = _CreateEvent(TRUE);
    hWaitingReadersEvent = _CreateEvent(FALSE);
    hWaitingWritersEvent = _CreateEvent(FALSE);
    hPrintMutex = CreateMutex(NULL, FALSE, NULL);
    if(!hPrintMutex) {
        handleFailure("CreateMutex");
    }
    printf("The maximum value to write is %d\r\n", MAXIMUM_VALUE);
    printf("Launching %d writers\r\n", NUMBER_OF_WRITERS);
    for(int i = 1; i <= NUMBER_OF_WRITERS; ++i) {
        threads[i - 1] = _CreateThread(&WriterProc, (LPVOID) i);
    }
    _WaitForSingleObject(hPrintMutex);
    printf("Launching %d readers\r\n", NUMBER_OF_READERS);
    _ReleaseMutex(hPrintMutex);
    for(int i = 1; i <= NUMBER_OF_READERS; ++i) {
        threads[NUMBER_OF_WRITERS + i - 1] = _CreateThread(&ReaderProc, (LPVOID) i);
    }
    
    // Ждём
    _WaitForSingleObject(hPrintMutex);
    printf("Waiting for Godot\r\n");
    _ReleaseMutex(hPrintMutex);
    if(WaitForMultipleObjects(num_threads, threads, TRUE, INFINITE) != WAIT_OBJECT_0) {
        handleFailure("Waiting for threads to finish");
    }
    
    // Для аккуратности
    HANDLE handles[] = {hEntryEvent, hWaitingReadersEvent, hWaitingWritersEvent, hPrintMutex};
    for(size_t i = 0; i < sizeof(handles) / sizeof(HANDLE); ++i) {
        CloseHandle(handles[i]);
    }
    for(int i = 0; i < num_threads; ++i) {
        CloseHandle(threads[i]);
    }
	return 0;
}
