#include <Bela.h>
#include <libraries/Trill/Trill.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>

#include "tsqueue.h"

#define NUM_CAP_CHANNELS 8
#define UDP_PORT 5700
#define RECEIVER_IP "192.168.2.226"

Trill touchSensor;
uint32_t mask = 0b00000000000000000000000011111111; // enable channels 0–7

// UDP socket variables
int sock;
struct sockaddr_in serverAddr;

// Time peroiods
unsigned int gTaskSleepTime = 3000; // time for reading from touch sensor (in usec)

// Queue for sensor readings
struct LogEntry
{
    unsigned int timestamp;
    float gSensorReading[NUM_CAP_CHANNELS] = {0.0};
};
TSQueue<LogEntry> gSensorQueue;

// Add an entry to the thread safe queue
void pushSensorsToQueue(std::vector<float> input, unsigned int timestamp)
{
    LogEntry currentReading;
    currentReading.timestamp = timestamp;

    for (unsigned int i = 0; i < NUM_CAP_CHANNELS; i++)
        currentReading.gSensorReading[i] = input[i];

    gSensorQueue.push(currentReading);
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
            for (unsigned int i = 0; i < NUM_CAP_CHANNELS; i++)
            {
                message += std::to_string(entry.gSensorReading[i]);
                if (i < NUM_CAP_CHANNELS - 1)
                    message += ",";
            }
            message += "]}";
            sendto(sock, message.c_str(), message.size(), 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
        }
        usleep(100000);
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
    touchSensor.setMode(Trill::RAW);
    touchSensor.setScanSettings(1, 12); // 1 = fast update speed, 12 bit resolution -> 380us scan time
    touchSensor.setPrescaler(2);
    touchSensor.setChannelMask(mask);	// apply channel mask

    // Start all auxiliary task loops
    Bela_runAuxiliaryTask(readFromSensor);
    Bela_runAuxiliaryTask(sendSensorQueue);

	touchSensor.printDetails();
	std::cout << "Using " << touchSensor.getNumChannels() << " channels" << std::endl;
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
    
    // Configure UDP reciever for messages from python
    const int PORT = 5005;
    const int BUFFER_SIZE = 1024;
    char buffer[BUFFER_SIZE];

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        return 1;
    }

    sockaddr_in pythonAddr{};
    pythonAddr.sin_family = AF_INET;
    pythonAddr.sin_port = htons(PORT);
    pythonAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr*)&pythonAddr, sizeof(pythonAddr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return 1;
    }

    std::cout << "Waiting for UDP message on port " << PORT << "...\n";
    sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);

    ssize_t bytesReceived = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0,
                                     (struct sockaddr*)&clientAddr, &addrLen);
    if (bytesReceived < 0) {
        perror("recvfrom failed");
        close(sockfd);
        return 1;
    }

    buffer[bytesReceived] = '\0'; // null-terminate the string
    std::cout << "Received: " << buffer << std::endl;

    close(sockfd);

    return true;
}

/* Render function that is called audioSampleRate/audioFrames times per second.
 *	Verify that  block size is set to 256 frames in order to achieve a calling frequency of ~172hz.
 */
void render(BelaContext *context, void *userData)
{
    static unsigned int time = 0;
    pushSensorsToQueue(touchSensor.rawData, time);
    time += 1;
}

void cleanup(BelaContext *context, void *userData)
{
}