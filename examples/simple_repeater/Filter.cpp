#include "Filter.h"

// These are copied into the reply buffer verbatim. The help line is already
// close to the limit, so guard it at compile time rather than let a future
// subcommand overflow it silently.
static const char FILTER_HELP[] =
    "> filter [ help | on | off | reset | dryrun | types | count | stats | hops | rate | channel | hash | malformed | advert | path | sender | text | watch | age ]";
static const char FILTER_STATS_HELP[] =
    "> filter stats [ hops | rate | channel | hash | malformed | top | advert | path | air | sender | text | age ]";
static const char FILTER_PATH_HELP[] =
    "> filter path [ list | add | remove ] <hex prefix, 2-8 digits>";
static const char FILTER_RULE_HELP[] =
    "> filter sender|text [ list | add <pattern> [secs] [prob] | remove <pattern> ]";
static const char FILTER_WATCH_HELP[] =
    "> filter watch [ list | add | remove ] <#name>";
static const char FILTER_RULE_ARGS_ERR[] =
    "> Filter: error <secs> range is 0-65535, <prob> range is 1-100";

static_assert(sizeof(FILTER_HELP) <= FILTER_REPLY_SIZE, "filter help text no longer fits the reply buffer");
static_assert(sizeof(FILTER_STATS_HELP) <= FILTER_REPLY_SIZE, "filter stats help text no longer fits the reply buffer");
static_assert(sizeof(FILTER_PATH_HELP) <= FILTER_REPLY_SIZE, "filter path help text no longer fits the reply buffer");
static_assert(sizeof(FILTER_RULE_HELP) <= FILTER_REPLY_SIZE, "filter rule help text no longer fits the reply buffer");
static_assert(sizeof(FILTER_WATCH_HELP) <= FILTER_REPLY_SIZE, "filter watch help text no longer fits the reply buffer");

bool Filter::drop(const mesh::Packet* packet) {
  // The airtime this drop saves is what the repeater would have billed for
  // relaying it; in dry-run it is what the drop *would* save.
  if (_radio != nullptr) {
    FilterStat::recordAir(_cnt, _radio->getEstAirtimeFor(packet->getRawLength()));
  }
  return _prefs.dryrun;   // dry-run: counted, but still forwarded
}

bool Filter::allowPacketForward(const mesh::Packet* packet) {
  if (!_prefs.filter_enabled) return true;

  // do not filter direct
  if (packet->isRouteDirect()) return true;

  // priority
  if (hasPriority(packet)) return true;

  uint8_t type = packet->getPayloadType();

  uint8_t src;
  bool have_src = srcHash(packet, &src);

  // multi hash bytes
  if (packet->getPathHashSize() < _prefs.minimal_hash_bytes) {
    FilterStat::recordHash(_cnt, packet->getPathHashSize(), type);
    if (have_src) FilterStat::recordSrc(_cnt, src);
    return drop(packet);
  }

  // blocked path prefixes (a rogue upstream repeater)
  int path_slot = FilterPath::findMatch(packet->path, packet->getPathHashSize(), packet->getPathHashCount(),
                                        _prefs.path_block, FILTER_PATH_COUNT);
  if (path_slot >= 0) {
    FilterStat::recordPath(_cnt, path_slot);
    if (have_src) FilterStat::recordSrc(_cnt, src);
    return drop(packet);
  }

  if (type < PAYLOAD_TYPE_COUNT) {
    // hops max
    if (packet->getPathHashCount() >= _prefs.payload_prefs[type].hops_max) {
      FilterStat::recordHops(_cnt, type);
      if (have_src) FilterStat::recordSrc(_cnt, src);
      return drop(packet);
    }
    // per-origin advert window, ahead of the per-type limiter so a repeat
    // advert never eats the budget of a legitimate one
    if (type == PAYLOAD_TYPE_ADVERT && packet->payload_len >= ADVERT_KEY_LEN) {
      if (!_advert.allow(packet->payload, millis())) {   // payload starts with the origin's pub_key
        FilterStat::recordAdvert(_cnt);
        if (have_src) FilterStat::recordSrc(_cnt, src);
        return drop(packet);
      }
    }
    // rate limiter (with optional probabilistic soft cutoff)
    if (!_limiters[type].allow(_rtc->getCurrentTime(), nextRandom())) {
      FilterStat::recordRate(_cnt, type);
      if (have_src) FilterStat::recordSrc(_cnt, src);
      return drop(packet);
    }
  }

  // channels
  if (type == PAYLOAD_TYPE_GRP_TXT) {
    // too short to hold a channel hash plus a MAC, so it cannot be a group text
    // regardless of the malformed scan setting. Counted so the drop is visible.
    if (packet->payload_len <= PATH_HASH_SIZE + CIPHER_MAC_SIZE) {
      FilterStat::recordMalformed(_cnt, MALFORMED_SHORT);
      return drop(packet);
    }

    uint8_t channel_hash = packet->payload[0];

    // blocked
    for (int i=0; i<FILTER_CHANNEL_COUNT; i++) {
      ChannelDetails &ch = _prefs.filter_channels[i];
      if (ch.name[0] == '\0') continue;
      if (channel_hash == ch.channel.hash[0]) {
        FilterStat::recordChannel(_cnt, i);
        return drop(packet);
      }
    }

    // Content checks need the plaintext: the malformed scan (Public only), and
    // the age limit and sender/text rules (Public plus the watch list). Decrypt once.
    bool want_malformed = _prefs.filter_malformed && channel_hash == PUBLIC_CHANNEL_HASH;
    bool want_rules = hasContentRules();
    const uint8_t* secret = (want_rules || _prefs.age_mins > 0) ? watchedSecret(channel_hash) : nullptr;
    if (want_malformed || secret != nullptr) {
      uint8_t data[MAX_PACKET_PAYLOAD + 1];
      int len = mesh::Utils::MACThenDecrypt(secret != nullptr ? secret : PUBLIC_CHANNEL_SECRET, data,
                                            &packet->payload[PATH_HASH_SIZE], packet->payload_len - PATH_HASH_SIZE);

      // malformed
      if (want_malformed) {
        uint8_t reason;
        if (!validMessageContent(data, len, &reason)) {
          FilterStat::recordMalformed(_cnt, reason);
          return drop(packet);
        }
      }

      // message age; len < 4 covers a failed MAC (hash collision with a
      // channel we do not hold the key for), which has no timestamp to read
      if (secret != nullptr && _prefs.age_mins > 0 && len >= 4) {
        uint32_t ts;
        memcpy(&ts, &data[0], 4);
        if (MessageAge::tooOld(ts, _rtc->getCurrentTime(), _prefs.age_mins)) {
          FilterStat::recordAge(_cnt);
          return drop(packet);
        }
      }

      // sender / text rules; len == 0 means the MAC failed (hash collision with
      // a channel we do not hold the key for), so there is nothing to match
      if (secret != nullptr && want_rules && len > 0) {
        FilterRules::GroupText gt;
        if (FilterRules::parseGroupText(data, len, &gt)) {
          uint32_t now = millis();
          uint8_t roll = nextRandom() % 100;
          int slot = FilterRules::evaluate(_prefs.sender_rules, FILTER_RULE_COUNT, now, roll, _sender_last, _cnt.sender_pass,
                                           [&](const SenderRule& r) { return FilterRules::matchName(r.name, gt.sender, gt.sender_len); });
          if (slot >= 0) {
            FilterStat::recordSender(_cnt, slot);
            return drop(packet);
          }
          slot = FilterRules::evaluate(_prefs.text_rules, FILTER_RULE_COUNT, now, roll, _text_last, _cnt.text_pass,
                                       [&](const TextRule& r) { return FilterRules::matchText(r.text, gt.text, gt.text_len); });
          if (slot >= 0) {
            FilterStat::recordText(_cnt, slot);
            return drop(packet);
          }
        }
      }
    }
  }

  // allowed
  return true;
}

