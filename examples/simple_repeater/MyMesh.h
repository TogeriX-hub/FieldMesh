#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include <RTClib.h>
#include <target.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
  using File = fs::File;
#endif

#ifdef WITH_RS232_BRIDGE
#include "helpers/bridges/RS232Bridge.h"
#define WITH_BRIDGE
#endif

#ifdef WITH_ESPNOW_BRIDGE
#include "helpers/bridges/ESPNowBridge.h"
#define WITH_BRIDGE
#endif

#include <helpers/AdvertDataHelpers.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/ClientACL.h>
#include <helpers/CommonCLI.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/StatsFormatHelper.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/RegionMap.h>
#include <helpers/RoutingPolicy.h>
#include "RateLimiter.h"
#include "FieldMeshRepeater.h"   // FieldMesh (only active with FIELDMESH_REPEATER)

#ifdef WITH_BRIDGE
extern AbstractBridge* bridge;
#endif

struct RepeaterStats {
  uint16_t batt_milli_volts;
  uint16_t curr_tx_queue_len;
  int16_t  noise_floor;
  int16_t  last_rssi;
  uint32_t n_packets_recv;
  uint32_t n_packets_sent;
  uint32_t total_air_time_secs;
  uint32_t total_up_time_secs;
  uint32_t n_sent_flood, n_sent_direct;
  uint32_t n_recv_flood, n_recv_direct;
  uint16_t err_events;                // was 'n_full_events'
  int16_t  last_snr;   // x 4
  uint16_t n_direct_dups, n_flood_dups;
  uint32_t total_rx_air_time_secs;
  uint32_t n_recv_errors;
};

struct NeighbourInfo {
  mesh::Identity id;
  uint32_t advert_timestamp;
  uint32_t heard_timestamp;
  int8_t snr; // multiplied by 4, user should divide to get float value
};

#ifndef FIRMWARE_BUILD_DATE
  #define FIRMWARE_BUILD_DATE   "15 Oct 2026"
#endif

#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION   "v1.18.0"
#endif

#define FIRMWARE_ROLE "repeater"

#define PACKET_LOG_FILE  "/packet_log"

class MyMesh : public mesh::Mesh, public CommonCLICallbacks
#ifdef FIELDMESH_REPEATER
  , public FieldMeshUIHost
