#include <iostream>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <thread>
#include <unistd.h>
#include "json.hpp" // json handling
#include "mqtt/client.h" // paho mqtt
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <fstream>

#define QOS 1
#define BROKER_ADDRESS "tcp://localhost:1883"

double cpu_percentage(){
     std::ifstream stat_file("/proc/stat");
    std::string line;
    
    // Read the first line of /proc/stat to get the CPU usage information
    std::getline(stat_file, line);
    
    // Extract the CPU time values from the line
    unsigned long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    sscanf(line.c_str(), "cpu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
           &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest, &guest_nice);
    
    // Calculate the total CPU time
    unsigned long total_time = user + nice + system + idle + iowait + irq + softirq + steal;
    
    // Wait for 1 second
    sleep(1);
    
    // Read the /proc/stat file again to get the updated CPU time
    stat_file.seekg(0);
    std::getline(stat_file, line);
    
    // Extract the updated CPU time values
    unsigned long user_new, nice_new, system_new, idle_new, iowait_new, irq_new, softirq_new, steal_new, guest_new, guest_nice_new;
    sscanf(line.c_str(), "cpu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
           &user_new, &nice_new, &system_new, &idle_new, &iowait_new, &irq_new, &softirq_new, &steal_new, &guest_new, &guest_nice_new);
    
    // Calculate the elapsed CPU time
    unsigned long elapsed_time = user_new + nice_new + system_new + idle_new + iowait_new + irq_new + softirq_new + steal_new - total_time;
    
    // Calculate the CPU usage percentage
    double cpu_percentage = (double)elapsed_time / sysconf(_SC_CLK_TCK);
    return cpu_percentage;
}

double cpu_temperature(){
    std::ifstream temp_file("/proc/acpi/thermal_zone/THRM/temperature");
    if (!temp_file) {
        std::cerr << "Failed to open temperature file." << std::endl;
        return 1;
    }

    int temperature;
    temp_file >> temperature;

    // Convert millidegrees Celsius to degrees Celsius
    double celsius = static_cast<double>(temperature) / 1000;
    return celsius;
}

double memory_used(){
     std::ifstream meminfo_file("/proc/meminfo");
    if (!meminfo_file) {
        std::cerr << "Failed to open /proc/meminfo file." << std::endl;
        return -1;
    }

    std::string line;
    std::string mem_total_str;
    std::string mem_available_str;

    while (std::getline(meminfo_file, line)) {
        std::istringstream iss(line);
        std::string key;
        std::string value;
        iss >> key >> value;

        if (key == "MemTotal:") {
            mem_total_str = value;
        } else if (key == "MemAvailable:") {
            mem_available_str = value;
        }

        if (!mem_total_str.empty() && !mem_available_str.empty()) {
            break;
        }
    }

    if (mem_total_str.empty() || mem_available_str.empty()) {
        std::cerr << "Failed to retrieve memory information from /proc/meminfo." << std::endl;
        return -1;
    }

    double mem_total = std::stod(mem_total_str);
    double mem_available = std::stod(mem_available_str);

    double mem_usage_percentage = (mem_total - mem_available) / mem_total * 100.0;

    return mem_usage_percentage;
}