// The 1-byte identity hash of whoever originated the packet, where the payload
// carries one. ACK and TRACE hold no identity and group traffic is encrypted,
// so those drops only ever show up in the totals.
bool Filter::srcHash(const mesh::Packet* packet, uint8_t* out) {
  switch (packet->getPayloadType()) {
    case PAYLOAD_TYPE_ADVERT:
      if (packet->payload_len < 1) return false;
      *out = packet->payload[0];   // payload starts with the sender's pub_key
      return true;

    case PAYLOAD_TYPE_REQ:
    case PAYLOAD_TYPE_RESPONSE:
    case PAYLOAD_TYPE_TXT_MSG:
    case PAYLOAD_TYPE_ANON_REQ:
    case PAYLOAD_TYPE_PATH:
      if (packet->payload_len < 2) return false;
      *out = packet->payload[1];   // dest hash first, then src
      return true;

    default:
      return false;
  }
}

bool Filter::hasPriority(const mesh::Packet* packet) {
  uint8_t type = packet->getPayloadType();
  if (type != PAYLOAD_TYPE_REQ &&
      type != PAYLOAD_TYPE_RESPONSE &&
      type != PAYLOAD_TYPE_TXT_MSG &&
      type != PAYLOAD_TYPE_ANON_REQ &&
      type != PAYLOAD_TYPE_PATH) return false;

  if (packet->payload_len < 2) return false;

  uint8_t dst_hash = packet->payload[0];
  uint8_t src_hash = packet->payload[1];

  // check ACL contacts
  for (int i = 0; i < _acl->getNumClients(); i++) {
    ClientInfo* client = _acl->getClientByIdx(i);
    if (client->id.isHashMatch(&src_hash) || client->id.isHashMatch(&dst_hash)) return true;
  }
  return false;
}