#endif
{
  FILESYSTEM* _fs;
  uint32_t last_millis;
  uint64_t uptime_millis;
  unsigned long next_local_advert, next_flood_advert;
  bool _logging;
  NodePrefs _prefs;
  ClientACL  acl;
  CommonCLI _cli;
  uint8_t reply_data[MAX_PACKET_PAYLOAD];
  uint8_t reply_path[MAX_PATH_SIZE];
  uint8_t reply_path_len;
  TransportKeyStore key_store;
  RegionMap region_map, temp_map;
  RegionEntry* load_stack[8];
  RegionEntry* recv_pkt_region;
  TransportKey default_scope;
  RateLimiter discover_limiter, anon_limiter;
  uint32_t pending_discover_tag;
  unsigned long pending_discover_until;
  bool region_load_active;
  unsigned long dirty_contacts_expiry;
#if MAX_NEIGHBOURS
  NeighbourInfo neighbours[MAX_NEIGHBOURS];
#endif
  CayenneLPP telemetry;
  unsigned long set_radio_at, revert_radio_at;
  float pending_freq;
  float pending_bw;
  uint8_t pending_sf;
  uint8_t pending_cr;
  int  matching_peer_indexes[MAX_CLIENTS];
#ifdef FIELDMESH_REPEATER
  FieldMeshRepeaterPrefs _fm;
  bool _fm_advert_after_radio;   // send a zero-hop advert right after the pending radio params are applied

  void fmBegin();
  bool fmSavePrefs();
  uint32_t _fm_clock_saved;
  unsigned long _fm_clock_next_save;
  void fmClockRestore();
  void fmClockSaveLoop();

  // dashboard data (FieldMeshStats.cpp)
  FmNode* _fm_nodes;            // PSRAM if available
  int _fm_max_nodes;
  uint8_t _fm_team[FM_MAX_TEAM][PUB_KEY_SIZE];
  int _fm_num_team;
  FmActBucket _fm_act[FM_ACT_BUCKETS];
  int _fm_act_idx;
  unsigned long _fm_act_next;
  unsigned long _fm_act_air_mark;
  uint32_t _fm_type_counts[16];
  FmFeedEntry _fm_feed[FM_FEED_SIZE];
  int _fm_feed_next;
  float _fm_env_temp, _fm_env_hum;
  uint8_t _fm_env_flags;        // 1 = temp from sensor, 2 = humidity, 4 = temp from MCU
  unsigned long _fm_env_next;
  void fmStatsBegin();
  void fmStatsLoop();
  void fmStatsRx(const mesh::Packet* pkt);
  void fmStatsTx(const mesh::Packet* pkt);
  void fmNodeHeard(const mesh::Packet* pkt, const mesh::Identity& id, const uint8_t* app_data, size_t app_data_len);
  FmNode* fmNodeFind(const uint8_t* key, int key_len);
  const char* fmNodeNameByHash(uint8_t hash);   // NULL if unknown or ambiguous
  bool fmTeamHas(const uint8_t* pub_key) const;
  bool fmTeamSet(const uint8_t* pub_key, bool on);
  void fmTeamLoad();
  void fmTeamSave();
  bool fmStatsHandleCommand(const char* command, char* reply);
  uint32_t fmDutyMillisLastHour();

  const char* _fm_boot_reason;

  // WiFi dashboard (FieldMeshWeb.cpp)
  void fmWebBuildNodes(struct FmJson& j);
  void fmWebBuildNetwork(struct FmJson& j);
  void fmWebBuildBoard(struct FmJson& j);
  void fmWebBuildSetup(struct FmJson& j);
  bool fmWebStart();
  void fmWebStop();
  bool fmWebIsOn() const;
  int  fmWebStations() const;
  const char* fmWebCurrentSsid() const;
  const char* fmWebCurrentHost() const;   // "<name>" for http://<name>.local
  void fmWebHostName(char* dest, size_t sz, const char* name);
  void fmWebSsid(char* dest, size_t sz);
  void fmWebLoop();
  bool fmHandleCommand(uint32_t sender_timestamp, const char* command, char* reply);

  // Room server as a second identity (FieldMeshRoom.cpp). The repeater itself stays a
  // normal repeater (own identity, own ACL); only packets for the room identity are
  // taken out of the normal receive path.
  mesh::LocalIdentity _room_id;
  bool _room_id_loaded;
  ClientACL _room_acl;           // RAM only: never load()/save(), ClientACL uses the fixed file /s_contacts of the repeater
  FmRoomPost _room_posts[FM_ROOM_MAX_POSTS];   // cyclic queue
  int _room_next_post_idx, _room_next_client_idx;
  unsigned long _room_next_push;
  uint16_t _room_num_posted, _room_num_pushes;
  uint8_t _room_reply[MAX_PACKET_PAYLOAD];
  FmRoomPrivate _room_private[FM_ROOM_MAX_PRIVATE];   // bot replies, only for the asker
  int _room_next_private_idx;
  unsigned long _room_posts_dirty_at, _room_clients_dirty_at;   // lazy saving
  RateLimiter _room_bot_limiter;

  // channels the box listens to + last SOS (FieldMeshChannels.cpp)
  FmChannel _fm_channels[FM_MAX_CHANNELS];
  int _fm_num_channels;
  FmSosEntry _fm_sos[FM_MAX_SOS];
  int _fm_sos_next;
  void fmChannelsLoad();
  void fmChannelsSave();
  bool fmChannelsHandleCommand(const char* command, char* reply);

  bool fmRoomActive() const { return _fm.room_enabled && _room_id_loaded; }
  bool fmRoomLoadIdentity(bool create);
  void fmRoomPrepare(mesh::Packet* pkt);   // set the room identity as sender (src hash)
  bool fmRoomRecv(mesh::Packet* pkt);      // true: packet was for the room and is consumed
  void fmRoomOnLogin(mesh::Packet* pkt, const uint8_t* secret, const mesh::Identity& sender, uint8_t* data, size_t len);
  void fmRoomOnData(mesh::Packet* pkt, ClientInfo* client, const uint8_t* secret, uint8_t* data, size_t len);
  void fmRoomOnPath(ClientInfo* client, uint8_t* path, uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len);
  int  fmRoomHandleRequest(ClientInfo* sender, uint32_t sender_timestamp, uint8_t* payload, size_t payload_len);
  void fmRoomStorePost(const mesh::Identity& author, const char* text);
  void fmRoomPushPost(ClientInfo* client, FmRoomPost& post);
  uint8_t fmRoomUnsyncedCount(ClientInfo* client);
  bool fmRoomProcessAck(const uint8_t* data);
  void fmRoomLoop();
  void fmRoomSendAdvert(bool flood, int delay_millis);
  int  fmRoomNumPosts() const;
  void fmRoomClearPosts();
  void fmRoomClearUsers();   // log out everyone
  void fmRoomLoadData();     // posts + users from flash, keeps the clock ahead of them
  void fmRoomSaveData();     // pending lazy writes
  void fmRoomBootPost();     // F1: system post with the reason of the (re)start
  bool fmRoomBot(mesh::Packet* pkt, ClientInfo* client, uint32_t sender_timestamp, const char* text);   // true = bot command or /CLI (not posted)
  void fmRoomQueuePrivate(ClientInfo* client, const char* text);
  bool fmBoxEnv(float& temp_c, float& hum_pct, bool& has_hum);   // F3: box sensor (or MCU temperature)
#endif
#if defined(WITH_RS232_BRIDGE)
  RS232Bridge bridge;
#elif defined(WITH_ESPNOW_BRIDGE)
  ESPNowBridge bridge;
#endif

  void putNeighbour(const mesh::Identity& id, uint32_t timestamp, float snr);
  uint8_t handleLoginReq(const mesh::Identity& sender, const uint8_t* secret, uint32_t sender_timestamp, const uint8_t* data, bool is_flood);
  uint8_t handleAnonRegionsReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  uint8_t handleAnonOwnerReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  uint8_t handleAnonClockReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  int handleRequest(ClientInfo* sender, uint32_t sender_timestamp, uint8_t* payload, size_t payload_len);
  mesh::Packet* createSelfAdvert();

  File openAppend(const char* fname);
  bool isLooped(const mesh::Packet* packet, const uint8_t max_counters[]);

protected:
  float getAirtimeBudgetFactor() const override {
    return _prefs.airtime_factor;
  }

  bool allowPacketForward(const mesh::Packet* packet) override;
  const char* getLogDateTime() override;
  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;

  void logRx(mesh::Packet* pkt, int len, float score) override;
  void logTx(mesh::Packet* pkt, int len) override;
  void logTxFail(mesh::Packet* pkt, int len) override;
  int calcRxDelay(float score, uint32_t air_time) const override;

  uint32_t getRetransmitDelay(const mesh::Packet* packet) override;
  uint32_t getDirectRetransmitDelay(const mesh::Packet* packet) override;

  int getInterferenceThreshold() const override {
    return _prefs.interference_threshold;
  }
  bool getCADEnabled() const override {
    return _prefs.cad_enabled;
  }
  int getAGCResetInterval() const override {
    return ((int)_prefs.agc_reset_interval) * 4000;   // milliseconds
  }
  uint8_t getExtraAckTransmitCount() const override {
    return _prefs.multi_acks;
  }

#if ENV_INCLUDE_GPS == 1
  void applyGpsPrefs() {
    sensors.setSettingValue("gps", _prefs.gps_enabled?"1":"0");
  }
#endif

  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override;

  void onAnonDataRecv(mesh::Packet* packet, const uint8_t* secret, const mesh::Identity& sender, uint8_t* data, size_t len) override;
  int searchPeersByHash(const uint8_t* hash) override;
  void getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) override;
  void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len);
  void onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data, size_t len) override;
  bool onPeerPathRecv(mesh::Packet* packet, int sender_idx, const uint8_t* secret, uint8_t* path, uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onControlDataRecv(mesh::Packet* packet) override;
#ifdef FIELDMESH_REPEATER
  void onAckRecv(mesh::Packet* packet, uint32_t ack_crc) override;   // ACKs for room post pushes
  int searchChannelsByHash(const uint8_t* hash, mesh::GroupChannel channels[], int max_matches) override;   // channel list of the box
  void onGroupDataRecv(mesh::Packet* packet, uint8_t type, const mesh::GroupChannel& channel, uint8_t* data, size_t len) override;   // SOS -> board
#endif

  void sendFloodReply(mesh::Packet* packet, unsigned long delay_millis, uint8_t path_hash_size);

public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables);

  void begin(FILESYSTEM* fs);
  void sendNodeDiscoverReq();
  const char* getFirmwareVer() override { return FIRMWARE_VERSION; }
  const char* getBuildDate() override { return FIRMWARE_BUILD_DATE; }
  const char* getRole() override { return FIRMWARE_ROLE; }
  const char* getNodeName() { return _prefs.node_name; }
  NodePrefs* getNodePrefs() {
    return &_prefs;
  }
  bool isHiddenNode() const {
    return _prefs.advert_interval == 0 && _prefs.flood_advert_interval == 0;
  }

  void savePrefs() override {
    _cli.savePrefs(_fs);
  }

  void sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis, uint8_t path_hash_size);

  // CommonCLICallbacks
  void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) override;
  bool formatFileSystem() override;
  void sendSelfAdvertisement(int delay_millis, bool flood) override;
  void updateAdvertTimer() override;
  void updateFloodAdvertTimer() override;

  void setLoggingOn(bool enable) override { _logging = enable; }

  void eraseLogFile() override {
    _fs->remove(PACKET_LOG_FILE);
  }

  void dumpLogFile() override;
  void setTxPower(int8_t power_dbm) override;
  void formatNeighborsReply(char *reply) override;
  void removeNeighbor(const uint8_t* pubkey, int key_len) override;
  void formatStatsReply(char *reply) override;
  void formatRadioStatsReply(char *reply) override;
  void formatPacketStatsReply(char *reply) override;
  void startRegionsLoad() override;
  bool saveRegions() override;
  void onDefaultRegionChanged(const RegionEntry* r) override;

  mesh::LocalIdentity& getSelfId() override { return self_id; }

  void saveIdentity(const mesh::LocalIdentity& new_id) override;
  void clearStats() override;

  void handleCommand(uint32_t sender_timestamp, char* command, char* reply);
  void loop();

