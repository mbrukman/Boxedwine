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

#include "kpoll.h"
#include "kscheduler.h"

S32 internal_poll(KPollData* data, U32 count, U32 timeout) {
    S32 result = 0;
    U32 i;
    KThread* thread = KThread::currentThread();
    bool interrupted = !thread->inSignal && thread->interrupted;
    KPollData* firstData=data;

    if (interrupted)
        thread->interrupted = false;
    for (i=0;i<count;i++) {
        KFileDescriptor* fd = thread->process->getFileDescriptor(data->fd);
        data->revents = 0;
        if (fd) {
            if (!fd->kobject->isOpen()) {
                data->revents = K_POLLHUP;
            } else {
                if ((data->events & K_POLLIN) != 0 && fd->kobject->isReadReady()) {
                    data->revents |= K_POLLIN;
                } else if ((data->events & K_POLLOUT) != 0 && fd->kobject->isWriteReady()) {
                    data->revents |= K_POLLOUT;
                }
            }
            if (data->revents!=0) {
                result++;
            }
        }
        data++;
    }
    if (result>0) {		
        thread->waitStartTime = 0;        
        return result;
    }
    if (timeout==0) {
        thread->waitStartTime = 0;        
        return 0;
    }	
    if (thread->waitStartTime) {
        if (getMilliesSinceStart() - thread->waitStartTime > timeout) {
            thread->waitStartTime = 0;
            return 0;
        }	
        if (interrupted) {			
            return -K_EINTR;
        }
        data = firstData;
        for (i=0;i<count;i++) {
            KFileDescriptor* fd = thread->process->getFileDescriptor(data->fd);
            fd->kobject->waitForEvents(data->events);
            data++;
        }
        if (!thread->timer.active) {
            if (timeout<0xF0000000)
                timeout+=thread->waitStartTime;
            thread->timer.millies = timeout;
            addTimer(&thread->timer);
        }
    } else {		
        if (interrupted) {			
            return -K_EINTR;
        }
        thread->waitStartTime = getMilliesSinceStart();

        data = firstData;
        for (i=0;i<count;i++) {
            KFileDescriptor* fd = thread->process->getFileDescriptor(data->fd);
            if (fd) {
                fd->kobject->waitForEvents(data->events);
            }
            data++;
        }
        if (!thread->timer.active) {
            if (timeout<0xF0000000)
                timeout+=thread->waitStartTime;
            thread->timer.millies = timeout;
            addTimer(&thread->timer);
        }
    }	
    return -K_WAIT;
}

U32 kpoll(U32 pfds, U32 nfds, U32 timeout) {
    U32 i;
    S32 result;
    U32 address = pfds;

    KPollData* pollData = new KPollData[nfds];

    for (i=0;i<nfds;i++) {
        pollData[i].fd = readd(address); address += 4;
        pollData[i].events = readw(address); address += 2;
        pollData[i].revents = readw(address); address += 2;
    }

    result = internal_poll(pollData, nfds, timeout);
    if (result >= 0) { 
        pfds+=6;
        for (i=0;i<nfds;i++) {
            writew(pfds, pollData[i].revents);
            pfds+=8;
        }
    }
    delete[] pollData;
    return result;
}


U32 kselect(U32 nfds, U32 readfds, U32 writefds, U32 errorfds, U32 timeout) {
    S32 result = 0;
    U32 i;
    int count = 0;
    U32 pollCount = 0;
    KPollData* pollData = new KPollData[nfds];

    for (i=0;i<nfds;) {
        U32 readbits = 0;
        U32 writebits = 0;
        U32 errorbits = 0;
        U32 b;

        if (readfds!=0) {
            readbits = readb(readfds + i / 8);
        }
        if (writefds!=0) {
            writebits = readb(writefds + i / 8);
        }
        if (errorfds!=0) {
            errorbits = readb(errorfds + i / 8);
        }
        for (b = 0; b < 8 && i < nfds; b++, i++) {
            U32 mask = 1 << b;
            U32 r = readbits & mask;
            U32 w = writebits & mask;
            U32 e = errorbits & mask;
            if (r || w || e) {
                U32 events = 0;
                if (r)
                    events |= K_POLLIN;
                if (w)
                    events |= K_POLLHUP|K_POLLOUT;
                if (e)
                    events |= K_POLLERR;
                pollData[pollCount].events = events;
                pollData[pollCount].fd = i;
                pollCount++;
            }
        }
    }
    if (timeout==0)
        timeout = 0x7FFFFFFF;
    else {
        timeout = readd(timeout) * 1000 + readd(timeout + 4) / 1000;
    }

    result = internal_poll(pollData, pollCount, timeout);
    if (result == -K_WAIT) {
        delete[] pollData;
        return result;
    }

    if (readfds)
        zeroMemory(readfds, (nfds + 7) / 8);
    if (writefds)
        zeroMemory(writefds, (nfds + 7) / 8);
    if (errorfds)
        zeroMemory(errorfds, (nfds + 7) / 8);

    if (result <= 0) {
        delete[] pollData;
        return result;
    }

    for (i=0;i<pollCount;i++) {
        U32 found = 0;
        FD fd = pollData[i].fd;
        U32 revent = pollData[i].revents;

        if (readfds!=0 && ((revent & K_POLLIN) || (revent & K_POLLHUP))) {
            U8 v = readb(readfds + fd / 8);
            v |= 1 << (fd % 8);
            writeb(readfds + fd / 8, v);
            found = 1;
        }
        if (writefds!=0 && (revent & K_POLLOUT)) {
            U8 v = readb(writefds + fd / 8);
            v |= 1 << (fd % 8);
            writeb(writefds + fd / 8, v);
            found = 1;
        }
        if (errorfds!=0 && (revent & K_POLLERR)) {
            U8 v = readb(errorfds + fd / 8);
            v |= 1 << (fd % 8);
            writeb(errorfds + fd / 8, v);
            found = 1;
        }
        if (found) {
            count++;
        }
    }
    delete[] pollData;
    return count;
}