void Filter::handleCommand(FILESYSTEM* fs, char* command, char* reply) {
  const char* parts[6];
  int n = mesh::Utils::parseTextParts(command, parts, 6, ' ');

  if (n <= 1) {
    FilterStat::formatSummary(reply, FILTER_REPLY_SIZE, _cnt, _prefs.filter_enabled, _prefs.dryrun);
  }

  if (n == 2) {
    if (strcmp(parts[1], "help") == 0) {
      strcpy(reply, FILTER_HELP);
    } else if (strcmp(parts[1], "types") == 0) {
      strcpy(reply, "00=REQ\n01=RESPONSE\n02=TXT_MSG\n03=ACK\n04=ADVERT\n05=GRP_TXT\n06=GRP_DATA\n07=ANON_REQ\n08=PATH\n09=TRACE\n10=MULTIPART\n11=CONTROL");
    } else if (strcmp(parts[1], "on") == 0) {
      _prefs.filter_enabled = true;
      strcpy(reply, "> Filter: on");
      save(fs);
    } else if (strcmp(parts[1], "off") == 0) {
      _prefs.filter_enabled = false;
      strcpy(reply, "> Filter: off");
      save(fs);
    } else if (strcmp(parts[1], "reset") == 0) {
      resetPrefs();
      strcpy(reply, "> Filter: preferences reset");
      save(fs);
    } else if (strcmp(parts[1], "count") == 0) {
      FilterStat::formatCount(reply, FILTER_REPLY_SIZE, _cnt);
    } else if (strcmp(parts[1], "stats") == 0) {
      strcpy(reply, FILTER_STATS_HELP);
    } else if (strcmp(parts[1], "hops") == 0) {
      formatResponse(reply, ResponseType::HOPS);
    } else if (strcmp(parts[1], "rate") == 0) {
      formatResponse(reply, ResponseType::RATE);
    } else if (strcmp(parts[1], "channel") == 0) {
      strcpy(reply, "> filter channel [list | add | remove] <#name | Public>");
    } else if (strcmp(parts[1], "hash") == 0) {
      sprintf(reply, "> Filter: minimal %d bytes path hash size", _prefs.minimal_hash_bytes);
    } else if (strcmp(parts[1], "malformed") == 0) {
      sprintf(reply, "> Filter: malformed text scan %s", _prefs.filter_malformed ? "on" : "off");
    } else if (strcmp(parts[1], "dryrun") == 0) {
      sprintf(reply, "> Filter: dry-run %s", _prefs.dryrun ? "on" : "off");
    } else if (strcmp(parts[1], "advert") == 0) {
      sprintf(reply, "> Filter: advert origin window %uh (cache %d/%d)", (unsigned)_prefs.advert_hours,
              _advert.getCount(), _advert.getCapacity());
    } else if (strcmp(parts[1], "path") == 0) {
      strcpy(reply, FILTER_PATH_HELP);
    } else if (strcmp(parts[1], "sender") == 0 || strcmp(parts[1], "text") == 0) {
      strcpy(reply, FILTER_RULE_HELP);
    } else if (strcmp(parts[1], "watch") == 0) {
      strcpy(reply, FILTER_WATCH_HELP);
    } else if (strcmp(parts[1], "age") == 0) {
      FilterStat::formatStatsAge(reply, FILTER_REPLY_SIZE, _cnt, _prefs.age_mins,
                                 MessageAge::clockSet(_rtc->getCurrentTime()));
    } else {
      strcpy(reply, "> Filter: command error");
    }
  }

  if (n >= 3) {

    // stats
    if (strcmp(parts[1], "stats") == 0) {
      if (strcmp(parts[2], "hops") == 0) {
        FilterStat::formatStatsHops(reply, FILTER_REPLY_SIZE, _cnt, _prefs.payload_prefs);
      } else if (strcmp(parts[2], "rate") == 0) {
        FilterStat::formatStatsRate(reply, FILTER_REPLY_SIZE, _cnt, _prefs.payload_prefs);
      } else if (strcmp(parts[2], "hash") == 0) {
        FilterStat::formatStatsHash(reply, FILTER_REPLY_SIZE, _cnt);
      } else if (strcmp(parts[2], "channel") == 0) {
        listChannelNames(reply, FILTER_REPLY_SIZE, true);
      } else if (strcmp(parts[2], "malformed") == 0) {
        FilterStat::formatStatsMalformed(reply, FILTER_REPLY_SIZE, _cnt);
      } else if (strcmp(parts[2], "top") == 0) {
        FilterStat::formatStatsTop(reply, FILTER_REPLY_SIZE, _cnt);
      } else if (strcmp(parts[2], "advert") == 0) {
        FilterStat::formatStatsAdvert(reply, FILTER_REPLY_SIZE, _cnt, _prefs.advert_hours,
                                      _advert.getCount(), _advert.getCapacity());
      } else if (strcmp(parts[2], "path") == 0) {
        FilterStat::formatPathList(reply, FILTER_REPLY_SIZE, _prefs.path_block, &_cnt);
      } else if (strcmp(parts[2], "air") == 0) {
        FilterStat::formatStatsAir(reply, FILTER_REPLY_SIZE, _cnt);
      } else if (strcmp(parts[2], "sender") == 0) {
        FilterStat::formatRuleList(reply, FILTER_REPLY_SIZE, _prefs.sender_rules, FILTER_RULE_COUNT, _cnt.sender_slot, _cnt.sender_pass);
      } else if (strcmp(parts[2], "text") == 0) {
        FilterStat::formatRuleList(reply, FILTER_REPLY_SIZE, _prefs.text_rules, FILTER_RULE_COUNT, _cnt.text_slot, _cnt.text_pass);
      } else if (strcmp(parts[2], "age") == 0) {
        FilterStat::formatStatsAge(reply, FILTER_REPLY_SIZE, _cnt, _prefs.age_mins,
                                   MessageAge::clockSet(_rtc->getCurrentTime()));
      } else {
        strcpy(reply, FILTER_STATS_HELP);
      }

    // hops
    } else if (strcmp(parts[1], "hops") == 0) {

      if (n == 4) {
        uint8_t type = atoi(parts[2]);
        uint8_t count = atoi(parts[3]);

        if (type < 0 || type >= PAYLOAD_TYPE_COUNT) {
          strcpy(reply, "> Filter: error <type> range is 0-11");
        } else if (count < 0 || count > 64) {
          strcpy(reply, "> Filter: error <max_hops> range is 0-64");
        } else {
          _prefs.payload_prefs[type].hops_max = count;
          save(fs);
          strcpy(reply, "> Filter: OK");
        }
      } else {
        strcpy(reply, "> Filter: syntax error 'filter hops <type> <max_hops>'");
      }

    // rate
    } else if (strcmp(parts[1], "rate") == 0) {

      if (n == 5 || n == 6) {
        uint8_t type = atoi(parts[2]);
        uint16_t limit = atoi(parts[3]);
        uint32_t secs = atoi(parts[4]);
        uint16_t soft = (n == 6) ? atoi(parts[5]) : 0;   // omitted -> hard cutoff

        if (type < 0 || type >= PAYLOAD_TYPE_COUNT) {
          strcpy(reply, "> Filter: error <type> range is 0-11");
        } else if (soft != 0 && soft >= limit) {
          strcpy(reply, "> Filter: error <soft> must be less than <limit>");
        } else {
          _prefs.payload_prefs[type].rate_limit = limit;
          _prefs.payload_prefs[type].rate_secs = secs;
          _prefs.soft_limit[type] = soft;
          _limiters[type].init(limit, secs, soft);
          save(fs);
          strcpy(reply, "> Filter: OK");
        }
      } else {
        strcpy(reply, "> Filter: syntax error 'filter rate <type> <limit> <secs> [soft]'");
      }

    // channel
    } else if (strcmp(parts[1], "channel") == 0) {

      if (strcmp(parts[2], "list") == 0) {
        listChannelNames(reply, FILTER_REPLY_SIZE);
      } else if (n >= 4 && strcmp(parts[2], "add") == 0) {
        if (addChannel(parts[3])) {
          sprintf(reply, "> Filter: channel %s added", parts[3]);
          save(fs);
        } else {
          strcpy(reply, "Failed");
        }
      } else if (n >= 4 && strcmp(parts[2], "remove") == 0) {
        if (removeChannel(parts[3])) {
          sprintf(reply, "> Filter: channel %s removed", parts[3]);
          save(fs);
        } else {
          strcpy(reply, "Failed");
        }
      } else {
        strcpy(reply, "> Filter: syntax error 'filter channel [list | add | remove] <#name | Public>'");
      }

    // hash
    } else if (strcmp(parts[1], "hash") == 0) {
      uint8_t count = atoi(parts[2]);
      if (count < 1 || count > 3) {
          strcpy(reply, "> Filter: error hash bytes range is 1-3");
      } else {
        _prefs.minimal_hash_bytes = count;
        save(fs);
        strcpy(reply, "> Filter: OK");
      }

    // malformed
    } else if (strcmp(parts[1], "malformed") == 0) {
      if (strcmp(parts[2], "on") == 0) {
        _prefs.filter_malformed = true;
        strcpy(reply, "> Filter: malformed scan on");
        save(fs);
      } else if (strcmp(parts[2], "off") == 0) {
        _prefs.filter_malformed = false;
        strcpy(reply, "> Filter: malformed scan off");
        save(fs);
      }

    // dryrun
    } else if (strcmp(parts[1], "dryrun") == 0) {
      if (strcmp(parts[2], "on") == 0) {
        _prefs.dryrun = true;
        strcpy(reply, "> Filter: dry-run on");
        save(fs);
      } else if (strcmp(parts[2], "off") == 0) {
        _prefs.dryrun = false;
        strcpy(reply, "> Filter: dry-run off");
        save(fs);
      } else {
        strcpy(reply, "> Filter: syntax error 'filter dryrun <on | off>'");
      }

    // advert
    } else if (strcmp(parts[1], "advert") == 0) {
      if (strcmp(parts[2], "clear") == 0) {
        _advert.clear();
        strcpy(reply, "> Filter: advert cache cleared");
      } else {
        long hours = atol(parts[2]);
        if (hours < 0 || hours > ADVERT_MAX_HOURS) {
          strcpy(reply, "> Filter: error <hours> range is 0-720");
        } else {
          _prefs.advert_hours = (uint16_t)hours;
          _advert.setWindowHours(_prefs.advert_hours);
          save(fs);
          strcpy(reply, "> Filter: OK");
        }
      }

    // message age
    } else if (strcmp(parts[1], "age") == 0) {
      const char* v = parts[2];
      bool digits = v[0] != '\0';
      for (const char* c = v; *c; c++) {
        if (*c < '0' || *c > '9') { digits = false; break; }
      }
      long mins = digits ? atol(v) : -1;
      if (strcmp(v, "off") == 0) mins = 0;
      if (mins < 0 || mins > FILTER_AGE_MAX_MINS) {
        strcpy(reply, "> Filter: error <minutes> range is 1-10080, or off");
      } else {
        _prefs.age_mins = (uint16_t)mins;
        save(fs);
        strcpy(reply, "> Filter: OK");
      }

    // path
    } else if (strcmp(parts[1], "path") == 0) {
      char hex[2 * FILTER_PATH_MAX_LEN + 1];
      if (strcmp(parts[2], "list") == 0) {
        FilterStat::formatPathList(reply, FILTER_REPLY_SIZE, _prefs.path_block, nullptr);
      } else if (n >= 4 && strcmp(parts[2], "add") == 0) {
        if (addPath(parts[3], hex)) {
          sprintf(reply, "> Filter: path %s added", hex);
          save(fs);
        } else if (hex[0] == '\0') {
          strcpy(reply, "> Filter: error path prefix is 2-8 hex digits");
        } else {
          strcpy(reply, "Failed");
        }
      } else if (n >= 4 && strcmp(parts[2], "remove") == 0) {
        if (removePath(parts[3], hex)) {
          sprintf(reply, "> Filter: path %s removed", hex);
          save(fs);
        } else if (hex[0] == '\0') {
          strcpy(reply, "> Filter: error path prefix is 2-8 hex digits");
        } else {
          strcpy(reply, "Failed");
        }
      } else {
        strcpy(reply, FILTER_PATH_HELP);
      }

    // sender / text rules: list | add <pattern> [secs] [prob] | remove <pattern>
    } else if (strcmp(parts[1], "sender") == 0 || strcmp(parts[1], "text") == 0) {
      bool is_sender = strcmp(parts[1], "sender") == 0;
      if (strcmp(parts[2], "list") == 0) {
        if (is_sender) {
          FilterStat::formatRuleList(reply, FILTER_REPLY_SIZE, _prefs.sender_rules, FILTER_RULE_COUNT, nullptr, nullptr);
        } else {
          FilterStat::formatRuleList(reply, FILTER_REPLY_SIZE, _prefs.text_rules, FILTER_RULE_COUNT, nullptr, nullptr);
        }
      } else if (n >= 4 && strcmp(parts[2], "add") == 0) {
        uint16_t secs;
        uint8_t prob;
        size_t max_len = is_sender ? FILTER_SENDER_LEN - 1 : FILTER_TEXT_LEN - 1;
        if (strlen(parts[3]) > max_len) {
          sprintf(reply, "> Filter: error <pattern> max %u chars", (unsigned)max_len);
        } else if (!FilterRules::parseArgs(n >= 5 ? parts[4] : nullptr, n >= 6 ? parts[5] : nullptr, &secs, &prob)) {
          strcpy(reply, FILTER_RULE_ARGS_ERR);
        } else if (is_sender ? addSenderRule(parts[3], secs, prob) : addTextRule(parts[3], secs, prob)) {
          sprintf(reply, "> Filter: %s %s added", parts[1], parts[3]);
          save(fs);
        } else {
          strcpy(reply, "Failed");
        }
      } else if (n >= 4 && strcmp(parts[2], "remove") == 0) {
        if (is_sender ? removeSenderRule(parts[3]) : removeTextRule(parts[3])) {
          sprintf(reply, "> Filter: %s %s removed", parts[1], parts[3]);
          save(fs);
        } else {
          strcpy(reply, "Failed");
        }
      } else {
        strcpy(reply, FILTER_RULE_HELP);
      }

    // watch list: channels decrypted for the sender/text rules
    } else if (strcmp(parts[1], "watch") == 0) {
      if (strcmp(parts[2], "list") == 0) {
        listWatchNames(reply, FILTER_REPLY_SIZE);
      } else if (n >= 4 && strcmp(parts[2], "add") == 0) {
        if (addWatch(parts[3])) {
          sprintf(reply, "> Filter: watch %s added", parts[3]);
          save(fs);
        } else {
          strcpy(reply, "Failed");
        }
      } else if (n >= 4 && strcmp(parts[2], "remove") == 0) {
        if (removeWatch(parts[3])) {
          sprintf(reply, "> Filter: watch %s removed", parts[3]);
          save(fs);
        } else {
          strcpy(reply, "Failed");
        }
      } else {
        strcpy(reply, FILTER_WATCH_HELP);
      }
    } else {
      strcpy(reply, "> Filter: command error");
    }
  }
}

