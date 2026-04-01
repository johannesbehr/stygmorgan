#ifndef CONTROLINTERFACE_H
#define CONTROLINTERFACE_H

#include "stygmorgan.h"

#include <string>


class RMGMO;  // forward declaration

class ControlInterface {
public:
    ControlInterface(RMGMO* engine);

    // verarbeitet eine JSON-Zeile
    void process_input(const std::string& line);

private:
    RMGMO* rmgmo;
    void onBeatEvent(const BeatEvent& e);

    void send_ok();
    void send_error(const std::string& msg);
};

#endif