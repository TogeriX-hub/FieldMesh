// FieldMesh repeater: room server as a second identity on the same radio.
// Only compiled in with -D FIELDMESH_REPEATER=1.
//
// The repeater keeps its own identity, ACL and all repeater functions (neighbours, paths,
// regions). The room ("BaseCamp") has its own key pair and advertises as ADV_TYPE_ROOM.
// Packets addressed to the room are taken out of the receive path in MyMesh::onRecvPacket()
// only if they decrypt with the room key; everything else goes through mesh::Mesh as usual.
// Room logic is ported from examples/simple_room_server (MeshCore 1.18.0); admin login on
// the room is disabled on purpose (the box is administered via the repeater identity).

#include "MyMesh.h"
#include <new>   // placement new (room client list reset)

#ifdef FIELDMESH_REPEATER

#define FM_ROOM_RESPONSE_DELAY       300    // = SERVER_RESPONSE_DELAY of the room server
#define FM_ROOM_ACK_DELAY            200    // = TXT_ACK_DELAY
#define FM_ROOM_REPLY_DELAY_MILLIS   1500
#define FM_ROOM_PUSH_NOTIFY_DELAY    2000
#define FM_ROOM_SYNC_PUSH_INTERVAL   1200
#define FM_ROOM_PUSH_ACK_TIMEOUT_FLOOD   12000
#define FM_ROOM_PUSH_TIMEOUT_BASE        4000
#define FM_ROOM_PUSH_ACK_TIMEOUT_FACTOR  2000
#define FM_ROOM_POST_SYNC_DELAY_SECS 6
#define FM_ROOM_FIRMWARE_VER_LEVEL   1
#define FM_ROOM_RESP_LOGIN_OK        0

#define FM_ROOM_REQ_GET_STATUS       0x01
#define FM_ROOM_REQ_KEEP_ALIVE       0x02
#define FM_ROOM_REQ_GET_TELEMETRY    0x03

struct FmRoomStats {   // same layout as ServerStats of the room server (read by the apps)
  uint16_t batt_milli_volts;
  uint16_t curr_tx_queue_len;
  int16_t noise_floor;
  int16_t last_rssi;
  uint32_t n_packets_recv;
  uint32_t n_packets_sent;
  uint32_t total_air_time_secs;
  uint32_t total_up_time_secs;
  uint32_t n_sent_flood, n_sent_direct;
  uint32_t n_recv_flood, n_recv_direct;
  uint16_t err_events;
  int16_t last_snr;    // x 4
  uint16_t n_direct_dups, n_flood_dups;
  uint16_t n_posted, n_post_push;
};

bool MyMesh::fmRoomLoadIdentity(bool create) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  IdentityStore store(*_fs, "");
#else
  IdentityStore store(*_fs, "/identity");   // same place as the repeater identity (main.cpp)
#endif
  if (store.load(FM_ROOM_IDENTITY_NAME, _room_id)) {
    _room_id_loaded = true;
    return true;
  }
  if (!create) return false;

  // New key pair. The 1-byte hash must differ from the repeater's, so packets for the
  // repeater and for the room can never be confused (0x00/0xFF are reserved).
  int count = 0;
  do {
    _room_id = radio_new_identity();
    count++;
  } while (count < 20 && (_room_id.pub_key[0] == 0x00 || _room_id.pub_key[0] == 0xFF
                          || _room_id.pub_key[0] == self_id.pub_key[0]));
  store.save(FM_ROOM_IDENTITY_NAME, _room_id);
  _room_id_loaded = true;
  return true;
}

void MyMesh::fmRoomPrepare(mesh::Packet* pkt) {
  // createDatagram()/createPathReturn() always write self_id as sender hash (payload[1]).
  // The MAC only covers the encrypted part, so the hash byte can be replaced safely.
  pkt->payload[1] = _room_id.pub_key[0];
}