// ---- sender / text rules -------------------------------------------------------

template <typename Rule>
static bool addRule(Rule* rules, char* (*field)(Rule&), const char* pattern, size_t max_len, uint16_t secs, uint8_t prob) {
  if (pattern == nullptr || pattern[0] == '\0' || strlen(pattern) > max_len) return false;

  int free_slot = -1;
  for (int i = 0; i < FILTER_RULE_COUNT; i++) {
    char* pat = field(rules[i]);
    if (pat[0] == '\0') {
      if (free_slot < 0) free_slot = i;
    } else if (strcmp(pat, pattern) == 0) {
      return false;   // already present
    }
  }
  if (free_slot < 0) return false;

  Rule& r = rules[free_slot];
  memset(&r, 0, sizeof(r));
  strncpy(field(r), pattern, max_len);
  r.secs = secs;
  r.prob = prob;
  return true;
}

template <typename Rule>
static int findRule(Rule* rules, char* (*field)(Rule&), const char* pattern) {
  if (pattern == nullptr || pattern[0] == '\0') return -1;
  for (int i = 0; i < FILTER_RULE_COUNT; i++) {
    if (strcmp(field(rules[i]), pattern) == 0) return i;
  }
  return -1;
}

static char* senderField(SenderRule& r) { return r.name; }
static char* textField(TextRule& r) { return r.text; }

