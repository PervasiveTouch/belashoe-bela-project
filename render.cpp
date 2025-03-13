#include <Bela.h>
#include <libraries/Trill/Trill.h>
#include <libraries/Gui/Gui.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>

#include "circularbuffer.h"

#define NUM_CAP_CHANNELS 8
#define BUFFER_SIZE 500
#define LOGGING_INTERVAL 5000 // in microseconds
#define LOGGING_FREQUENCY 200 // in hz

Trill touchSensor;
Gui gui;

std::ofstream file;

unsigned int gLogIntervalFrames = 0;

// Sleep time for auxiliary task in microseconds-> according to the scan time at normal speed and 13 bits
unsigned int gTaskSleepTime = 1400;
// Time period (in seconds) after which data will be sent to the GUI
float gTimePeriod = 0.1;

// Initial calibration and circular buffer for calculating max value of each sensor over time
float initialCalibration[8] = {0.04, 0.04, 0.05, 0.06, 0.07, 0.06, 0.05, 0.06};
CircularBuffer sensorBuffers[NUM_CAP_CHANNELS] = {
    CircularBuffer(BUFFER_SIZE), CircularBuffer(BUFFER_SIZE), CircularBuffer(BUFFER_SIZE), CircularBuffer(BUFFER_SIZE),
    CircularBuffer(BUFFER_SIZE), CircularBuffer(BUFFER_SIZE), CircularBuffer(BUFFER_SIZE), CircularBuffer(BUFFER_SIZE)};

// Buffer for sensor readings
struct LogEntry
{
    float gSensorReading[NUM_CAP_CHANNELS] = {0.0};
};
std::vector<LogEntry> dataBuffer;

void writeBufferToCSV()
{
    if (!file.is_open())
    {
        const std::string filename = "data.csv";
        file.open(filename, std::ios::app); // Open file in append mode
        if (!file.is_open())
        {
            std::cerr << "Unable to open file: " << filename << std::endl;
            return;
        }
    }

    for (const auto &entry : dataBuffer)
    {
        for (float value : entry.gSensorReading)
        {
            file << "," << value;
        }
        file << "\n";
    }

    file.close();
    
    std::cout << "Saved " << dataBuffer.size() << " entries" << std::endl;
}

void logTouchInputToBuffer(std::vector<float> input)
{
    LogEntry currentReading;

    for (unsigned int i = 0; i < NUM_CAP_CHANNELS; i++)
        currentReading.gSensorReading[i] = input[i];

    dataBuffer.push_back(currentReading);
}

void closeCSVFile()
{
    if (file.is_open())
    {
        file.close();
    }
}

void readFromSensor(void *)
{
    while (!Bela_stopRequested())
    {
        // Read raw data from sensor in specified intervals
        touchSensor.readI2C();
        usleep(gTaskSleepTime);
    }
}

void writeLog(void *)
{
    while (!Bela_stopRequested())
    {
    	// Write all accumulated values to buffer each second
        writeBufferToCSV();
        dataBuffer.clear();
        usleep(1000000);
    }
}

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
    Bela_runAuxiliaryTask(writeLog);
    Bela_runAuxiliaryTask(sendToGui);

    // push initial calibration to buffer
    for (int i = 0; i < NUM_CAP_CHANNELS; i++)
    {
        sensorBuffers[i].push_back(initialCalibration[i]);
    }

	// Calculate how many frames it takes to achieve the specified logging frequency
    gLogIntervalFrames = context->audioSampleRate / LOGGING_FREQUENCY;
    
    std::cout << "audioSampleRate: " << context->audioSampleRate << ", audioFrames: " << context->audioFrames << std::endl;

    return true;
}

void render(BelaContext *context, void *userData)
{
    static unsigned int count = 0;

	// Iterate over all frames in the render call
    for (unsigned int i = 0; i < context->audioFrames; i++)
    {
    	// If enough frames have passed, log the values
        if (count >= gLogIntervalFrames)
        {
            logTouchInputToBuffer(touchSensor.rawData);
            count = 0;
        }
        count++;
    }
}

void cleanup(BelaContext *context, void *userData)
{
    closeCSVFile();
}