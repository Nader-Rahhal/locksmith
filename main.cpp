#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <unistd.h>
#include <ncurses.h>
#include <pthread.h>
#include <sys/types.h>    
#include <sys/sysctl.h>   
#include <sys/proc_info.h> 
#include <libproc.h>     
#include <vector>
#include <pwd.h>

struct raw_proc {
    int pid;
    struct proc_taskinfo proc_info;
    struct proc_bsdinfo bsdinfo;
};

struct processed_proc {
    int pid;
    std::string start_time;
    std::string username;
    std::string name;
    float cpu_usage;
    int thread_count;
};


class SystemData {

    private:

        std::atomic<int> next_ticket_raw;
        std::atomic<int> now_serving_raw;

        std::atomic<int> next_ticket_processed;
        std::atomic<int> now_serving_processed;
        
        int proc_count;
        pid_t* pids;
        std::vector<struct raw_proc> raw_proc_data;
        std::vector<struct processed_proc> processed_proc_data;

    public:

        SystemData(){
            pids = nullptr;
            proc_count = 0;
            next_ticket_raw.store(0);
            now_serving_raw.store(0);
            next_ticket_processed.store(0);
            now_serving_processed.store(0);
        }

        int getProcCount(){
            return proc_count;
        }

        pid_t* getProcTable(){
            return pids;
        }

        const std::vector<struct raw_proc> getRawProcessData(){
            return raw_proc_data;
        }
        
        const std::vector<struct processed_proc>& getProcessedProcessData(){
            return processed_proc_data;
        }

        bool acquire_unprocessed_buffer_lock(){
            int my_ticket = next_ticket_raw.fetch_add(1);
            while (now_serving_raw.load() != my_ticket){
                sched_yield();
            }
            return true;
        }

        bool acquire_processed_buffer_lock(){
            int my_ticket = next_ticket_processed.fetch_add(1);
            while (now_serving_processed.load() != my_ticket){
                sched_yield();
            }
            return true;
        }

        void release_unprocessed_buffer_lock(){
            now_serving_raw.fetch_add(1);        
            return;
        }

        void release_processed_buffer_lock(){
            now_serving_processed.fetch_add(1);
            return;
        }

