#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <map>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

using namespace std;

const int TOTAL_PROCESSES = 10;
const int TOTAL_CORES = 4;
//const int TOTAL_CORES = 32;

class Process {
public:
    int id;
    int print_count;
    int completed_count = 0;
    queue<string> print_commands;
    string finishedTime;

    Process(int id, int print_count) : id(id), print_count(print_count) {
        for (int i = 0; i < print_count; ++i) {
            print_commands.push("Hello world from process " + to_string(id) + "!");
        }
    }
};

mutex mtx;
condition_variable cv;
queue<Process*> process_queue;
map<int, Process*> running_processes;
vector<Process*> finished_processes;
bool done = false;

std::string format_time(const std::chrono::system_clock::time_point& tp) {
    std::time_t now_c = std::chrono::system_clock::to_time_t(tp);

    std::tm now_tm;

#ifdef _WIN32
    if (localtime_s(&now_tm, &now_c) != 0) {
        throw std::runtime_error("Failed to convert time using localtime_s");
    }
#else

    std::tm* local_tm_ptr = std::localtime(&now_c);
    if (!local_tm_ptr) {
        throw std::runtime_error("Failed to convert time using localtime");
    }
    now_tm = *local_tm_ptr;
#endif

    std::stringstream ss;
    ss << std::put_time(&now_tm, "%m/%d/%Y %H:%M:%S");
    return ss.str();
}

void print_status() {

    string input;

    while (finished_processes.size() < TOTAL_PROCESSES) {
        std::cout << "root:\> ";

        getline(cin, input);

        if (input != "screen -ls")
        {
            std::cout << "Invalid screen command" << endl;
            continue;
        }

        input = "";

        lock_guard<mutex> lock(mtx);

        std::cout << "---------------------------------------------" << endl;
        std::cout << "Running processes:" << endl;

        std::cout << "CPU utilization: " << "100%" << endl;
        std::cout << "Cores used: " << TOTAL_CORES << endl;
        std::cout << "Core available: " << "0" << endl;

        //std::cout << "Size: " << running_processes.size() << endl;

        for (const auto& entry : running_processes) {
            auto& process = entry.second;
            std::cout << "process" << setw(2) << setfill('0') << process->id << " (" << format_time(chrono::system_clock::now())
                << ") Core: " << entry.first << " " << process->completed_count << " / " << process->print_count << endl;
        }

        std::cout << endl << "Finished processes:" << endl;
        for (const auto& process : finished_processes) {
            std::cout << "process" << setw(2) << setfill('0') << process->id << " (" << process->finishedTime
                << ") Finished " << process->print_count << " / " << process->print_count << endl;
        }


        std::cout << "---------------------------------------------" << endl;

    }
}

void scheduler_thread(int total_processes) {
    for (int i = 0; i < total_processes; ++i) {
        Process* p = new Process(i, 100);
        {
            lock_guard<mutex> lock(mtx);
            process_queue.push(p);
        }
        cv.notify_one();
        this_thread::sleep_for(chrono::seconds(1));
    }
    {
        lock_guard<mutex> lock(mtx);
        done = true;
    }
    cv.notify_all();
}

void worker_thread(int core_id) {
    while (true) {
        Process* process = nullptr;
        {
            unique_lock<mutex> lock(mtx);
            cv.wait(lock, [] { return !process_queue.empty() || done; });
            if (!process_queue.empty()) {
                process = process_queue.front();
                process_queue.pop();
                running_processes[core_id] = process;
            }
        }
        if (process) {
            ofstream file("process_" + to_string(process->id) + ".txt", ios::app);
            while (!process->print_commands.empty()) {
                auto now = chrono::system_clock::now();
                file << "(" << format_time(now) << ") "
                    << "Core:" << core_id << " \"" << process->print_commands.front() << "\"" << "\n";
                process->print_commands.pop();
                process->completed_count++;
                this_thread::sleep_for(chrono::milliseconds(100));
            }

            process->finishedTime = format_time(chrono::system_clock::now());

            file.close();
            {
                lock_guard<mutex> lock(mtx);
                running_processes.erase(core_id);
                finished_processes.push_back(process);
            }
        }
        else if (done) {
            break;
        }
    }
}

int main() {

    string command;
    bool loop = true;

    while (loop) {
        std::cout << "Enter a command: ";
        getline(cin, command);

        if (command == "scheduler-test") {
            vector<thread> workers;
            for (int i = 0; i < TOTAL_CORES; ++i) {
                workers.push_back(thread(worker_thread, i));
            }

            thread scheduler(scheduler_thread, TOTAL_PROCESSES);
            thread printer(print_status);

            scheduler.join();
            for (auto& worker : workers) {
                worker.join();
            }

            {
                lock_guard<mutex> lock(mtx);
                done = true;
            }
            cv.notify_all();
            printer.join();

            std::cout << "All processes finished." << endl;
        }
        else if (command == "exit") {
            loop = false;
        }
        else {
            std::cout << "Invalid command..." << endl;
        }
    }

    return 0;
}