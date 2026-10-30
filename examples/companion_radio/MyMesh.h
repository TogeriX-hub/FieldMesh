#pragma once

#include <Arduino.h>
#include <Mesh.h>

/*------------ Frame Protocol --------------*/
#define FIRMWARE_VER_CODE 14

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE "15 Oct 2026"
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "v1.18.0"
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
#include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
#include <LittleFS.h>
#elif defined(ESP32)
#include <SPIFFS.h>
#endif

#include "DataStore.h"
#include "NodePrefs.h"
#include "GpsCycle.h"   // FieldMesh v6.1
#include "TrackScheduler.h"   // FieldMesh v6.1
#include "FloodLog.h"   // FieldMesh v6.2 diagnostics (only with -D FM_FLOOD_LOG=1)

#include <RTClib.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <target.h>

/* ---------------------------------- CONFIGURATION ------------------------------------- */

#ifndef LORA_FREQ
#define LORA_FREQ 915.0
#endif
#ifndef LORA_BW
#define LORA_BW 250
#endif
#ifndef LORA_SF
#define LORA_SF 10
#endif
#ifndef LORA_CR
#define LORA_CR 5
#endif
#ifndef LORA_TX_POWER
#define LORA_TX_POWER 20
#endif
#ifndef MAX_LORA_TX_POWER
#define MAX_LORA_TX_POWER LORA_TX_POWER
#endif

#ifndef MAX_CONTACTS
#define MAX_CONTACTS 100
#endif

#ifndef OFFLINE_QUEUE_SIZE
#define OFFLINE_QUEUE_SIZE 16
#endif

#ifndef BLE_NAME_PREFIX
#define BLE_NAME_PREFIX "MeshCore-"
#endif

#include <helpers/BaseChatMesh.h>
#include <helpers/TransportKeyStore.h>

/* -------------------------------------------------------------------------------------- */

#define REQ_TYPE_GET_STATUS             0x01 // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE             0x02
#define REQ_TYPE_GET_TELEMETRY_DATA     0x03

// Copied from simple_repeater
#define CTL_TYPE_NODE_DISCOVER_REQ      0x80
#define CTL_TYPE_NODE_DISCOVER_RESP     0x90

struct AdvertPath {
  uint8_t pubkey_prefix[7];
  uint8_t path_len;
  char    name[32];
  uint32_t recv_timestamp;
  uint8_t path[MAX_PATH_SIZE];
  bool has_gps;                // true if at least one advert with GPS was received
  uint32_t gps_timestamp;      // timestamp of last GPS advert — used for TRACKING page
};

class MyMesh : public BaseChatMesh, public DataStoreHost {
public:
  class Listener {
    public:
      virtual void onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) = 0;
      virtual void onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) = 0;
      virtual void onQueueSizeChanged(int msgcount) = 0;
      virtual void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) { }
      virtual void onControlDataRecv(const mesh::Packet* pkt) { }
      virtual void onChannelDataRecv(mesh::Packet *pkt, const mesh::GroupChannel &channel, uint16_t data_type,
                                     const uint8_t *data, size_t data_len) { }
      virtual void onACKRecv(uint32_t ack_crc) { }
      virtual uint8_t onUnhandledRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                                         uint8_t len, uint8_t *reply) { return 0; /* unknown request type */ }
      virtual bool onUnhandledResponse(const ContactInfo &from, uint32_t tag, const uint8_t* data, uint8_t len) { return false; /* not handled */ }
      virtual void onTraceRecv(mesh::Packet *pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                               const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) { }
      virtual void onRawDataRecv(mesh::Packet *pkt) { }
      // FieldMesh (#23): RTC jumped by delta_secs (app time sync, GPS) — shift stored timestamps
      virtual void onClockAdjusted(int32_t delta_secs) { }
      virtual ~Listener() { }
  };

  MyMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables, DataStore& store);

  void begin();
  void startInterface(BaseSerialInterface &serial);
  void setListener(Listener* listener) { _listener = listener; }

  const char *getNodeName();
  NodePrefs *getNodePrefs();
  uint32_t getBLEPin();
  void setBLEPin(uint32_t active_pin);

  void loop();
  void handleCmdFrame(size_t len);
  bool advert();
  void enterCLIRescue();

  int  getRecentlyHeard(AdvertPath dest[], int max_num);

