#pragma once

// FieldMesh repeater additions (Station G2 Off-Grid box).
// Everything here is only active with -D FIELDMESH_REPEATER=1, so all other
// repeater builds keep the upstream behaviour.

#ifdef FIELDMESH_REPEATER

#include <Arduino.h>
#include <Identity.h>
#include <Mesh.h>
#include <helpers/ConfigSerializer.h>
#include "../companion_radio/OffGrid.h"   // shared Off-Grid parameters (EU/US chosen from the normal frequency)

#define FM_REPEATER_PREFS_FILE  "/fm_repeater.json"
#define FM_ROOM_IDENTITY_NAME   "fm_room"     // second identity (IdentityStore, next to "_main")

#ifndef FM_ROOM_MAX_POSTS
  #define FM_ROOM_MAX_POSTS     32            // ring buffer, same as upstream MAX_UNSYNCED_POSTS
#endif
#define FM_ROOM_POST_TEXT_LEN   (160-9)       // same as upstream MAX_POST_TEXT_LEN

struct FmRoomPost {
  mesh::Identity author;
  uint32_t post_timestamp;   // by OUR clock
  char text[FM_ROOM_POST_TEXT_LEN+1];
};

// Bot replies (!ping, !status) go only to the one who asked: own small queue,
// not stored on the board and not saved.
#define FM_ROOM_MAX_PRIVATE     8
struct FmRoomPrivate {
  mesh::Identity to;
  FmRoomPost post;
};

// Channels the box listens to (P4). A channel with "sos" in its name is an SOS channel.
#define FM_CHANNELS_FILE        "/fm_channels"
#define FM_MAX_CHANNELS         8
#define FM_CHANNEL_KEY_LEN      16                   // 128-bit channel key (hashtag and private channels)
struct FmChannel {
  char name[32];
  mesh::GroupChannel channel;   // secret (zero padded) + 1-byte hash
};

#define FM_MAX_SOS              8                    // last SOS heard (for 'get sos' and the dashboard)
struct FmSosEntry {
  char sender[32];
  char info[64];               // e.g. "48.7636,9.1621 bat78%"
  uint32_t heard_at;           // by OUR clock
  unsigned long heard_millis;  // 0 = empty
};

#define FM_ROOM_POSTS_FILE      "/fm_room_posts"     // board survives a reboot
#define FM_ROOM_CLIENTS_FILE    "/fm_room_clients"   // logged-in users survive a reboot
#define FM_ROOM_SAVE_DELAY      10000                // lazy write after the last change (ms)

// FieldMesh settings live in their own file, so the upstream /prefs.json stays untouched.
class FieldMeshRepeaterPrefs : public ConfigSerializer {
public:
  uint8_t offgrid = 0;          // 1 = Off-Grid radio params active
  float   normal_freq = 0;      // radio params saved before switching to Off-Grid
  float   normal_bw = 0;
  uint8_t normal_sf = 0;
  uint8_t normal_cr = 0;
  uint8_t room_enabled = 0;     // 1 = room server identity active
  char    room_name[32] = "";   // empty -> repeater name
  char    room_pass[16] = "";   // empty -> login without password (read + write)

protected:
  void structure() override {
    def("offgrid", offgrid);
    def("n_freq", normal_freq);
    def("n_bw", normal_bw);
    def("n_sf", normal_sf);
    def("n_cr", normal_cr);
    def("room_en", room_enabled);
    def("room_name", room_name, sizeof(room_name));
    def("room_pass", room_pass, sizeof(room_pass));
  }
};

// What the display/menu needs from the mesh, without knowing MyMesh.
class FieldMeshUIHost {
public:
  virtual bool fmIsOffGrid() const = 0;
  virtual void fmSetOffGrid(bool on, char* reply) = 0;   // reply: short status text (may be NULL)
  virtual bool fmIsRoomOn() const = 0;
  virtual const char* fmRoomName() const = 0;
  virtual void fmRoomCounts(int& clients, int& posts) = 0;
  virtual void fmSetRoom(bool on, const char* name, char* reply) = 0;   // name NULL -> last name / repeater name
  virtual void fmRoomReset() = 0;   // posts, users and password; name, key and on/off stay
};

#endif
