#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

String https_get_string(String uri) {
  Serial.printf("https_get_string(%s)\n", uri.c_str());

  WiFiClientSecure *client = new WiFiClientSecure;
  String payload;
  if (client) {
    client->setInsecure();
    {
      HTTPClient https;
      Serial.print("[HTTPS] begin...\n");
      if (https.begin(*client, uri)) {
        https.addHeader("User-Agent", "ESP32/NEXRADCore");
        Serial.print("[HTTPS] GET...\n");
        int httpCode = https.GET();
        if (httpCode > 0) {
          Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
          if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            payload = https.getString();
          }
        } else {
          Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        }
        https.end();
      } else {
        Serial.printf("[HTTPS] Unable to connect\n");
      }
    }
    delete client;
  } else {
    Serial.println("Unable to create client");
  }
  return payload;
}

uint8_t *https_response_buf = nullptr;
int https_response_len      = 0;
int https_last_http_code    = 0;

void https_get_response_buf(String uri) {
  https_response_buf   = nullptr;
  https_response_len   = 0;
  https_last_http_code = 0;

  Serial.printf("https_request(%s)\n", uri.c_str());
  Serial.printf("Free heap: %d\n", ESP.getFreeHeap());

  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) {
    Serial.println("[HTTPS] Failed to create client");
    return;
  }

  client->setInsecure();

  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  https.addHeader("User-Agent", "ESP32/NEXRADCore");
  https.setTimeout(15000);

  if (!https.begin(*client, uri)) {
    Serial.println("[HTTPS] Unable to connect");
    delete client;
    return;
  }

  int httpCode = https.GET();
  https_last_http_code = httpCode;
  Serial.printf("[HTTPS] code: %d\n", httpCode);

  if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
    int contentLen = https.getSize();
    Serial.printf("[HTTPS] content-length: %d, heap: %d\n", contentLen, ESP.getFreeHeap());

    const int MAX_SIZE = 100 * 1024;
    int allocSize = (contentLen > 0 && contentLen <= MAX_SIZE) ? contentLen : MAX_SIZE;
    https_response_buf = (uint8_t *)malloc(allocSize);

    if (!https_response_buf) {
      Serial.printf("[HTTPS] malloc(%d) failed (heap=%d)\n", allocSize, ESP.getFreeHeap());
    } else {
      // Use getStreamPtr() so we always read from the active (post-redirect) connection.
      // Read in a loop until content-length satisfied, connection closed, or 20s deadline.
      WiFiClient *stream = https.getStreamPtr();
      stream->setTimeout(15000);  // 15 s idle timeout between chunks
      int pos       = 0;
      int remaining = contentLen; // -1 when unknown (chunked / no Content-Length)
      unsigned long deadline = millis() + 20000UL;

      while (pos < allocSize && millis() < deadline) {
        int avail = stream->available();
        if (avail > 0) {
          int toRead = avail;
          if (remaining > 0 && toRead > remaining) toRead = remaining;
          if (pos + toRead > allocSize)            toRead = allocSize - pos;
          int n = stream->readBytes(https_response_buf + pos, toRead);
          pos += n;
          if (remaining > 0) remaining -= n;
          if (remaining == 0) break;  // got everything promised by Content-Length
        } else {
          if (!https.connected()) break;  // server closed connection
          delay(1);
        }
      }

      Serial.printf("[HTTPS] Read %d bytes\n", pos);
      if (pos > 0) {
        https_response_len = pos;
        uint8_t *shrunk = (uint8_t *)realloc(https_response_buf, pos);
        if (shrunk) https_response_buf = shrunk;
      } else {
        free(https_response_buf);
        https_response_buf = nullptr;
      }
    }
  } else {
    Serial.printf("[HTTPS] error: %s\n", https.errorToString(httpCode).c_str());
  }

  https.end();
  delete client;
}
