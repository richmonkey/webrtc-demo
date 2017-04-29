#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "webrtc/examples/voip/load.h"


class FileLock {
public:
    FileLock(std::string& path) {
        fd_ = open(path.c_str(), O_RDWR|O_CREAT, 0666);
    }

    ~FileLock() {
        if (fd_ != -1) {
            close(fd_);
        }
    }
    
    bool ReadLock() {
        if (fd_ == -1) {
            return false;
        }
        struct flock lock;
        lock.l_type    = F_RDLCK;   
        lock.l_start   = 0;
        lock.l_whence  = SEEK_SET;
        lock.l_len     = 0;        
        int r = fcntl(fd_, F_SETLK, &lock);
        if (r == -1) {
            printf("file read lock error\n");
            return false;
        }
        return true;
    }

    bool WriteLock() {
        if (fd_ == -1) {
            return false;
        }
        
        struct flock lock;
        lock.l_type    = F_WRLCK;   
        lock.l_start   = 0;
        lock.l_whence  = SEEK_SET;
        lock.l_len     = 0;        
        int r = fcntl(fd_, F_SETLK, &lock);
        if (r == -1) {
            printf("file write lock error\n");
            return false;
        }
        return true;
    }

    bool Unlock() {
        if (fd_ == -1) {
            return false;
        }
                
        struct flock lock;
        lock.l_type    = F_UNLCK;   
        lock.l_start   = 0;
        lock.l_whence  = SEEK_SET;
        lock.l_len     = 0;        
        int r = fcntl(fd_, F_SETLK, &lock);
        if (r == -1) {
            printf("file  unlock error\n");
            return false;
        }
        return true;        
    }

    bool Write(const std::string& content) {
        if (fd_ == -1) {
            return false;
        }

        ftruncate(fd_, 0);
        size_t n = write(fd_, content.c_str(), content.length());
        if (n != content.length()) {
            return false;
        }
        return true;
    }

    std::string Read() {
        char buf[64*1024] = {0};
        int n = read(fd_, buf, 64*1024);
        if (n <= 0) {
            return "";
        }
        return std::string(buf);
    }
    
private:
    int fd_;
};

void SetCameraLoad(std::string& camera_id, int load) {
    std::string p = "/tmp/load_" + camera_id + ".lock";
    FileLock fl(p);
    fl.WriteLock();

    char buf[256];
    snprintf(buf, 256, "%d", load);

    fl.Write(std::string(buf));
    fl.Unlock();
}

int GetCameraLoad(std::string& camera_id) {
  std::string p = "/tmp/load_" + camera_id + ".lock";
    FileLock fl(p);
    fl.ReadLock();
    std::string content = fl.Read();
    fl.Unlock();

    int load = 0;
    if (content.c_str()) {
        load = atoi(content.c_str());
    }
    return load;
}

void SetCameraList(std::vector<std::string>& cameras) {
    std::string p = "/tmp/load_cameras";

    std::string content;
    int size = 0;
    for (size_t i = 0; i < cameras.size(); i++) {
        size += cameras[i].length() + 1;
    }

    content.reserve(size);
    for (size_t i = 0; i < cameras.size(); i++) {
        content += cameras[i];
        content += "\n";
    }
    
    FileLock fl(p);
    fl.WriteLock();
    fl.Write(content);
    fl.Unlock();    
}

std::vector<std::string> GetCameraList() {
    std::string p = "/tmp/load_cameras";
     
    FileLock fl(p);
    fl.ReadLock();
    std::string content = fl.Read();
    fl.Unlock();

    std::string buff;
    std::vector<std::string> v;
	
	for(size_t i = 0; i < content.length(); i++) {
        int c = content[i];
		if(c != '\n') {
            buff += content[i];
        } else {
            if(c == '\n' && buff != "") {
                v.push_back(buff);
                buff = "";
            }
        }
	}
	if(buff != "") {
        v.push_back(buff);
    }
	
	return v;
    
}
