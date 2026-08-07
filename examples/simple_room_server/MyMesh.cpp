#include "MyMesh.h"
#include <algorithm>
#include <helpers/RxReservePacketManager.h>

#define REPLY_DELAY_MILLIS          1500
#define PUSH_NOTIFY_DELAY_MILLIS    2000
#define SYNC_PUSH_INTERVAL          1200

#define PUSH_ACK_TIMEOUT_FLOOD      12000
#define PUSH_TIMEOUT_BASE           4000
#define PUSH_ACK_TIMEOUT_FACTOR     2000

#define POST_SYNC_DELAY_SECS        6

#define FIRMWARE_VER_LEVEL       1

#define REQ_TYPE_GET_STATUS         0x01 // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE         0x02
#define REQ_TYPE_GET_TELEMETRY_DATA 0x03
#define REQ_TYPE_GET_ACCESS_LIST    0x05

#define RESP_SERVER_LOGIN_OK        0 // response to ANON_REQ

#define LAZY_CONTACTS_WRITE_DELAY    5000

struct ServerStats {
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
  uint16_t err_events; // was 'n_full_events'
  int16_t last_snr;    // x 4
  uint16_t n_direct_dups, n_flood_dups;
  uint16_t n_posted, n_post_push;
};

void MyMesh::addPost(ClientInfo *client, const char *postData) {
  storePost(client->id, postData);
}

void MyMesh::addSystemPost(const char *postData) {
  if (!postData || postData[0] == 0) return;

  MESH_DEBUG_PRINTLN("room.post: addSystemPost: %s", postData);

  storePost(self_id, postData);
}

void MyMesh::storePost(const mesh::Identity &author, const char *postData) {
  int idx = next_post_idx;
  // TODO: suggested postData format: <title>/<descrption>
  posts[idx].author = author; // add to cyclic queue
  StrHelper::strncpy(posts[idx].text, postData, MAX_POST_TEXT_LEN);

  posts[idx].post_timestamp = getRTCClock()->getCurrentTimeUnique();
  MESH_DEBUG_PRINTLN("room.post: storePost idx=%d text=%s", idx, posts[idx].text);
  MESH_DEBUG_PRINTLN("room.post: timestamp=%u", posts[idx].post_timestamp);
  next_post_idx = (next_post_idx + 1) % MAX_UNSYNCED_POSTS;

  next_push = futureMillis(PUSH_NOTIFY_DELAY_MILLIS);
  _num_posted++; // stats
  MESH_DEBUG_PRINTLN("room.post: next_post_idx=%d num_posted=%d push scheduled", next_post_idx, _num_posted);
}

void MyMesh::pushPostToClient(ClientInfo *client, PostInfo &post) {
  MESH_DEBUG_PRINTLN("room.post: pushPostToClient text=%s", post.text);
  int len = 0;
  memcpy(&reply_data[len], &post.post_timestamp, 4);
  len += 4; // this is a PAST timestamp... but should be accepted by client

  uint8_t attempt;
  getRNG()->random(&attempt, 1); // need this for re-tries, so packet hash (and ACK) will be different
  reply_data[len++] = (TXT_TYPE_SIGNED_PLAIN << 2) | (attempt & 3); // 'signed' plain text

  // encode prefix of post.author.pub_key
  memcpy(&reply_data[len], post.author.pub_key, 4);
  len += 4; // just first 4 bytes

  int text_len = strlen(post.text);
  memcpy(&reply_data[len], post.text, text_len);
  len += text_len;

  // calc expected ACK reply
  mesh::Utils::sha256((uint8_t *)&client->extra.room.pending_ack, 4, reply_data, len, client->id.pub_key, PUB_KEY_SIZE);
  client->extra.room.push_post_timestamp = post.post_timestamp;

  auto reply = createDatagram(PAYLOAD_TYPE_TXT_MSG, client->id, client->shared_secret, reply_data, len);
  if (reply) {
    if (client->out_path_len == OUT_PATH_UNKNOWN) {
      unsigned long delay_millis = 0;
      sendFloodScoped(default_scope, reply, delay_millis, _prefs.path_hash_mode + 1); // REVISIT
      client->extra.room.ack_timeout = futureMillis(PUSH_ACK_TIMEOUT_FLOOD);
    } else {
      sendDirect(reply, client->out_path, client->out_path_len);

      uint8_t path_hash_count = client->out_path_len & 63;
      client->extra.room.ack_timeout = futureMillis(PUSH_TIMEOUT_BASE + PUSH_ACK_TIMEOUT_FACTOR * (path_hash_count + 1));
    }
    _num_post_pushes++; // stats
  } else {
    client->extra.room.pending_ack = 0;
    MESH_DEBUG_PRINTLN("Unable to push post to client");
  }
}

uint8_t MyMesh::getUnsyncedCount(ClientInfo *client) {
  uint8_t count = 0;
  for (int k = 0; k < MAX_UNSYNCED_POSTS; k++) {
    if (posts[k].post_timestamp > client->extra.room.sync_since // is new post for this Client?
        && !posts[k].author.matches(client->id)) {   // don't push posts to the author
      count++;
    }
  }
  return count;
}

bool MyMesh::processAck(const uint8_t *data) {
  for (int i = 0; i < acl.getNumClients(); i++) {
    auto client = acl.getClientByIdx(i);
    if (client->extra.room.pending_ack && memcmp(data, &client->extra.room.pending_ack, 4) == 0) { // got an ACK from Client!
      client->extra.room.pending_ack = 0; // clear this, so next push can happen
      client->extra.room.push_failures = 0;
      client->extra.room.sync_since = client->extra.room.push_post_timestamp; // advance Client's SINCE timestamp, to sync next post
      return true;
    }
  }
  return false;
}

mesh::Packet *MyMesh::createSelfAdvert() {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE];
  uint8_t app_data_len = _cli.buildAdvertData(ADV_TYPE_ROOM, app_data);

  return createAdvert(self_id, app_data, app_data_len);
}

File MyMesh::openAppend(const char *fname) {
#if defined(NRF52_PLATFORM)
  return _fs->open(fname, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return _fs->open(fname, "a");
#else
  return _fs->open(fname, "a", true);
#endif
}

int MyMesh::handleRequest(ClientInfo *sender, uint32_t sender_timestamp, uint8_t *payload,
                          size_t payload_len) {
  // uint32_t now = getRTCClock()->getCurrentTimeUnique();
  // memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
  memcpy(reply_data, &sender_timestamp, 4); // reflect sender_timestamp back in response packet (kind of like a 'tag')

  if (payload[0] == REQ_TYPE_GET_STATUS) {
    ServerStats stats;
    stats.batt_milli_volts = board.getBattMilliVolts();
    stats.curr_tx_queue_len = _mgr->getOutboundCount(0xFFFFFFFF);
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
    stats.n_posted = _num_posted;
    stats.n_post_push = _num_post_pushes;

    memcpy(&reply_data[4], &stats, sizeof(stats));
    return 4 + sizeof(stats);
  }
  if (payload[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
    uint8_t perm_mask = ~(payload[1]); // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions

    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    // query other sensors -- target specific
    if ((sender->permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) {
      perm_mask = 0x00;  // just base telemetry allowed
    }
    sensors.querySensors(perm_mask, telemetry);

    // This default temperature will be overridden by external sensors (if any)
    float temperature = board.getMCUTemperature();
    if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
      telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
    }

    uint8_t tlen = telemetry.getSize();
    memcpy(&reply_data[4], telemetry.getBuffer(), tlen);
    return 4 + tlen; // reply_len
  }
  if (payload[0] == REQ_TYPE_GET_ACCESS_LIST && sender->isAdmin()) {
    uint8_t res1 = payload[1];   // reserved for future  (extra query params)
    uint8_t res2 = payload[2];
    if (res1 == 0 && res2 == 0) {
      uint8_t ofs = 4;
      for (int i = 0; i < acl.getNumClients() && ofs + 7 <= sizeof(reply_data) - 4; i++) {
        auto c = acl.getClientByIdx(i);
        if (!c->isAdmin()) continue;  // skip non-Admin entries
        memcpy(&reply_data[ofs], c->id.pub_key, 6); ofs += 6;  // just 6-byte pub_key prefix
        reply_data[ofs++] = c->permissions;
      }
      return ofs;
    }
  }
  return 0; // unknown command
}

void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
#if MESH_PACKET_LOGGING
  if (Serial.availableForWrite() > 0) {
    Serial.print(getLogDateTime());
    Serial.print(" RAW: ");
    mesh::Utils::printHex(Serial, raw, len);
    Serial.println();
  }
#endif

#ifdef WITH_MQTT_BRIDGE
  if (_prefs.bridge_enabled) {
    // Store raw radio data for MQTT messages (same as repeater)
    if (bridge) bridge->storeRawRadioData(raw, len, snr, rssi);
  }
#endif
}

void MyMesh::logRx(mesh::Packet *pkt, int len, float score) {
#ifdef WITH_MQTT_BRIDGE
  // MQTT bridge: always feed RX packets — bridge decides based on mqtt.rx setting
  if (_prefs.bridge_enabled && bridge) bridge->onPacketReceived(pkt);
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": RX, len=%d (type=%d, route=%s, payload_len=%d) SNR=%d RSSI=%d score=%d", len,
               pkt->getPayloadType(), pkt->isRouteDirect() ? "D" : "F", pkt->payload_len,
               (int)_radio->getLastSNR(), (int)_radio->getLastRSSI(), (int)(score * 1000));

      if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ ||
          pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
      } else {
        f.printf("\n");
      }
      f.close();
    }
  }
}
void MyMesh::logTx(mesh::Packet *pkt, int len) {
#ifdef WITH_MQTT_BRIDGE
  // MQTT bridge: always feed TX packets — bridge decides based on mqtt.tx setting
  if (_prefs.bridge_enabled && bridge) bridge->sendPacket(pkt);
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": TX, len=%d (type=%d, route=%s, payload_len=%d)", len, pkt->getPayloadType(),
               pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);

      if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ ||
          pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
      } else {
        f.printf("\n");
      }
      f.close();
    }
  }
}
void MyMesh::logTxFail(mesh::Packet *pkt, int len) {
  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": TX FAIL!, len=%d (type=%d, route=%s, payload_len=%d)\n", len, pkt->getPayloadType(),
               pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);
      f.close();
    }
  }
}

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
  if (_prefs.rx_delay_base <= 0.0f) return 0;
  return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