protected:
  float getAirtimeBudgetFactor() const override;
  int getInterferenceThreshold() const override;
  bool getCADEnabled() const override;
  int getAGCResetInterval() const override {
    return ((int)_prefs.agc_reset_interval) * 4000;   // milliseconds
  }
  int calcRxDelay(float score, uint32_t air_time) const override;
  uint32_t getRetransmitDelay(const mesh::Packet *packet) override;
  uint32_t getDirectRetransmitDelay(const mesh::Packet *packet) override;
  uint8_t getExtraAckTransmitCount() const override;
  bool filterRecvFloodPacket(mesh::Packet* packet) override;
  bool allowPacketForward(const mesh::Packet* packet) override;

  void sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis);
  void sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis=0) override;
  void sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis=0) override;

  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;
  bool isAutoAddEnabled() const override;
  bool shouldAutoAddContactType(uint8_t type) const override;
  bool shouldOverwriteWhenFull() const override;
  uint8_t getAutoAddMaxHops() const override;
  void onContactsFull() override;
  void onContactOverwrite(const uint8_t* pub_key) override;
  bool onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onContactPathUpdated(const ContactInfo &contact) override;
  ContactInfo* processAck(const uint8_t *data) override;
  void queueMessage(const ContactInfo &from, uint8_t txt_type, mesh::Packet *pkt, uint32_t sender_timestamp,
                    const uint8_t *extra, int extra_len, const char *text);

  void onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                     const char *text) override;
  void onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                         const char *text) override;
  void onCLICommandRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                         const char *text, char* reply) override;
  void onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                           const uint8_t *sender_prefix, const char *text) override;
  void onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                            const char *text) override;
  void onChannelDataRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint16_t data_type,
                         const uint8_t *data, size_t data_len) override;

  uint8_t onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                           uint8_t len, uint8_t *reply) override;
  void onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) override;
  void onControlDataRecv(mesh::Packet *packet) override;
  void onRawDataRecv(mesh::Packet *packet) override;
  void onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                   const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) override;

  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override;
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override;
  void onSendTimeout() override;

  // DataStoreHost methods
  bool onContactLoaded(const ContactInfo& contact) override { return addContact(contact); }
  bool getContactForSave(uint32_t idx, ContactInfo& contact) override { return getContactByIdx(idx, contact); }
  bool onChannelLoaded(uint8_t channel_idx, const ChannelDetails& ch) override { return setChannel(channel_idx, ch); }
  bool getChannelForSave(uint8_t channel_idx, ChannelDetails& ch) override { return getChannel(channel_idx, ch); }

  void clearPendingReqs() {
    pending_login = pending_status = pending_telemetry = pending_discovery = pending_req = 0;
  }

public:
  void savePrefs() {
    _prefs.node_lat = sensors.node_lat;
    _prefs.node_lon = sensors.node_lon;
    _store->savePrefs(_prefs);
    _prefs.clearDirty();
  }

#if ENV_INCLUDE_GPS == 1
  void applyGpsPrefs() {
    sensors.setSettingValue("gps", _prefs.gps_enabled ? "1" : "0");
    if (_prefs.gps_interval > 0) {
      char interval_str[12];  // Max: 24 hours = 86400 seconds (5 digits + null)
      sprintf(interval_str, "%u", _prefs.gps_interval);
      sensors.setSettingValue("gps_interval", interval_str);
    }
  }
#endif

  // To check if there is pending work
  bool hasPendingWork() const;

