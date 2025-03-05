#include <atomic>
#include <iostream>
#include <unistd.h>

#include <sys/types.h>    // For basic type definitions
#include <sys/sysctl.h>   // For sysctl API functions
#include <sys/proc_info.h> // For process info structures
#include <libproc.h>      // For supplementary process functions

class SystemData {

    private:
        std::atomic<bool> lock_taken;
        void* buffer;
        size_t buffer_size;


        struct kinfo_proc* procs;
        int proc_count;

    public:

        SystemData(){
            buffer_size = 0;
            lock_taken = false;
            buffer = nullptr;
            procs = nullptr;
            proc_count = 0;
        }

        bool acquire_lock(){
            bool expected = false;
            return lock_taken.compare_exchange_strong(expected, true);
        }

        void release_lock(){
            lock_taken.store(false);
        }

        void fetchData(){


            int mib[3] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL };
            size_t new_size = 0;


            sysctl(mib, 3, NULL, &new_size, NULL, 0);

            if (new_size != buffer_size) {
                if (buffer) free(buffer);
                buffer = malloc(new_size);
                buffer_size = new_size;
            }

            sysctl(mib, 3, buffer, &buffer_size, NULL, 0);

            procs = (struct kinfo_proc*)buffer;
            proc_count = buffer_size / sizeof(struct kinfo_proc);

            release_lock();
        }
};

void* producer(void* arg){
    SystemData* lock = (SystemData*)arg;
    while(!lock->acquire_lock()){
        sleep(1000);
    }

    lock->fetchData();

    lock->release_lock();
}

void* master_consumer(void* arg){
    SystemData* lock = (SystemData*)arg;

    while(!lock->acquire_lock()){
        sleep(1000);
    }

    // shoot off various consumers for data collection to UI

    // these will update a shared buffer called ProcessedData, when master lock is finished it will trigger a rerender



    lock->release_lock();
}



int main(){

    SystemData lock;
    
    // Test the lock
    if (lock.acquire_lock()) {
        std::cout << "Lock acquired" << std::endl;
        lock.release_lock();
    }

    int mib[3] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL };

    size_t oldlenp = 0;

    sysctl(mib, 3, NULL, &oldlenp, NULL, 0);

    void *oldp = malloc(oldlenp);

    sysctl(mib, 3, oldp, &oldlenp, NULL, 0);

    struct kinfo_proc* procs = (struct kinfo_proc*)oldp;
    int count = oldlenp / sizeof(struct kinfo_proc);

    free(oldp);
    return 0;
}