bool Filter::addSenderRule(const char* name, uint16_t secs, uint8_t prob) {
  return addRule(_prefs.sender_rules, senderField, name, FILTER_SENDER_LEN - 1, secs, prob);
}

bool Filter::removeSenderRule(const char* name) {
  int i = findRule(_prefs.sender_rules, senderField, name);
  if (i < 0) return false;
  memset(&_prefs.sender_rules[i], 0, sizeof(SenderRule));
  _sender_last[i] = 0;
  return true;
}

bool Filter::addTextRule(const char* text, uint16_t secs, uint8_t prob) {
  return addRule(_prefs.text_rules, textField, text, FILTER_TEXT_LEN - 1, secs, prob);
}

bool Filter::removeTextRule(const char* text) {
  int i = findRule(_prefs.text_rules, textField, text);
  if (i < 0) return false;
  memset(&_prefs.text_rules[i], 0, sizeof(TextRule));
  _text_last[i] = 0;
  return true;
}

bool Filter::hasContentRules(void) const {
  for (int i = 0; i < FILTER_RULE_COUNT; i++) {
    if (_prefs.sender_rules[i].name[0] != '\0') return true;
    if (_prefs.text_rules[i].text[0] != '\0') return true;
  }
  return false;
}

// The key to decrypt a group text with, when the rules may read it: Public is
// always readable, plus every channel on the watch list. nullptr otherwise.
const uint8_t* Filter::watchedSecret(uint8_t channel_hash) const {
  if (channel_hash == PUBLIC_CHANNEL_HASH) return PUBLIC_CHANNEL_SECRET;
  for (int i = 0; i < FILTER_WATCH_COUNT; i++) {
    const ChannelDetails& ch = _prefs.watch_channels[i];
    if (ch.name[0] == '\0') continue;
    if (ch.channel.hash[0] == channel_hash) return ch.channel.secret;
  }
  return nullptr;
}