bool MyMesh::fmRoomRecv(mesh::Packet* pkt) {
  if (!fmRoomActive()) return false;
  if (pkt->isRouteDirect() && pkt->getPathHashCount() > 0) return false;   // still in transit: normal forwarding

  uint8_t type = pkt->getPayloadType();
  uint8_t data[MAX_PACKET_PAYLOAD + 1];

  if (type == PAYLOAD_TYPE_ANON_REQ) {   // login request
    int i = 0;
    uint8_t dest_hash = pkt->payload[i++];
    if (!_room_id.isHashMatch(&dest_hash)) return false;
    uint8_t* sender_pub_key = &pkt->payload[i]; i += PUB_KEY_SIZE;
    if (i + 2 >= pkt->payload_len) return false;
    if (getTables()->wasSeen(pkt)) return false;

    mesh::Identity sender(sender_pub_key);
    uint8_t secret[PUB_KEY_SIZE];
    _room_id.calcSharedSecret(secret, sender);
    int len = mesh::Utils::MACThenDecrypt(secret, data, &pkt->payload[i], pkt->payload_len - i);
    if (len <= 0) return false;   // not for the room (hash collision): normal path

    getTables()->markSeen(pkt);
    fmRoomOnLogin(pkt, secret, sender, data, len);
    return true;
  }

  if (type == PAYLOAD_TYPE_REQ || type == PAYLOAD_TYPE_RESPONSE || type == PAYLOAD_TYPE_TXT_MSG || type == PAYLOAD_TYPE_PATH) {
    int i = 0;
    uint8_t dest_hash = pkt->payload[i++];
    uint8_t src_hash = pkt->payload[i++];
    if (!_room_id.isHashMatch(&dest_hash)) return false;
    if (i + CIPHER_MAC_SIZE >= pkt->payload_len) return false;
    if (getTables()->wasSeen(pkt)) return false;

    for (int k = 0; k < _room_acl.getNumClients(); k++) {
      ClientInfo* client = _room_acl.getClientByIdx(k);
      if (!client->id.isHashMatch(&src_hash)) continue;

      int len = mesh::Utils::MACThenDecrypt(client->shared_secret, data, &pkt->payload[i], pkt->payload_len - i);
      if (len <= 0) continue;

      getTables()->markSeen(pkt);
      if (type == PAYLOAD_TYPE_PATH) {   // same decoding as mesh::Mesh::onRecvPacket()
        int j = 0;
        uint8_t path_len = data[j++];
        if (mesh::Packet::isValidPathLen(path_len)) {
          uint8_t hash_size = (path_len >> 6) + 1;
          uint8_t hash_count = path_len & 63;
          uint8_t* path = &data[j]; j += hash_size*hash_count;
          if (j < len) {
            uint8_t extra_type = data[j++] & 0x0F;
            fmRoomOnPath(client, path, path_len, extra_type, &data[j], len - j);
          } else {
            fmRoomOnPath(client, path, path_len, 0, NULL, 0);
          }
        }
      } else if (type == PAYLOAD_TYPE_REQ || type == PAYLOAD_TYPE_TXT_MSG) {
        fmRoomOnData(pkt, client, client->shared_secret, data, len);
      }
      return true;   // consumed (RESPONSE to the room: nothing to do)
    }
    return false;   // no matching room client: maybe for another node with the same hash
  }
  return false;
}

void MyMesh::fmRoomOnLogin(mesh::Packet* pkt, const uint8_t* secret, const mesh::Identity& sender, uint8_t* data, size_t len) {
  if (len < 8) return;
  uint32_t sender_timestamp, sender_sync_since;
  memcpy(&sender_timestamp, data, 4);
  memcpy(&sender_sync_since, &data[4], 4);   // sender's "sync messages SINCE x" timestamp
  data[len] = 0;   // ensure null terminator
  const char* password = (const char*)&data[8];

  ClientInfo* client = NULL;
  if (password[0] == 0) {   // blank password: known client logging in again
    client = _room_acl.getClient(sender.pub_key, PUB_KEY_SIZE);
  }
  if (client == NULL) {
    // Only the room password (empty = open room). No admin login on the room.
    if (strcmp(password, _fm.room_pass) != 0) {
      MESH_DEBUG_PRINTLN("FM room: incorrect password");
      return;   // no response, client will time out
    }
    client = _room_acl.putClient(sender, 0);
    if (sender_timestamp <= client->last_timestamp) {
      MESH_DEBUG_PRINTLN("FM room: possible replay attack!");
      return;
    }
    client->last_timestamp = sender_timestamp;
    client->extra.room.sync_since = sender_sync_since;
    client->extra.room.pending_ack = 0;
    client->extra.room.push_failures = 0;
    client->last_activity = getRTCClock()->getCurrentTime();
    client->permissions = PERM_ACL_READ_WRITE;
    memcpy(client->shared_secret, secret, PUB_KEY_SIZE);
  }

  if (pkt->isRouteFlood()) {
    client->out_path_len = OUT_PATH_UNKNOWN;   // need to rediscover out_path
  }

  uint32_t now = getRTCClock()->getCurrentTimeUnique();
  memcpy(_room_reply, &now, 4);
  _room_reply[4] = FM_ROOM_RESP_LOGIN_OK;
  _room_reply[5] = 0;   // legacy keep-alive interval
  _room_reply[6] = 0;   // not admin
  _room_reply[7] = client->permissions;
  getRNG()->random(&_room_reply[8], 4);   // packet-hash uniqueness
  _room_reply[12] = FM_ROOM_FIRMWARE_VER_LEVEL;

  _room_next_push = futureMillis(FM_ROOM_PUSH_NOTIFY_DELAY);   // let the RESPONSE arrive first

  if (pkt->isRouteFlood()) {
    // tell the sender the path to here, and carry the response along
    mesh::Packet* path = createPathReturn(sender, client->shared_secret, pkt->path, pkt->path_len,
                                          PAYLOAD_TYPE_RESPONSE, _room_reply, 13);
    if (path) {
      fmRoomPrepare(path);
      sendFloodReply(path, FM_ROOM_RESPONSE_DELAY, pkt->getPathHashSize());
    }
  } else {
    mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, sender, client->shared_secret, _room_reply, 13);
    if (reply) {
      fmRoomPrepare(reply);
      if (client->out_path_len != OUT_PATH_UNKNOWN) {
        sendDirect(reply, client->out_path, client->out_path_len, FM_ROOM_RESPONSE_DELAY);
      } else {
        sendFloodReply(reply, FM_ROOM_RESPONSE_DELAY, pkt->getPathHashSize());
      }
    }
  }
}