        void fetchRawData(){
            raw_proc_data.clear();
    
            proc_count = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
            
            if (pids != nullptr) {
                free(pids);
            }
            
            pids = (pid_t*)malloc(sizeof(pid_t) * proc_count);
            proc_count = (proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pid_t) * proc_count)) / sizeof(pid_t);

            raw_proc_data.resize(proc_count);
    
            for (int i = 0; i < proc_count; i++){
                struct raw_proc& new_raw_proc = raw_proc_data[i];
                new_raw_proc.pid = pids[i];
                
                if (pids[i] <= 0) {
                    continue;
                }

                int task_ret = proc_pidinfo(pids[i], PROC_PIDTASKINFO, 0, 
                                          &new_raw_proc.proc_info, 
                                          sizeof(new_raw_proc.proc_info));
                                 
                int bsd_ret = proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, 
                                         &new_raw_proc.bsdinfo, 
                                         sizeof(new_raw_proc.bsdinfo));
                                 
                if (task_ret <= 0 || bsd_ret <= 0) {
                    new_raw_proc.pid = 0; // Mark as invalid
                }
            }
        }

        void processRawData(){
            processed_proc_data.clear();
            
            int valid_count = 0;
            for (int i = 0; i < proc_count; i++) {
                if (raw_proc_data[i].pid > 0) {
                    valid_count++;
                }
            }
            
            processed_proc_data.resize(valid_count);
            
            int valid_idx = 0;
            for (int i = 0; i < proc_count; i++) {
                if (raw_proc_data[i].pid <= 0) {
                    continue;
                }
                
                struct processed_proc& new_processed_proc = processed_proc_data[valid_idx++];
                new_processed_proc.pid = raw_proc_data[i].pid;
                
                char name[PROC_PIDPATHINFO_MAXSIZE];
                proc_name(raw_proc_data[i].pid, &name, PROC_PIDPATHINFO_MAXSIZE);
                new_processed_proc.name = name;

                uint64_t seconds = raw_proc_data[i].bsdinfo.pbi_start_tvsec;
                std::time_t time = static_cast<time_t>(seconds);
                struct tm* start_time = std::localtime(&time);

                char buffer[80];
                strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", start_time);
                new_processed_proc.start_time = buffer;

                struct passwd *password = getpwuid(raw_proc_data[i].bsdinfo.pbi_uid);
                if (password != nullptr) {
                    new_processed_proc.username = password->pw_name;
                } else {
                    new_processed_proc.username = "null";
                }

                new_processed_proc.thread_count = raw_proc_data[i].proc_info.pti_threadnum;                
            }
            
            int actual_valid_count = valid_idx;
            
            std::vector<uint64_t> samp_1(actual_valid_count);
            std::vector<pid_t> pids_copy(actual_valid_count);
            uint64_t num_cores = sysconf(_SC_NPROCESSORS_ONLN);

            for (int i = 0; i < actual_valid_count; i++) {
                for (int j = 0; j < proc_count; j++) {
                    if (processed_proc_data[i].pid == raw_proc_data[j].pid) {
                        samp_1[i] = raw_proc_data[j].proc_info.pti_total_system + raw_proc_data[j].proc_info.pti_total_user;
                        pids_copy[i] = processed_proc_data[i].pid;
                        break;
                    }
                }
            }

            sleep(1);

            fetchRawData();
            const uint64_t NS_PER_SEC = 1000000000ULL;

            for (int i = 0; i < actual_valid_count; i++) {
                bool found = false;
                for (int j = 0; j < proc_count; j++) {
                    if (pids_copy[i] == raw_proc_data[j].pid) {
                        uint64_t samp_2 = raw_proc_data[j].proc_info.pti_total_system + raw_proc_data[j].proc_info.pti_total_user;
                        double cpu_usage = ((double)(samp_2 - samp_1[i]) / (1.0 * NS_PER_SEC * num_cores)) * 100.0;
                        processed_proc_data[i].cpu_usage = cpu_usage;
                        found = true;
                        break;
                    }
                }
                
                if (!found) {
                    processed_proc_data[i].cpu_usage = 0.0;
                }
            }
            
            proc_count = actual_valid_count;
        }
};

void* producer(void* arg){
    SystemData* data = (SystemData*)arg;
    
    while(!data->acquire_unprocessed_buffer_lock()){
        sleep(1);
    }

    data->fetchRawData();

    data->release_unprocessed_buffer_lock();
    
    return nullptr;
}

void* master_consumer(void* arg){
    SystemData* data = (SystemData*)arg;
    
    while(!data->acquire_unprocessed_buffer_lock()){
        sleep(1);
    }
    
    while(!data->acquire_processed_buffer_lock()){
        sleep(1);
    }
    
    data->processRawData();
    
    data->release_processed_buffer_lock();
    data->release_unprocessed_buffer_lock();
    
    return nullptr;
}