const char *MyMesh::getLogDateTime() {
  static char tmp[32];
  uint32_t now = getRTCClock()->getCurrentTime();
  DateTime dt = DateTime(now);
  sprintf(tmp, "%02d:%02d:%02d - %d/%d/%d U", dt.hour(), dt.minute(), dt.second(), dt.day(), dt.month(),
          dt.year());
  return tmp;
}

uint32_t MyMesh::getRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}
uint32_t MyMesh::getDirectRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.direct_tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}

bool MyMesh::allowPacketForward(const mesh::Packet *packet) {
  if (_prefs.disable_fwd) return false;
  if (packet->isRouteFlood()) {
    if (packet->getPathHashCount() >= _prefs.flood_max) return false;
    if (packet->getRouteType() == ROUTE_TYPE_FLOOD && packet->getPathHashCount() >= _prefs.flood_max_unscoped) return false;
    if (packet->getPayloadType() == PAYLOAD_TYPE_ADVERT && packet->getPathHashCount() >= _prefs.flood_max_advert) return false;
  }
  return true;
}

mesh::DispatcherAction MyMesh::onRecvPacket(mesh::Packet* pkt) {
  if (pkt->getRouteType() == ROUTE_TYPE_TRANSPORT_FLOOD) {
    recv_pkt_region = region_map.findMatch(pkt, REGION_DENY_FLOOD);
  } else if (pkt->getRouteType() == ROUTE_TYPE_FLOOD) {
    if (region_map.getWildcard().flags & REGION_DENY_FLOOD) {
      recv_pkt_region = NULL;
    } else {
      recv_pkt_region =  &region_map.getWildcard();
    }
  } else {
    recv_pkt_region = NULL;
  }
  return Mesh::onRecvPacket(pkt);
}

void MyMesh::onAnonDataRecv(mesh::Packet *packet, const uint8_t *secret, const mesh::Identity &sender,
                            uint8_t *data, size_t len) {
  if (packet->getPayloadType() == PAYLOAD_TYPE_ANON_REQ) { // received an initial request by a possible admin
                                                           // client (unknown at this stage)
    uint32_t sender_timestamp, sender_sync_since;
    memcpy(&sender_timestamp, data, 4);
    memcpy(&sender_sync_since, &data[4], 4); // sender's "sync messags SINCE x" timestamp

    data[len] = 0;                                        // ensure null terminator

    ClientInfo* client = NULL;
    if (data[8] == 0) {   // blank password, just check if sender is in ACL
      client = acl.getClient(sender.pub_key, PUB_KEY_SIZE);
      if (client == NULL) {
      #if MESH_DEBUG
        MESH_DEBUG_PRINTLN("Login, sender not in ACL");
      #endif
      }
    }
    if (client == NULL) {
      uint8_t perm;
      if (strcmp((char *)&data[8], _prefs.password) == 0) { // check for valid admin password
        perm = PERM_ACL_ADMIN;
      } else {
        if (strcmp((char *)&data[8], _prefs.guest_password) == 0) {   // check the room/public password
          perm = PERM_ACL_READ_WRITE;
        } else if (_prefs.allow_read_only) {
          perm = PERM_ACL_GUEST;
        } else {
          MESH_DEBUG_PRINTLN("Incorrect room password");
          return; // no response. Client will timeout
        }
      }

      client = acl.putClient(sender, 0);  // add to known clients (if not already known)
      if (sender_timestamp <= client->last_timestamp) {
        MESH_DEBUG_PRINTLN("possible replay attack!");
        return;
      }

      MESH_DEBUG_PRINTLN("Login success!");
      client->last_timestamp = sender_timestamp;
      client->extra.room.sync_since = sender_sync_since;
      client->extra.room.pending_ack = 0;
      client->extra.room.push_failures = 0;

      client->last_activity = getRTCClock()->getCurrentTime();
      client->permissions &= ~0x03;
      client->permissions |= perm;
      memcpy(client->shared_secret, secret, PUB_KEY_SIZE);

      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
    }

    if (packet->isRouteFlood()) {
      client->out_path_len = OUT_PATH_UNKNOWN;  // need to rediscover out_path
    }

    uint32_t now = getRTCClock()->getCurrentTimeUnique();
    memcpy(reply_data, &now, 4); // response packets always prefixed with timestamp
    // TODO: maybe reply with count of messages waiting to be synced for THIS client?
    reply_data[4] = RESP_SERVER_LOGIN_OK;
    reply_data[5] = 0; // Legacy: was recommended keep-alive interval (secs / 16)
    reply_data[6] = (client->isAdmin() ? 1 : (client->permissions == 0 ? 2 : 0));
    // LEGACY: reply_data[7] = getUnsyncedCount(client);
    reply_data[7] = client->permissions; // NEW
    getRNG()->random(&reply_data[8], 4);   // random blob to help packet-hash uniqueness
    reply_data[12] = FIRMWARE_VER_LEVEL;  // New field

    next_push = futureMillis(PUSH_NOTIFY_DELAY_MILLIS); // delay next push, give RESPONSE packet time to arrive first

    if (packet->isRouteFlood()) {
      // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
      mesh::Packet *path = createPathReturn(sender, client->shared_secret, packet->path, packet->path_len,
                                            PAYLOAD_TYPE_RESPONSE, reply_data, 13);
      if (path) sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
    } else {
      mesh::Packet *reply = createDatagram(PAYLOAD_TYPE_RESPONSE, sender, client->shared_secret, reply_data, 13);
      if (reply) {
        if (client->out_path_len != OUT_PATH_UNKNOWN) { // we have an out_path, so send DIRECT
          sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
        } else {
          sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
        }
      }
    }
  }
}

int MyMesh::searchPeersByHash(const uint8_t *hash) {
  int n = 0;
#if defined(WITH_MQTT_NEIGHBORS)
  if (neighbor_discover_active) {
    for (int i = 0; i < neighbor_discover_count && n < MAX_CLIENTS; i++) {
      auto& nb = neighbours[neighbor_discover[i].neighbour_idx];
      // ACL clients already have a matching peer entry and shared secret. Adding
      // a second overlay entry would decrypt first and intercept their normal
      // CLI/request traffic for the duration of discovery.
      if (acl.getClient(nb.id.pub_key, PUB_KEY_SIZE) != nullptr) continue;
      if (nb.heard_timestamp > 0 && nb.id.isHashMatch(hash)) {
        matching_peer_indexes[n++] = NEIGHBOR_DISCOVER_PEER_BASE + i;
      }
    }
  }
#endif
  for (int i = 0; i < acl.getNumClients() && n < MAX_CLIENTS; i++) {
    if (acl.getClientByIdx(i)->id.isHashMatch(hash)) {
      matching_peer_indexes[n++] = i; // store the INDEXES of matching contacts (for subsequent 'peer' methods)
    }
  }
  return n;
}

void MyMesh::getPeerSharedSecret(uint8_t *dest_secret, int peer_idx) {
  int i = matching_peer_indexes[peer_idx];
#if defined(WITH_MQTT_NEIGHBORS)
  if (neighbor_discover_active && i >= NEIGHBOR_DISCOVER_PEER_BASE) {
    int oi = i - NEIGHBOR_DISCOVER_PEER_BASE;
    if (oi >= 0 && oi < neighbor_discover_count) {
      self_id.calcSharedSecret(dest_secret, neighbours[neighbor_discover[oi].neighbour_idx].id);
      return;
    }
  }
#endif
  if (i >= 0 && i < acl.getNumClients()) {
    // lookup pre-calculated shared_secret
    memcpy(dest_secret, acl.getClientByIdx(i)->shared_secret, PUB_KEY_SIZE);
  } else {
    MESH_DEBUG_PRINTLN("getPeerSharedSecret: Invalid peer idx: %d", i);
  }
}

