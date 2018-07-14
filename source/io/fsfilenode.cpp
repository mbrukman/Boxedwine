#include "boxedwine.h"

#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#include UNISTD
#include UTIME
#include MKDIR_INCLUDE

#include "fsfilenode.h"
#include "fsdiropennode.h"
#include "fsfileopennode.h"
#include "kstat.h"
#include "fszipnode.h"

FsFileNode::FsFileNode(U32 id, U32 rdev, const std::string& path, const std::string& link, bool isDirectory, BoxedPtr<FsNode> parent) : FsNode(File, id, rdev, path, link, isDirectory, parent) {
    this->nativePath = Fs::localPathToRemote(path);
}

bool FsFileNode::remove() {
    bool result = false;
    bool exists = Fs::doesNativePathExist(this->nativePath);

    if (exists)
        result = unlink(nativePath.c_str())==0;
    // if the file failed to be deleted and it exists then its because someone else has it open, 
    // so we need to close all references, move the file then re-open the file for those handles
    // that still have it open.  By moving the file it will appear that it was deleted, but handles
    // that have it open can still use it, just like Linux does.
    if (!result && exists && this->openNodes.size()) {
        S64* tmpPos = new S64[this->openNodes.size()];
        U32 i=0;

        this->openNodes.for_each([&tmpPos,&i](KListNode<FsOpenNode*>* n) {
            FsOpenNode* openNode = n->data;
            tmpPos[i++] = openNode->getFilePointer();
            openNode->close();
        });
        
        Fs::makeLocalDirs("/tmp/del");

        std::string newNativePath;

        for (i=0;i<100000000;i++) {			
            newNativePath = Fs::localPathToRemote("/tmp/del/del"+std::to_string(i)+".tmp");
            if (!Fs::doesNativePathExist(newNativePath))
                break;
        }

        if (::rename(nativePath.c_str(), newNativePath.c_str())!=0) {
            klog("could not rename %s", nativePath.c_str());
        }

        this->nativePath = newNativePath;

        i=0;
        this->openNodes.for_each([&tmpPos,&i](KListNode<FsOpenNode*>* n) {
            FsOpenNode* openNode = n->data;
            openNode->reopen();
            openNode->seek(tmpPos[i++]);
        });
        result = true;
    }
    if (result) {
        this->removeNodeFromParent();
    }
    return result;
}

U64 FsFileNode::lastModified() {
    struct stat buf;

    if (stat(this->nativePath.c_str(), &buf)==0) {
        return buf.st_mtime*1000l;
    }
    if (this->zipNode)
        return this->zipNode->lastModified();
    return 0;
}

U64 FsFileNode::length() {
    struct stat buf;

    if (stat(this->nativePath.c_str(), &buf)==0) {
        return buf.st_size;
    }
    if (this->zipNode)
        return this->zipNode->length();
    return 0;
}

void FsFileNode::ensurePathIsLocal() {
    if (this->zipNode && !Fs::doesNativePathExist(this->nativePath)) {
        if (this->isDirectory()) {
            Fs::makeLocalDirs(this->path);
        } else {
            std::string parentPath = Fs::getParentPath(this->path);
            Fs::makeLocalDirs(parentPath.c_str());
            this->zipNode->moveToFileSystem();
        }
    }
}

FsOpenNode* FsFileNode::open(U32 flags) {
    U32 openFlags = O_BINARY;
    U32 f;
    
    if (this->isDirectory()) {
        return new FsDirOpenNode(Fs::getNodeFromLocalPath("", this->path, true), flags);
    }
    if ((flags & K_O_ACCMODE)==K_O_RDONLY) {
        openFlags|=O_RDONLY;
    } else {
        if ((flags & K_O_ACCMODE)==K_O_WRONLY) {
            openFlags|=O_WRONLY;        
        } else {
            openFlags|=O_RDWR;            
        }
        std::string parentPath = Fs::getNativeParentPath(this->nativePath);
        if (!Fs::doesNativePathExist(parentPath))
            Fs::makeLocalDirs(this->getParent()->path);
    }
    if (flags & K_O_CREAT) {
        openFlags|=O_CREAT;
    }
    if (flags & K_O_EXCL) {
        openFlags|=O_EXCL;
    }
    if (flags & K_O_TRUNC) {
        openFlags|=O_TRUNC;
    }
    if (flags & K_O_APPEND) {
        openFlags|=O_APPEND;
    }
    f = ::open(this->nativePath.c_str(), openFlags, 0666);	
    if (!f || f==0xFFFFFFFF) {
        if (this->zipNode && (flags & K_O_ACCMODE)==K_O_RDONLY)
            return this->zipNode->open(flags);
        return 0;
    }
    return new FsFileOpenNode(this, flags, f);
}

U32 FsFileNode::getType(bool checkForLink) {	
    if (this->isDirectory())
        return 4; // DT_DIR
    if (checkForLink && this->isLink()) 
        return 10; // DT_LNK
    return 8; // DT_REG
}