void MyMesh::fmRoomOnData(mesh::Packet* pkt, ClientInfo* client, const uint8_t* secret, uint8_t* data, size_t len) {
  uint8_t type = pkt->getPayloadType();

  if (type == PAYLOAD_TYPE_TXT_MSG && len > 5) {   // a new post
    uint32_t sender_timestamp;
    memcpy(&sender_timestamp, data, 4);   // by sender's clock (could be wrong)
    uint8_t flags = (data[4] >> 2);

    if (flags != TXT_TYPE_PLAIN) {
      MESH_DEBUG_PRINTLN("FM room: ignoring text type %02x (no CLI on the room)", (uint32_t)flags);
      return;   // CLI goes to the repeater identity, no reply and no ACK here
    }
    if (sender_timestamp < client->last_timestamp) {
      MESH_DEBUG_PRINTLN("FM room: possible replay attack");
      return;
    }
    bool is_retry = (sender_timestamp == client->last_timestamp);
    client->last_timestamp = sender_timestamp;
    client->last_activity = getRTCClock()->getCurrentTime();
    client->extra.room.push_failures = 0;   // push can resume

    data[len] = 0;   // C string again (text may be padded with zeroes)

    uint32_t ack_hash;   // proves to the sender that we got it
    mesh::Utils::sha256((uint8_t *)&ack_hash, 4, data, 5 + strlen((char *)&data[5]), client->id.pub_key, PUB_KEY_SIZE);

    if ((client->permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) return;   // read only: no post, no ACK

    if (!is_retry) {
      fmRoomStorePost(client->id, (const char *)&data[5]);
    }

    if (client->out_path_len == OUT_PATH_UNKNOWN) {
      mesh::Packet* ack = createAck(ack_hash);
      if (ack) sendFloodReply(ack, FM_ROOM_ACK_DELAY, pkt->getPathHashSize());
    } else {
      uint32_t d = FM_ROOM_ACK_DELAY;
      if (getExtraAckTransmitCount() > 0) {
        mesh::Packet* a1 = createMultiAck(ack_hash, 1);
        if (a1) sendDirect(a1, client->out_path, client->out_path_len, d);
        d += 300;
      }
      mesh::Packet* a2 = createAck(ack_hash);
      if (a2) sendDirect(a2, client->out_path, client->out_path_len, d);
    }
    return;
  }

  if (type == PAYLOAD_TYPE_REQ && len >= 5) {
    uint32_t sender_timestamp;
    memcpy(&sender_timestamp, data, 4);
    if (sender_timestamp < client->last_timestamp) {
      MESH_DEBUG_PRINTLN("FM room: possible replay attack");
      return;
    }
    client->last_timestamp = sender_timestamp;
    client->last_activity = getRTCClock()->getCurrentTime();   // keeps the client connection alive
    client->extra.room.push_failures = 0;

    if (data[4] == FM_ROOM_REQ_KEEP_ALIVE && pkt->isRouteDirect()) {
      uint32_t force_since = 0;
      if (len >= 9) {
        memcpy(&force_since, &data[5], 4);   // optional: last post_timestamp the client got (may be padding)
      } else {
        memcpy(&data[5], &force_since, 4);   // zeroes for the ack_hash below
      }
      if (force_since > 0) {
        client->extra.room.sync_since = force_since;
      }
      client->extra.room.pending_ack = 0;

      if (client->out_path_len != OUT_PATH_UNKNOWN) {   // keep-alive answer only DIRECT
        uint32_t ack_hash;
        mesh::Utils::sha256((uint8_t *)&ack_hash, 4, data, 9, client->id.pub_key, PUB_KEY_SIZE);
        mesh::Packet* reply = createAck(ack_hash);
        if (reply) {
          reply->payload[reply->payload_len++] = fmRoomUnsyncedCount(client);   // unsynced counter at the end
          sendDirect(reply, client->out_path, client->out_path_len, FM_ROOM_RESPONSE_DELAY);
        }
      }
      return;
    }

    int reply_len = fmRoomHandleRequest(client, sender_timestamp, &data[4], len - 4);
    if (reply_len <= 0) return;

    if (pkt->isRouteFlood()) {
      mesh::Packet* path = createPathReturn(client->id, secret, pkt->path, pkt->path_len,
                                            PAYLOAD_TYPE_RESPONSE, _room_reply, reply_len);
      if (path) {
        fmRoomPrepare(path);
        sendFloodReply(path, FM_ROOM_RESPONSE_DELAY, pkt->getPathHashSize());
      }
    } else {
      mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, client->id, secret, _room_reply, reply_len);
      if (reply) {
        fmRoomPrepare(reply);
        if (client->out_path_len != OUT_PATH_UNKNOWN) {
          sendDirect(reply, client->out_path, client->out_path_len, FM_ROOM_RESPONSE_DELAY);
        } else {
          sendFloodReply(reply, FM_ROOM_RESPONSE_DELAY, pkt->getPathHashSize());
        }
      }
    }
  }
}

