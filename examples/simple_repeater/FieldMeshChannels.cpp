// FieldMesh repeater: channel list of the box (P4: SOS into the room board).
// Only compiled in with -D FIELDMESH_REPEATER=1.
//
// Upstream a repeater decrypts no channels (mesh::Mesh::searchChannelsByHash() returns 0).
// The FieldMesh box keeps a small list of channels it listens to. Forwarding is unchanged:
// mesh::Mesh::onRecvPacket() calls routeRecvPacket() after decrypting either way.
// A channel whose name contains "sos" is an SOS channel (same rule as the companion,
// MyMesh::isSosChannel()); every "!SOS ..." heard there goes onto the room board.

#include "MyMesh.h"
#include "FieldMeshFs.h"

#ifdef FIELDMESH_REPEATER

static bool fmIsSosChannelName(const char* name) {   // case-insensitive "sos" anywhere (companion rule)
  for (const char* p = name; p[0] && p[1] && p[2]; p++) {
    if (tolower(p[0]) == 's' && tolower(p[1]) == 'o' && tolower(p[2]) == 's') return true;
  }
  return false;
}

static void fmChannelCalcHash(FmChannel& ch) {
  // same as BaseChatMesh::addChannel(): hash = sha256 over the 16-byte secret
  mesh::Utils::sha256(ch.channel.hash, sizeof(ch.channel.hash), ch.channel.secret, FM_CHANNEL_KEY_LEN);
}

void MyMesh::fmChannelsLoad() {
  _fm_num_channels = 0;
  if (!fmFsExists(_fs, FM_CHANNELS_FILE)) return;
  File f = fmFsOpenRead(_fs, FM_CHANNELS_FILE);
  if (!f) return;
  uint8_t hdr[3];
  if (f.read(hdr, 3) == 3 && hdr[0] == 'F' && hdr[1] == 'K' && hdr[2] == 1) {
    while (_fm_num_channels < FM_MAX_CHANNELS) {
      FmChannel& ch = _fm_channels[_fm_num_channels];
      memset(&ch, 0, sizeof(ch));
      if (f.read((uint8_t *)ch.name, sizeof(ch.name)) != sizeof(ch.name)) break;
      if (f.read(ch.channel.secret, FM_CHANNEL_KEY_LEN) != FM_CHANNEL_KEY_LEN) break;
      ch.name[sizeof(ch.name) - 1] = 0;
      if (ch.name[0] == 0) continue;
      fmChannelCalcHash(ch);
      _fm_num_channels++;
    }
  }
  f.close();
}

void MyMesh::fmChannelsSave() {
  File f = fmFsOpenWriteTmp(_fs, FM_CHANNELS_FILE);
  if (!f) return;
  uint8_t hdr[3] = { 'F', 'K', 1 };
  bool ok = f.write(hdr, 3) == 3;
  for (int i = 0; ok && i < _fm_num_channels; i++) {
    ok = f.write((uint8_t *)_fm_channels[i].name, sizeof(_fm_channels[i].name)) == sizeof(_fm_channels[i].name)
      && f.write(_fm_channels[i].channel.secret, FM_CHANNEL_KEY_LEN) == FM_CHANNEL_KEY_LEN;
  }
  f.close();
  if (ok) fmFsCommit(_fs, FM_CHANNELS_FILE);
}

int MyMesh::searchChannelsByHash(const uint8_t* hash, mesh::GroupChannel channels[], int max_matches) {
  int n = 0;
  for (int i = 0; i < _fm_num_channels && n < max_matches; i++) {
    if (_fm_channels[i].channel.hash[0] == hash[0]) {
      channels[n++] = _fm_channels[i].channel;
    }
  }
  return n;
}

void MyMesh::onGroupDataRecv(mesh::Packet* packet, uint8_t type, const mesh::GroupChannel& channel, uint8_t* data, size_t len) {
  if (type != PAYLOAD_TYPE_GRP_TXT || len < 5) return;
  if ((data[4] >> 2) != TXT_TYPE_PLAIN) return;

  const FmChannel* ch = NULL;
  for (int i = 0; i < _fm_num_channels; i++) {
    if (memcmp(_fm_channels[i].channel.secret, channel.secret, FM_CHANNEL_KEY_LEN) == 0) {
      ch = &_fm_channels[i];
      break;
    }
  }
  if (ch == NULL || !fmIsSosChannelName(ch->name)) return;   // only SOS channels for now (P4)

  data[len] = 0;   // C string (text may be padded with zeroes)
  const char* text = (const char *)&data[5];   // "<sender>: <message>"
  const char* sos = strstr(text, "!SOS");      // strstr: the sender name is prepended
  if (sos == NULL) return;

  // sender = everything before ": " (never split on " | ", can be part of a node name)
  char sender[32];
  const char* sep = strstr(text, ": ");
  if (sep != NULL && sep < sos) {
    int n = sep - text;
    if (n > (int)sizeof(sender) - 1) n = sizeof(sender) - 1;
    memcpy(sender, text, n);
    sender[n] = 0;
  } else {
    strcpy(sender, "?");
  }
  const char* info = sos + 4;   // e.g. " 48.7636,9.1621 bat78%" or " no GPS bat78%"
  while (*info == ' ') info++;

  // remember for 'get sos' (and later the dashboard)
  FmSosEntry& e = _fm_sos[_fm_sos_next];
  StrHelper::strncpy(e.sender, sender, sizeof(e.sender));
  StrHelper::strncpy(e.info, info, sizeof(e.info));
  e.heard_at = getRTCClock()->getCurrentTime();
  e.heard_millis = millis();
  _fm_sos_next = (_fm_sos_next + 1) % FM_MAX_SOS;

  // onto the room board (everyone logged in gets it pushed)
  if (fmRoomActive()) {
    char msg[FM_ROOM_POST_TEXT_LEN+1];
    snprintf(msg, sizeof(msg), "SOS from %s: %s (%s)", sender, info[0] ? info : "-", ch->name);
    fmRoomStorePost(_room_id, msg);
  }
}

