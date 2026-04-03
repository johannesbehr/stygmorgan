#include "controlinterface.h"
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

ControlInterface::ControlInterface(RMGMO* engine)
    : rmgmo(engine)
{
    engine->events().sink<BeatEvent>()
    .connect<&ControlInterface::onBeatEvent>(this);
    engine->events().sink<TransportStateEvent>()
    .connect<&ControlInterface::onTransportStateEvent>(this);
}

void ControlInterface::onBeatEvent(const BeatEvent& e) {
   // if (e.sender == &tank1) 
   //std::cout << "Beatevent (" << e.beat <<" - " << e.bar  << " )\n";
   // Todo: pass Event to client
}

void ControlInterface::onTransportStateEvent(const TransportStateEvent& e) {
   // Todo: pass Event to client
}

void ControlInterface::send_ok()
{
    std::cout << R"({"status":"ok"})" << std::endl;
}

void ControlInterface::send_error(const std::string& msg)
{
    json j;
    j["status"] = "error";
    j["msg"] = msg;
    std::cout << j.dump() << std::endl;
}

void ControlInterface::process_input(const std::string& line)
{

    if (!json::accept(line)) {
        send_error("invalid json");
        return;
    }

    auto j = json::parse(line);

    if (!j.contains("cmd")) {
        send_error("missing cmd");
        return;
    }

    std::string cmd = j["cmd"];

    // --- START ---
    if (cmd == "ostart") {
        if (rmgmo) {
            if(!rmgmo->bplay){
                rmgmo->ostart();
                send_ok();
            } else {
                send_error("engine already playing");
            }
        } else {
            send_error("engine not available");
        }
    }

    // --- STOP ---
    else if (cmd == "ostop") {
        if (rmgmo) {
            if(rmgmo->bplay){
                rmgmo->ostop();
                send_ok();
            } else {
                send_error("engine not playing");
            }
        } else {
            send_error("engine not available");
        }
    }

    // --- TEMPO ---
    else if (cmd == "tempo") {
        if (!j.contains("value")) {
            send_error("missing value");
            return;
        }

        int bpm = j["value"];

        if (rmgmo) {
            rmgmo->bpm = bpm;
            rmgmo->set_tempo();
            send_ok();
        } else {
            send_error("engine not available");
        }
    }

    // --- LIST STYLES (Dummy erstmal) ---
    else if (cmd == "list_styles") {
    auto styles = rmgmo->get_styles();

    std::cout << "{ \"styles\": [";

    for (size_t i = 0; i < styles.size(); ++i) {
        std::cout << "\"" << styles[i] << "\"";
        if (i < styles.size() - 1) std::cout << ",";
    }

    std::cout << "] }" << std::endl;
    }

    else if (cmd == "select_style") {
        int id = j["id"];
        rmgmo->select_style(id);
        std::cout << "{ \"status\": \"ok\" }" << std::endl;
    }

    // --- UNKNOWN ---
    else {
        send_error("unknown command");
    }

}