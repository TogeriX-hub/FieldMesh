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
#include "FieldMeshFs.h"
#include <new>   // placement new (room client list reset)
#include <helpers/sensors/LPPDataHelpers.h>   // box sensor values for !status

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
#define FM_ROOM_LAT_OFFSET           0.000045   // ~5 m south of the repeater (1 deg lat = ~111 km)

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
    fmRoomLoadData();   // board and users from before the restart
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
    _room_clients_dirty_at = futureMillis(FM_ROOM_SAVE_DELAY);
  }

  // Every login (also a known user with blank password, e.g. after a restart where the user
  // list comes from flash with last_activity = 0) makes the user active again, so pushes resume.
  client->last_activity = getRTCClock()->getCurrentTime();
  client->extra.room.push_failures = 0;
  client->extra.room.pending_ack = 0;

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

    if (!is_retry && !fmRoomBot(pkt, client, sender_timestamp, (const char *)&data[5])) {   // !commands, /CLI: private reply, no post
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
        _room_clients_dirty_at = futureMillis(FM_ROOM_SAVE_DELAY);
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
  _room_clients_dirty_at = futureMillis(FM_ROOM_SAVE_DELAY);
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
  _room_posts_dirty_at = futureMillis(FM_ROOM_SAVE_DELAY);
}

