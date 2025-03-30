#include <Bela.h>
#include <libraries/Trill/Trill.h>
#include <libraries/Gui/Gui.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>

#include "circularbuffer.h"
#include "tsqueue.h"

#define NUM_CAP_CHANNELS 8
#define BUFFER_SIZE 500
#define UDP_PORT 5701
#define RECEIVER_IP "192.168.178.179"

Trill touchSensor;
Gui gui;

std::ofstream file;

// UDP socket variables
int sock;
struct sockaddr_in serverAddr;

// Time peroiods
unsigned int gTaskSleepTime = 1400;	// (in usec) -> according to scan time at normal speed and 13 bits
float gTimePeriod = 0.1;			// (in seconds) after which data will be sent to the GUI

// Queue for sensor readings
struct LogEntry
{
	unsigned int timestamp;
    float gSensorReading[NUM_CAP_CHANNELS] = {0.0};
};
TSQueue<LogEntry> gSensorQueue;

// Initial calibration and circular buffer for calculating max value of each sensor over time
float initialCalibration[NUM_CAP_CHANNELS] = {0.04, 0.04, 0.05, 0.06, 0.07, 0.06, 0.05, 0.06};
CircularBuffer sensorBuffers[NUM_CAP_CHANNELS] = {
    CircularBuffer(BUFFER_SIZE), CircularBuffer(BUFFER_SIZE), CircularBuffer(BUFFER_SIZE), CircularBuffer(BUFFER_SIZE),
    CircularBuffer(BUFFER_SIZE), CircularBuffer(BUFFER_SIZE), CircularBuffer(BUFFER_SIZE), CircularBuffer(BUFFER_SIZE)};


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

/* Function for sending raw sensor data and updated max values for each sensor to gui. 
 *	Calling frequency is defined by the interval given in gTimePeriod (in seconds).
 */
void sendToGui(void *)
{
    while (!Bela_stopRequested())
    {
        // Send rawData to the GUI
        gui.sendBuffer(0, touchSensor.rawData); // Channel 0
        for (int i = 0; i < NUM_CAP_CHANNELS; i++)
        {
            sensorBuffers[i].push_back(touchSensor.rawData[i]);
        }
        // Get and send the max of each sensor
        float max_values[NUM_CAP_CHANNELS];
        for (int i = 0; i < NUM_CAP_CHANNELS; i++)
        {
            max_values[i] = sensorBuffers[i].getMax();
        }
        gui.sendBuffer(1, max_values); // Channel 1

        usleep(gTimePeriod * 1000000);
    }
}

/* Function for sending all entries accumulated in the sensor queue to the UDP socket defined in setup(). 
 *	If all entries are sent, the function sleeps for a short time before retrying, if the queue is not empty.
 */
void sendSensorQueue(void *)
{
	unsigned int counter = 0;
	while(!Bela_stopRequested())
	{
		while (!gSensorQueue.empty()) 
		{
			counter += 1;
	        LogEntry entry = gSensorQueue.pop();
	        std::string message = "{\"touch-sensors\":[";
	        message += to_string(entry.timestamp) + ",";
            for (unsigned int i = 0; i < NUM_CAP_CHANNELS; i++)
            {
	            message += std::to_string(entry.gSensorReading[i]);
	            if (i < NUM_CAP_CHANNELS - 1)
	                message += ",";
            }
	        message += "]}";
	        sendto(sock, message.c_str(), message.size(), 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
		}
		usleep(1000000);
		std::cout << "num popped from queue: " << counter << std::endl;
		counter = 0;
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
    touchSensor.setMode(Trill::DIFF);
    touchSensor.setScanSettings(2, 13); // 2 = normal update speed, 13 bit resolution -> 1400us scan time
    touchSensor.setPrescaler(3);

    gui.setup(context->projectName);

	// Start all auxiliary task loops
    Bela_runAuxiliaryTask(readFromSensor);
    Bela_runAuxiliaryTask(sendToGui);
    Bela_runAuxiliaryTask(sendSensorQueue);

    // push initial calibration to buffer
    for (int i = 0; i < NUM_CAP_CHANNELS; i++)
    {
        sensorBuffers[i].push_back(initialCalibration[i]);
    }
    
    std::cout << "audioSampleRate: " << context->audioSampleRate << ", audioFrames: " << context->audioFrames << std::endl;
    
    // Create UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
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
	// TODO: Timestamp mitschicken, aber nicht von int->string
	pushSensorsToQueue(touchSensor.rawData, time);
	time += 1;
}

void cleanup(BelaContext *context, void *userData)
{

}