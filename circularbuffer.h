#include <vector>
#include <iostream>
#include <deque>
#include <stdexcept>
using namespace std;

class CircularBuffer {
private:
    vector<float> buffer;   // Store float values
    deque<size_t> maxDeque; // Store indices of max values
    size_t head;
    size_t tail;
    size_t capacity;

public:
    CircularBuffer(size_t capacity) {
        this->capacity = capacity;
        this->head = 0;
        this->tail = 0;
        buffer.resize(capacity, 0.0f);  // Initialize buffer with 0.0f
    }

    void push_back(float element) {
        // Remove all elements smaller than the new one from the deque
        while (!maxDeque.empty() && buffer[maxDeque.back()] <= element) {
            maxDeque.pop_back();
        }

        // Insert the new element into the buffer
        buffer[head] = element;
        maxDeque.push_back(head); // Store index of new element

        head = (head + 1) % capacity;

        // If the buffer is full, remove the oldest value
        if (head == tail) {
            if (!maxDeque.empty() && maxDeque.front() == tail) {
                maxDeque.pop_front();
            }
            tail = (tail + 1) % capacity;
        }
    }

    float getMax() const {
        if (empty()) {
            // throw out_of_range("Buffer is empty");
            return 0.0f;
        }
        return buffer[maxDeque.front()];
    }

    bool empty() const { return head == tail; }

    size_t size() const {
        return (head >= tail) ? (head - tail) : (capacity - (tail - head));
    }

    void printBuffer() const {
        size_t idx = tail;
        while (idx != head) {
            cout << buffer[idx] << " ";
            idx = (idx + 1) % capacity;
        }
        cout << endl;
    }
};