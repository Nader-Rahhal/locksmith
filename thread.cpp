#include <iostream>
#include <thread>
#include <vector>
#include <random>
#include <chrono>
#include <unistd.h>

void worker_thread(int id, int sleep_seconds) {
    std::cout << "Thread " << id << " started, sleeping for " << sleep_seconds << " seconds" << std::endl;
    sleep(sleep_seconds);
    std::cout << "Thread " << id << " finished" << std::endl;
}

int main() {
    // Seed random number generator with current time
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Get process ID for identification
    pid_t pid = getpid();
    std::cout << "Test program PID: " << pid << std::endl;
    
    // Run in an infinite loop
    while (true) {
        // Generate a random number of threads between 1 and 20
        std::uniform_int_distribution<> thread_count_dist(1, 20);
        int num_threads = thread_count_dist(gen);
        
        std::cout << "Creating " << num_threads << " threads..." << std::endl;
        
        std::vector<std::thread> threads;
        
        // Create the threads
        for (int i = 0; i < num_threads; i++) {
            // Each thread will sleep for 3-10 seconds
            std::uniform_int_distribution<> sleep_dist(3, 10);
            int sleep_time = sleep_dist(gen);
            
            threads.push_back(std::thread(worker_thread, i, sleep_time));
        }
        
        // Wait for all threads to finish
        for (auto& t : threads) {
            t.join();
        }
        
        std::cout << "All threads finished. Waiting before starting next batch..." << std::endl;
        
        // Wait a few seconds before starting the next batch
        sleep(2);
    }
    
    return 0;
}
