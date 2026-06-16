#ifndef TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_RTAUDIOHELPER_H
#define TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_RTAUDIOHELPER_H

// --------------------------
#include <string>
#include <mutex>
#ifndef REPLAY_MODE
#include <rtaudio/RtAudio.h>
#include <alsa/asoundlib.h>
#else
// Stub types for replay mode (no ALSA dependency)
struct snd_ctl;
typedef snd_ctl snd_ctl_t;
#endif
// --------------------------
#include "Led.h"
#include "spdlog/spdlog.h"
#include "ConfigHelper.h"

using namespace std;

class AlsaHelper
{
 private:
    static snd_ctl_t *s_ctl;
    static int s_card_id;
    static std::mutex s_ctl_mutex;

 public:
    static void init_ctl(int card_id, ConfigHelper *config_helper);
    static void close_ctl(ConfigHelper *config_helper);
    static int set_led_value(int card_id, int control_id, int led_value, ConfigHelper *config_helper);
    static int bulk_led_value(int card_id, int control_ids[], int led_value, int num_controls, ConfigHelper *config_helper);
    static int get_traktor_device(ConfigHelper *config_helper);

};


#endif //TRAKTOR_KONTROL_S4_MK1_DRIVER_LINUX_RTAUDIOHELPER_H
