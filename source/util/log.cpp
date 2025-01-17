/*
 *  Copyright (C) 2016  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "boxedwine.h"

#include <stdio.h>
#include <stdarg.h>
#include "knativethread.h"
#include "knativesystem.h"

#ifdef BOXEDWINE_MSVC
#include <Windows.h>
#endif

FILE* logFile;

void kpanic(const char* msg, ...) {
    va_list argptr;
    va_start(argptr, msg);
    vfprintf(stderr, msg, argptr);
    if (logFile) {
        vfprintf(logFile, msg, argptr);
    }    
    fprintf(stderr, "\n");
    if (logFile) {
        fprintf(logFile, "\n");
        fflush(logFile);
        fclose(logFile);
    }
    char buff[1024];
    vsnprintf(buff, sizeof(buff), msg, argptr);
#ifdef BOXEDWINE_MSVC
    OutputDebugStringA(buff);
    OutputDebugStringA("\n");
#endif
    va_end(argptr);
    if (KSystem::videoEnabled) {
        KNativeSystem::exit(buff, 1);
    } else {
        KNativeThread::sleep(5000);
    }
}

void kwarn(const char* msg, ...) {
#ifdef _DEBUG
    va_list argptr;
    va_start(argptr, msg);
    BOXEDWINE_CRITICAL_SECTION;

    vfprintf(stderr, msg, argptr);
    if (logFile) {
        vfprintf(logFile, msg, argptr);
    }    
    fprintf(stderr, "\n");
    if (logFile) {
        fprintf(logFile, "\n");
    }
#ifdef BOXEDWINE_MSVC
    char buff[1024];
    vsnprintf(buff, sizeof(buff), msg, argptr);
    OutputDebugStringA(buff);
    OutputDebugStringA("\n");
#endif
    va_end(argptr);
#endif
}

void klog(const char* msg, ...) {
    va_list argptr;
    va_start(argptr, msg);
    BOXEDWINE_CRITICAL_SECTION;
    
    vfprintf(stdout, msg, argptr);
    if (logFile) {
        vfprintf(logFile, msg, argptr);
    }
    fprintf(stdout, "\n");
    if (logFile) {
        fprintf(logFile, "\n");
        fflush(logFile);
    }
    fflush(stdout);
#ifdef BOXEDWINE_MSVC
    char buff[1024];
    vsnprintf(buff, sizeof(buff), msg, argptr);
    OutputDebugStringA(buff);
    OutputDebugStringA("\n");
#endif
    va_end(argptr);
}
