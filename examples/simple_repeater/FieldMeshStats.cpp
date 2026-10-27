// FieldMesh repeater: data for the dashboard (P5b).
// Only compiled in with -D FIELDMESH_REPEATER=1.
//
// - Node table: every valid advert the box hears (name, type, hops, SNR, position).
//   PSRAM if available. Full: the node heard longest ago is replaced, team members never.
// - Team (favourites): keys in /fm_team, set by the admin (CLI now, dashboard setup later).
// - Activity: packets in/out and own transmit time in 2-minute buckets, last hour.
// - Packet type counters and a list of the last packets.
// All of it lives in RAM except the team; nothing grows over time.

#include "MyMesh.h"
#include "FieldMeshFs.h"
#include <helpers/AdvertDataHelpers.h>

#ifdef FIELDMESH_REPEATER

#define FM_ENV_INTERVAL   30000   // box sensor read every 30 s (I2C)

void MyMesh::fmStatsBegin() {
  if (_fm_nodes == NULL) {
#if defined(ESP32) && defined(BOARD_HAS_PSRAM)
    _fm_nodes = (FmNode*)ps_malloc(sizeof(FmNode) * FM_MAX_NODES);
    _fm_max_nodes = _fm_nodes ? FM_MAX_NODES : 0;
#endif
    if (_fm_nodes == NULL) {
      _fm_nodes = (FmNode*)malloc(sizeof(FmNode) * FM_MAX_NODES_NO_PSRAM);
      _fm_max_nodes = _fm_nodes ? FM_MAX_NODES_NO_PSRAM : 0;
    }
    if (_fm_nodes) memset(_fm_nodes, 0, sizeof(FmNode) * _fm_max_nodes);
  }
  fmTeamLoad();
  _fm_act_air_mark = getTotalAirTime();
  _fm_act_next = futureMillis(FM_ACT_BUCKET_MS);
}

// ---- node table ----

FmNode* MyMesh::fmNodeFind(const uint8_t* key, int key_len) {
  for (int i = 0; i < _fm_max_nodes; i++) {
    if (_fm_nodes[i].heard_at && memcmp(_fm_nodes[i].pub_key, key, key_len) == 0) return &_fm_nodes[i];
  }
  return NULL;
}

const char* MyMesh::fmNodeNameByHash(uint8_t hash) {
  const char* found = NULL;
  for (int i = 0; i < _fm_max_nodes; i++) {
    if (_fm_nodes[i].heard_at && _fm_nodes[i].pub_key[0] == hash) {
      if (found) return NULL;   // ambiguous (hash collision)
      found = _fm_nodes[i].name;
    }
  }
  return found;
}

void MyMesh::fmNodeHeard(const mesh::Packet* pkt, const mesh::Identity& id, const uint8_t* app_data, size_t app_data_len) {
  if (_fm_nodes == NULL) return;
  if (_room_id_loaded && _room_id.matches(id.pub_key)) return;   // our own room, rebroadcast by others

  AdvertDataParser parser(app_data, app_data_len);
  if (!parser.isValid()) return;

  FmNode* n = fmNodeFind(id.pub_key, PUB_KEY_SIZE);
  if (n == NULL) {
    // free slot, else the node heard longest ago that is not in the team
    FmNode* oldest = NULL;
    for (int i = 0; i < _fm_max_nodes; i++) {
      FmNode* c = &_fm_nodes[i];
      if (c->heard_at == 0) { oldest = c; break; }
      if (fmTeamHas(c->pub_key)) continue;
      if (oldest == NULL || c->heard_at < oldest->heard_at) oldest = c;
    }
    if (oldest == NULL) return;   // only team members in a full table
    n = oldest;
    memset(n, 0, sizeof(*n));
    memcpy(n->pub_key, id.pub_key, PUB_KEY_SIZE);
  }
  if (parser.hasName()) StrHelper::strncpy(n->name, parser.getName(), sizeof(n->name));
  n->type = parser.getType();
  n->hops = pkt->isRouteFlood() ? pkt->getPathHashCount() : 0;
  n->snr4 = (int8_t)(pkt->getSNR() * 4);
  n->heard_at = getRTCClock()->getCurrentTime();
  if (n->adverts < 0xFFFF) n->adverts++;
  if (parser.hasLatLon() && (parser.getIntLat() != 0 || parser.getIntLon() != 0)) {
    n->has_pos = 1;
    n->lat = parser.getIntLat();
    n->lon = parser.getIntLon();
    n->pos_at = n->heard_at;
  }
  if (fmTeamHas(n->pub_key)) _fm_team_dirty = true;   // saved lazily (fmStatsLoop)
}