bool Filter::addWatch(const char* name) {
  if (name == nullptr || name[0] == '\0' || strlen(name) >= sizeof(ChannelDetails::name)) return false;
  if (strcmp(name, "Public") == 0) return false;   // always watched

  int free_slot = -1;
  for (int i = 0; i < FILTER_WATCH_COUNT; i++) {
    ChannelDetails& ch = _prefs.watch_channels[i];
    if (ch.name[0] == '\0') {
      if (free_slot < 0) free_slot = i;
    } else if (strcmp(ch.name, name) == 0) {
      return false;
    }
  }
  if (free_slot < 0) return false;

  ChannelDetails& ch = _prefs.watch_channels[free_slot];
  strncpy(ch.name, name, sizeof(ch.name) - 1);
  ch.name[sizeof(ch.name) - 1] = '\0';
  getChannelHash(name, &ch.channel);
  return true;
}

bool Filter::removeWatch(const char* name) {
  if (name == nullptr || name[0] == '\0') return false;
  for (int i = 0; i < FILTER_WATCH_COUNT; i++) {
    ChannelDetails& ch = _prefs.watch_channels[i];
    if (strcmp(ch.name, name) == 0) {
      ch.name[0] = '\0';
      return true;
    }
  }
  return false;
}

void Filter::listWatchNames(char* out_buf, size_t out_size) {
  if (out_buf == nullptr || out_size == 0) return;

  FilterStat::Buf b(out_buf, out_size);
  char channel_hex[4];
  int listed = 0;

  b.add("Public (11)");
  listed++;
  for (int i = 0; i < FILTER_WATCH_COUNT; i++) {
    const ChannelDetails& ch = _prefs.watch_channels[i];
    if (ch.name[0] == '\0') continue;
    mesh::Utils::toHex(channel_hex, ch.channel.hash, 1);
    b.add(",%s (%s)", ch.name, channel_hex);
    listed++;
  }
  b.markTruncated("..");
}

void Filter::formatResponse(char *reply, ResponseType rtype) {
  FilterStat::Buf b(reply, FILTER_REPLY_SIZE);

  b.add(rtype == ResponseType::HOPS ? "[TYPE: MAX_HOPS]" : "[TYPE: LIMIT,SECS,SOFT]");

  for (uint8_t i = 0; i < PAYLOAD_TYPE_COUNT; ++i) {
    if (rtype == ResponseType::HOPS) {
      b.add("\n%02u: %u", (unsigned)i, (unsigned)_prefs.payload_prefs[i].hops_max);
    } else {
      b.add("\n%02u: %u,%lu,%u",
            (unsigned)i,
            (unsigned)_prefs.payload_prefs[i].rate_limit,
            (unsigned long)_prefs.payload_prefs[i].rate_secs,
            (unsigned)_prefs.soft_limit[i]);
    }
  }
  b.markTruncated("..");
}

bool Filter::addChannel(const char* name) {
  if (name == nullptr || name[0] == '\0') return false;

  for (int i=0; i<FILTER_CHANNEL_COUNT; i++) {
    ChannelDetails &ch = _prefs.filter_channels[i];
    if (ch.name[0] == '\0') {
      strncpy(ch.name, name, sizeof(ch.name)-1);
      getChannelHash(name, &ch.channel);
      return true;
    }
  }
  return false;
}

bool Filter::removeChannel(const char* name) {
  if (name == nullptr || name[0] == '\0') return false;

  for (int i=0; i<FILTER_CHANNEL_COUNT; i++) {
    ChannelDetails &ch = _prefs.filter_channels[i];
    if (strcmp(ch.name, name) == 0) {
      ch.name[0] = '\0';
      return true;
    }
  }
  return false;
}