void MyMesh::fmRoomQueuePrivate(ClientInfo* client, const char* text) {
  FmRoomPrivate* q = &_room_private[_room_next_private_idx];
  q->to = client->id;
  q->post.author = _room_id;
  StrHelper::strncpy(q->post.text, text, sizeof(q->post.text));
  q->post.post_timestamp = getRTCClock()->getCurrentTimeUnique();
  _room_next_private_idx = (_room_next_private_idx + 1) % FM_ROOM_MAX_PRIVATE;
  _room_next_push = futureMillis(FM_ROOM_PUSH_NOTIFY_DELAY);
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
  for (int k = 0; k < FM_ROOM_MAX_PRIVATE; k++) {   // bot replies waiting for this client
    if (_room_private[k].post.post_timestamp > client->extra.room.sync_since && _room_private[k].to.matches(client->id)) {
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
      _room_clients_dirty_at = futureMillis(FM_ROOM_SAVE_DELAY);
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
  fmRoomSaveData();   // pending lazy writes (also while the room is off)
  fmClockSaveLoop();  // clock high-water mark (FieldMeshRepeater.cpp)

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

  // next client (round robin)
  if (_room_next_client_idx >= _room_acl.getNumClients()) _room_next_client_idx = 0;
  ClientInfo* client = _room_acl.getClientByIdx(_room_next_client_idx);
  bool did_push = false;
  if (client->extra.room.pending_ack == 0 && client->last_activity != 0 && client->extra.room.push_failures < 3) {
    // Oldest unsynced entry for this client: board posts and its own bot replies together,
    // strictly by timestamp. sync_since jumps to the pushed timestamp on ACK, so an older
    // entry pushed later would never arrive.
    FmRoomPost* next = NULL;
    bool next_is_board = false;
    for (int k = 0; k < FM_ROOM_MAX_POSTS; k++) {
      FmRoomPost* p = &_room_posts[k];
      if (p->post_timestamp > client->extra.room.sync_since && !p->author.matches(client->id)   // not to the author
          && (next == NULL || p->post_timestamp < next->post_timestamp)) {
        next = p;
        next_is_board = true;
      }
    }
    for (int k = 0; k < FM_ROOM_MAX_PRIVATE; k++) {
      FmRoomPrivate* q = &_room_private[k];
      if (q->post.post_timestamp > client->extra.room.sync_since && q->to.matches(client->id)
          && (next == NULL || q->post.post_timestamp < next->post_timestamp)) {
        next = &q->post;
        next_is_board = false;
      }
    }
    uint32_t now = getRTCClock()->getCurrentTime();
    // board posts wait a few seconds (as upstream); if the oldest one is still waiting, wait for it
    if (next && (!next_is_board || now >= next->post_timestamp + FM_ROOM_POST_SYNC_DELAY_SECS)) {
      fmRoomPushPost(client, *next);
      did_push = true;
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
  } else {
    // Same location as the repeater, moved ~5 m south so both are visible on a map.
    // 0/0 means "no location set": keep it as it is.
    double lat = (_prefs.advert_loc_policy == ADVERT_LOC_SHARE) ? sensors.node_lat : _prefs.node_lat;
    double lon = (_prefs.advert_loc_policy == ADVERT_LOC_SHARE) ? sensors.node_lon : _prefs.node_lon;
    if (lat != 0.0 || lon != 0.0) lat -= FM_ROOM_LAT_OFFSET;
    AdvertDataBuilder builder(ADV_TYPE_ROOM, name, lat, lon);
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
  _room_posts_dirty_at = futureMillis(FM_ROOM_SAVE_DELAY);
}

void MyMesh::fmRoomClearUsers() {
  // NOT _room_acl.clear(): that deletes /s_contacts, the ACL file of the repeater.
  // The room list is never loaded from a file, so rebuild it in place.
  _room_acl.~ClientACL();
  new (&_room_acl) ClientACL();
  _room_next_client_idx = 0;
  _room_clients_dirty_at = futureMillis(FM_ROOM_SAVE_DELAY);
}

// ---- saving (posts and users survive a restart) ----

void MyMesh::fmRoomLoadData() {
  uint32_t newest = 0;

  // posts, oldest first: magic 'F','P', version 1, then per post: pub_key, timestamp, text length, text
  if (fmFsExists(_fs, FM_ROOM_POSTS_FILE)) {
    File f = fmFsOpenRead(_fs, FM_ROOM_POSTS_FILE);
    if (f) {
      uint8_t hdr[3];
      if (f.read(hdr, 3) == 3 && hdr[0] == 'F' && hdr[1] == 'P' && hdr[2] == 1) {
        memset(_room_posts, 0, sizeof(_room_posts));
        int n = 0;
        while (true) {
          uint8_t pub_key[PUB_KEY_SIZE];
          uint32_t ts;
          uint8_t len;
          char text[FM_ROOM_POST_TEXT_LEN+1];
          if (f.read(pub_key, PUB_KEY_SIZE) != PUB_KEY_SIZE) break;
          if (f.read((uint8_t *)&ts, 4) != 4) break;
          if (f.read(&len, 1) != 1 || len > FM_ROOM_POST_TEXT_LEN) break;
          if (f.read((uint8_t *)text, len) != len) break;
          text[len] = 0;

          FmRoomPost* p = &_room_posts[n % FM_ROOM_MAX_POSTS];   // more than fit: oldest drop out
          p->author = mesh::Identity(pub_key);
          p->post_timestamp = ts;
          memcpy(p->text, text, len + 1);
          if (ts > newest) newest = ts;
          n++;
        }
        _room_next_post_idx = n % FM_ROOM_MAX_POSTS;
      }
      f.close();
    }
  }

  // users: magic 'F','C', version 1, then per user: pub_key, permissions, sync_since, out_path_len, out_path
  if (fmFsExists(_fs, FM_ROOM_CLIENTS_FILE)) {
    File f = fmFsOpenRead(_fs, FM_ROOM_CLIENTS_FILE);
    if (f) {
      uint8_t hdr[3];
      if (f.read(hdr, 3) == 3 && hdr[0] == 'F' && hdr[1] == 'C' && hdr[2] == 1) {
        _room_acl.~ClientACL();
        new (&_room_acl) ClientACL();
        while (true) {
          uint8_t pub_key[PUB_KEY_SIZE];
          uint8_t perm, out_path_len;
          uint32_t sync_since;
          uint8_t out_path[MAX_PATH_SIZE];
          if (f.read(pub_key, PUB_KEY_SIZE) != PUB_KEY_SIZE) break;
          if (f.read(&perm, 1) != 1) break;
          if (f.read((uint8_t *)&sync_since, 4) != 4) break;
          if (f.read(&out_path_len, 1) != 1) break;
          if (f.read(out_path, MAX_PATH_SIZE) != MAX_PATH_SIZE) break;

          ClientInfo* c = _room_acl.putClient(mesh::Identity(pub_key), perm);
          c->extra.room.sync_since = sync_since;
          c->out_path_len = out_path_len;
          memcpy(c->out_path, out_path, MAX_PATH_SIZE);
          _room_id.calcSharedSecret(c->shared_secret, pub_key);   // not stored, always recalculated
          if (sync_since > newest) newest = sync_since;
        }
      }
      f.close();
    }
  }
  _room_next_client_idx = 0;

  // Without power the G2 loses its clock and starts on an old date. New posts must still be
  // newer than everything stored, otherwise they are never pushed (sync_since compares timestamps).
  // So the clock never stays behind the newest stored post / user.
  if (newest > 0 && getRTCClock()->getCurrentTime() <= newest) {
    getRTCClock()->setCurrentTime(newest + 1);
  }
}

void MyMesh::fmRoomSaveData() {
  if (_room_posts_dirty_at && millisHasNowPassed(_room_posts_dirty_at)) {
    _room_posts_dirty_at = 0;
    File f = fmFsOpenWriteTmp(_fs, FM_ROOM_POSTS_FILE);
    if (f) {
      uint8_t hdr[3] = { 'F', 'P', 1 };
      bool ok = f.write(hdr, 3) == 3;
      for (int k = 0, idx = _room_next_post_idx; ok && k < FM_ROOM_MAX_POSTS; k++, idx = (idx + 1) % FM_ROOM_MAX_POSTS) {   // oldest first
        FmRoomPost* p = &_room_posts[idx];
        if (p->post_timestamp == 0) continue;   // empty slot
        uint8_t len = strlen(p->text);
        ok = f.write(p->author.pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE
          && f.write((uint8_t *)&p->post_timestamp, 4) == 4
          && f.write(&len, 1) == 1
          && f.write((uint8_t *)p->text, len) == len;
      }
      f.close();
      if (ok) fmFsCommit(_fs, FM_ROOM_POSTS_FILE);   // flash full: keep the old file instead of a cut one
    }
  }
  if (_room_clients_dirty_at && millisHasNowPassed(_room_clients_dirty_at)) {
    _room_clients_dirty_at = 0;
    File f = fmFsOpenWriteTmp(_fs, FM_ROOM_CLIENTS_FILE);
    if (f) {
      uint8_t hdr[3] = { 'F', 'C', 1 };
      bool ok = f.write(hdr, 3) == 3;
      for (int i = 0; ok && i < _room_acl.getNumClients(); i++) {
        ClientInfo* c = _room_acl.getClientByIdx(i);
        if (c->permissions == 0) continue;
        ok = f.write(c->id.pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE
          && f.write(&c->permissions, 1) == 1
          && f.write((uint8_t *)&c->extra.room.sync_since, 4) == 4
          && f.write(&c->out_path_len, 1) == 1
          && f.write(c->out_path, MAX_PATH_SIZE) == MAX_PATH_SIZE;
      }
      f.close();
      if (ok) fmFsCommit(_fs, FM_ROOM_CLIENTS_FILE);
    }
  }
}

// ---- F1 boot message, F2 bot, F3 box sensor ----

void MyMesh::fmRoomBootPost() {
  if (!fmRoomActive()) return;
  uint32_t reason = board.getResetReason();
#if defined(ESP32)
  if (reason == ESP_RST_DEEPSLEEP) return;   // wake-up, not a restart
#endif
  char msg[80];
  snprintf(msg, sizeof(msg), "Box started (%s)", board.getResetReasonString(reason));
  fmRoomStorePost(_room_id, msg);
}

static bool fmIsCmd(const char* text, const char* cmd) {
  while (*text == ' ') text++;
  size_t n = strlen(cmd);
  if (strncasecmp(text, cmd, n) != 0) return false;
  text += n;
  while (*text == ' ') text++;
  return *text == 0;   // nothing after the command
}

bool MyMesh::fmRoomBot(mesh::Packet* pkt, ClientInfo* client, uint32_t sender_timestamp, const char* text) {
  while (*text == ' ') text++;
  bool is_cli = (text[0] == '/');
  bool is_ping = fmIsCmd(text, "!ping");
  bool is_status = fmIsCmd(text, "!status");
  bool is_help = fmIsCmd(text, "!help");
  bool is_wx = fmIsCmd(text, "!wetter") || fmIsCmd(text, "!weather");
  bool is_cmds = strncasecmp(text, "!commands", 9) == 0 && (text[9] == 0 || text[9] == ' ');   // !commands [topic]
  if (!is_cli && !is_ping && !is_status && !is_help && !is_wx && !is_cmds) return false;   // normal post (also other '!' texts, e.g. a manual !SOS)

  // Admin = this key is a known admin of the REPEATER (same device key for all contacts).
  // The room itself has no admin login.
  ClientInfo* rep = acl.getClient(client->id.pub_key, PUB_KEY_SIZE);
  bool is_admin = rep != NULL && rep->isAdmin();

  if (!is_admin && !_room_bot_limiter.allow(getRTCClock()->getCurrentTime())) return true;   // too many: no reply, no post

  char msg[FM_ROOM_POST_TEXT_LEN+1];
  if (is_cli) {
    if (!is_admin) {
      fmRoomQueuePrivate(client, "Admins only - log in to the repeater as admin with this device first");
      return true;
    }
    char cmd[FM_ROOM_POST_TEXT_LEN+1];
    char reply[200];
    StrHelper::strncpy(cmd, &text[1], sizeof(cmd));
    if (cmd[0] == 0) {
      fmRoomQueuePrivate(client, "Usage: /<command>, e.g. /get radio - !commands for a list");
      return true;
    }
    reply[0] = 0;
    // Remote CLI: sender_timestamp != 0, so serial-only commands (get prv.key, erase, set freq ...) stay blocked.
    handleCommand(sender_timestamp ? sender_timestamp : 1, cmd, reply);
    if (set_radio_at) {
      set_radio_at = futureMillis(8000);   // radio change (e.g. Off-Grid): let the reply reach the admin first
    }
    StrHelper::strncpy(msg, reply[0] ? reply : "OK", sizeof(msg));
    for (char* c = msg; *c; c++) { if (*c == '\n' || *c == '\r') *c = ' '; }   // single line in the chat
    fmRoomQueuePrivate(client, msg);
    return true;
  }
  if (is_help) {   // users: one message
    fmRoomQueuePrivate(client, "!wetter = weather (answer to all), !ping = signal check, !status = box status. Admins: !commands");
    return true;
  }
  if (is_wx) {   // answer to everyone: a post on the board
    fmWxText(msg, sizeof(msg));
    fmRoomStorePost(_room_id, msg);
    return true;
  }
  if (is_cmds) {   // admins: one message per topic
    if (!is_admin) {
      fmRoomQueuePrivate(client, "Admins only - log in to the repeater as admin with this device first. Commands for everyone: !help");
      return true;
    }
    const char* topic = &text[9];
    while (*topic == ' ') topic++;
    const char* m;
    if (strcasecmp(topic, "room") == 0) {
      m = "/set roomserver <name>|on|off, /set room.pass <pw>, /room.reset, /room.clear posts|users|pass|name|all, /get roomserver";
    } else if (strcasecmp(topic, "radio") == 0) {
      m = "/set offgrid on|off, /get offgrid, /get radio. Box position: /set lat <v>, /set lon <v>";
    } else if (strcasecmp(topic, "wifi") == 0) {
      m = "/set wifi on|off, /get wifi. Dashboard: http://<room name>.local or 192.168.4.1, update: Setup > Firmware update";
    } else if (strcasecmp(topic, "sos") == 0) {
      m = "/channel.add #name or <name> <32 hex key>, /channel.del <name>, /get channels, /get sos";
    } else if (strcasecmp(topic, "team") == 0) {
      m = "/team.add <name>, /team.del <name>, /get team, /get nodes";
    } else if (strcasecmp(topic, "weather") == 0) {
      m = "/set weather.loc <lat>,<lon> or box, /get weather. Data comes from phones with internet in the box WiFi";
    } else {
      m = "!commands room|radio|wifi|sos|team|weather - any repeater command: /<command>, e.g. /get radio. Full list: README";
    }
    fmRoomQueuePrivate(client, m);
    return true;
  }

  if (is_ping) {
    char route[16];
    if (pkt->isRouteFlood()) {
      unsigned hops = pkt->getPathHashCount();
      snprintf(route, sizeof(route), "%u hop%s", hops, hops == 1 ? "" : "s");
    } else {
      strcpy(route, "direct");
    }
    snprintf(msg, sizeof(msg), "Pong - SNR %.1f dB, RSSI %d dBm, %s", pkt->getSNR(), (int)radio_driver.getLastRSSI(), route);
  } else {
    unsigned up = (unsigned)(uptime_millis / 1000);
    char env[32] = "";
    float t, h;
    bool has_h;
    if (fmBoxEnv(t, h, has_h)) {
      if (has_h) {
        snprintf(env, sizeof(env), " | Box %.1fC %.0f%%", t, h);
      } else {
        snprintf(env, sizeof(env), " | Box %.1fC", t);
      }
    } else {
      float mcu = board.getMCUTemperature();   // no sensor: chip temperature (not the air in the box)
      if (!isnan(mcu)) snprintf(env, sizeof(env), " | MCU %.0fC", mcu);
    }
    snprintf(msg, sizeof(msg), "Up %ud %uh %um | %s %.3f MHz | %d users, %d posts%s",
             up / 86400, (up / 3600) % 24, (up / 60) % 60,
             _fm.offgrid ? "Off-Grid" : "Normal", _prefs.freq,
             _room_acl.getNumClients(), fmRoomNumPosts(), env);
  }
  fmRoomQueuePrivate(client, msg);
  return true;
}

bool MyMesh::fmBoxEnv(float& temp_c, float& hum_pct, bool& has_hum) {
  bool has_temp = false;
  has_hum = false;
  telemetry.reset();
  sensors.querySensors(TELEM_PERM_ENVIRONMENT, telemetry);   // e.g. BME280 on the I2C of the G2
  LPPReader reader(telemetry.getBuffer(), telemetry.getSize());
  uint8_t ch, type;
  while (reader.readHeader(ch, type)) {
    if (type == LPP_TEMPERATURE && !has_temp) {
      has_temp = reader.readTemperature(temp_c);
    } else if (type == LPP_RELATIVE_HUMIDITY && !has_hum) {
      has_hum = reader.readRelativeHumidity(hum_pct);
    } else {
      reader.skipData(type);
    }
  }
  return has_temp;
}

int MyMesh::fmRoomNumPosts() const {
  int n = 0;
  for (int k = 0; k < FM_ROOM_MAX_POSTS; k++) {
    if (_room_posts[k].post_timestamp != 0) n++;
  }
  return n;
}

void MyMesh::fmRoomReset() {
  fmRoomClearPosts();
  fmRoomClearUsers();
  _fm.room_pass[0] = 0;   // open room again
  fmSavePrefs();
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
