#include <iostream>
#include <libproc.h>
#include <pwd.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <pid>" << std::endl;
        return 1;
    }

    // Get PID from command line
    pid_t pid = atoi(argv[1]);
    
    // Get process info
    struct proc_taskinfo taskinfo;
    int result = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &taskinfo, sizeof(taskinfo));
    
    if (result <= 0) {
        std::cerr << "Error getting task info for pid " << pid << std::endl;
        return 1;
    }
    
    // Get process name
    char name[PROC_PIDPATHINFO_MAXSIZE];
    proc_name(pid, name, sizeof(name));
    
    // Get process arguments
    char pathbuf[PROC_PIDPATHINFO_MAXSIZE];
    proc_pidpath(pid, pathbuf, sizeof(pathbuf));
    
    // Get process owner (user)
    struct proc_bsdinfo bsdinfo;
    result = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bsdinfo, sizeof(bsdinfo));
    
    // Look up username from uid
    struct passwd *pw = getpwuid(bsdinfo.pbi_uid);
    std::string username = pw ? pw->pw_name : std::to_string(bsdinfo.pbi_uid);
    
    // Display process information
    std::cout << "Process ID: " << pid << std::endl;
    std::cout << "User: " << username << std::endl;
    std::cout << "Command Name: " << name << std::endl;
    std::cout << "Full Path: " << pathbuf << std::endl;
    std::cout << "CPU Time (user): " << taskinfo.pti_total_user / 1000000000.0 << " seconds" << std::endl;
    std::cout << "CPU Time (system): " << taskinfo.pti_total_system / 1000000000.0 << " seconds" << std::endl;
    std::cout << "Memory Usage: " << taskinfo.pti_resident_size / (1024*1024) << " MB" << std::endl;
    std::cout << "Thread Count: " << taskinfo.pti_threadnum << std::endl;
    
    return 0;
}
