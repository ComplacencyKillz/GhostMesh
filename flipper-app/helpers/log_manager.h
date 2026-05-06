#pragma once

#include <furi_hal.h>
#include <stdint.h>

// Append one received-message entry to the daily SD card CSV log.
// Path: /ext/apps_data/ghostmesh/log_YYYYMMDD.csv
// Columns: timestamp,node_id,message,rssi,snr
// dt must be the already-fetched RTC datetime for this message.
void log_rx_message(const char* sender, const char* text,
                    int16_t rssi, float snr, const DateTime* dt);
