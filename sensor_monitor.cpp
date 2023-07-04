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

// Function to calculate CPU percentage
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

// Function to publish CPU usage to MQTT broker
void publishToMqttCPU(mqtt::client& client, nlohmann::json j, std::string machineId, int freq) {
 while(true){
  // Get the current time in ISO 8601 format.
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm* now_tm = std::localtime(&now_c);
        std::stringstream ss;
        ss << std::put_time(now_tm, "%FT%TZ");
        std::string timestamp = ss.str();

        double cpu_used = cpu_percentage(); // Monitors CPU usage percentage


        // Publish the JSON message to the appropriate topic.
        std::string topic_cpuused = "/sensors/" + machineId + "/cpuused"; 
        mqtt::message msg_cpu(topic_cpuused, j.dump(), QOS, false);
        j["timestamp"] = timestamp;
        j["value"] = cpu_used;
        client.publish(msg_cpu);
        std::clog << "message published - topic: " << topic_cpuused << " - message: " << j.dump() << std::endl;    
        std::this_thread::sleep_for(std::chrono::seconds(freq-1));      
 }
}

// Function to calculate memory usage percentage
double memory_used(){
     std::ifstream meminfo_file("/proc/meminfo");
    if (!meminfo_file) {
        std::cerr << "Failed to open /proc/meminfo file." << std::endl;
        return -1;
    }

    std::string line;
    std::string mem_total_str;
    std::string mem_available_str;

    // Read each line of the /proc/meminfo file
    while (std::getline(meminfo_file, line)) {
        std::istringstream iss(line);
        std::string key;
        std::string value;
        iss >> key >> value;

        // Check if the line contains "MemTotal:" or "MemAvailable:"
        // If it does, store the corresponding value in the respective variables
        if (key == "MemTotal:") {
            mem_total_str = value;
        } else if (key == "MemAvailable:") {
            mem_available_str = value;
        }

        // If both mem_total_str and mem_available_str are not empty,
        // it means we have retrieved the required memory information
        // Break out of the loop to stop reading further lines
        if (!mem_total_str.empty() && !mem_available_str.empty()) {
            break;
        }
    }

    if (mem_total_str.empty() || mem_available_str.empty()) {
        std::cerr << "Failed to retrieve memory information from /proc/meminfo." << std::endl;
        return -1;
    }

    // Convert the retrieved memory values from string to double
    double mem_total = std::stod(mem_total_str);
    double mem_available = std::stod(mem_available_str);

    // Calculate the memory usage percentage by subtracting available memory from total memory,
    // dividing by total memory, and multiplying by 100
    double mem_usage_percentage = (mem_total - mem_available) / mem_total * 100.0;

    return mem_usage_percentage;
}

// Function to publish memory usage to MQTT broker
void publishToMqttMEM(mqtt::client& client, nlohmann::json j, std::string machineId, int freq) {
 while(true){
  // Get the current time in ISO 8601 format.
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm* now_tm = std::localtime(&now_c);
        std::stringstream ss;
        ss << std::put_time(now_tm, "%FT%TZ");
        std::string timestamp = ss.str();

        double mem_used = memory_used(); // Monitors CPU usage percentage


        std::string topic_memused = "/sensors/" + machineId + "/memused";
        mqtt::message msg_mem(topic_memused, j.dump(), QOS, false);
        j["timestamp"] = timestamp;
        j["value"] = mem_used;
        client.publish(msg_mem);
        std::clog << "message published - topic: " << topic_memused << " - message: " << j.dump() << std::endl;
        

        std::this_thread::sleep_for(std::chrono::seconds(freq));    
 }
}

// Function to create the JSON payload
nlohmann::json createJsonPayload(const std::string& machineId, const std::vector<nlohmann::json>& sensors) {
    nlohmann::json payload;
    payload["machine_id"] = machineId;
    payload["sensors"] = sensors;
    return payload;
}

// Function to publish sensor monitors to MQTT broker
void publishToMqttINMSG(mqtt::client& client, nlohmann::json j, std::string machineId, int freqcpu, int freqmem, int freq) {
 while(true){
        std::vector<nlohmann::json> sensors;
        std::string sensor_monitors = "/sensor_monitors";
        mqtt::message msg_mem(sensor_monitors, j.dump(), QOS, false);
        j["machine_id"] = machineId;
        nlohmann::json sensor1, sensor2;
        sensor1["sensor_id"] = {"memused"};
        sensor1["data_type"] = {"double"};
        sensor1["data_interval"] = {std::to_string(freqmem)};
        sensors.push_back(sensor1);  
        
        sensor2["sensor_id"] = {"cpuused"};
        sensor2["data_type"] = {"double"};
        sensor2["data_interval"] = {std::to_string(freqcpu)};
        sensors.push_back(sensor2);

        nlohmann::json payload = createJsonPayload(machineId, sensors);
        client.publish(msg_mem);
        std::clog << "message published - topic: " << sensor_monitors << " - message: " << payload.dump() << std::endl;
        
        std::this_thread::sleep_for(std::chrono::seconds(freq));    
 }
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
        // Construct the JSON message.
        nlohmann::json j;
        int cpufreq, memfreq, msgfreq;
    std::clog << "Enter the CPU reading frequency (s): ";
    std::cin >> cpufreq;
    std::clog << std::endl;
    std::clog << "Enter the MEMORY reading frequency (s): ";
    std::cin >> memfreq;
    std::clog << std::endl;
    std::clog << "Enter the sensors message frequency (s): ";
    std::cin >> msgfreq;
    std::clog << std::endl;

    // Start the status message thread
    std::thread inMessafe(publishToMqttINMSG, std::ref(client), j, machineId, cpufreq, memfreq, msgfreq);

    // Start the CPU thread
    std::thread cpuThread(publishToMqttCPU, std::ref(client), j, machineId, cpufreq); 

    // Start the memory thread
    std::thread memoryThread(publishToMqttMEM, std::ref(client), j, machineId, memfreq); 

    inMessafe.join();
    cpuThread.join();
    memoryThread.join();

    return EXIT_SUCCESS;
}
