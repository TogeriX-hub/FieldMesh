#pragma once

// FieldMesh: text rules for channel messages, shared by the companion and the FieldMesh
// repeater. Header-only without dependencies so the native unit tests can use it.

#include <ctype.h>
#include <string.h>

// Channel message text is "<sender name>: <message>".
// Sender = everything before the first ": " - never split on " | ", it can be part of a
// node name (e.g. "Juno48 | Region: Hello" -> "Juno48 | Region").
// Returns false (out_name untouched) when there is no ": ", the name is empty or it does
// not fit into max_len (incl. terminating zero).
static inline bool fmExtractSenderName(const char* text, char* out_name, int max_len) {
  if (text == NULL) return false;
  const char* colon = strstr(text, ": ");
  if (colon != NULL) {
    int len = (int)(colon - text);
    if (len > 0 && len < max_len) {
      strncpy(out_name, text, len);
      out_name[len] = 0;
      return true;
    }
  }
  return false;  // format not recognised
}

// SOS channel: the channel name contains "sos", case-insensitive ("SOS", "#sos", "TeamSOS").
static inline bool fmIsSosChannelName(const char* name) {
  if (name == NULL) return false;
  for (const char* p = name; p[0] && p[1] && p[2]; p++) {
    if (tolower(p[0]) == 's' && tolower(p[1]) == 'o' && tolower(p[2]) == 's') return true;
  }
  return false;
}