// `formatted` receives the canonical upper-case prefix, or "" when `hex` did
// not parse, so the caller can tell a bad prefix from a full or missing slot.
bool Filter::addPath(const char* hex, char* formatted) {
  formatted[0] = '\0';
  PathPrefix p;
  if (!FilterPath::parse(hex, &p)) return false;
  FilterPath::format(formatted, 2 * FILTER_PATH_MAX_LEN + 1, p);

  int free_slot = -1;
  for (int i = 0; i < FILTER_PATH_COUNT; i++) {
    PathPrefix& cur = _prefs.path_block[i];
    if (cur.len == 0) {
      if (free_slot < 0) free_slot = i;
    } else if (cur.len == p.len && memcmp(cur.bytes, p.bytes, p.len) == 0) {
      return false;   // already blocked
    }
  }
  if (free_slot < 0) return false;
  _prefs.path_block[free_slot] = p;
  return true;
}

bool Filter::removePath(const char* hex, char* formatted) {
  formatted[0] = '\0';
  PathPrefix p;
  if (!FilterPath::parse(hex, &p)) return false;
  FilterPath::format(formatted, 2 * FILTER_PATH_MAX_LEN + 1, p);

  for (int i = 0; i < FILTER_PATH_COUNT; i++) {
    PathPrefix& cur = _prefs.path_block[i];
    if (cur.len == p.len && memcmp(cur.bytes, p.bytes, p.len) == 0) {
      memset(&cur, 0, sizeof(cur));
      return true;
    }
  }
  return false;
}

bool Filter::getChannelHash(const char* name, mesh::GroupChannel* gc) {
  if (name == nullptr || name[0] == '\0' || gc == nullptr) return false;

  // get channel secret
  memset(gc->secret, 0, PUB_KEY_SIZE);
  if (strcmp(name, "Public") == 0) {
    memcpy(gc->secret, PUBLIC_CHANNEL_SECRET, PUB_KEY_SIZE);
  } else {
    mesh::Utils::sha256(gc->secret, 16, (const uint8_t*)name, strlen(name));
  }

  // get channel hash
  mesh::Utils::sha256(gc->hash, sizeof(gc->hash), gc->secret, 16);
  return true;
}

void Filter::listChannelNames(char *out_buf, size_t out_size, bool with_counts) {
    if (out_buf == nullptr || out_size == 0) return;

    FilterStat::Buf b(out_buf, out_size);
    char channel_hex[4];
    int listed = 0;

    for (int i=0; i<FILTER_CHANNEL_COUNT; i++) {
        const char *name = _prefs.filter_channels[i].name;
        if (name[0] == '\0') continue;

        mesh::Utils::toHex(channel_hex, _prefs.filter_channels[i].channel.hash, 1);
        if (with_counts) {
          b.add(listed > 0 ? ",%s (%s): %lu" : "%s (%s): %lu",
                name, channel_hex, (unsigned long)_cnt.channel_slot[i]);
        } else {
          b.add(listed > 0 ? ",%s (%s)" : "%s (%s)", name, channel_hex);
        }
        listed++;
    }

    // no channels
    if (listed == 0) {
      b.add("None");
      return;
    }
    b.markTruncated("..");
}

bool Filter::validMessageContent(const uint8_t* data, uint8_t len, uint8_t* reason) {
  if (data == nullptr || len <= 5) {
    *reason = MALFORMED_SHORT;
    return false;
  }

  // check timestamp
  uint32_t timestamp;
  memcpy(&timestamp, &data[0], 4);
  if (MessageAge::implausible(timestamp, _rtc->getCurrentTime())) {
    *reason = MALFORMED_TIMESTAMP;
    return false;
  }

  // check message type
  uint8_t txt_type = data[4] >> 2;
  if (txt_type != TXT_TYPE_PLAIN) return true;

  // calculate text length
  uint8_t txt_len = 5;
  while (txt_len < len && data[txt_len] != 0) txt_len++;
  txt_len -= 5;
  if (!txt_len) {
    *reason = MALFORMED_EMPTY;
    return false;
  }

  // valid UTF8
  if (!isValidUTF8(&data[5], txt_len)) {
    *reason = MALFORMED_UTF8;
    return false;
  }

  // valid
  return true;
}

bool Filter::isValidUTF8(const uint8_t* data, uint8_t len) {
  if (data == nullptr || len == 0) return false;

  uint8_t i = 0;
  while (i < len) {
    uint8_t c = data[i++];
    if (c == 0) break;
    if (c < 0x80) continue;

    uint32_t codepoint;
    uint8_t needed;
    if ((c & 0xE0) == 0xC0) {
      codepoint = c & 0x1F;
      needed = 1;
      if (codepoint == 0) return false;
    } else if ((c & 0xF0) == 0xE0) {
      codepoint = c & 0x0F;
      needed = 2;
    } else if ((c & 0xF8) == 0xF0) {
      codepoint = c & 0x07;
      needed = 3;
    } else {
      return false;
    }

    if (i + needed > len) return false;
    for (uint8_t j = 0; j < needed; j++) {
      uint8_t cc = data[i++];
      if ((cc & 0xC0) != 0x80) return false;
      codepoint = (codepoint << 6) | (cc & 0x3F);
    }

    if (needed == 1 && codepoint < 0x80) return false;
    if (needed == 2 && codepoint < 0x0800) return false;
    if (needed == 3 && codepoint < 0x10000) return false;
    if (codepoint > 0x10FFFF) return false;
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return false;
    if (codepoint >= 0xFDD0 && codepoint <= 0xFDEF) return false;
    if ((codepoint & 0xFFFE) == 0xFFFE) return false;
  }
  return true;
}

