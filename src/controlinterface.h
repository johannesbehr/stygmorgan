#ifndef CONTROLINTERFACE_H
#define CONTROLINTERFACE_H

#include "stygmorgan.h"

#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

struct Message {
    enum Type { RESPONSE, EVENT } type;
    std::string payload;
};

class MessageQueue {
public:
    void push(const Message& msg);
    Message pop();

private:
    std::queue<Message> q;
    std::mutex m;
    std::condition_variable cv;
};

class ControlInterface {
public:
    enum Mode {
        STDIO,
        FIFO
    };

    ControlInterface(RMGMO* engine);
    ~ControlInterface();

    // NEU
    bool init_pipe(Mode mode);

    void start();

private:
    RMGMO* rmgmo;

    int read_fd = -1;
    int write_fd = -1;
    Mode mode;
    

    MessageQueue queue;

    std::thread writer_thread;
    std::thread reader_thread;

    std::atomic<bool> running;

    std::mutex command_mutex;

    // intern
    void writer_loop();
    void reader_loop();

    void process_input(const std::string& line);

    // events
    void onBeatEvent(const BeatEvent& e);
    void onTransportStateEvent(const TransportStateEvent& e);

    // send
    void send_json(const std::string& json);
    void send_ok();
    void send_error(const std::string& msg);
};

#endif