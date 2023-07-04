#include <iostream>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <bson.h>
#include <mongoc.h>
#include "json.hpp"
#include "mqtt/client.h"

#define QOS 1
#define BROKER_ADDRESS "tcp://localhost:1883"
#define INACTIVITY_THRESHOLD 10.0
#define MEMORY_USAGE_THRESHOLD 90.0

void insert_document(mongoc_collection_t *collection, std::string machine_id, std::string timestamp_str, double value) {
    bson_error_t error;
    bson_oid_t oid;
    bson_t *doc;

    std::tm tm{};
    std::istringstream ss{timestamp_str};
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    std::time_t time_t_timestamp = std::mktime(&tm);

    doc = bson_new();
    BSON_APPEND_UTF8(doc, "machine_id", machine_id.c_str());
    BSON_APPEND_TIME_T(doc, "timestamp", time_t_timestamp);
    BSON_APPEND_DOUBLE(doc, "value", value);

    if (!mongoc_collection_insert_one(collection, doc, NULL, NULL, &error)) {
        std::cerr << "Failed to insert doc: " << error.message << std::endl;
    }

    bson_destroy(doc);
}

void insert_alarm(mongoc_collection_t *collection, std::string machine_id, std::string sensor_id, std::string description) {
    bson_error_t error;
    bson_t *doc = bson_new();

    BSON_APPEND_UTF8(doc, "machine_id", machine_id.c_str());
    BSON_APPEND_UTF8(doc, "sensor_id", sensor_id.c_str());
    BSON_APPEND_UTF8(doc, "description", description.c_str());

    if (!mongoc_collection_insert_one(collection, doc, NULL, NULL, &error)) {
        std::cerr << "Failed to insert alarm: " << error.message << std::endl;
    }

    bson_destroy(doc);
}

std::vector<std::string> split(const std::string &str, char delim) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delim)) {
        tokens.push_back(token);
    }
    return tokens;
}

int main(int argc, char *argv[]) {
    std::string clientId = "clientId";
    mqtt::client client(BROKER_ADDRESS, clientId);

    // Initialize MongoDB client and connect to the database.
    mongoc_init();
    mongoc_client_t *mongodb_client = mongoc_client_new("mongodb://localhost:27017");
    mongoc_database_t *database = mongoc_client_get_database(mongodb_client, "sensors_data");
    mongoc_collection_t *alarms_collection = mongoc_database_get_collection(database, "alarms");

    // Create an MQTT callback.
    class callback : public virtual mqtt::callback {
        mongoc_database_t *db;
        mongoc_collection_t *alarms_coll;

    public:
        callback(mongoc_database_t *db, mongoc_collection_t *alarms_coll)
            : db(db), alarms_coll(alarms_coll) {}

        void message_arrived(mqtt::const_message_ptr msg) override {
            auto j = nlohmann::json::parse(msg->get_payload());

            std::string topic = msg->get_topic();
            auto topic_parts = split(topic, '/');
            std::string machine_id = topic_parts[2];
            std::string sensor_id = topic_parts[3];

            std::string timestamp = j["timestamp"];
            double value = j["value"];
          std::clog << timestamp << " " << value << std::endl;
            // Get the current timestamp (in seconds)
            int current_timestamp = std::time(nullptr);

            // Update the last seen timestamp for the sensor
            last_seen_map[sensor_id] = current_timestamp;

            // Check for inactivity alarms
            if (inactivity_count_map.find(sensor_id) == inactivity_count_map.end()) {
                inactivity_count_map[sensor_id] = 0;
            } else {
                inactivity_count_map[sensor_id]++;
                if (inactivity_count_map[sensor_id] >= INACTIVITY_THRESHOLD) {
                    std::string alarm_description = "Sensor inativo por dez períodos de tempo previstos";
                    insert_alarm(alarms_coll, machine_id, sensor_id, alarm_description);
                    inactivity_count_map[sensor_id] = 0; // Reset the inactivity count
                }
            }

            // Check for high memory usage alarms
            if (sensor_id == "menused" && value > MEMORY_USAGE_THRESHOLD) {
                std::string alarm_description = "high memory usage";
                insert_alarm(alarms_coll, machine_id, sensor_id, alarm_description);
            }

            // Get collection and persist the document.
            mongoc_collection_t *collection = mongoc_database_get_collection(db, sensor_id.c_str());
            insert_document(collection, machine_id, timestamp, value);
            mongoc_collection_destroy(collection);
        }

    private:
        std::map<std::string, int> last_seen_map;
        std::map<std::string, int> inactivity_count_map;
    };

    callback cb(database, alarms_collection);
    client.set_callback(cb);

    // Connect to the MQTT broker.
    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);

    try {
        client.connect(connOpts);
        client.subscribe("/sensors/#", QOS);
    } catch (mqtt::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Cleanup resources
    mongoc_collection_destroy(alarms_collection);
    mongoc_database_destroy(database);
    mongoc_client_destroy(mongodb_client);
    mongoc_cleanup();

    return EXIT_SUCCESS;
}