// Function to render UI
void render_ui(SystemData& sys, int startRow) {
    sys.acquire_processed_buffer_lock();
    
    int maxRows = LINES - 2; 
    int proc_count = sys.getProcCount();
    const std::vector<struct processed_proc>& processed_proc_data = sys.getProcessedProcessData();
    
    // Clear process list area
    for (int i = 2; i < LINES; i++) {
        move(i, 0);
        clrtoeol();
    }
    
    // Display processes with current scroll position
    for (int i = 0; i < maxRows && (startRow + i) < proc_count; i++) {
        int idx = startRow + i;
        
        // Highlight high CPU usage processes
        if (processed_proc_data[idx].cpu_usage > 50.0) {
            attron(COLOR_PAIR(1)); // Red for high CPU
        } else if (processed_proc_data[idx].cpu_usage > 20.0) {
            attron(COLOR_PAIR(2)); // Green for moderate CPU
        }
        
        // Print all fields for each process
        mvprintw(i + 2, 0, "%-8d %-20s %-15s %6.1f%% %-20s %-10d", 
                processed_proc_data[idx].pid,
                processed_proc_data[idx].name.substr(0, 20).c_str(),
                processed_proc_data[idx].username.substr(0, 15).c_str(),
                processed_proc_data[idx].cpu_usage,
                processed_proc_data[idx].start_time.c_str(),
                processed_proc_data[idx].thread_count);
        
        // Turn off highlighting
        if (processed_proc_data[idx].cpu_usage > 20.0) {
            attroff(COLOR_PAIR(1) | COLOR_PAIR(2));
        }
    }

    // Display scrollbar or scroll indicator if needed
    if (proc_count > maxRows) {
        int scrollbarHeight = maxRows * maxRows / proc_count;
        int scrollbarPos = startRow * maxRows / proc_count;
        
        for (int i = 0; i < maxRows; i++) {
            if (i >= scrollbarPos && i < scrollbarPos + scrollbarHeight) {
                mvprintw(i + 2, COLS - 1, "|");
            } else {
                mvprintw(i + 2, COLS - 1, " ");
            }
        }
    }
    
    // Display scroll position and help text
    mvprintw(LINES-1, 0, "Scroll: %d/%d | Press 'r' to refresh data | 'q' to quit", 
            startRow, proc_count > maxRows ? proc_count - maxRows : 0);
    
    // Refresh the screen
    refresh();
    
    sys.release_processed_buffer_lock();
}

int main() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    start_color();
    curs_set(0);    
    init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_CYAN, COLOR_BLACK);    
    attron(A_BOLD | COLOR_PAIR(3));
    mvprintw(0, 0, "%-8s %-20s %-15s %-8s %-20s %-10s", 
             "PID", "NAME", "USER", "CPU%", "START TIME", "THREADS");
    attroff(A_BOLD | COLOR_PAIR(3));
    
    mvhline(1, 0, ACS_HLINE, COLS);
    
    SystemData sys;
    
    pthread_t producer_thread;
    pthread_t consumer_thread;
    
    pthread_create(&producer_thread, NULL, producer, (void*)&sys);
    pthread_join(producer_thread, NULL);
    
    pthread_create(&consumer_thread, NULL, master_consumer, (void*)&sys);
    pthread_join(consumer_thread, NULL);
    
    int startRow = 0;
    bool running = true;
    
    render_ui(sys, startRow);
    
    while (running) {
        timeout(100); 
        int ch = getch();
        
        bool need_redraw = false;
        
        int maxRows = LINES - 2;
        int proc_count = sys.getProcCount();
        
        switch(ch) {
            case KEY_UP:
            case 'k':
                if (startRow > 0) {
                    startRow--;
                    need_redraw = true;
                }
                break;
            case KEY_DOWN:
            case 'j':
                if (startRow + maxRows < proc_count) {
                    startRow++;
                    need_redraw = true;
                }
                break;
            case KEY_PPAGE: // Page Up
                startRow = startRow > maxRows ? startRow - maxRows : 0;
                need_redraw = true;
                break;
            case KEY_NPAGE: // Page Down
                startRow = (startRow + maxRows < proc_count - maxRows) ? 
                          startRow + maxRows : (proc_count > maxRows ? proc_count - maxRows : 0);
                need_redraw = true;
                break;
            case KEY_HOME:
                startRow = 0;
                need_redraw = true;
                break;
            case KEY_END:
                startRow = proc_count > maxRows ? proc_count - maxRows : 0;
                need_redraw = true;
                break;
            case 'r': // Refresh data
                mvprintw(LINES-1, 40, "Refreshing data...");
                refresh();
                
                pthread_create(&producer_thread, NULL, producer, (void*)&sys);
                pthread_join(producer_thread, NULL);
                
                pthread_create(&consumer_thread, NULL, master_consumer, (void*)&sys);
                pthread_join(consumer_thread, NULL);
                
                need_redraw = true;
                break;
            case 'q':
                running = false;
                break;
        }
        
        if (need_redraw) {
            render_ui(sys, startRow);
        }
    }
    
    endwin();
    return 0;
}