bool MyMesh::fmChannelsHandleCommand(const char* command, char* reply) {
  if (memcmp(command, "channel.add ", 12) == 0) {
    char tmp[100];
    StrHelper::strncpy(tmp, &command[12], sizeof(tmp));
    char* name = tmp;
    while (*name == ' ') name++;
    char* key = strchr(name, ' ');
    if (key) {
      *key++ = 0;
      while (*key == ' ') key++;
      char* end = key + strlen(key);
      while (end > key && end[-1] == ' ') *--end = 0;
    }
    if (name[0] == 0 || strlen(name) >= sizeof(_fm_channels[0].name)) {
      strcpy(reply, "Err - use: channel.add #name  or  channel.add <name> <32 hex key>");
      return true;
    }

    FmChannel ch;
    memset(&ch, 0, sizeof(ch));
    StrHelper::strncpy(ch.name, name, sizeof(ch.name));
    if (key == NULL || key[0] == 0) {
      if (name[0] != '#') {
        strcpy(reply, "Err - private channel needs its key: channel.add <name> <32 hex key>");
        return true;
      }
      // hashtag channel: key = first 16 bytes of sha256("#name")
      mesh::Utils::sha256(ch.channel.secret, FM_CHANNEL_KEY_LEN, (const uint8_t *)name, strlen(name));
    } else if (strlen(key) != FM_CHANNEL_KEY_LEN * 2 || !mesh::Utils::fromHex(ch.channel.secret, FM_CHANNEL_KEY_LEN, key)) {
      strcpy(reply, "Err - key must be 32 hex characters");
      return true;
    }
    fmChannelCalcHash(ch);

    int idx = -1;
    for (int i = 0; i < _fm_num_channels; i++) {
      if (strcmp(_fm_channels[i].name, ch.name) == 0) { idx = i; break; }   // same name: replace
    }
    if (idx < 0) {
      if (_fm_num_channels >= FM_MAX_CHANNELS) {
        sprintf(reply, "Err - max %d channels", FM_MAX_CHANNELS);
        return true;
      }
      idx = _fm_num_channels++;
    }
    _fm_channels[idx] = ch;
    fmChannelsSave();
    sprintf(reply, "OK - channel '%s' added%s", ch.name, fmIsSosChannelName(ch.name) ? " (SOS)" : "");
    return true;
  }
  if (memcmp(command, "channel.del ", 12) == 0) {
    const char* name = &command[12];
    while (*name == ' ') name++;
    for (int i = 0; i < _fm_num_channels; i++) {
      if (strcmp(_fm_channels[i].name, name) == 0) {
        for (int k = i; k < _fm_num_channels - 1; k++) _fm_channels[k] = _fm_channels[k + 1];
        _fm_num_channels--;
        fmChannelsSave();
        sprintf(reply, "OK - channel '%s' removed", name);
        return true;
      }
    }
    strcpy(reply, "Err - no such channel");
    return true;
  }
  if (strcmp(command, "get channels") == 0) {
    if (_fm_num_channels == 0) {
      strcpy(reply, "> none");
      return true;
    }
    char* dp = reply;
    dp += sprintf(dp, "> ");
    for (int i = 0; i < _fm_num_channels; i++) {
      int room = 150 - (dp - reply);
      if (room < 40) break;
      dp += snprintf(dp, room, "%s%s%s", i > 0 ? ", " : "", _fm_channels[i].name,
                     fmIsSosChannelName(_fm_channels[i].name) ? " (SOS)" : "");
    }
    return true;
  }
  if (strcmp(command, "get sos") == 0) {
    int last = (_fm_sos_next + FM_MAX_SOS - 1) % FM_MAX_SOS;
    FmSosEntry& e = _fm_sos[last];
    if (e.heard_millis == 0) {
      strcpy(reply, "> no SOS heard");
    } else {
      unsigned mins = (unsigned)((millis() - e.heard_millis) / 60000UL);
      snprintf(reply, 150, "> %s: %s (%u min ago)", e.sender, e.info[0] ? e.info : "-", mins);
    }
    return true;
  }
  return false;
}

#endif