double io_performance(){
    // Command to measure I/O performance using dd
    const char* command = "dd if=/dev/zero of=tempfile bs=1M count=1024 conv=fdatasync";

    // Create a pipe to capture the output of the command
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        std::cerr << "Failed to open pipe for command execution." << std::endl;
        return -1;
    }

    // Read the output of the command
    const int max_buffer = 256;
    char buffer[max_buffer];
    std::string output;
    while (!feof(pipe)) {
        if (fgets(buffer, max_buffer, pipe) != nullptr)
            output += buffer;
    }
    pclose(pipe);

    // Parse the output to extract the relevant information
    const char* throughput_str = "copied, ";
    const char* speed_unit_str = " in ";
    const char* speed_end_str = "/s";
    std::size_t throughput_pos = output.find(throughput_str);
    std::size_t speed_unit_pos = output.find(speed_unit_str, throughput_pos + 1);
    std::size_t speed_end_pos = output.find(speed_end_str, speed_unit_pos + 1);

    if (throughput_pos == std::string::npos || speed_unit_pos == std::string::npos || speed_end_pos == std::string::npos) {
        std::cerr << "Failed to parse output for I/O performance measurement." << std::endl;
        return -1;
    }

    std::string throughput_substr = output.substr(throughput_pos + strlen(throughput_str), speed_unit_pos - throughput_pos - strlen(throughput_str));
    std::string speed_unit_substr = output.substr(speed_unit_pos + strlen(speed_unit_str), speed_end_pos - speed_unit_pos - strlen(speed_unit_str));

    double throughput = std::stod(throughput_substr);
    std::string speed_unit = speed_unit_substr;

    // Convert throughput to bytes/s if necessary
    if (speed_unit == "MB") {
        throughput *= 1024 * 1024;
    } else if (speed_unit == "KB") {
        throughput *= 1024;
    }

    return throughput;
}

int main(int argc, char* argv[]) {
    std::string clientId = "sensor-monitor";
    mqtt::client client(BROKER_ADDRESS, clientId);

    // Connect to the MQTT broker.
    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);

    try {
        client.connect(connOpts);
    } catch (mqtt::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    std::clog << "connected to the broker" << std::endl;

    // Get the unique machine identifier, in this case, the hostname.
    char hostname[1024];
    gethostname(hostname, 1024);
    std::string machineId(hostname);

    while (true) {
       // Get the current time in ISO 8601 format.
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm* now_tm = std::localtime(&now_c);
        std::stringstream ss;
        ss << std::put_time(now_tm, "%FT%TZ");
        std::string timestamp = ss.str();

       // double value = cpu_percentage();

        //int value = rand();
        double cpu_used = cpu_percentage(); // Monitors CPU usage percentage
        double mem_used = memory_used(); // Monitors de total memory used 
        double io_perf = io_performance(); // Monitors disk I/O performance
        double cpu_temp = cpu_temperature(); // Monitors CPU temperatrue;

        

        // Construct the JSON message.
        nlohmann::json j;


        // Publish the JSON message to the appropriate topic.
        std::string topic_cpuused = "/sensors/" + machineId + "/cpuused"; 
        mqtt::message msg_cpu(topic_cpuused, j.dump(), QOS, false);
        j["timestamp"] = timestamp;
        j["value"] = cpu_used;
        client.publish(msg_cpu);
        std::clog << "message published - topic: " << topic_cpuused << " - message: " << j.dump() << std::endl;        

        std::string topic_memused = "/sensors/" + machineId + "/memused";
        mqtt::message msg_mem(topic_memused, j.dump(), QOS, false);
        j["timestamp"] = timestamp;
        j["value"] = mem_used;
        client.publish(msg_mem);
        std::clog << "message published - topic: " << topic_memused << " - message: " << j.dump() << std::endl;
        
        std::string topic_ioperf = "/sensors/" + machineId + "/ioperf";
        mqtt::message msg_io(topic_ioperf, j.dump(), QOS, false);
        std::clog << "message published - topic: " << topic_ioperf << " - message: " << j.dump() << std::endl;
        j["timestamp"] = timestamp;
        j["value"] = io_perf;
        client.publish(msg_io);

        std::string topic_cputemp = "/sensors/" + machineId + "/cputemp";
        mqtt::message msg_temp(topic_cputemp, j.dump(), QOS, false);
        std::clog << "message published - topic: " << topic_cputemp << " - message: " << j.dump() << std::endl;
        j["timestamp"] = timestamp;
        j["value"] = cpu_temp;
        client.publish(msg_temp);

        // Sleep for some time.
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return EXIT_SUCCESS;
}
