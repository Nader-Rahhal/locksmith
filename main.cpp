#include <atomic>
#include <iostream>
#include <unistd.h>
#include <ncurses.h>
#include <pthread.h>
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

        int getProcCount(){
            return proc_count;
        }

        struct kinfo_proc* getProcTable(){
            return procs;
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

int main() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    start_color();
    
    // Define color pairs
    init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_CYAN, COLOR_BLACK); // For header
    
    // Get system data
    SystemData sys;
    pthread_t producer_thread;
    pthread_create(&producer_thread, NULL, producer, (void*)&sys);
    pthread_join(producer_thread, NULL); 
    
    int procCount = sys.getProcCount();
    struct kinfo_proc* procs = sys.getProcTable();
    
    // Clear the screen
    clear();
    
    // Create the header bar
    attron(A_BOLD | COLOR_PAIR(3));
    mvprintw(0, 0, "%-10s %-20s %-10s %-10s %-8s", "PID", "COMMAND", "USER", "CPU%");
    attroff(A_BOLD | COLOR_PAIR(3));
    
    // Draw a line under the header
    mvhline(1, 0, ACS_HLINE, COLS);
    
    // Display process information
    int row = 2;
    for (int i = 0; i < procCount && row < LINES; i++) {
        mvprintw(row, 0, "%-10d %-20s %-10d %-10.1f", 
                 procs[i].kp_proc.p_pid, 
                 procs[i].kp_proc.p_comm,
                 procs[i].kp_eproc.e_ucred.cr_uid,
                 (float)procs[i].kp_proc.p_pctcpu
                 );
        row++;
    }
    
    // Refresh the screen and wait for a keypress
    refresh();
    getch();
    
    // Clean up
    endwin();
    return 0;
}
