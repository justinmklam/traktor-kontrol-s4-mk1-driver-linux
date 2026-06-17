#include <iostream>
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <atomic>
#include "includes/MidiHelper.h"
#include "includes/EvDevHelper.h"
#include "includes/cxxopts.hpp"
#include "includes/UtilsHelper.h"
#include "includes/ConfigHelper.h"
#include "includes/AlsaHelper.h"
#include "includes/PerfCounters.h"
#include "includes/LedWriter.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/async.h"

using namespace std;

MidiHelper *rtmidi_helper;
EvDevHelper *evdev_helper;
ConfigHelper *config_helper;

void save_pid()
{
  ofstream pid_file;
  pid_file.open (config_helper->get_string_value("pid_file").c_str());
  auto pid = getpid();
  pid_file << pid;
  pid_file.close();
}

bool delete_pid_file()
{
  shared_ptr<spdlog::logger> logger = spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
  if(remove(config_helper->get_string_value("pid_file").c_str()) != 0){
    logger->error( "[main::delete_pid_file] Error removing PID file");
    return false;
  }
  logger->info( "[main::delete_pid_file] PID file removed");
  return true;
}

// §13: Atomic flag set by signal handler — poll() in evdev loop checks this via EINTR
std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int signum) {
    (void)signum;
    g_shutdown_requested.store(true, std::memory_order_relaxed);
}

void capture_signals() {
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;  // no SA_RESTART — let poll() return EINTR so the loop re-checks
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    // SIGKILL cannot be caught; SIGSEGV/SIGABRT should crash, not clean up
}

void cleanup_and_exit() {
    PerfCounters::report();
    EvDevHelper::shutdown_buttons_leds(::config_helper);
    AlsaHelper::close_ctl(::config_helper);
    delete_pid_file();
}

static void show_main_configuration() {
  shared_ptr<spdlog::logger> logger = spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));
  logger->set_level(config_helper->log_level);

  logger->info( "[main::init_application] Traktor Kontrol S4 Mk1 Driver for Linux started");

  logger->info("[main:show_main_configuration] Current arguments:");

  if (config_helper->log_level == spdlog::level::info) {
    logger->info("[main:show_main_configuration] Log level: INFO");
  } else {
    logger->info("[main:show_main_configuration] Log level: DEBUG");
  }

  if (config_helper->log_mode == LOG_FILE) {
    logger->info("[main:show_main_configuration] Log mode: LOG IN FILE");
  }
  else{
    logger->info("[main:show_main_configuration] Log mode: LOG IN CONSOLE");
  }
}

static void init_application() {
  shared_ptr<spdlog::logger> logger = spdlog::get(config_helper->get_string_value("traktor_s4_logger_name"));

  // §7g: Scan for the ALSA device once, share between helpers
  int traktor_device_id = AlsaHelper::get_traktor_device(config_helper);
  if (traktor_device_id == -1){
    logger->error("[main::init_application] Traktor Kontrol S4 Device not found.... Bye!");
    exit(EXIT_FAILURE);
  }

  // §3: Create the async LED writer (opens its own ALSA ctl handle)
  auto *led_writer = new LedWriter(traktor_device_id);

  logger->info("[main::init_application] Starting helpers....");
  evdev_helper = new EvDevHelper(config_helper, traktor_device_id, led_writer);
  rtmidi_helper = new MidiHelper(config_helper, traktor_device_id, led_writer);

  logger->info("[main::init_application] Get MIDI information....");
  MidiHelper::show_midi_information(rtmidi_helper, config_helper);

  logger->info("[main::init_application] Initializing leds....");
  evdev_helper->initialize_buttons_leds(config_helper);

  logger->info("[main::init_application] Reading events from Traktor Kontrol S4 Mk1....");
  evdev_helper->read_events_from_device(rtmidi_helper->pMidiOut);
}

int main(int argc, char **argv)
{
    capture_signals();
    config_helper = new ConfigHelper();

    cxxopts::Options options("TraktorKontrolS4DriverLinux", "Driver for Native Instruments Traktor Kontrol S4 Mk1");
    options.add_options()
        ("help", "Shows this help")
        ("logLevel", "Enable Log level: debug | info (default)", cxxopts::value<std::string>())
        ("logMode", "Enable Log model: console | logfile (default)", cxxopts::value<std::string>())
        ("configFile", "Set PATH for config.json path (default: ../config.json)", cxxopts::value<std::string>());

    try{
      auto result = options.parse(argc, argv);
      if (result.count("help"))
      {
        std::cout << options.help() << std::endl;
        exit(EXIT_SUCCESS);
      }

      string config_file_uri = "../config.json";
      if (result.count("configFile")){
        config_file_uri = result["configFile"].as<std::string>();
      }

      if (!config_helper->init_config(config_file_uri)) {
        cerr << "Error reading config file (config.json)" << endl;
        exit (EXIT_FAILURE);
      }

      save_pid();

      if (result.count("logLevel")){
        if(result["logLevel"].as<std::string>() == "debug"){
          config_helper->log_level = spdlog::level::debug;
        }
        else if(result["logLevel"].as<std::string>() == "info"){
          config_helper->log_level = spdlog::level::info;
        }
      }
      else{
        config_helper->log_level = spdlog::level::info;
      }

      spdlog::drop_all();

      if (result.count("logMode") && (result["logMode"].as<std::string>() == "console")){
        config_helper->log_mode = LOG_CONSOLE;
        auto console_logger = spdlog::stdout_color_mt(
            config_helper->get_string_value("traktor_s4_logger_name"));
      }
      else{
        config_helper->log_mode = LOG_FILE;
        auto file_logger = spdlog::basic_logger_mt<spdlog::async_factory>(
            config_helper->get_string_value("traktor_s4_logger_name"), config_helper->get_string_value("traktor_s4_log_file"));
      }


    }
    catch(cxxopts::exceptions::no_such_option error){
      std::cout << options.help() << std::endl;
      exit(EXIT_SUCCESS);
    }

    show_main_configuration();
    init_application();

    cleanup_and_exit();
    return 0;
}