void MyMesh::fmRoomOnPath(ClientInfo* client, uint8_t* path, uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  client->out_path_len = mesh::Packet::copyPath(client->out_path, path, path_len);   // for sendDirect()
  client->last_activity = getRTCClock()->getCurrentTime();
  if (extra_type == PAYLOAD_TYPE_ACK && extra_len >= 4) {
    fmRoomProcessAck(extra);   // encoded ACK
  }
  // no reciprocal path (same as the room server)
}

int MyMesh::fmRoomHandleRequest(ClientInfo* sender, uint32_t sender_timestamp, uint8_t* payload, size_t payload_len) {
  memcpy(_room_reply, &sender_timestamp, 4);   // reflect sender_timestamp (tag)

  if (payload[0] == FM_ROOM_REQ_GET_STATUS) {
    FmRoomStats stats;
    stats.batt_milli_volts = board.getBattMilliVolts();
    stats.curr_tx_queue_len = _mgr->getOutboundTotal();
    stats.noise_floor = (int16_t)_radio->getNoiseFloor();
    stats.last_rssi = (int16_t)radio_driver.getLastRSSI();
    stats.n_packets_recv = radio_driver.getPacketsRecv();
    stats.n_packets_sent = radio_driver.getPacketsSent();
    stats.total_air_time_secs = getTotalAirTime() / 1000;
    stats.total_up_time_secs = uptime_millis / 1000;
    stats.n_sent_flood = getNumSentFlood();
    stats.n_sent_direct = getNumSentDirect();
    stats.n_recv_flood = getNumRecvFlood();
    stats.n_recv_direct = getNumRecvDirect();
    stats.err_events = _err_flags;
    stats.last_snr = (int16_t)(radio_driver.getLastSNR() * 4);
    stats.n_direct_dups = ((SimpleMeshTables *)getTables())->getNumDirectDups();
    stats.n_flood_dups = ((SimpleMeshTables *)getTables())->getNumFloodDups();
    stats.n_posted = _room_num_posted;
    stats.n_post_push = _room_num_pushes;
    memcpy(&_room_reply[4], &stats, sizeof(stats));
    return 4 + sizeof(stats);
  }
  if (payload[0] == FM_ROOM_REQ_GET_TELEMETRY) {   // basic telemetry only (room clients are not admins)
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    sensors.querySensors(0x00, telemetry);
    float temperature = board.getMCUTemperature();
    if (!isnan(temperature)) {
      telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature);
    }
    uint8_t tlen = telemetry.getSize();
    memcpy(&_room_reply[4], telemetry.getBuffer(), tlen);
    return 4 + tlen;
  }
  return 0;   // unknown request
}