void MyMesh::onPeerDataRecv(mesh::Packet *packet, uint8_t type, int sender_idx, const uint8_t *secret,
                            uint8_t *data, size_t len) {
  int i = matching_peer_indexes[sender_idx];
#if defined(WITH_MQTT_NEIGHBORS)
  if (neighbor_discover_active && i >= NEIGHBOR_DISCOVER_PEER_BASE) {
    int oi = i - NEIGHBOR_DISCOVER_PEER_BASE;
    if (type == PAYLOAD_TYPE_RESPONSE && oi >= 0 && oi < neighbor_discover_count) {
      handleNeighborDiscoverResponse(oi, data, len);
    }
    return;
  }
#endif
  if (i < 0 || i >= acl.getNumClients()) { // get from our known_clients table (sender SHOULD already be known in this context)
    MESH_DEBUG_PRINTLN("onPeerDataRecv: invalid peer idx: %d", i);
    return;
  }
  auto client = acl.getClientByIdx(i);

#if defined(WITH_MQTT_NEIGHBORS)
  // Neighbors that are already ACL clients are intentionally not added to the
  // temporary peer overlay above. Consume only a matching discovery response
  // here, then leave every other payload on the normal ACL path.
  if (neighbor_discover_active && type == PAYLOAD_TYPE_RESPONSE) {
    for (int oi = 0; oi < neighbor_discover_count; oi++) {
      auto& nb = neighbours[neighbor_discover[oi].neighbour_idx];
      if (client->id.matches(nb.id) && handleNeighborDiscoverResponse(oi, data, len)) {
        return;
      }
    }
  }
#endif

  if (type == PAYLOAD_TYPE_TXT_MSG && len > 5) { // a CLI command or new Post
    uint32_t sender_timestamp;
    memcpy(&sender_timestamp, data, 4); // timestamp (by sender's RTC clock - which could be wrong)
    uint8_t flags = (data[4] >> 2);        // message attempt number, and other flags

    if (!(flags == TXT_TYPE_PLAIN || flags == TXT_TYPE_CLI_DATA)) {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: unsupported command flags received: flags=%02x", (uint32_t)flags);
    } else if (sender_timestamp >= client->last_timestamp) { // prevent replay attacks, but send Acks for retries
      bool is_retry = (sender_timestamp == client->last_timestamp);
      client->last_timestamp = sender_timestamp;

      uint32_t now = getRTCClock()->getCurrentTimeUnique();
      client->last_activity = now;
      client->extra.room.push_failures = 0; // reset so push can resume (if prev failed)

      // len can be > original length, but 'text' will be padded with zeroes
      data[len] = 0; // need to make a C string again, with null terminator

      uint32_t ack_hash; // calc truncated hash of the message timestamp + text + sender pub_key, to prove to
                         // sender that we got it
      mesh::Utils::sha256((uint8_t *)&ack_hash, 4, data, 5 + strlen((char *)&data[5]), client->id.pub_key,
                          PUB_KEY_SIZE);

      uint8_t temp[166];
      bool send_ack;
      if (flags == TXT_TYPE_CLI_DATA) {
        if (client->isAdmin()) {
          if (is_retry) {
            temp[5] = 0; // no reply
          } else {
            handleCommand(sender_timestamp, (char *)&data[5], (char *)&temp[5]);
            temp[4] = (TXT_TYPE_CLI_DATA << 2); // attempt and flags,  (NOTE: legacy was: TXT_TYPE_PLAIN)
          }
          send_ack = false;
        } else {
          temp[5] = 0;      // no reply
          send_ack = false; // and no ACK...  user shoudn't be sending these
        }
      } else { // TXT_TYPE_PLAIN
        if ((client->permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) {
          temp[5] = 0;      // no reply
          send_ack = false; // no ACK
        } else {
          if (!is_retry) {
            addPost(client, (const char *)&data[5]);
          }
          temp[5] = 0; // no reply (ACK is enough)
          send_ack = true;
        }
      }

      uint32_t delay_millis;
      if (send_ack) {
        if (client->out_path_len == OUT_PATH_UNKNOWN) {
          mesh::Packet *ack = createAck(ack_hash);
          if (ack) sendFloodReply(ack, TXT_ACK_DELAY, packet->getPathHashSize());
          delay_millis = TXT_ACK_DELAY + REPLY_DELAY_MILLIS;
        } else {
          uint32_t d = TXT_ACK_DELAY;
          if (getExtraAckTransmitCount() > 0) {
            mesh::Packet *a1 = createMultiAck(ack_hash, 1);
            if (a1) sendDirect(a1, client->out_path, client->out_path_len, d);
            d += 300;
          }

          mesh::Packet *a2 = createAck(ack_hash);
          if (a2) sendDirect(a2, client->out_path, client->out_path_len, d);
          delay_millis = d + REPLY_DELAY_MILLIS;
        }
      } else {
        delay_millis = 0;
      }

      int text_len = strlen((char *)&temp[5]);
      if (text_len > 0) {
        if (now == sender_timestamp) {
          // WORKAROUND: the two timestamps need to be different, in the CLI view
          now++;
        }
        memcpy(temp, &now, 4); // mostly an extra blob to help make packet_hash unique

        // calc expected ACK reply
        // mesh::Utils::sha256((uint8_t *)&expected_ack_crc, 4, temp, 5 + text_len, self_id.pub_key,
        // PUB_KEY_SIZE);

        auto reply = createDatagram(PAYLOAD_TYPE_TXT_MSG, client->id, secret, temp, 5 + text_len);
        if (reply) {
          if (client->out_path_len == OUT_PATH_UNKNOWN) {
            sendFloodReply(reply, delay_millis + SERVER_RESPONSE_DELAY, packet->getPathHashSize());
          } else {
            sendDirect(reply, client->out_path, client->out_path_len, delay_millis + SERVER_RESPONSE_DELAY);
          }
        }
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
    }
  } else if (type == PAYLOAD_TYPE_REQ && len >= 5) {
    uint32_t sender_timestamp;
    memcpy(&sender_timestamp, data, 4); // timestamp (by sender's RTC clock - which could be wrong)
    if (sender_timestamp < client->last_timestamp) { // prevent replay attacks
      MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
    } else {
      client->last_timestamp = sender_timestamp;

      uint32_t now = getRTCClock()->getCurrentTime();
      client->last_activity = now; // <-- THIS will keep client connection alive
      client->extra.room.push_failures = 0;   // reset so push can resume (if prev failed)

      if (data[4] == REQ_TYPE_KEEP_ALIVE && packet->isRouteDirect()) { // request type
        uint32_t forceSince = 0;
        if (len >= 9) {                     // optional - last post_timestamp client received
          memcpy(&forceSince, &data[5], 4); // NOTE: this may be 0, if part of decrypted PADDING!
        } else {
          memcpy(&data[5], &forceSince, 4); // make sure there are zeroes in payload (for ack_hash calc below)
        }
        if (forceSince > 0) {
          client->extra.room.sync_since = forceSince; // force-update the 'sync since'
        }

        client->extra.room.pending_ack = 0;

        // TODO: Throttle KEEP_ALIVE requests!
        // if client sends too quickly, evict()

        // RULE: only send keep_alive response DIRECT!
        if (client->out_path_len != OUT_PATH_UNKNOWN) {
          uint32_t ack_hash; // calc ACK to prove to sender that we got request
          mesh::Utils::sha256((uint8_t *)&ack_hash, 4, data, 9, client->id.pub_key, PUB_KEY_SIZE);

          auto reply = createAck(ack_hash);
          if (reply) {
            reply->payload[reply->payload_len++] = getUnsyncedCount(client); // NEW: add unsynced counter to end of ACK packet
            sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
          }
        }
      } else {
        int reply_len = handleRequest(client, sender_timestamp, &data[4], len - 4);
        if (reply_len > 0) { // valid command
          if (packet->isRouteFlood()) {
            // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
            mesh::Packet *path = createPathReturn(client->id, secret, packet->path, packet->path_len,
                                                  PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
            if (path) sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
          } else {
            mesh::Packet *reply = createDatagram(PAYLOAD_TYPE_RESPONSE, client->id, secret, reply_data, reply_len);
            if (reply) {
              if (client->out_path_len != OUT_PATH_UNKNOWN) { // we have an out_path, so send DIRECT
                sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
              } else {
                sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
              }
            }
          }
        }
      }
    }
  }
}

bool MyMesh::onPeerPathRecv(mesh::Packet *packet, int sender_idx, const uint8_t *secret, uint8_t *path,
                            uint8_t path_len, uint8_t extra_type, uint8_t *extra, uint8_t extra_len) {
  // TODO: prevent replay attacks
  int i = matching_peer_indexes[sender_idx];

  if (i >= 0 && i < acl.getNumClients()) { // get from our known_clients table (sender SHOULD already be known in this context)
    MESH_DEBUG_PRINTLN("PATH to client, path_len=%d", (uint32_t)path_len);
    auto client = acl.getClientByIdx(i);
    client->out_path_len = mesh::Packet::copyPath(client->out_path, path, path_len); // store a copy of path, for sendDirect()
    client->last_activity = getRTCClock()->getCurrentTime();
  } else {
    MESH_DEBUG_PRINTLN("onPeerPathRecv: invalid peer idx: %d", i);
  }

  if (extra_type == PAYLOAD_TYPE_ACK && extra_len >= 4) {
    // also got an encoded ACK!
    processAck(extra);
  }

  // NOTE: no reciprocal path send!!
  return false;
}

void MyMesh::onAckRecv(mesh::Packet *packet, uint32_t ack_crc) {
  if (processAck((uint8_t *)&ack_crc)) {
    packet->markDoNotRetransmit(); // ACK was for this node, so don't retransmit
  }
}

#if defined(WITH_MQTT_NEIGHBORS)

#define CTL_TYPE_NODE_DISCOVER_REQ   0x80
#define CTL_TYPE_NODE_DISCOVER_RESP  0x90

void MyMesh::putNeighbour(const mesh::Identity &id, uint32_t timestamp, float snr) {
  // find existing neighbour, else use least recently updated
  uint32_t oldest_timestamp = 0xFFFFFFFF;
  NeighbourInfo *neighbour = &neighbours[0];
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    // if neighbour already known, we should update it
    if (id.matches(neighbours[i].id)) {
      neighbour = &neighbours[i];
      break;
    }

    // otherwise we should update the least recently updated neighbour
    if (neighbours[i].heard_timestamp < oldest_timestamp) {
      neighbour = &neighbours[i];
      oldest_timestamp = neighbour->heard_timestamp;
    }
  }

  // update neighbour info
  neighbour->id = id;
  neighbour->advert_timestamp = timestamp;
  neighbour->heard_timestamp = getRTCClock()->getCurrentTime();
  neighbour->snr = (int8_t)(snr * 4);
}

static bool isShare(const mesh::Packet *packet) {
  if (packet->hasTransportCodes()) {
    return packet->transport_codes[0] == 0 && packet->transport_codes[1] == 0;  // codes { 0, 0 } means 'send to nowhere'
  }
  return false;
}

void MyMesh::onAdvertRecv(mesh::Packet *packet, const mesh::Identity &id, uint32_t timestamp,
                          const uint8_t *app_data, size_t app_data_len) {
  mesh::Mesh::onAdvertRecv(packet, id, timestamp, app_data, app_data_len); // chain to super impl

  // if this a zero hop advert (and not via 'Share'), add it to neighbours
  if (packet->getPathHashCount() == 0 && !isShare(packet)) {
    AdvertDataParser parser(app_data, app_data_len);
    if (parser.isValid() && parser.getType() == ADV_TYPE_REPEATER) { // just keep neigbouring Repeaters
      putNeighbour(id, timestamp, packet->getSNR());
    }
  }
}

void MyMesh::onControlDataRecv(mesh::Packet* packet) {
  uint8_t type = packet->payload[0] & 0xF0;    // just test upper 4 bits
  // NOTE: a room server is ADV_TYPE_ROOM, so it does NOT answer node-discover
  // requests (which filter for repeaters). It only records repeater responses
  // to its own discovery, to build the neighbour table.
  if (type == CTL_TYPE_NODE_DISCOVER_RESP && packet->payload_len >= 6) {
    uint8_t node_type = packet->payload[0] & 0x0F;
    if (node_type != ADV_TYPE_REPEATER) {
      return;
    }
    if (packet->payload_len < 6 + PUB_KEY_SIZE) {
      MESH_DEBUG_PRINTLN("onControlDataRecv: DISCOVER_RESP pubkey too short: %d", (uint32_t)packet->payload_len);
      return;
    }

    if (pending_discover_tag == 0 || millisHasNowPassed(pending_discover_until)) {
      pending_discover_tag = 0;
      return;
    }
    uint32_t tag;
    memcpy(&tag, &packet->payload[2], 4);
    if (tag != pending_discover_tag) {
      return;
    }

    mesh::Identity id(&packet->payload[6]);
    if (id.matches(self_id)) {
      return;
    }
    putNeighbour(id, getRTCClock()->getCurrentTime(), packet->getSNR());
  }
}

void MyMesh::sendNodeDiscoverReq() {
  uint8_t data[10];
  data[0] = CTL_TYPE_NODE_DISCOVER_REQ; // prefix_only=0
  data[1] = (1 << ADV_TYPE_REPEATER);
  getRNG()->random(&data[2], 4); // tag
  memcpy(&pending_discover_tag, &data[2], 4);
  pending_discover_until = futureMillis(60000);
  uint32_t since = 0;
  memcpy(&data[6], &since, 4);

  auto pkt = createControlData(data, sizeof(data));
  if (pkt) {
    sendZeroHop(pkt);
  }
}

#endif // WITH_MQTT_NEIGHBORS

MyMesh::MyMesh(mesh::MainBoard &board, mesh::Radio &radio, mesh::MillisecondClock &ms, mesh::RNG &rng,
               mesh::RTCClock &rtc, mesh::MeshTables &tables)
    : mesh::Mesh(radio, ms, rng, rtc, *createObserverPacketManager(32), tables),
      region_map(key_store), temp_map(key_store),
      _cli(board, rtc, sensors, region_map, acl, &_prefs, this),
      telemetry(MAX_PACKET_PAYLOAD - 4)
#ifdef WITH_MQTT_BRIDGE
      , bridge(nullptr)
#endif
{
  last_millis = 0;
  uptime_millis = 0;
  next_local_advert = next_flood_advert = 0;
  dirty_contacts_expiry = 0;
  _logging = false;
  region_load_active = false;
  set_radio_at = revert_radio_at = 0;
  recv_pkt_region = NULL;

  // defaults
  _prefs.airtime_factor = 1.0;
  _prefs.rx_delay_base = 0.0f;   // off by default, was 10.0
  _prefs.tx_delay_factor = 0.5f; // was 0.25f;
  _prefs.direct_tx_delay_factor = 0.2f; // was zero
  StrHelper::strncpy(_prefs.node_name, ADVERT_NAME, sizeof(_prefs.node_name));
  _prefs.node_lat = ADVERT_LAT;
  _prefs.node_lon = ADVERT_LON;
  StrHelper::strncpy(_prefs.password, ADMIN_PASSWORD, sizeof(_prefs.password));
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
  _prefs.disable_fwd = 1;
  _prefs.advert_interval = 1;        // default to 2 minutes for NEW installs
  _prefs.flood_advert_interval = 47; // 47 hours
  _prefs.flood_max = 64;
  _prefs.flood_max_unscoped = 64;
  _prefs.flood_max_advert = 8;
  _prefs.interference_threshold = 0; // disabled
  _prefs.radio_fem_rxgain = 1;       // LoRa FEM RX gain on by default (FEM boards)
  _prefs.cad_enabled = 0;            // hardware CAD before TX (off by default; 'set cad on')
#ifdef ROOM_PASSWORD
  StrHelper::strncpy(_prefs.guest_password, ROOM_PASSWORD, sizeof(_prefs.guest_password));
#endif

  // GPS defaults
  _prefs.gps_enabled = 0;
  _prefs.gps_interval = 0;
  _prefs.advert_loc_policy = ADVERT_LOC_PREFS;

#if defined(USE_SX1262) || defined(USE_SX1268)
#ifdef SX126X_RX_BOOSTED_GAIN
  _prefs.rx_boosted_gain = SX126X_RX_BOOSTED_GAIN;
#else
  _prefs.rx_boosted_gain = 1; // enabled by default;
#endif
#endif
  _prefs.radio_fem_rxgain = 1;

  // Observer defaults (alert.*, etc.) moved to applyMQTTDefaults() — they live
  // in /mqtt_prefs now, not NodePrefs.

  // bridge defaults (same as repeater)
  _prefs.bridge_enabled = 1;    // enabled
  _prefs.bridge_delay   = 500;  // milliseconds
  _prefs.bridge_pkt_src = 1;    // logRx (RX packets)
  _prefs.bridge_baud = 115200;  // baud rate
  _prefs.bridge_channel = 1;    // channel 1

  // MQTT/WiFi/timezone/radio_watchdog defaults live in /mqtt_prefs now (see applyMQTTDefaults).
  // DMC fork defaults (dutchmeshcore-1/2 brokers, Europe/Amsterdam) are set via the
  // MQTT_DEFAULT_* macros in MQTTDefaults.h that applyMQTTDefaults() consumes.

  next_post_idx = 0;
  next_client_idx = 0;
  next_push = 0;
  memset(posts, 0, sizeof(posts));
  _num_posted = _num_post_pushes = 0;

#if defined(WITH_MQTT_NEIGHBORS)
  pending_discover_tag = 0;
  pending_discover_until = 0;
  neighbor_discover_count = 0;
  neighbor_discover_active = false;
  neighbor_table_refresh_active = false;
  neighbor_table_refresh_periodic = false;
  neighbor_discover_until = 0;
  next_neighbors_publish = 0;
  self_scopes_buf[0] = 0;
#endif

  memset(default_scope.key, 0, sizeof(default_scope.key));
}

void MyMesh::begin(FILESYSTEM *fs) {
  mesh::Mesh::begin();
  _fs = fs;
  // load persisted prefs
  _cli.loadPrefs(_fs);

  acl.load(_fs, self_id);
  region_map.load(_fs);

  // establish default-scope
  {
    RegionEntry* r = region_map.getDefaultRegion();
    if (r) {
      region_map.getTransportKeysFor(*r, &default_scope, 1);
    } else {
#ifdef DEFAULT_FLOOD_SCOPE_NAME
      r = region_map.findByName(DEFAULT_FLOOD_SCOPE_NAME);
      if (r == NULL) {
        r = region_map.putRegion(DEFAULT_FLOOD_SCOPE_NAME, 0);  // auto-create the default scope region
        if (r) { r->flags = 0; }   // Allow-flood
      }
      if (r) {
        region_map.setDefaultRegion(r);
        region_map.getTransportKeysFor(*r, &default_scope, 1);
      }
#endif
    }
  }

  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_driver.setTxPower(_prefs.tx_power_dbm);
  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);
  board.setLoRaFemLnaEnabled(_prefs.radio_fem_rxgain);

  updateAdvertTimer();
  updateFloodAdvertTimer();

  board.setAdcMultiplier(_prefs.adc_multiplier);

#if ENV_INCLUDE_GPS == 1
  applyGpsPrefs();
#endif
#ifdef WITH_MQTT_BRIDGE
  if (_prefs.bridge_enabled) {
    // Defer construction to avoid static init crashes on ESP32 classic
    bridge = new MQTTBridge(&_prefs, _cli.getObserverPrefs(), _mgr, getRTCClock(), &self_id);
    if (bridge) {
      // Set device public key for MQTT topics
      char device_id[65];
      mesh::LocalIdentity self_id = getSelfId();
      mesh::Utils::toHex(device_id, self_id.pub_key, PUB_KEY_SIZE);
      MESH_DEBUG_PRINTLN("Setting device ID: %s", device_id);
      bridge->setDeviceID(device_id);

      // Set firmware version
      bridge->setFirmwareVersion(getFirmwareVer());

      // Set board model
      bridge->setBoardModel(_cli.getBoard()->getManufacturerName());

      // Set build date
      bridge->setBuildDate(getBuildDate());

      // Set stats sources for automatic stats collection
      bridge->setStatsSources(this, _radio, _cli.getBoard(), _ms);

      bridge->begin();
    }
  }
#endif

  // Wire fault-alert reporter. begin() is safe regardless of bridge state.
  // Passing `this` as the callbacks lets the reporter resolve a TransportKey
  // scope (alert.region override, falling back to default_scope) so alert
  // floods ride the same scope as adverts/channel messages.
#ifdef WITH_MQTT_BRIDGE
  _alerter.begin(&_prefs, _cli.getObserverPrefs(), this, this);
  _alerter.setBridge(bridge);
#endif
}

void MyMesh::sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis, uint8_t path_hash_size) {
  if (scope.isNull()) {
    sendFlood(pkt, delay_millis, path_hash_size);
  } else {
    uint16_t codes[2];
    codes[0] = scope.calcTransportCode(pkt);
    codes[1] = 0;  // REVISIT: set to 'home' Region, for sender/return region?
    sendFlood(pkt, codes, delay_millis, path_hash_size);
  }
}