// ---- team ----

// A known node talked to the room or the repeater (login, post, keep-alive): it is still in
// reach, even if it sends no adverts. Only nodes already in the table (name from an advert, or
// a team member restored after a restart); position stays as it was.
void MyMesh::fmNodeSeen(const uint8_t* pub_key, const mesh::Packet* pkt) {
  if (_fm_nodes == NULL) return;
  FmNode* n = fmNodeFind(pub_key, PUB_KEY_SIZE);
  if (n == NULL) return;
  n->heard_at = getRTCClock()->getCurrentTime();
  if (pkt->isRouteFlood()) {   // direct-routed packets do not tell the hop count
    n->hops = pkt->getPathHashCount();
    if (n->hops == 0) n->snr4 = (int8_t)(pkt->getSNR() * 4);   // SNR is only the node's own with 0 hops
  }
  if (fmTeamHas(n->pub_key)) _fm_team_dirty = true;
}

bool MyMesh::fmTeamHas(const uint8_t* pub_key) const {
  for (int i = 0; i < _fm_num_team; i++) {
    if (memcmp(_fm_team[i], pub_key, PUB_KEY_SIZE) == 0) return true;
  }
  return false;
}

bool MyMesh::fmTeamSet(const uint8_t* pub_key, bool on) {
  for (int i = 0; i < _fm_num_team; i++) {
    if (memcmp(_fm_team[i], pub_key, PUB_KEY_SIZE) == 0) {
      if (on) return true;
      for (int k = i; k < _fm_num_team - 1; k++) memcpy(_fm_team[k], _fm_team[k + 1], PUB_KEY_SIZE);
      _fm_num_team--;
      fmTeamSave();
      return true;
    }
  }
  if (!on) return true;
  if (_fm_num_team >= FM_MAX_TEAM) return false;
  memcpy(_fm_team[_fm_num_team++], pub_key, PUB_KEY_SIZE);
  fmTeamSave();
  return true;
}

void MyMesh::fmTeamLoad() {
  _fm_num_team = 0;
  if (!fmFsExists(_fs, FM_TEAM_FILE)) return;
  File f = fmFsOpenRead(_fs, FM_TEAM_FILE);
  if (!f) return;
  static_assert(sizeof(FmNode) <= 255, "team file stores the record size in one byte");
  uint8_t hdr[3] = { 0, 0, 0 };
  if (f.read(hdr, 3) == 3 && hdr[0] == 'F' && hdr[1] == 'M' && hdr[2] == 1) {   // old format: keys only
    while (_fm_num_team < FM_MAX_TEAM && f.read(_fm_team[_fm_num_team], PUB_KEY_SIZE) == PUB_KEY_SIZE) {
      _fm_num_team++;
    }
  } else if (hdr[0] == 'F' && hdr[1] == 'M' && hdr[2] == 2) {   // key + last node data (FmNode)
    uint8_t rec_size = 0;
    f.read(&rec_size, 1);
    FmNode rec;
    uint8_t skip[32];
    while (_fm_num_team < FM_MAX_TEAM && rec_size >= PUB_KEY_SIZE) {
      memset(&rec, 0, sizeof(rec));
      size_t want = rec_size < sizeof(rec) ? rec_size : sizeof(rec);
      if (f.read((uint8_t*)&rec, want) != want) break;
      for (size_t left = rec_size - want; left > 0; ) {   // newer firmware wrote bigger records
        size_t n = left < sizeof(skip) ? left : sizeof(skip);
        if (f.read(skip, n) != n) { left = 0; break; }
        left -= n;
      }
      memcpy(_fm_team[_fm_num_team++], rec.pub_key, PUB_KEY_SIZE);
      // back into the node table, so the team shows up right after a restart (with its last data)
      if (rec_size == sizeof(FmNode) && rec.heard_at != 0 && _fm_nodes && fmNodeFind(rec.pub_key, PUB_KEY_SIZE) == NULL) {
        for (int i = 0; i < _fm_max_nodes; i++) {
          if (_fm_nodes[i].heard_at == 0) { _fm_nodes[i] = rec; break; }
        }
      }
    }
  }
  f.close();
}