void MyMesh::fmRoomStorePost(const mesh::Identity& author, const char* text) {
  int idx = _room_next_post_idx;
  _room_posts[idx].author = author;
  StrHelper::strncpy(_room_posts[idx].text, text, FM_ROOM_POST_TEXT_LEN);
  _room_posts[idx].post_timestamp = getRTCClock()->getCurrentTimeUnique();
  _room_next_post_idx = (_room_next_post_idx + 1) % FM_ROOM_MAX_POSTS;
  _room_next_push = futureMillis(FM_ROOM_PUSH_NOTIFY_DELAY);
  _room_num_posted++;
}

void MyMesh::fmRoomPushPost(ClientInfo* client, FmRoomPost& post) {
  int len = 0;
  memcpy(&_room_reply[len], &post.post_timestamp, 4); len += 4;   // PAST timestamp, accepted by the client

  uint8_t attempt;
  getRNG()->random(&attempt, 1);   // different packet hash (and ACK) for re-tries
  _room_reply[len++] = (TXT_TYPE_SIGNED_PLAIN << 2) | (attempt & 3);

  memcpy(&_room_reply[len], post.author.pub_key, 4); len += 4;   // author prefix

  int text_len = strlen(post.text);
  memcpy(&_room_reply[len], post.text, text_len); len += text_len;

  // expected ACK
  mesh::Utils::sha256((uint8_t *)&client->extra.room.pending_ack, 4, _room_reply, len, client->id.pub_key, PUB_KEY_SIZE);
  client->extra.room.push_post_timestamp = post.post_timestamp;

  mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_TXT_MSG, client->id, client->shared_secret, _room_reply, len);
  if (reply) {
    fmRoomPrepare(reply);
    if (client->out_path_len == OUT_PATH_UNKNOWN) {
      sendFloodScoped(default_scope, reply, 0, _prefs.path_hash_mode + 1);
      client->extra.room.ack_timeout = futureMillis(FM_ROOM_PUSH_ACK_TIMEOUT_FLOOD);
    } else {
      sendDirect(reply, client->out_path, client->out_path_len);
      uint8_t path_hash_count = client->out_path_len & 63;
      client->extra.room.ack_timeout = futureMillis(FM_ROOM_PUSH_TIMEOUT_BASE + FM_ROOM_PUSH_ACK_TIMEOUT_FACTOR * (path_hash_count + 1));
    }
    _room_num_pushes++;
  } else {
    client->extra.room.pending_ack = 0;
  }
}

uint8_t MyMesh::fmRoomUnsyncedCount(ClientInfo* client) {
  uint8_t count = 0;
  for (int k = 0; k < FM_ROOM_MAX_POSTS; k++) {
    if (_room_posts[k].post_timestamp > client->extra.room.sync_since
        && !_room_posts[k].author.matches(client->id)) {
      count++;
    }
  }
  return count;
}

bool MyMesh::fmRoomProcessAck(const uint8_t* data) {
  for (int i = 0; i < _room_acl.getNumClients(); i++) {
    ClientInfo* client = _room_acl.getClientByIdx(i);
    if (client->extra.room.pending_ack && memcmp(data, &client->extra.room.pending_ack, 4) == 0) {
      client->extra.room.pending_ack = 0;   // next push can happen
      client->extra.room.push_failures = 0;
      client->extra.room.sync_since = client->extra.room.push_post_timestamp;   // advance SINCE
      return true;
    }
  }
  return false;
}

void MyMesh::onAckRecv(mesh::Packet* packet, uint32_t ack_crc) {
  if (fmRoomActive() && fmRoomProcessAck((uint8_t *)&ack_crc)) {
    packet->markDoNotRetransmit();   // ACK was for the room, don't retransmit
  }
}

