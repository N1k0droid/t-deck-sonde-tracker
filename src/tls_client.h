// HTTPS client that never closes descriptor 0.
// arduino-esp32 2.0.9: stop_ssl_socket() clears the whole sslclient_context with memset, so after one stop()
// the socket is 0 instead of -1, and the next stop() calls close(0). That happens on every failed connection:
// WiFiClientSecure::connect() stops, http.end() does nothing (not connected), ~HTTPClient() stops again.
// Descriptor 0 is the first file open on the SD card (the console never opens stdin): on 2026-11-21 the
// uploader task closed the SD log under the main loop, which then crashed in FatFS (core dump).
// stop() is virtual (Client), so the library's own calls land here too.
#pragma once
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// One TLS session at a time across the tasks (uploads, internet reads, prediction server): each session
// takes some 40 KB of heap. Declare it before the client, so the client is gone before the lock is let go.
inline SemaphoreHandle_t tls_session_mutex()
{
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
}

// waitMs: how long to wait for the other session. The Wi-Fi task passes a short wait and skips its turn when
// busy, so a slow read of the internet task (up to 25 s) never stops Wi-Fi, scans and APRS.
class TlsSession
{
public:
    explicit TlsSession(bool take = true, uint32_t waitMs = portMAX_DELAY)
    {
        held = take && xSemaphoreTake(tls_session_mutex(), waitMs == portMAX_DELAY ? portMAX_DELAY
                                                                                     : pdMS_TO_TICKS(waitMs)) == pdTRUE;
        busy = take && !held;
    }
    ~TlsSession()
    {
        if (held) xSemaphoreGive(tls_session_mutex());
    }
    bool busy; // asked, but the other session did not end in time: do not connect

private:
    bool held;
};

class TlsClient : public WiFiClientSecure
{
public:
    void stop() override
    {
        WiFiClientSecure::stop();
        sslclient->socket = -1;
    }
};