void MyMesh::fmTeamSave() {
  File f = fmFsOpenWriteTmp(_fs, FM_TEAM_FILE);
  if (!f) return;
  // format 2: header, record size, then one FmNode per team member (key + name, type,
  // position, last heard). Only the key is filled if the member is not in the table.
  uint8_t hdr[4] = { 'F', 'M', 2, (uint8_t)sizeof(FmNode) };
  bool ok = f.write(hdr, 4) == 4;
  for (int i = 0; ok && i < _fm_num_team; i++) {
    FmNode rec;
    FmNode* n = fmNodeFind(_fm_team[i], PUB_KEY_SIZE);
    if (n) {
      rec = *n;
    } else {
      memset(&rec, 0, sizeof(rec));
      memcpy(rec.pub_key, _fm_team[i], PUB_KEY_SIZE);
    }
    ok = f.write((uint8_t*)&rec, sizeof(rec)) == sizeof(rec);
  }
  f.close();
  if (ok) fmFsCommit(_fs, FM_TEAM_FILE);
  _fm_team_dirty = false;
  _fm_team_next_save = futureMillis(FM_TEAM_SAVE_INTERVAL);
}

// ---- counters ----

void MyMesh::fmStatsRx(const mesh::Packet* pkt) {
  FmActBucket& b = _fm_act[_fm_act_idx];
  if (b.rx < 0xFFFF) b.rx++;
  uint8_t type = pkt->getPayloadType() & 0x0F;
  _fm_type_counts[type]++;

  FmFeedEntry& e = _fm_feed[_fm_feed_next];
  memset(&e, 0, sizeof(e));
  e.at = getRTCClock()->getCurrentTime();
  e.type = type;
  e.flood = pkt->isRouteFlood() ? 1 : 0;
  e.hops = pkt->getPathHashCount();
  e.snr4 = (int8_t)(pkt->getSNR() * 4);
  if ((type == PAYLOAD_TYPE_REQ || type == PAYLOAD_TYPE_RESPONSE || type == PAYLOAD_TYPE_TXT_MSG || type == PAYLOAD_TYPE_PATH)
      && pkt->payload_len > 2) {
    e.src_hash = pkt->payload[1];   // dest hash, src hash, ...
    e.has_src = 1;
  } else if (type == PAYLOAD_TYPE_ADVERT && pkt->payload_len >= PUB_KEY_SIZE) {
    memcpy(e.advert_key, pkt->payload, sizeof(e.advert_key));
    e.has_src = 2;
  }
  _fm_feed_next = (_fm_feed_next + 1) % FM_FEED_SIZE;
}

void MyMesh::fmStatsTx(const mesh::Packet* pkt) {
  FmActBucket& b = _fm_act[_fm_act_idx];
  if (b.tx < 0xFFFF) b.tx++;
}

uint32_t MyMesh::fmDutyMillisLastHour() {
  uint32_t sum = getTotalAirTime() - _fm_act_air_mark;   // current bucket so far
  for (int i = 0; i < FM_ACT_BUCKETS; i++) {
    if (i != _fm_act_idx) sum += _fm_act[i].tx_air_ms;
  }
  return sum;
}

