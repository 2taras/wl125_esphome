#pragma once

#include "esphome/core/defines.h"

#ifdef USE_WASHER_CAPTURE

#include <array>
#include <mutex>

#include "esphome/components/web_server_base/web_server_base.h"
#include "washer_frame.h"

namespace esphome::washer_uart {

class WasherCapture : public AsyncWebHandler {
 public:
  WasherCapture();
  void record(FrameView frame, uint8_t channel);
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 private:
  static constexpr size_t CAPACITY = 256;

  struct Record {
    uint64_t ms;
    uint64_t id;
    uint8_t channel;
    uint8_t size;
    uint8_t data[MAX_FRAME_SIZE];
  };

  // The UART loop writes on the main task. The HTTP task copies a record under
  // the lock and releases it before writing to the socket.
  std::mutex mutex_;
  std::array<Record, CAPACITY> records_{};
  // Only the HTTP task uses this buffer. Batch small frame records into TCP-sized chunks.
  std::array<char, 1460> response_;
  uint64_t next_id_{1};
  char boot_id_[17];
};

}  // namespace esphome::washer_uart

#endif
