#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <helpers/CommonCLI.h>
#include "FieldMeshRepeater.h"   // FieldMesh menu (only active with FIELDMESH_REPEATER)

class UITask {
  mesh::MainBoard* _board;
  DisplayDriver* _display;
  unsigned long _next_read, _next_refresh, _auto_off;
  int _prevBtnState;
  NodePrefs* _node_prefs;
  char _version_info[32];
  unsigned long _powering_off_at = 0;
  unsigned long _started_at = 0;

  void renderCurrScreen();
#ifdef FIELDMESH_REPEATER
  FieldMeshUIHost* _fm_host = NULL;
  uint8_t _fm_page = 0;
  char _fm_msg[24];
  unsigned long _fm_msg_until = 0;
  unsigned long _fm_confirm_until = 0;   // second long press confirms a reset

  void fmRenderPage();
  void fmDrawHeader(const char* title);
  void fmOnClick();
  void fmOnLongPress();
#endif
public:
  UITask(mesh::MainBoard& board, DisplayDriver& display) : _board(&board), _display(&display) { _next_read = _next_refresh = 0; }
  void begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version);
#ifdef FIELDMESH_REPEATER
  void setFieldMeshHost(FieldMeshUIHost* host) { _fm_host = host; }
#endif

  void loop();
};