void MyMesh::fmStatsLoop() {
  if (_fm_team_dirty && (_fm_team_next_save == 0 || millisHasNowPassed(_fm_team_next_save))) {
    fmTeamSave();
  }

  if (millisHasNowPassed(_fm_act_next)) {
    _fm_act_next = futureMillis(FM_ACT_BUCKET_MS);
    unsigned long air = getTotalAirTime();
    _fm_act[_fm_act_idx].tx_air_ms = air - _fm_act_air_mark;
    _fm_act_air_mark = air;
    _fm_act_idx = (_fm_act_idx + 1) % FM_ACT_BUCKETS;
    memset(&_fm_act[_fm_act_idx], 0, sizeof(FmActBucket));
  }

  if (_fm_env_next == 0 || millisHasNowPassed(_fm_env_next)) {
    _fm_env_next = futureMillis(FM_ENV_INTERVAL);
    float t = 0, h = 0;
    bool has_h = false;
    if (fmBoxEnv(t, h, has_h)) {
      _fm_env_temp = t;
      _fm_env_hum = h;
      _fm_env_flags = 1 | (has_h ? 2 : 0);
    } else {
      float mcu = board.getMCUTemperature();
      if (!isnan(mcu)) {
        _fm_env_temp = mcu;
        _fm_env_flags = 4;
      } else {
        _fm_env_flags = 0;
      }
    }
  }
}

// ---- CLI ----

static bool fmNameEq(const char* a, const char* b) {
  return strcasecmp(a, b) == 0;
}

bool MyMesh::fmStatsHandleCommand(const char* command, char* reply) {
  bool add = memcmp(command, "team.add ", 9) == 0;
  bool del = memcmp(command, "team.del ", 9) == 0;
  if (add || del) {
    const char* name = &command[9];
    while (*name == ' ') name++;
    for (int i = 0; i < _fm_max_nodes; i++) {
      if (_fm_nodes[i].heard_at && fmNameEq(_fm_nodes[i].name, name)) {
        if (fmTeamSet(_fm_nodes[i].pub_key, add)) {
          sprintf(reply, "OK - '%s' %s team", _fm_nodes[i].name, add ? "added to" : "removed from");
        } else {
          sprintf(reply, "Err - max %d team members", FM_MAX_TEAM);
        }
        return true;
      }
    }
    strcpy(reply, "Err - node not heard yet (name as in its advert)");
    return true;
  }
  if (memcmp(command, "team.key ", 9) == 0) {   // team.key <first 6 bytes as hex> on|off (dashboard)
    char hex[13];
    StrHelper::strncpy(hex, &command[9], sizeof(hex));
    const char* arg = strchr(&command[9], ' ');
    uint8_t key[6];
    if (strlen(hex) < 12 || arg == NULL || !mesh::Utils::fromHex(key, 6, hex)) {
      strcpy(reply, "Err - use: team.key <12 hex> on|off");
      return true;
    }
    while (*arg == ' ') arg++;
    bool on = strcmp(arg, "on") == 0;
    FmNode* n = fmNodeFind(key, 6);
    if (n == NULL) {
      strcpy(reply, "Err - node not in the table");
    } else if (fmTeamSet(n->pub_key, on)) {
      sprintf(reply, "OK - '%s' %s team", n->name, on ? "added to" : "removed from");
    } else {
      sprintf(reply, "Err - max %d team members", FM_MAX_TEAM);
    }
    return true;
  }
  if (strcmp(command, "get team") == 0) {
    char* dp = reply;
    dp += sprintf(dp, "> %d: ", _fm_num_team);
    for (int i = 0; i < _fm_num_team; i++) {
      int room = 150 - (dp - reply);
      if (room < 36) break;
      FmNode* n = fmNodeFind(_fm_team[i], PUB_KEY_SIZE);
      if (n && n->name[0]) {
        dp += snprintf(dp, room, "%s%s", i ? ", " : "", n->name);
      } else {
        dp += snprintf(dp, room, "%s%02X%02X%02X", i ? ", " : "", _fm_team[i][0], _fm_team[i][1], _fm_team[i][2]);
      }
    }
    return true;
  }
  if (strcmp(command, "get nodes") == 0) {
    int count = 0, with_pos = 0;
    for (int i = 0; i < _fm_max_nodes; i++) {
      if (_fm_nodes[i].heard_at) {
        count++;
        if (_fm_nodes[i].has_pos) with_pos++;
      }
    }
    sprintf(reply, "> %d nodes heard (%d with position), table %d", count, with_pos, _fm_max_nodes);
    return true;
  }
  return false;
}

#endif