bool MyMesh::resolveAlertScope(TransportKey& dest) {
  // Same resolution policy as simple_repeater: alert.region > default_scope.
#ifdef WITH_MQTT_BRIDGE
  const char* alert_region = _cli.getObserverPrefs()->alert_region;
  if (alert_region[0]) {
    auto r = region_map.findByNamePrefix(alert_region);
    if (r && region_map.getTransportKeysFor(*r, &dest, 1) > 0 && !dest.isNull()) {
      return true;
    }
  }
#endif
  if (!default_scope.isNull()) {
    dest = default_scope;
    return true;
  }
  return false;
}

void MyMesh::sendFloodReply(mesh::Packet* packet, unsigned long delay_millis, uint8_t path_hash_size) {
  if (recv_pkt_region && !recv_pkt_region->isWildcard()) {  // if _request_ packet scope is known, send reply with same scope
    TransportKey scope;
    if (region_map.getTransportKeysFor(*recv_pkt_region, &scope, 1) > 0) {
      sendFloodScoped(scope, packet, delay_millis, path_hash_size);
    } else {
      sendFlood(packet, delay_millis, path_hash_size);   // send un-scoped
    }
  } else {
    sendFlood(packet, delay_millis, path_hash_size);   // send un-scoped
  }
}

void MyMesh::applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) {
  set_radio_at = futureMillis(2000); // give CLI reply some time to be sent back, before applying temp radio params
  pending_freq = freq;
  pending_bw = bw;
  pending_sf = sf;
  pending_cr = cr;

  revert_radio_at = futureMillis(2000 + timeout_mins * 60 * 1000); // schedule when to revert radio params
}

