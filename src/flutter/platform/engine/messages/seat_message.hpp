#ifndef SEAT_MESSAGE_H
#define SEAT_MESSAGE_H

#include "flutter_embedder.h"

class Core;

void send_flutter_input_event(
    FlutterPointerPhase phase, FlutterPointerDeviceKind device_kind,
    FlutterPointerSignalKind signal_kind, double x = 0.0, double y = 0.0,
    int device = 0.0, size_t timestamp = 0.0, int buttons = 0.0,
    double scroll_delta_x = 0.0, double scroll_delta_y = 0.0, int view_id = 0.0,
    double pan_x = 0.0, double pan_y = 0.0, double rotation = 0.0);

void send_flutter_mouse_event(FlutterPointerPhase phase,
    FlutterPointerSignalKind signal_kind,
    double scroll_delta_x = 0.0,
    double scroll_delta_y = 0.0, size_t timestamp = 0,
    int view_id  = 0, double pan_x = 0.0,
    double pan_y = 0.0, double rotation = 0.0);

void send_flutter_trackpad_event(FlutterPointerPhase phase,
    size_t timestamp = 0, double pan_x   = 0.0,
    double pan_y     = 0.0, double scale = 1.0,
    double rotation  = 0.0, int view_id  = 0);

void send_gesture_swipe_begin(uint32_t fingers,
    uint32_t time_msec);

void send_gesture_swipe_update(double dx, double dy,
    uint32_t time_msec);

void send_gesture_swipe_end(bool cancelled, uint32_t time_msec);

#endif