U32 FsFileNode::getMode() {
    U32 result = K__S_IREAD | K__S_IEXEC | (FsFileNode::getType(false) << 12);
    if (KThread::currentThread()->process->userId == 0 ||  stringStartsWith(this->path, "/tmp") ||  stringStartsWith(this->path, "/var") ||  stringStartsWith(this->path, "/home")) {
        result|=K__S_IWRITE;
    }
    return result;
}

S32 translateErr(U32 e) {
    switch (e) {
    case EPERM: return K_EPERM;
    case ENOENT: return K_ENOENT;
    case ESRCH: return K_ESRCH;
    case EINTR: return K_EINTR;
    case EIO: return K_EIO;
    case ENXIO: return K_ENXIO;
    //case E2BIG: return K_E2BIG;
    case ENOEXEC: return K_ENOEXEC;
    case EBADF: return K_EBADF;
    case ECHILD: return K_ECHILD;
    case EAGAIN: return K_EAGAIN;
    case ENOMEM: return K_ENOMEM;
    case EACCES: return K_EACCES;
    case EFAULT: return K_EFAULT;
    case EBUSY: return K_EBUSY;
    case EEXIST: return K_EEXIST;
    case EXDEV: return K_EXDEV;
    case ENODEV: return K_ENODEV;
    case ENOTDIR: return K_ENOTDIR;
    case EISDIR: return K_EISDIR;
    case ENFILE: return K_ENFILE;
    case EMFILE: return K_EMFILE;
    case ENOTTY: return K_ENOTTY;
    case EFBIG: return K_EFBIG;
    case ENOSPC: return K_ENOSPC;
    case ESPIPE: return K_ESPIPE;
    case EROFS: return K_EROFS;
    //case EMLINK: return K_EMLINK;
    case EPIPE: return K_EPIPE;
    //case EDOM: return K_EDOM;
    //case EDEADLK: return K_EDEADLK;
    case ENAMETOOLONG: return K_ENAMETOOLONG;
    case ENOLCK: return K_ENOLCK;
    case ENOSYS: return K_ENOSYS;
    case ENOTEMPTY: return K_ENOTEMPTY;
    default: return K_EIO;
    }
}

U32 FsFileNode::rename(const std::string& path) {
    U32 result;
    S64* tmpPos = NULL;

    this->ensurePathIsLocal();
    if (this->openNodes.size()) {
        int i=0;
        tmpPos = new S64[this->openNodes.size()];
        this->openNodes.for_each([&tmpPos,&i](KListNode<FsOpenNode*>* n) {
            FsOpenNode* openNode = n->data;
            tmpPos[i++] = openNode->getFilePointer();
            openNode->close();
        });
    }

    BoxedPtr<FsNode> parent = Fs::getNodeFromLocalPath("", Fs::getParentPath(path), true);
    if (!parent) {
        return -K_ENOENT;
    }

    std::string nativePath = Fs::localPathToRemote(parent->path+"/"+Fs::getFileNameFromPath(path));

    if (Fs::doesNativePathExist(nativePath)) {
        BoxedPtr<FsNode> existingNode = Fs::getNodeFromLocalPath("", path, false);
        if (existingNode->isDirectory()!=this->isDirectory()) {
            if (existingNode->isDirectory())
                return -K_EISDIR;
            return -K_ENOTDIR;
        }
        if (existingNode->isDirectory()) {
            U32 result = existingNode->removeDir();
            if (result!=0)
                return result;
        } else {
            existingNode->remove();
        }
    }
    result = ::rename(this->nativePath.c_str(), nativePath.c_str());

    if (result==0) {
        this->removeNodeFromParent();
        this->path = path;
        this->nativePath = nativePath;
        this->name = Fs::getFileNameFromPath(path);
        std::string parentPath = Fs::getParentPath(path);
        BoxedPtr<FsNode> parent = Fs::getNodeFromLocalPath("", parentPath, false);
        parent->addChild(this);
        this->parent = parent;
    } else {
        result = -translateErr(errno);
    }
    int i=0;
    this->openNodes.for_each([&tmpPos,&i](KListNode<FsOpenNode*>* n) {
        FsOpenNode* openNode = n->data;
        openNode->reopen();
        openNode->seek(tmpPos[i++]);
    });
    return result;
}

U32 FsFileNode::removeDir() {
    if (!this->isDirectory() || this->isLink())
        return -K_ENOTDIR;
    if (this->getChildCount()) {
        return -K_ENOTEMPTY;
    }    
    if (Fs::doesNativePathExist(this->nativePath) && ::rmdir(this->nativePath.c_str()) < 0) {
        return -translateErr(errno);
    }
    this->getParent()->removeChildByName(this->name);
    return 0;
}

U32 FsFileNode::setTimes(U64 lastAccessTime, U32 lastAccessTimeNano, U64 lastModifiedTime, U32 lastModifiedTimeNano) {
    struct utimbuf settime = {0, 0};

    this->ensurePathIsLocal();
    if (lastAccessTime) {
        settime.actime = lastAccessTime;
    }
    if (lastModifiedTime) {
        settime.modtime = lastModifiedTime;
    }       
    utime(this->nativePath.c_str(),&settime);
    return 0; // no error checking, we don't care if this fails
}