bool MyMesh::formatFileSystem() {
#if defined(NRF52_PLATFORM)
  return InternalFS.format();
#elif defined(RP2040_PLATFORM)
  return LittleFS.format();
#elif defined(ESP32)
  return SPIFFS.format();
#else
#error "need to implement file system erase"
  return false;
#endif
}

void MyMesh::sendSelfAdvertisement(int delay_millis, bool flood) {
  mesh::Packet *pkt = createSelfAdvert();
  if (pkt) {
    if (flood) {
      sendFloodScoped(default_scope, pkt, delay_millis, _prefs.path_hash_mode + 1);
    } else {
      sendZeroHop(pkt, delay_millis);
    }
  } else {
    MESH_DEBUG_PRINTLN("ERROR: unable to create advertisement packet!");
  }
}

void MyMesh::updateAdvertTimer() {
  if (_prefs.advert_interval > 0) { // schedule local advert timer
    next_local_advert = futureMillis((int)((uint32_t)_prefs.advert_interval * 2 * 60 * 1000));
  } else {
    next_local_advert = 0; // stop the timer
  }
}
void MyMesh::updateFloodAdvertTimer() {
  if (_prefs.flood_advert_interval > 0) { // schedule flood advert timer
    next_flood_advert = futureMillis(((uint32_t)_prefs.flood_advert_interval) * 60 * 60 * 1000);
  } else {
    next_flood_advert = 0; // stop the timer
  }
}

void MyMesh::dumpLogFile() {
#if defined(RP2040_PLATFORM)
  File f = _fs->open(PACKET_LOG_FILE, "r");
#else
  File f = _fs->open(PACKET_LOG_FILE);
#endif
  if (f) {
    while (f.available()) {
      int c = f.read();
      if (c < 0) break;
      Serial.print((char)c);
    }
    f.close();
  }
}

void MyMesh::setTxPower(int8_t power_dbm) {
  radio_driver.setTxPower(power_dbm);
}

bool MyMesh::setRxBoostedGain(bool enable) {
  return radio_driver.setRxBoostedGainMode(enable);
}

void MyMesh::saveIdentity(const mesh::LocalIdentity &new_id) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  IdentityStore store(*_fs, "");
#elif defined(ESP32)
  IdentityStore store(*_fs, "/identity");
#elif defined(RP2040_PLATFORM)
  IdentityStore store(*_fs, "/identity");
#else
#error "need to define saveIdentity()"
#endif
  store.save("_main", new_id);
}

void MyMesh::startRegionsLoad() {
  temp_map.resetFrom(region_map);   // rebuild regions in a temp instance
  memset(load_stack, 0, sizeof(load_stack));
  load_stack[0] = &temp_map.getWildcard();
  region_load_active = true;
}

bool MyMesh::saveRegions() {
  return region_map.save(_fs);
}

void MyMesh::onDefaultRegionChanged(const RegionEntry* r) {
  if (r) {
    region_map.getTransportKeysFor(*r, &default_scope, 1);
  } else {
    memset(default_scope.key, 0, sizeof(default_scope.key));
  }
}

void MyMesh::clearStats() {
  radio_driver.resetStats();
  resetStats();
  ((SimpleMeshTables *)getTables())->resetStats();
}

