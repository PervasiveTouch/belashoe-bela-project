#include <Bela.h>
#include <libraries/Trill/Trill.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <mutex>

#include "tsqueue.h"

#define NUM_CAP_CHANNELS 8
#define UDP_PORT 5700
#define RECEIVER_IP "192.168.178.179"

Trill touchSensor;
std::mutex gTouchSensorMutex;

// UDP socket variables
int sock;
struct sockaddr_in serverAddr;

// Time peroiods
unsigned int gTaskSleepTime = 1400; // (in usec) -> according to scan time at normal speed and 13 bits

// Queue for sensor readings
struct LogEntry
{
    unsigned int timestamp;
    float gSensorReading[NUM_CAP_CHANNELS] = {0.0};
};
TSQueue<LogEntry> gSensorQueue;
TSQueue<LogEntry> gBaselineQueue;

// Add an entry to a thread safe queue
void pushSensorsToQueue(TSQueue<LogEntry>& queue ,std::vector<float> input, unsigned int timestamp)
{
    LogEntry currentReading;
    currentReading.timestamp = timestamp;

    for (unsigned int i = 0; i < NUM_CAP_CHANNELS; i++)
        currentReading.gSensorReading[i] = input[i];

    queue.push(currentReading);
}

// Read raw data from sensor in specified intervals
void readFromSensor(void *)
{
    while (!Bela_stopRequested())
    {
        touchSensor.readI2C();
        usleep(gTaskSleepTime);
    }
}

// Read raw data from sensor in specified intervals
void readSensorBaseline(void *)
{
    while (!Bela_stopRequested())
    {
    	gTouchSensorMutex.lock();
		touchSensor.setMode(Trill::BASELINE);
        std::cout << touchSensor.getMode() << std::endl;
        touchSensor.readI2C();
        pushSensorsToQueue(gBaselineQueue, touchSensor.rawData, 0);
        touchSensor.setMode(Trill::RAW);
        gTouchSensorMutex.unlock();
        usleep(1000000);
    }
}

void generateMessage(std::string& message, const LogEntry& entry)
{
	for (unsigned int i = 0; i < NUM_CAP_CHANNELS; i++)
    {
        message += std::to_string(entry.gSensorReading[i]);
        if (i < NUM_CAP_CHANNELS - 1)
            message += ",";
    }
    message += "]}";
}

/* Function for sending all entries accumulated in the sensor queue to the UDP socket defined in setup().
 *	If all entries are sent, the function sleeps for a short time before retrying, if the queue is not empty.
 */
void sendSensorQueue(void *)
{
    while (!Bela_stopRequested())
    {
        while (!gSensorQueue.empty())
        {
            LogEntry entry = gSensorQueue.pop();
            std::string message = "{\"shoe_data\":[";
            message += std::to_string(entry.timestamp) + ",";
            generateMessage(message, entry);
            // std::cout << message << std::endl;
            sendto(sock, message.c_str(), message.size(), 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
        }
        while (!gBaselineQueue.empty())
        {
            LogEntry entry = gBaselineQueue.pop();
            std::string message = "{\"shoe_baseline\":[";
            generateMessage(message, entry);
            std::cout << message << std::endl;
            sendto(sock, message.c_str(), message.size(), 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
        }
        std::cout << "Sent the queue!" << std::endl;
        usleep(1000000);
    }
}

bool setup(BelaContext *context, void *userData)
{
    // Setup a Trill Craft on i2c bus 1, using the default address.
    if (touchSensor.setup(1, Trill::CRAFT) != 0)
    {
        fprintf(stderr, "Unable to initialise Trill Craft\n");
        return false;
    }
    touchSensor.printDetails();
    touchSensor.setMode(Trill::RAW);
    touchSensor.setScanSettings(2, 13); // 2 = normal update speed, 13 bit resolution -> 1400us scan time
    touchSensor.setPrescaler(3);
    
    usleep(1000);
    touchSensor.updateBaseline();

    // Start all auxiliary task loops
    Bela_runAuxiliaryTask(readFromSensor);
    Bela_runAuxiliaryTask(sendSensorQueue);
    Bela_runAuxiliaryTask(readSensorBaseline);

    std::cout << "audioSampleRate: " << context->audioSampleRate << ", audioFrames: " << context->audioFrames << std::endl;
    std::cout << "Logging rate: " << context->audioSampleRate/context->audioFrames << "hz" << std::endl;

    // Create UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        rt_printf("Error creating UDP socket\n");
        return false;
    }

    // Configure server address
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, RECEIVER_IP, &serverAddr.sin_addr);
    std::cout << "Using server address: " << RECEIVER_IP << ":" << UDP_PORT << std::endl;

    return true;
}

/* Render function that is called audioSampleRate/audioFrames times per second.
 *	Verify that  block size is set to 256 frames in order to achieve a calling frequency of ~172hz.
 */
void render(BelaContext *context, void *userData)
{
    static unsigned int time = 0;
    if (gTouchSensorMutex.try_lock())
    {
    	pushSensorsToQueue(gSensorQueue, touchSensor.rawData, time);
    	gTouchSensorMutex.unlock();
    }
    time += 1;
}

void cleanup(BelaContext *context, void *userData)
{
}