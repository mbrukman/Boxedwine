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

#ifndef __KSYSTEM_H__
#define __KSYSTEM_H__

#include "platform.h"

extern U32 screenCx;
extern U32 screenCy;

#define UID 1
#define GID 1000

#define MAX_STACK_SIZE (4*1024*1024)
#define MAX_ADDRESS_SPACE 0xFFFF0000
#define MAX_NUMBER_OF_FILES 0xFFF
#define MAX_DATA_SIZE 1024*1024*1024

#define CALL_BACK_ADDRESS 0xFFFF0000
#define SIG_RETURN_ADDRESS CALL_BACK_ADDRESS

class MappedFileCache;
class KTimer;
class CPU;
class KProcess;
class KThread;

class SHM : public BoxedPtrBase {
public:
    SHM(U32 id, U32 key) : id(id), key(key) {}

    void incAttach() {this->nattch++;}
    void decAttach() {this->nattch--;}

    std::vector<U8*> pages;
    const U32 id;
    U32 len;
    const U32 key;
    U32 cpid;
    U32 lpid;
    U64 ctime;
    U64 dtime;
    U64 atime;
    U32 nattch;
    U32 markedForDelete;
    U32 cuid;
    U32 cgid;
};

class KSystem {
public:
    static U32 nextThreadId;    

    // helpers
    static void writeStat(const std::string& path, U32 buf, bool is64, U64 st_dev, U64 st_ino, U32 st_mode, U64 st_rdev, U64 st_size, U32 st_blksize, U64 st_blocks, U64 mtime, U32 linkCount);
    static void eraseFileCache(const std::string& name);
    static KProcess* getProcess(U32 id);
    static BoxedPtr<MappedFileCache> getFileCache(const std::string& name);
    static const std::unordered_map<U32, KProcess*>& getProcesses();
    static void eraseProcess(U32 id);
    static void addProcess(U32 id, KProcess* process);

    // syscalls
    static U32 clock_getres(U32 clk_id, U32 timespecAddress);
    static U32 clock_gettime(U32 clock_id, U32 tp);
    static U32 getpgid(U32 pid);
    static U32 gettimeofday(U32 tv, U32 tz);
    static U32 kill(S32 pid, U32 signal);
    static U32 prlimit64(U32 pid, U32 resource, U32 newlimit, U32 oldlimit);
    static U32 setpgid(U32 pid, U32 gpid);
    static U32 shmget(U32 key, U32 size, U32 flags);
    static U32 shmat(U32 shmid, U32 shmaddr, U32 shmflg, U32 rtnAddr);
    static U32 shmdt(U32 shmaddr);
    static U32 shmctl(U32 shmid, U32 cmd, U32 buf);
    static U32 sysinfo(U32 address);
    static U32 times(U32 buf);
    static U32 tgkill(U32 threadGroupId, U32 threadId, U32 signal);
    static U32 ugetrlimit(U32 resource, U32 rlim);
    static U32 uname(U32 address);
    static U32 waitpid(S32 pid, U32 statusAddress, U32 options);        

private:
    static std::unordered_map<void*, SHM*> shm;
    static std::unordered_map<U32, KProcess*> processes;
    static std::unordered_map<std::string, BoxedPtr<MappedFileCache> > fileCache;
};

void initCallbacksInProcess(KProcess* process);

// returns pid
U32 getProcessCount();

U32 getMilliesSinceStart();
void printStacks();
void runThreadSlice(KThread* thread);

void ksyscall(CPU* cpu, U32 eipCount);

#endif