#if defined(WITH_BRIDGE)
  void setBridgeState(bool enable) override {
    if (enable == bridge.isRunning()) return;
    if (enable)
    {
      bridge.begin();
    }
    else 
    {
      bridge.end();
    }
  }

  void restartBridge() override {
    if (!bridge.isRunning()) return;
    bridge.end();
    bridge.begin();
  }
#endif

  // To check if there is pending work
  bool hasPendingWork() const;

  bool setRxBoostedGain(bool enable) override;

#ifdef FIELDMESH_REPEATER
  // FieldMeshUIHost
  bool fmIsOffGrid() const override { return _fm.offgrid != 0; }
  void fmSetOffGrid(bool on, char* reply) override;
  bool fmIsRoomOn() const override { return fmRoomActive(); }
  const char* fmRoomName() const override { return _fm.room_name[0] ? _fm.room_name : _prefs.node_name; }
  void fmRoomCounts(int& clients, int& posts) override { clients = _room_acl.getNumClients(); posts = fmRoomNumPosts(); }
  void fmSetRoom(bool on, const char* name, char* reply) override;
  void fmRoomReset() override;
  bool fmIsWifiOn() const override { return fmWebIsOn(); }
  const char* fmWifiSsid() const override { return fmWebCurrentSsid(); }
  int fmWifiStations() const override { return fmWebStations(); }
  void fmSetWifi(bool on, char* reply) override;
#endif

  #if defined(USE_LR2021)
  virtual bool configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) override;
  #endif

};
