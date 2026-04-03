#include "controlinterface.h"

#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>

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

bool ControlInterface::init_pipe(Mode mode,
                                const std::string& in,
                                const std::string& out)
{
    if (mode == STDIO) {
        read_fd  = STDIN_FILENO;
        write_fd = STDOUT_FILENO;
        return true;
    }

    if (mode == FIFO) {
        read_fd = open(in.c_str(), O_RDONLY);
        write_fd = open(out.c_str(), O_WRONLY);

        if (read_fd < 0 || write_fd < 0) {
            perror("fifo open");
            return false;
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
    char buffer[1024];
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
    }
}

void ControlInterface::writer_loop() {
    while (running) {
        Message msg = queue.pop();
        if (!running) break;

        std::string out = msg.payload + "\n";
        write(write_fd, out.c_str(), out.size());
    }
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
    }
    else if (cmd == "select_style") {
        int id = j["id"];
        rmgmo->select_style(id);
        send_ok();
    }
    else {
        send_error("unknown command");
    }
}