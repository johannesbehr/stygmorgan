#include "controlinterface.h"

#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h> 

using json = nlohmann::json;

//
// Queue
//

void MessageQueue::push(const Message& msg) {
    std::lock_guard<std::mutex> lock(m);
    q.push(msg);
    cv.notify_one();
}

Message MessageQueue::pop() {
    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&]{ return !q.empty(); });

    Message msg = q.front();
    q.pop();
    return msg;
}

//
// ControlInterface
//

ControlInterface::ControlInterface(RMGMO* engine)
    : rmgmo(engine), running(false)
{
    engine->events().sink<BeatEvent>()
        .connect<&ControlInterface::onBeatEvent>(this);

    engine->events().sink<TransportStateEvent>()
        .connect<&ControlInterface::onTransportStateEvent>(this);
}

ControlInterface::~ControlInterface() {
    running = false;

    queue.push({Message::EVENT, ""});

    if (reader_thread.joinable()) reader_thread.join();
    if (writer_thread.joinable()) writer_thread.join();
}

//
// INIT
//

bool ControlInterface::init_pipe(Mode mode)
{
   
   this->mode = mode;

    if (mode == STDIO) {
        read_fd  = STDIN_FILENO;
        write_fd = STDOUT_FILENO;
        return true;
    }

    if (mode == FIFO) {
        // Check if Pipes exist, if not create them
        if (access("/tmp/stygmorgan_in", F_OK) == -1) {
            if (mkfifo("/tmp/stygmorgan_in", 0666) != 0) {
                perror("mkfifo in");
                return false; 
            }
        }
        if (access("/tmp/stygmorgan_out", F_OK) == -1) {
            if (mkfifo("/tmp/stygmorgan_out", 0666) != 0) {           
                perror("mkfifo out");
                return false; 
            }
        }           
        return true;
    }

    return false;
}

//
// START
//

void ControlInterface::start() {
    running = true;

    reader_thread = std::thread(&ControlInterface::reader_loop, this);
    writer_thread = std::thread(&ControlInterface::writer_loop, this);
}

//
// THREADS
//

void ControlInterface::reader_loop() {
    /*char buffer[1024];
    std::string line;

    while (running) {
        ssize_t n = read(read_fd, buffer, sizeof(buffer)-1);
        if (n <= 0) continue;

        buffer[n] = '\0';
        line += buffer;

        size_t pos;
        while ((pos = line.find('\n')) != std::string::npos) {
            std::string cmd = line.substr(0, pos);
            line.erase(0, pos + 1);

            process_input(cmd);
        }
    }*/

    
    char buffer[1024];
    std::string line;

    if(mode == FIFO) {
        // Open the FIFO for reading

        std::cout << "ControlInterface: Opening FIFO for reading." << std::endl;

        read_fd = open("/tmp/stygmorgan_in", O_RDONLY);
        if (read_fd < 0) {
            perror("fifo open");
            return;
        }
    }

    struct pollfd pfd;
    pfd.fd = read_fd;
    pfd.events = POLLIN;

    std::cout << "ControlInterface: Reader thread started" << std::endl;

    while (running) {

        int ret = poll(&pfd, 1, 100); // 100ms timeout

        if (ret <= 0) continue;

        if (pfd.revents & POLLIN) {
            ssize_t n = read(read_fd, buffer, sizeof(buffer)-1);
            if (n <= 0) continue;

            buffer[n] = '\0';
            line += buffer;
            std::cout << "ControlInterface: Read data: " << buffer << std::endl; 
           
            size_t pos;
            while ((pos = line.find('\n')) != std::string::npos) {
                std::string cmd = line.substr(0, pos);
                line.erase(0, pos + 1);
                std::cout << "ControlInterface: Processing input: " << cmd << std::endl;    
                process_input(cmd);
            }
        }
    }

    std::cout << "ControlInterface: Reader thread exiting" << std::endl;

}

void ControlInterface::writer_loop() {

    if(mode == FIFO) {
        // Open the FIFO for writing
        std::cout << "ControlInterface: Opening FIFO for writing." << std::endl;
        write_fd = open("/tmp/stygmorgan_out", O_WRONLY);
        if (write_fd < 0) {
            perror("fifo open");
            return;
        }

        // Send initial message to indicate we're ready   
        std::string msg = "\n";
        write(write_fd, msg.c_str(), msg.size());
    }
   
    std::cout << "ControlInterface: Writer thread started" << std::endl;

    while (running) {
        Message msg = queue.pop();
        if (!running) break;

        std::cout << "Sendeing Message: " << msg.payload << std::endl;
        std::string out = msg.payload + "\n";
        write(write_fd, out.c_str(), out.size());
    }

    std::cout << "ControlInterface: Writer thread exiting" << std::endl;    
}

//
// SEND
//

void ControlInterface::send_json(const std::string& s) {
    queue.push({Message::RESPONSE, s});
}

void ControlInterface::send_ok() {
    send_json(R"({"status":"ok"})");
}

void ControlInterface::send_error(const std::string& msg) {
    json j;
    j["status"] = "error";
    j["msg"] = msg;
    send_json(j.dump());
}

//
// EVENTS
//

void ControlInterface::onBeatEvent(const BeatEvent& e) {
    json j;
    j["event"] = "beat";
    j["beat"] = e.beat;
    j["bar"] = e.bar;

    queue.push({Message::EVENT, j.dump()});
}

void ControlInterface::onTransportStateEvent(const TransportStateEvent& e) {
    json j;
    j["event"] = "transport";
    j["state"] = e.state;

    queue.push({Message::EVENT, j.dump()});
}

//
// COMMANDS
//

void ControlInterface::process_input(const std::string& line)
{
    std::lock_guard<std::mutex> lock(command_mutex);

    if (!json::accept(line)) {
        send_error("invalid json");
        return;
    }

    auto j = json::parse(line);

    std::string cmd = j.value("cmd", "");

    if (cmd == "ostart") {
        rmgmo->ostart();
        send_ok();
    }
    else if (cmd == "ostop") {
        rmgmo->ostop();
        send_ok();
    }
    else if (cmd == "tempo") {
        int bpm = j.value("value", 120);
        rmgmo->bpm = bpm;
        rmgmo->set_tempo();
        send_ok();
    }
    else if (cmd == "list_styles") {
        auto styles = rmgmo->get_styles();
        json j_out;
        j_out["styles"] = styles;

        send_json(j_out.dump());
    }else if (cmd == "ctoggle") {
        int channel = j.value("channel", -1);
        rmgmo->ctoggle(channel);
        send_ok();
    }
    else if (cmd == "select_style") {
        int id = j["id"];
        rmgmo->select_style(id+1);
        send_ok();
    }
    else {
        send_error("unknown command");
    }
}