void MyMesh::formatNeighborsReply(char *reply) {
#if defined(WITH_MQTT_NEIGHBORS)
  char *dp = reply;

  // create copy of neighbours list, skipping empty entries so we can sort it separately from main list
  int16_t neighbours_count = 0;
  NeighbourInfo* sorted_neighbours[MAX_NEIGHBOURS];
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    auto neighbour = &neighbours[i];
    if (neighbour->heard_timestamp > 0) {
      sorted_neighbours[neighbours_count] = neighbour;
      neighbours_count++;
    }
  }

  // sort neighbours newest to oldest
  std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
    return a->heard_timestamp > b->heard_timestamp; // desc
  });

  for (int i = 0; i < neighbours_count && dp - reply < 134; i++) {
    NeighbourInfo *neighbour = sorted_neighbours[i];

    // add new line if not first item
    if (i > 0) *dp++ = '\n';

    char hex[10];
    // get 4 bytes of neighbour id as hex
    mesh::Utils::toHex(hex, neighbour->id.pub_key, 4);

    // add next neighbour
    uint32_t secs_ago = getRTCClock()->getCurrentTime() - neighbour->heard_timestamp;
    sprintf(dp, "%s:%u:%d", hex, secs_ago, neighbour->snr);
    while (*dp)
      dp++; // find end of string
  }
  if (dp == reply) { // no neighbours, need empty response
    strcpy(dp, "-none-");
    dp += 6;
  }
  *dp = 0; // null terminator
#else
  strcpy(reply, "not supported");
#endif
}

void MyMesh::removeNeighbor(const uint8_t *pubkey, int key_len) {
#if defined(WITH_MQTT_NEIGHBORS)
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    NeighbourInfo *neighbour = &neighbours[i];
    if (memcmp(neighbour->id.pub_key, pubkey, key_len) == 0) {
      neighbours[i] = NeighbourInfo(); // clear neighbour entry
    }
  }
#else
  (void)pubkey; (void)key_len;
#endif
}

void MyMesh::formatStatsReply(char *reply) {
  StatsFormatHelper::formatCoreStats(reply, board, *_ms, _err_flags, _mgr);
}

void MyMesh::formatRadioStatsReply(char *reply) {
  StatsFormatHelper::formatRadioStats(reply, _radio, radio_driver, getTotalAirTime(), getReceiveAirTime());
}

void MyMesh::formatRadioDiagReply(char *reply) {
  StatsFormatHelper::formatRadioDiag(reply, _radio, radio_driver, *_ms, _err_flags, hasOutbound());
}

void MyMesh::formatPacketStatsReply(char *reply) {
  StatsFormatHelper::formatPacketStats(reply, radio_driver, getNumSentFlood(), getNumSentDirect(), 
                                       getNumRecvFlood(), getNumRecvDirect());
}

void MyMesh::handleCommand(uint32_t sender_timestamp, char *command, char *reply) {
  if (region_load_active) {
    if (StrHelper::isBlank(command)) {  // empty/blank line, signal to terminate 'load' operation
      region_map = temp_map;  // copy over the temp instance as new current map
      region_load_active = false;

      sprintf(reply, "OK - loaded %d regions", region_map.getCount());
    } else {
      char *np = command;
      while (*np == ' ') np++;   // skip indent
      int indent = np - command;

      char *ep = np;
      while (RegionMap::is_name_char(*ep)) ep++;
      if (*ep) { *ep++ = 0; }  // set null terminator for end of name

      while (*ep && *ep != 'F') ep++;  // look for (optional) flags

      if (indent > 0 && indent < 8 && strlen(np) > 0) {
        auto parent = load_stack[indent - 1];
        if (parent) {
          auto old = region_map.findByName(np);
          auto nw = temp_map.putRegion(np, parent->id, old ? old->id : 0);  // carry-over the current ID (if name already exists)
          if (nw) {
            nw->flags = old ? old->flags : (*ep == 'F' ? 0 : REGION_DENY_FLOOD);   // carry-over flags from curr

            load_stack[indent] = nw;  // keep pointers to parent regions, to resolve parent_id's
          }
        }
      }
      reply[0] = 0;
    }
    return;
  }

  while (*command == ' ')
    command++; // skip leading spaces

  if (strlen(command) > 4 && command[2] == '|') { // optional prefix (for companion radio CLI)
    memcpy(reply, command, 3);                    // reflect the prefix back
    reply += 3;
    command += 3;
  }

  // handle ACL related commands
  if (memcmp(command, "setperm ", 8) == 0) {   // format:  setperm {pubkey-hex} {permissions-int8}
    char* hex = &command[8];
    char* sp = strchr(hex, ' ');   // look for separator char
    if (sp == NULL) {
      strcpy(reply, "Err - bad params");
    } else {
      *sp++ = 0;   // replace space with null terminator

      uint8_t pubkey[PUB_KEY_SIZE];
      int hex_len = min(sp - hex, PUB_KEY_SIZE*2);
      if (mesh::Utils::fromHex(pubkey, hex_len / 2, hex)) {
        uint8_t perms = atoi(sp);
        if (acl.applyPermissions(self_id, pubkey, hex_len / 2, perms)) {
          dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);   // trigger acl.save()
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Err - invalid params");
        }
      } else {
        strcpy(reply, "Err - bad pubkey");
      }
    }
  } else if (sender_timestamp == 0 && strcmp(command, "get acl") == 0) {
    Serial.println("ACL:");
    for (int i = 0; i < acl.getNumClients(); i++) {
      auto c = acl.getClientByIdx(i);
      if (c->permissions == 0) continue;  // skip deleted (or guest) entries

      Serial.printf("%02X ", c->permissions);
      mesh::Utils::printHex(Serial, c->id.pub_key, PUB_KEY_SIZE);
      Serial.printf("\n");
    }
    reply[0] = 0;
#if defined(WITH_MQTT_NEIGHBORS)
  } else if (memcmp(command, "discover.neighbors", 18) == 0) {
    const char* sub = command + 18;
    while (*sub == ' ') sub++;
    if (*sub != 0) {
      strcpy(reply, "Err - discover.neighbors has no options");
    } else {
      sendNodeDiscoverReq();
      strcpy(reply, "OK - Discover sent");
    }
  } else if (memcmp(command, "discover.scopes", 15) == 0) {
    const char* sub = command + 15;
    while (*sub == ' ') sub++;
    if (*sub != 0) {
      strcpy(reply, "Err - discover.scopes has no options");
    } else if (pending_discover_tag != 0 &&
               !millisHasNowPassed(pending_discover_until) &&
               !neighbor_discover_active) {
      // Reuse an in-flight zero-hop discovery rather than querying the old
      // neighbor table. An explicit command takes ownership of a periodic
      // refresh so `mqtt.neighbors off` cannot cancel this one-shot request.
      // Validate up front so queuing cannot report OK for a request that is
      // certain to fail once the discovery window closes.
      if (!neighborDiscoverReady(reply)) {
        // reply already set
      } else {
        neighbor_table_refresh_active = true;
        neighbor_table_refresh_periodic = false;
        long remaining_ms = (long)(pending_discover_until - futureMillis(0));
        unsigned remaining_secs = remaining_ms > 0
          ? (unsigned)(((unsigned long)remaining_ms + 999UL) / 1000UL) : 0;
        sprintf(reply, "OK - scopes queued (%us discovery remaining)", remaining_secs);
        MESH_DEBUG_PRINTLN("Neighbor scopes queued behind active discovery (%us remaining)", remaining_secs);
      }
    } else if (!startNeighborDiscover(reply)) {
      // reply already set
    }
#elif defined(WITH_MQTT_BRIDGE)
  } else if (memcmp(command, "discover.scopes", 15) == 0) {
    strcpy(reply, "Err - not supported (requires PSRAM)");
#endif
  } else if (strncmp(command, "room.post", 9) == 0) {
    char* msg = command + 9;
    while (*msg == ' ') msg++;
    if (*msg == 0) {
      snprintf(reply, MAX_POST_TEXT_LEN, "ERR empty message");
    } else {
      addSystemPost(msg);
      snprintf(reply, MAX_POST_TEXT_LEN, "OK");
    }
  } else{
    _cli.handleCommand(sender_timestamp, command, reply);  // common CLI commands
  }
}

bool MyMesh::saveFilter(ClientInfo* client) {
  return client->isAdmin();    // only save Admins
}

