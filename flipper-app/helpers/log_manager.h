#pragma once

#include <furi_hal.h>
#include <stdint.h>
#include <stdbool.h>

// Append one received-message entry to the daily SD card CSV log.
// Path: /ext/apps_data/ghostmesh/log_YYYYMMDD.csv
// Columns: timestamp,node_id,message,lat,lon,rssi,snr  (matches tools/log_to_kml.py)
// dt must be the already-fetched RTC datetime for this message.
// has_pos/lat_i/lon_i: last-known GPS fix (deg*1e7); lat/lon left blank if !has_pos.
void log_rx_message(const char* sender, const char* text,
                    int16_t rssi, float snr, const DateTime* dt,
                    bool has_pos, int32_t lat_i, int32_t lon_i);