bool Filter::load(FILESYSTEM* fs) {
  // mix the clock into the PRNG so devices don't drop the same packets in lockstep
  _rng_state ^= (_rtc->getCurrentTime() << 1) | 1u;

  if (fs == nullptr || !fs->exists(FILTER_PREFS_FILE)) return true;

#if defined(RP2040_PLATFORM)
  File file = fs->open(FILTER_PREFS_FILE, "r");
#else
  File file = fs->open(FILTER_PREFS_FILE);
#endif

  if (!file) return false;

  size_t got = file.read(reinterpret_cast<uint8_t*>(&_prefs), sizeof(_prefs));

  // Files written by older firmware are shorter: each field appended since is
  // defaulted when the read did not cover it, so a newer field never inherits
  // whatever landed there.
  if (!FilterStat::fieldLoaded(got, offsetof(FilterPrefs, soft_limit), sizeof(_prefs.soft_limit))) {
    memset(_prefs.soft_limit, 0, sizeof(_prefs.soft_limit));
  }
  if (!FilterStat::fieldLoaded(got, offsetof(FilterPrefs, advert_hours), sizeof(_prefs.advert_hours))) {
    _prefs.advert_hours = 0;
  }
  if (!FilterStat::fieldLoaded(got, offsetof(FilterPrefs, dryrun), sizeof(_prefs.dryrun))) {
    _prefs.dryrun = false;
  }
  if (!FilterStat::fieldLoaded(got, offsetof(FilterPrefs, path_block), sizeof(_prefs.path_block))) {
    memset(_prefs.path_block, 0, sizeof(_prefs.path_block));
  }
  if (!FilterStat::fieldLoaded(got, offsetof(FilterPrefs, sender_rules), sizeof(_prefs.sender_rules))) {
    memset(_prefs.sender_rules, 0, sizeof(_prefs.sender_rules));
  }
  if (!FilterStat::fieldLoaded(got, offsetof(FilterPrefs, text_rules), sizeof(_prefs.text_rules))) {
    memset(_prefs.text_rules, 0, sizeof(_prefs.text_rules));
  }
  if (!FilterStat::fieldLoaded(got, offsetof(FilterPrefs, watch_channels), sizeof(_prefs.watch_channels))) {
    for (int i = 0; i < FILTER_WATCH_COUNT; i++) _prefs.watch_channels[i] = ChannelDetails();
  }
  if (!FilterStat::fieldLoaded(got, offsetof(FilterPrefs, age_mins), sizeof(_prefs.age_mins))) {
    _prefs.age_mins = 0;
  }

  _prefs.filter_enabled = constrain(_prefs.filter_enabled, 0, 1);

  for (uint8_t i = 0; i < PAYLOAD_TYPE_COUNT; ++i) {
    _prefs.payload_prefs[i].hops_max = constrain(_prefs.payload_prefs[i].hops_max, 0, 64);
    // a soft cutoff at/above the hard limit leaves no ramp room -> treat as off
    if (_prefs.soft_limit[i] >= _prefs.payload_prefs[i].rate_limit) _prefs.soft_limit[i] = 0;
    _limiters[i].init(_prefs.payload_prefs[i].rate_limit, _prefs.payload_prefs[i].rate_secs, _prefs.soft_limit[i]);
  }
  _prefs.minimal_hash_bytes = constrain(_prefs.minimal_hash_bytes, 1, 3);
  _prefs.filter_malformed = constrain(_prefs.filter_malformed, 0, 1);
  _prefs.dryrun = constrain(_prefs.dryrun, 0, 1);
  _prefs.advert_hours = constrain(_prefs.advert_hours, 0, ADVERT_MAX_HOURS);
  _prefs.age_mins = constrain(_prefs.age_mins, 0, FILTER_AGE_MAX_MINS);
  _advert.setWindowHours(_prefs.advert_hours);
  for (int i = 0; i < FILTER_PATH_COUNT; i++) {
    if (_prefs.path_block[i].len > FILTER_PATH_MAX_LEN) memset(&_prefs.path_block[i], 0, sizeof(PathPrefix));
  }
  for (int i = 0; i < FILTER_RULE_COUNT; i++) {
    SenderRule& sr = _prefs.sender_rules[i];
    sr.name[FILTER_SENDER_LEN - 1] = '\0';
    if (sr.prob == 0 || sr.prob > 100) sr.prob = 100;
    TextRule& tr = _prefs.text_rules[i];
    tr.text[FILTER_TEXT_LEN - 1] = '\0';
    if (tr.prob == 0 || tr.prob > 100) tr.prob = 100;
  }
  for (int i = 0; i < FILTER_WATCH_COUNT; i++) {
    ChannelDetails& ch = _prefs.watch_channels[i];
    ch.name[sizeof(ch.name) - 1] = '\0';
    if (ch.name[0] != '\0') getChannelHash(ch.name, &ch.channel);   // never trust a stored key
  }

  file.close();
  return true;
}

bool Filter::save(FILESYSTEM* fs) const {
  if (fs == nullptr) return false;

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(FILTER_PREFS_FILE);
  File file = fs->open(FILTER_PREFS_FILE, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = fs->open(FILTER_PREFS_FILE, "w");
#else
  File file = fs->open(FILTER_PREFS_FILE, "w", true);
#endif

  if (!file) return false;

  file.write(reinterpret_cast<const uint8_t*>(&_prefs), sizeof(_prefs));
  file.close();
  return true;
}