void MyMesh::loop() {
  // Check radio FIRST to ensure we don't miss incoming packets
  // MQTT processing can take time, so we prioritize radio reception
  mesh::Mesh::loop();
#ifdef WITH_MQTT_BRIDGE
  // bridge.loop() is now handled by FreeRTOS task on Core 0 - no need to call it here
#endif

  if (millisHasNowPassed(next_push) && acl.getNumClients() > 0) {
    // check for ACK timeouts
    for (int i = 0; i < acl.getNumClients(); i++) {
      auto c = acl.getClientByIdx(i);
      if (c->extra.room.pending_ack && millisHasNowPassed(c->extra.room.ack_timeout)) {
        c->extra.room.push_failures++;
        c->extra.room.pending_ack = 0; // reset  (TODO: keep prev expected_ack's in a list, incase they arrive LATER, after we retry)
        MESH_DEBUG_PRINTLN("pending ACK timed out: push_failures: %d", (uint32_t)c->extra.room.push_failures);
      }
    }
    // check next Round-Robin client, and sync next new post
    auto client = acl.getClientByIdx(next_client_idx);
    bool did_push = false;
    if (client->extra.room.pending_ack == 0 && client->last_activity != 0 &&
        client->extra.room.push_failures < 3) { // not already waiting for ACK, AND not evicted, AND retries not max
      MESH_DEBUG_PRINTLN("loop - checking for client %02X", (uint32_t)client->id.pub_key[0]);
      uint32_t now = getRTCClock()->getCurrentTime();
      for (int k = 0, idx = next_post_idx; k < MAX_UNSYNCED_POSTS; k++) {
        auto p = &posts[idx];
        if (now >= p->post_timestamp + POST_SYNC_DELAY_SECS &&
            p->post_timestamp > client->extra.room.sync_since // is new post for this Client?
            && !p->author.matches(client->id)) {   // don't push posts to the author
          // push this post to Client, then wait for ACK
          pushPostToClient(client, *p);
          did_push = true;
          MESH_DEBUG_PRINTLN("loop - pushed to client %02X: %s", (uint32_t)client->id.pub_key[0], p->text);
          break;
        }
        idx = (idx + 1) % MAX_UNSYNCED_POSTS; // wrap to start of cyclic queue
      }
    } else {
      MESH_DEBUG_PRINTLN("loop - skipping busy (or evicted) client %02X", (uint32_t)client->id.pub_key[0]);
    }
    next_client_idx = (next_client_idx + 1) % acl.getNumClients(); // round robin polling for each client

    if (did_push) {
      next_push = futureMillis(SYNC_PUSH_INTERVAL);
    } else {
      // were no unsynced posts for curr client, so process next client much quicker! (in next loop())
      next_push = futureMillis(SYNC_PUSH_INTERVAL / 8);
    }
  }

  if (next_flood_advert && millisHasNowPassed(next_flood_advert)) {
    mesh::Packet *pkt = createSelfAdvert();
    uint32_t delay_millis = 0;
    if (pkt) sendFloodScoped(default_scope, pkt, delay_millis, _prefs.path_hash_mode + 1);

    updateFloodAdvertTimer(); // schedule next flood advert
    updateAdvertTimer();      // also schedule local advert (so they don't overlap)
  } else if (next_local_advert && millisHasNowPassed(next_local_advert)) {
    mesh::Packet *pkt = createSelfAdvert();
    if (pkt) sendZeroHop(pkt);

    updateAdvertTimer(); // schedule next local advert
  }

  if (set_radio_at && millisHasNowPassed(set_radio_at)) { // apply pending (temporary) radio params
    set_radio_at = 0;                                     // clear timer
    radio_driver.setParams(pending_freq, pending_bw, pending_sf, pending_cr);
    MESH_DEBUG_PRINTLN("Temp radio params");
  }

  if (revert_radio_at && millisHasNowPassed(revert_radio_at)) { // revert radio params to orig
    revert_radio_at = 0;                                        // clear timer
    radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
    MESH_DEBUG_PRINTLN("Radio params restored");
  }

#if defined(WITH_MQTT_BRIDGE) && defined(OTA_MANIFEST_BASE)
  if (_ota_update_at && millisHasNowPassed(_ota_update_at)) { // deferred `ota update`
    _ota_update_at = 0;                                       // clear timer
    // The "Beginning update..." reply has now gone out. Free the bridge for heap
    // headroom, then flash: otaFromManifest reboots into the new image on success
    // (so this never returns); on any abort (already up to date, partition change,
    // download error) it returns and we resume the bridge.
    Serial.println("OTA: starting update");
    setBridgeState(false);
    char ota_reply[160];
    if (!_cli.getBoard()->otaFromManifest(getFirmwareVer(), false, ota_reply)) {
      Serial.print("OTA: aborted, resuming bridge - "); Serial.println(ota_reply);
      setBridgeState(true);
    }
  }
#endif

  // is pending dirty contacts write needed?
  if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry)) {
    acl.save(_fs, MyMesh::saveFilter);
    dirty_contacts_expiry = 0;
  }

  // TODO: periodically check for OLD/inactive entries in known_clients[], and evict

  // update uptime
  uint32_t now = millis();
  uptime_millis += now - last_millis;
  last_millis = now;

#ifdef WITH_MQTT_BRIDGE
  _alerter.onLoop(now);
#endif

#if defined(WITH_MQTT_NEIGHBORS)
  bool periodic_neighbors_enabled = _cli.getObserverPrefs()->mqtt_neighbors_enabled;
  if (neighbor_discover_active) {
    loopNeighborDiscover();
  } else if (neighbor_table_refresh_active) {
    if (neighbor_table_refresh_periodic && !periodic_neighbors_enabled) {
      // Turning periodic publishing off cancels a refresh that has not yet
      // reached the scope-query/publish phase. Re-enabling starts fresh.
      // pending_discover_tag is left alone: this refresh may have joined a
      // discover.neighbors the user started, and clearing the tag would drop
      // its remaining responses. An unused tag expires on its own.
      neighbor_table_refresh_active = false;
      neighbor_table_refresh_periodic = false;
      next_neighbors_publish = 0;
    } else if (pending_discover_tag == 0 || millisHasNowPassed(pending_discover_until)) {
      // The zero-hop discover.neighbors response window is complete. Freeze the
      // refreshed table into the scope-query overlay, then publish when those
      // per-neighbor requests finish.
      bool was_periodic = neighbor_table_refresh_periodic;
      pending_discover_tag = 0;
      neighbor_table_refresh_active = false;
      neighbor_table_refresh_periodic = false;
      char tmp_reply[80];
      const char* origin_str = was_periodic ? "periodic" : "manual";
      if (startNeighborDiscover(tmp_reply)) {
        MESH_DEBUG_PRINTLN("MQTT %s %s", origin_str, tmp_reply);
      } else {
        if (periodic_neighbors_enabled) {
          // Avoid retrying a failed refresh on every loop iteration.
          next_neighbors_publish = futureMillis(_cli.getObserverPrefs()->mqtt_neighbors_interval);
        }
        MESH_DEBUG_PRINTLN("MQTT %s neighbor scope discovery failed: %s", origin_str, tmp_reply);
      }
    }
  } else if (periodic_neighbors_enabled && bridge && bridge->isRunning()) {
    if (next_neighbors_publish == 0 ||
        (next_neighbors_publish != 0 && millisHasNowPassed(next_neighbors_publish))) {
      // Refresh the zero-hop neighbor cache first (the same operation as the
      // discover.neighbors CLI command). Scope queries begin after its 60-second
      // collection window instead of publishing a stale cached table.
      if (pending_discover_tag == 0 || millisHasNowPassed(pending_discover_until)) {
        pending_discover_tag = 0;
        sendNodeDiscoverReq();
        MESH_DEBUG_PRINTLN("MQTT periodic neighbor table refresh started");
      } else {
        MESH_DEBUG_PRINTLN("MQTT periodic refresh joined active neighbor discovery");
      }
      neighbor_table_refresh_active = true;
      neighbor_table_refresh_periodic = true;
    }
  }

  // Report the periodic-neighbors schedule for `get mqtt.status`. The timer lives
  // here, so the wrap-safe millis math stays on this side of the handoff.
  if (bridge) {
    if (neighbor_discover_active || neighbor_table_refresh_active) {
      bridge->setNeighborsSchedule(MQTTBridge::NBR_ACTIVE, 0);
    } else if (next_neighbors_publish == 0 || millisHasNowPassed(next_neighbors_publish)) {
      bridge->setNeighborsSchedule(MQTTBridge::NBR_DUE, 0);
    } else {
      long remaining_ms = (long)(next_neighbors_publish - futureMillis(0));
      uint32_t remaining_secs = remaining_ms > 0 ? (uint32_t)(remaining_ms / 1000) : 0;
      bridge->setNeighborsSchedule(MQTTBridge::NBR_SCHEDULED, remaining_secs);
    }
  }
#endif

#ifdef WITH_SNMP
  // Push radio stats to SNMP agent every 2 seconds
  if (_snmp_agent.isRunning()) {
    static unsigned long last_snmp_stats = 0;
    if (now - last_snmp_stats >= 2000) {
      last_snmp_stats = now;
      _snmp_agent.updateRadioStats(
        radio_driver.getPacketsRecv(), radio_driver.getPacketsSent(),
        radio_driver.getPacketsRecvErrors(),
        (int16_t)_radio->getNoiseFloor(),
        (int16_t)radio_driver.getLastRSSI(),
        (int16_t)(radio_driver.getLastSNR() * 4),
        getNumSentFlood(), getNumSentDirect(),
        getNumRecvFlood(), getNumRecvDirect(),
        getTotalAirTime() / 1000, uptime_millis / 1000);
    }
  }
