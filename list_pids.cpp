#include <iostream>
#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    // First call to get the number of processes
    int proc_count = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    
    // Allocate memory for the PIDs
    pid_t* pids = (pid_t*)malloc(sizeof(pid_t) * proc_count);
    if (!pids) {
        std::cerr << "Memory allocation failed" << std::endl;
        return 1;
    }
    
    // Second call to get the actual PIDs
    proc_count = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pid_t) * proc_count);
    if (proc_count <= 0) {
        std::cerr << "Failed to get process list" << std::endl;
        free(pids);
        return 1;
    }
    
    // Calculate actual number of processes (proc_count is now bytes returned)
    proc_count = proc_count / sizeof(pid_t);
    
    // Output the PIDs
    std::cout << "Found " << proc_count << " processes:" << std::endl;
    for (int i = 0; i < proc_count; i++) {
        // Skip PID 0
        if (pids[i] == 0) continue;
        
        std::cout << "PID: " << pids[i] << std::endl;
    }
    
    // Clean up
    free(pids);
    return 0;
}