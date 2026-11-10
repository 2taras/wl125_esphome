#include "washer_capture.h"

#ifdef USE_WASHER_CAPTURE

#include <algorithm>
#include <charconv>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include <esp_random.h>
#include <esp_timer.h>

namespace esphome::washer_uart {

WasherCapture::WasherCapture() {
  const uint32_t first = esp_random();
  const uint32_t second = esp_random();
  std::snprintf(this->boot_id_, sizeof(this->boot_id_), "%08" PRIx32 "%08" PRIx32, first, second);
}

void WasherCapture::record(FrameView frame, uint8_t channel) {
  const uint64_t ms = esp_timer_get_time() / 1000;
  const std::lock_guard<std::mutex> lock(this->mutex_);
  const uint64_t id = this->next_id_++;
  auto &record = this->records_[(id - 1) % CAPACITY];
  record.ms = ms;
  record.id = id;
  record.channel = channel;
  record.size = frame.size;
  std::memcpy(record.data, frame.data, frame.size);
}

bool WasherCapture::canHandle(AsyncWebServerRequest *request) const {
  char url[AsyncWebServerRequest::URL_BUF_SIZE];
  return request->method() == HTTP_GET && request->url_to(url) == "/api/uart";
}

void WasherCapture::handleRequest(AsyncWebServerRequest *request) {
  uint64_t after = 0;
  const auto *boot = request->getParam("boot_id");
  const auto *cursor = request->getParam("after");
  if (boot != nullptr && boot->value() == this->boot_id_ && cursor != nullptr) {
    const auto &value = cursor->value();
    const auto result = std::from_chars(value.data(), value.data() + value.size(), after);
    if (result.ec != std::errc() || result.ptr != value.data() + value.size()) {
      request->send(400, "text/plain", "Invalid frame cursor");
      return;
    }
  }

  uint64_t newest;
  {
    const std::lock_guard<std::mutex> lock(this->mutex_);
    newest = this->next_id_ - 1;
  }
  const uint64_t oldest = newest > CAPACITY ? newest - CAPACITY + 1 : 1;
  if (after > newest)
    after = 0;

  httpd_req_t *http = *request;
  httpd_resp_set_type(http, "application/json");
  httpd_resp_set_hdr(http, "Cache-Control", "no-store");
  auto &response = this->response_;
  size_t used = std::snprintf(response.data(), response.size(), "{\"boot_id\":\"%s\",\"baud\":9600,\"frames\":[",
                              this->boot_id_);
  constexpr size_t MAX_RECORD_JSON = 2 * MAX_FRAME_SIZE + 96;

  bool comma = false;
  for (uint64_t id = std::max(oldest, after + 1); id <= newest; id++) {
    Record record;
    {
      const std::lock_guard<std::mutex> lock(this->mutex_);
      record = this->records_[(id - 1) % CAPACITY];
    }
    if (record.id != id)
      continue;  // A slow client lost this record to the next turn of the ring.

    if (response.size() - used < MAX_RECORD_JSON) {
      if (httpd_resp_send_chunk(http, response.data(), used) != ESP_OK)
        return;
      used = 0;
    }
    const char digits[] = "0123456789abcdef";
    used += std::snprintf(response.data() + used, response.size() - used,
                          "%s{\"id\":%" PRIu64 ",\"ms\":%" PRIu64 ",\"ch\":%u,\"hex\":\"", comma ? "," : "",
                          record.id, record.ms, static_cast<unsigned>(record.channel));
    for (unsigned i = 0; i < record.size; i++) {
      response[used++] = digits[record.data[i] >> 4];
      response[used++] = digits[record.data[i] & 0x0F];
    }
    response[used++] = '"';
    response[used++] = '}';
    comma = true;
  }
  response[used++] = ']';
  response[used++] = '}';
  if (httpd_resp_send_chunk(http, response.data(), used) == ESP_OK)
    httpd_resp_send_chunk(http, nullptr, 0);
}

}  // namespace esphome::washer_uart

#endif