private:
  void writeOKFrame();
  void writeErrFrame(uint8_t err_code);
  void writeDisabledFrame();
  void writeContactRespFrame(uint8_t code, const ContactInfo &contact);
  void updateContactFromFrame(ContactInfo &contact, uint32_t& last_mod, const uint8_t *frame, int len);
  void addToOfflineQueue(const uint8_t frame[], int len);
  int getFromOfflineQueue(uint8_t frame[]);
  int getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) override { 
    return _store->getBlobByKey(key, key_len, dest_buf);
  }
  bool putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], int len) override {
    return _store->putBlobByKey(key, key_len, src_buf, len);
  }

  void checkCLIRescueCmd();
  bool handleCommand(const char* text, uint32_t sender_timestamp, char* reply);
  void checkSerialInterface();
  bool isValidClientRepeatFreq(uint32_t f) const;

  // helpers, short-cuts
  void saveChannels() { _store->saveChannels(this); }
  void saveContacts();

  DataStore* _store;
  NodePrefs _prefs;
  uint32_t pending_login;
  uint32_t pending_status;
  uint32_t pending_telemetry, pending_discovery;   // pending _TELEMETRY_REQ
  uint32_t pending_req;   // pending _BINARY_REQ
  BaseSerialInterface *_serial;
  Listener* _listener;

  ContactsIterator _iter;
  uint32_t _iter_filter_since;
  uint32_t _most_recent_lastmod;
  uint32_t _active_ble_pin;
  bool _iter_started;
  bool _cli_rescue;
  bool send_unscoped;   // force un-scoped flood (instead of using send_scope)
  char cli_command[80];
  char reply_buf[166];
  uint8_t app_target_ver;
  uint8_t *sign_data;
  uint32_t sign_data_len;
  unsigned long dirty_contacts_expiry;

  TransportKey send_scope;

  uint8_t cmd_frame[MAX_FRAME_SIZE + 1];
  uint8_t out_frame[MAX_FRAME_SIZE + 1];
  CayenneLPP telemetry;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];

    bool isChannelMsg() const;
  };
  int offline_queue_len;
  Frame offline_queue[OFFLINE_QUEUE_SIZE];

  struct AckTableEntry {
    unsigned long msg_sent;
    uint32_t ack;
    ContactInfo* contact;
  };
  #define EXPECTED_ACK_TABLE_SIZE 8
  AckTableEntry expected_ack_table[EXPECTED_ACK_TABLE_SIZE]; // circular table
  int next_ack_idx;

  #define ADVERT_PATH_TABLE_SIZE   16
  AdvertPath advert_paths[ADVERT_PATH_TABLE_SIZE]; // circular table

  // ---- FieldMesh ------------------------------------------------------------------------
  // All FieldMesh members of MyMesh are in this block. The methods are implemented in
  // MyMeshFieldMesh.cpp; MyMesh.cpp only calls the fm*() hooks at a few places
  // (list: UPSTREAM_SYNC.md). Keeps upstream syncs simple.
  bool _app_initialized = false;        // true after CMD_APP_START handshake completed
  unsigned long next_auto_advert = 0;   // millis() timestamp for next auto-advert (0=inactive)
  GpsCycle _gps_cycle;             // FieldMesh v6.1: GPS duty cycle + fresh own position
  TrackScheduler _track;           // FieldMesh v6.1: advert cadence while tracking
  bool _tracking_active = false;

  // FieldMesh (#23): RTC jump detection — reference point (RTC seconds + millis) of the last check
  uint32_t _clk_ref_rtc = 0;
  unsigned long _clk_ref_ms = 0;        // 0 = not anchored yet (first loop() sets the reference)
  void checkClockJump();

  // Hooks called from MyMesh.cpp
  void fmBegin();                              // begin(): restore the auto-advert timer
  void fmLoop();                               // loop(): GPS cycle, tracking, auto-advert, clock jump
  void fmUpdateAutoAdvertTimer();              // advert_loc_policy changed (app)
  void fmNoteAdvertGps(AdvertPath* p);         // onDiscoveredContact(): GPS info of the advert
  mesh::Packet* fmCreateSelfAdvert();          // advert(): position only with a fresh fix
  void fmSendSelfAdvert(mesh::Packet* pkt);    // advert(): flood in Off-Grid, zero-hop otherwise
  uint32_t fmRelayDelay(const mesh::Packet* packet);   // getRetransmitDelay() in Off-Grid (FloodPolicy.h)
  uint32_t fmFloodTimeoutExtra() const;                // calcFloodTimeoutMillisFor(): extra time in Off-Grid
  void fmFilterRelay(mesh::Packet* packet);            // filterRecvFloodPacket() in Off-Grid: drop duplicate advert relay
  void fmOnNetworkChange();                            // Off-Grid toggled / new radio params: drop SOS resend + queued relays
  uint32_t _fm_relay_skipped = 0;                      // queued advert relays dropped because another node relayed first
#ifdef FM_FLOOD_LOG
  FloodLog _fm_flog;                                   // FloodLog.h, diagnostic builds only
  void logRx(mesh::Packet* packet, int len, float score) override;
  void logTx(mesh::Packet* packet, int len) override;
  void logTxFail(mesh::Packet* packet, int len) override;
#endif

  // v6.2 Off-Grid managed flooding: wait for a free channel instead of forcing after 4 s
  uint32_t getCADFailMaxDuration() const override;

public:
  GpsCycle& gpsCycle() { return _gps_cycle; }   // FieldMesh v6.1 (UI: keep-awake, own position)
  uint32_t getRelaySkipped() const { return _fm_relay_skipped; }   // FieldMesh v6.2: Off-Grid flooding counter

  // Auto-advert is active when GPS sharing is enabled
  bool isAutoAdvertEnabled() const {
    return _prefs.advert_loc_policy == ADVERT_LOC_SHARE;
  }

  // Enable/disable GPS sharing and auto-advert together (called from UI button)
  void setGPSAdvertEnabled(bool enabled);

  // Off-Grid Mode: Radio-Parameter sichern/wiederherstellen + client_repeat setzen
  bool isOffGridActive() const { return _prefs.isRepeatEn(); }
  void toggleOffGrid();

  // V3: SOS-Nachricht in den SOS-Channel senden
  bool sendSOS();
  // v6.1: SOS channel = name contains "sos" (case-insensitive, e.g. "SOS", "#sos").
  // Used for sending and for the alarm: "!SOS" only raises the alarm in such a channel.
  static bool isSosChannel(const char* name);

  // V5: send channel message from UI (wrapper for sendGroupMessage, analogous to sendSOS)
  bool sendChannelMessage(uint8_t channel_idx, const char* text);

  // V5: favourite check for channel messages (extract sender name from text)
  bool isSenderFavorite(const char* text);

  // V6: extract sender name from channel message text ("NodeName: text") — used by UI listener
  static bool extractSenderName(const char* text, char* out_name, int max_len);
  // ---- end FieldMesh ----------------------------------------------------------------------
};

extern MyMesh the_mesh;