#endif
}

#if defined(WITH_MQTT_NEIGHBORS)

#include "helpers/MQTTMessageBuilder.h"
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

void MyMesh::getLocalScopes(char* buf, size_t len) {
  if (!buf || len == 0) return;
  buf[0] = 0;
  region_map.exportNamesTo(buf, (int)len, REGION_DENY_FLOOD);
}

bool MyMesh::sendAnonRegionsReq(const mesh::Identity& target, uint32_t& tag) {
  uint8_t secret[PUB_KEY_SIZE];
  self_id.calcSharedSecret(secret, target);

  tag = getRTCClock()->getCurrentTimeUnique();
  uint8_t inner[6];
  memcpy(inner, &tag, 4);
  inner[4] = 0x01; // ANON_REQ_TYPE_REGIONS
  inner[5] = 0x00; // zero-hop reply path

  mesh::Packet* pkt = createAnonDatagram(PAYLOAD_TYPE_ANON_REQ, self_id, target, secret, inner, sizeof(inner));
  if (!pkt) return false;
  sendDirect(pkt, NULL, 0, 0);
  return true;
}

bool MyMesh::handleNeighborDiscoverResponse(int overlay_idx, const uint8_t* data, size_t len) {
  if (overlay_idx < 0 || overlay_idx >= neighbor_discover_count) return false;
  NeighborDiscoverEntry& entry = neighbor_discover[overlay_idx];
  if (entry.status != ND_PENDING || len < 8) return false;

  uint32_t tag;
  memcpy(&tag, data, 4);
  if (tag != entry.tag) return false;

  size_t scope_len = len - 8;
  if (scope_len >= sizeof(entry.scopes)) {
    scope_len = sizeof(entry.scopes) - 1;
  }
  memcpy(entry.scopes, &data[8], scope_len);
  entry.scopes[scope_len] = 0;
  entry.status = ND_RESPONDED;
  return true;
}

static bool neighborPublishEntryComesBefore(
    const MQTTMessageBuilder::NeighborsMessageEntry& lhs,
    const MQTTMessageBuilder::NeighborsMessageEntry& rhs) {
  if (lhs.heard_secs_ago != rhs.heard_secs_ago) {
    return lhs.heard_secs_ago < rhs.heard_secs_ago;  // newer first
  }
  if (lhs.snr != rhs.snr) {
    return lhs.snr > rhs.snr;  // stronger first when equally recent
  }
  return strcmp(lhs.pubkey_hex, rhs.pubkey_hex) < 0;
}

void MyMesh::finishNeighborDiscover() {
  getLocalScopes(self_scopes_buf, sizeof(self_scopes_buf));

  char self_pubkey_hex[65];
  mesh::Utils::toHex(self_pubkey_hex, self_id.pub_key, PUB_KEY_SIZE);

  char origin[32];
  MQTTBridge::getEffectiveMqttOrigin(&_prefs, _cli.getObserverPrefs(), origin, sizeof(origin));

  char timestamp[40];
  MQTTMessageBuilder::formatIsoTimestampForMqtt(getRTCClock()->getCurrentTime(), 0, nullptr, timestamp, sizeof(timestamp));

  char pubkey_hex[MAX_NEIGHBOURS][65];
  MQTTMessageBuilder::NeighborsMessageEntry entries[MAX_NEIGHBOURS];
  uint32_t now_secs = getRTCClock()->getCurrentTime();

  for (int i = 0; i < neighbor_discover_count; i++) {
    auto& nb = neighbours[neighbor_discover[i].neighbour_idx];
    mesh::Utils::toHex(pubkey_hex[i], nb.id.pub_key, PUB_KEY_SIZE);
    entries[i].pubkey_hex = pubkey_hex[i];
    entries[i].snr = nb.snr / 4.0f;
    entries[i].heard_secs_ago = (nb.heard_timestamp > 0 && now_secs >= nb.heard_timestamp)
      ? (now_secs - nb.heard_timestamp) : 0;
    entries[i].scopes = neighbor_discover[i].scopes;
    switch (neighbor_discover[i].status) {
      case ND_RESPONDED: entries[i].status = "responded"; break;
      case ND_SEND_FAILED: entries[i].status = "send_failed"; break;
      default: entries[i].status = "timeout"; break;
    }
  }

  // The JSON builder drops entries from the tail when the fixed MQTT buffer is
  // full, so order the useful/recent neighbors first. Insertion sort is small,
  // allocation-free, and sufficient for MAX_NEIGHBOURS (currently at most 50).
  for (int i = 1; i < neighbor_discover_count; i++) {
    MQTTMessageBuilder::NeighborsMessageEntry entry = entries[i];
    int j = i;
    while (j > 0 && neighborPublishEntryComesBefore(entry, entries[j - 1])) {
      entries[j] = entries[j - 1];
      j--;
    }
    entries[j] = entry;
  }

#if defined(ESP_PLATFORM)
  char* json_buf = (char*)heap_caps_malloc(MQTTBridge::NEIGHBORS_JSON_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
#else
  char* json_buf = (char*)malloc(MQTTBridge::NEIGHBORS_JSON_BUFFER_SIZE);
#endif
  if (!json_buf) {
    neighbor_discover_active = false;
    neighbor_discover_count = 0;
    if (_cli.getObserverPrefs()->mqtt_neighbors_enabled) {
      next_neighbors_publish = futureMillis(_cli.getObserverPrefs()->mqtt_neighbors_interval);
    }
    return;
  }

  DynamicJsonDocument doc(MQTTBridge::NEIGHBORS_JSON_BUFFER_SIZE);
  int json_len = MQTTMessageBuilder::buildNeighborsMessage(
    doc, origin, self_pubkey_hex, timestamp, self_scopes_buf,
    entries, neighbor_discover_count,
    json_buf, MQTTBridge::NEIGHBORS_JSON_BUFFER_SIZE);

  if (json_len > 0 && bridge) {
    bridge->requestPublishNeighbors(json_buf, (size_t)json_len);
  }

#if defined(ESP_PLATFORM)
  heap_caps_free(json_buf);
#else
  free(json_buf);
#endif

  neighbor_discover_active = false;
  neighbor_discover_count = 0;
  if (_cli.getObserverPrefs()->mqtt_neighbors_enabled) {
    next_neighbors_publish = futureMillis(_cli.getObserverPrefs()->mqtt_neighbors_interval);
  }
}

void MyMesh::loopNeighborDiscover() {
  if (!neighbor_discover_active) return;

  bool all_done = true;
  for (int i = 0; i < neighbor_discover_count; i++) {
    if (neighbor_discover[i].status == ND_PENDING) {
      all_done = false;
      break;
    }
  }

  if (!all_done && !millisHasNowPassed(neighbor_discover_until)) {
    return;
  }

  if (!all_done) {
    for (int i = 0; i < neighbor_discover_count; i++) {
      if (neighbor_discover[i].status == ND_PENDING) {
        neighbor_discover[i].status = ND_TIMEOUT;
      }
    }
  }
  finishNeighborDiscover();
}

bool MyMesh::neighborDiscoverReady(char* reply) {
#if defined(ESP_PLATFORM)
  if (!psramFound()) {
    strcpy(reply, "Err - PSRAM not available");
    return false;
  }
#endif
  if (!bridge || !bridge->isRunning()) {
    strcpy(reply, "Err - MQTT bridge not running");
    return false;
  }
  return true;
}

bool MyMesh::startNeighborDiscover(char* reply) {
  if (neighbor_discover_active) {
    strcpy(reply, "Err - neighbor discover already active");
    return false;
  }
  if (!neighborDiscoverReady(reply)) {
    return false;  // reply already set
  }

  getLocalScopes(self_scopes_buf, sizeof(self_scopes_buf));
  neighbor_discover_count = 0;
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    if (neighbours[i].heard_timestamp > 0) {
      neighbor_discover[neighbor_discover_count].neighbour_idx = (uint8_t)i;
      neighbor_discover[neighbor_discover_count].scopes[0] = 0;
      neighbor_discover[neighbor_discover_count].status = ND_PENDING;
      uint32_t tag;
      if (sendAnonRegionsReq(neighbours[i].id, tag)) {
        neighbor_discover[neighbor_discover_count].tag = tag;
      } else {
        neighbor_discover[neighbor_discover_count].status = ND_SEND_FAILED;
      }
      neighbor_discover_count++;
    }
  }

  neighbor_discover_active = true;
  neighbor_discover_until = futureMillis(NEIGHBOR_DISCOVER_TIMEOUT_MS);

  if (neighbor_discover_count == 0) {
    finishNeighborDiscover();
    strcpy(reply, "OK - neighbor discover started (0 neighbors, self only)");
  } else {
    sprintf(reply, "OK - neighbor discover started (%u neighbors)", (unsigned)neighbor_discover_count);
  }
  return true;
}

#endif // WITH_MQTT_NEIGHBORS