void MyMesh::fmRoomLoop() {
  if (!fmRoomActive() || _room_acl.getNumClients() == 0) return;
  if (!millisHasNowPassed(_room_next_push)) return;

  // ACK timeouts
  for (int i = 0; i < _room_acl.getNumClients(); i++) {
    ClientInfo* c = _room_acl.getClientByIdx(i);
    if (c->extra.room.pending_ack && millisHasNowPassed(c->extra.room.ack_timeout)) {
      c->extra.room.push_failures++;
      c->extra.room.pending_ack = 0;
    }
  }

  // next client (round robin), push its next new post
  if (_room_next_client_idx >= _room_acl.getNumClients()) _room_next_client_idx = 0;
  ClientInfo* client = _room_acl.getClientByIdx(_room_next_client_idx);
  bool did_push = false;
  if (client->extra.room.pending_ack == 0 && client->last_activity != 0 && client->extra.room.push_failures < 3) {
    uint32_t now = getRTCClock()->getCurrentTime();
    for (int k = 0, idx = _room_next_post_idx; k < FM_ROOM_MAX_POSTS; k++) {
      FmRoomPost* p = &_room_posts[idx];
      if (now >= p->post_timestamp + FM_ROOM_POST_SYNC_DELAY_SECS
          && p->post_timestamp > client->extra.room.sync_since
          && !p->author.matches(client->id)) {   // not to the author
        fmRoomPushPost(client, *p);
        did_push = true;
        break;
      }
      idx = (idx + 1) % FM_ROOM_MAX_POSTS;   // oldest first
    }
  }
  _room_next_client_idx = (_room_next_client_idx + 1) % _room_acl.getNumClients();

  _room_next_push = futureMillis(did_push ? FM_ROOM_SYNC_PUSH_INTERVAL : FM_ROOM_SYNC_PUSH_INTERVAL / 8);
}

void MyMesh::fmRoomSendAdvert(bool flood, int delay_millis) {
  if (!fmRoomActive()) return;

  uint8_t app_data[MAX_ADVERT_DATA_SIZE];
  uint8_t app_data_len;
  const char* name = fmRoomName();
  if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {   // same location policy as the repeater advert
    AdvertDataBuilder builder(ADV_TYPE_ROOM, name);
    app_data_len = builder.encodeTo(app_data);
  } else if (_prefs.advert_loc_policy == ADVERT_LOC_SHARE) {
    AdvertDataBuilder builder(ADV_TYPE_ROOM, name, sensors.node_lat, sensors.node_lon);
    app_data_len = builder.encodeTo(app_data);
  } else {
    AdvertDataBuilder builder(ADV_TYPE_ROOM, name, _prefs.node_lat, _prefs.node_lon);
    app_data_len = builder.encodeTo(app_data);
  }

  mesh::Packet* pkt = createAdvert(_room_id, app_data, app_data_len);
  if (pkt == NULL) return;
  if (flood) {
    sendFloodScoped(default_scope, pkt, delay_millis, _prefs.path_hash_mode + 1);
  } else {
    sendZeroHop(pkt, delay_millis);
  }
}

void MyMesh::fmRoomClearPosts() {
  memset(_room_posts, 0, sizeof(_room_posts));
  _room_next_post_idx = 0;
}

void MyMesh::fmRoomClearUsers() {
  // NOT _room_acl.clear(): that deletes /s_contacts, the ACL file of the repeater.
  // The room list is never loaded from a file, so rebuild it in place.
  _room_acl.~ClientACL();
  new (&_room_acl) ClientACL();
  _room_next_client_idx = 0;
}

int MyMesh::fmRoomNumPosts() const {
  int n = 0;
  for (int k = 0; k < FM_ROOM_MAX_POSTS; k++) {
    if (_room_posts[k].post_timestamp != 0) n++;
  }
  return n;
}

void MyMesh::fmSetRoom(bool on, const char* name, char* reply) {
  char tmp[64];
  if (on) {
    if (name && name[0]) {
      StrHelper::strncpy(_fm.room_name, name, sizeof(_fm.room_name));
    }
    if (!_room_id_loaded && !fmRoomLoadIdentity(true)) {
      strcpy(tmp, "Err - no room identity");
    } else {
      bool was_on = _fm.room_enabled != 0;
      _fm.room_enabled = 1;
      fmSavePrefs();
      fmRoomSendAdvert(false, 500);    // neighbours see it right away
      fmRoomSendAdvert(true, 4000);    // and the rest of the mesh
      snprintf(tmp, sizeof(tmp), "OK - room '%s' %s", fmRoomName(), was_on ? "updated" : "ON");
    }
  } else {
    _fm.room_enabled = 0;
    fmSavePrefs();
    strcpy(tmp, "OK - room OFF");
  }
  if (reply) strcpy(reply, tmp);
}

#endif
