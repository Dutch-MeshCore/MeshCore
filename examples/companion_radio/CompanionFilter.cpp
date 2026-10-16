#include "CompanionFilter.h"

#include <Utils.h>

// Copied into the reply verbatim; guard them at compile time so a future
// subcommand cannot overflow the reply buffer silently.
static const char CFILTER_HELP[] =
    "> filter [ help | on | off | reset | stats | malformed | age | path | sender | text ]";
static const char CFILTER_STATS_HELP[] =
    "> filter stats [ malformed | age | path | sender | text ]";
static const char CFILTER_PATH_HELP[] =
    "> filter path [ list | add | remove ] <hex prefix, 2-8 digits>";
static const char CFILTER_RULE_HELP[] =
    "> filter sender|text [ list | add <pattern> [secs] [prob] | remove <pattern> ]";
static const char CFILTER_RULE_ARGS_ERR[] =
    "> Filter: error <secs> range is 0-65535, <prob> range is 1-100";

static_assert(sizeof(CFILTER_HELP) <= CFILTER_REPLY_SIZE, "filter help text no longer fits the reply buffer");
static_assert(sizeof(CFILTER_STATS_HELP) <= CFILTER_REPLY_SIZE, "filter stats help text no longer fits the reply buffer");
static_assert(sizeof(CFILTER_PATH_HELP) <= CFILTER_REPLY_SIZE, "filter path help text no longer fits the reply buffer");
static_assert(sizeof(CFILTER_RULE_HELP) <= CFILTER_REPLY_SIZE, "filter rule help text no longer fits the reply buffer");

// ---- preferences ---------------------------------------------------------------

void CompanionFilterPrefs::reset() {
  enabled = false;
  malformed = false;
  age_mins = 0;
  memset(path_block, 0, sizeof(path_block));
  memset(sender_rules, 0, sizeof(sender_rules));
  memset(text_rules, 0, sizeof(text_rules));
}

void CompanionFilterPrefs::sanitize() {
  if (age_mins > FILTER_AGE_MAX_MINS) age_mins = FILTER_AGE_MAX_MINS;
  for (int i = 0; i < FILTER_PATH_COUNT; i++) {
    if (path_block[i].len > FILTER_PATH_MAX_LEN) memset(&path_block[i], 0, sizeof(PathPrefix));
  }
  for (int i = 0; i < FILTER_RULE_COUNT; i++) {
    SenderRule& sr = sender_rules[i];
    sr.name[FILTER_SENDER_LEN - 1] = '\0';
    if (sr.prob == 0 || sr.prob > 100) sr.prob = 100;
    TextRule& tr = text_rules[i];
    tr.text[FILTER_TEXT_LEN - 1] = '\0';
    if (tr.prob == 0 || tr.prob > 100) tr.prob = 100;
  }
}

// ---- verdicts ------------------------------------------------------------------

bool CompanionFilter::wantsGroupText() const {
  if (!_prefs.enabled) return false;
  return _prefs.malformed || _prefs.age_mins > 0 ||
         FilterRules::anyRule(_prefs.sender_rules, FILTER_RULE_COUNT) ||
         FilterRules::anyRule(_prefs.text_rules, FILTER_RULE_COUNT);
}

CFilterVerdict CompanionFilter::checkPath(const mesh::Packet* packet) {
  if (!_prefs.enabled || packet == nullptr || !packet->isRouteFlood()) return CFilterVerdict::PASS;

  uint8_t type = packet->getPayloadType();
  if (type != PAYLOAD_TYPE_ADVERT && type != PAYLOAD_TYPE_GRP_TXT && type != PAYLOAD_TYPE_GRP_DATA) {
    return CFilterVerdict::PASS;
  }

  int slot = FilterPath::findMatch(packet->path, packet->getPathHashSize(), packet->getPathHashCount(),
                                   _prefs.path_block, FILTER_PATH_COUNT);
  if (slot < 0) return CFilterVerdict::PASS;
  FilterStat::recordPath(_cnt, slot);
  return CFilterVerdict::DROP;
}

CFilterVerdict CompanionFilter::checkGroupText(const uint8_t* data, int len, uint32_t now_secs, uint32_t now_ms, uint8_t roll) {
  if (!_prefs.enabled || data == nullptr || len <= 0) return CFilterVerdict::PASS;
  if (len > 255) len = 255;   // a LoRa payload never is; keeps the uint8_t length below honest

  // junk first: it is dropped outright, not merely hidden
  if (_prefs.malformed) {
    uint8_t reason;
    if (!MessageContent::validate(data, (uint8_t)len, now_secs, &reason)) {
      FilterStat::recordMalformed(_cnt, reason);
      return CFilterVerdict::DROP;
    }
  }

  if (_prefs.age_mins > 0 && len >= 4) {
    uint32_t ts;
    memcpy(&ts, data, 4);
    if (MessageAge::tooOld(ts, now_secs, _prefs.age_mins)) {
      FilterStat::recordAge(_cnt);
      return CFilterVerdict::DROP;
    }
  }

  // then the user's own rules
  FilterRules::GroupText gt;
  if (!FilterRules::parseGroupText(data, len, &gt)) return CFilterVerdict::PASS;

  int slot = FilterRules::evaluate(_prefs.sender_rules, FILTER_RULE_COUNT, now_ms, roll, _sender_last, _cnt.sender_pass,
                                   [&](const SenderRule& r) { return FilterRules::matchName(r.name, gt.sender, gt.sender_len); });
  if (slot >= 0) {
    FilterStat::recordSender(_cnt, slot);
    return CFilterVerdict::HIDE;
  }
  slot = FilterRules::evaluate(_prefs.text_rules, FILTER_RULE_COUNT, now_ms, roll, _text_last, _cnt.text_pass,
                               [&](const TextRule& r) { return FilterRules::matchText(r.text, gt.text, gt.text_len); });
  if (slot >= 0) {
    FilterStat::recordText(_cnt, slot);
    return CFilterVerdict::HIDE;
  }
  return CFilterVerdict::PASS;
}

bool CompanionFilter::alreadyJudged(const uint8_t hash[MAX_HASH_SIZE]) {
  for (int i = 0; i < _judged_count; i++) {
    if (memcmp(_judged[i], hash, MAX_HASH_SIZE) == 0) return true;
  }
  memcpy(_judged[_judged_next], hash, MAX_HASH_SIZE);
  _judged_next = (uint8_t)((_judged_next + 1) % CFILTER_JUDGED_COUNT);
  if (_judged_count < CFILTER_JUDGED_COUNT) _judged_count++;
  return false;
}

// ---- CLI -----------------------------------------------------------------------

bool CompanionFilter::isCommand(const char* command) {
  return command != nullptr && strncmp(command, "filter", 6) == 0 && (command[6] == '\0' || command[6] == ' ');
}

static void formatSummary(char* out, const Counters& c, bool enabled) {
  FilterStat::Buf b(out, CFILTER_REPLY_SIZE);
  b.add("> Filter %s: Hidden [ Malformed: %lu | Age: %lu | Path: %lu | Sender: %lu | Text: %lu ]",
        enabled ? "on" : "off",
        (unsigned long)c.malformed, (unsigned long)c.age, (unsigned long)c.path,
        (unsigned long)c.sender, (unsigned long)c.text);
  b.markTruncated("..");
}

bool CompanionFilter::handleCommand(const char* command, char* reply, uint32_t now_secs) {
  char buf[CFILTER_REPLY_SIZE];
  strncpy(buf, command, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  const char* parts[6];
  int n = mesh::Utils::parseTextParts(buf, parts, 6, ' ');
  bool changed = false;

  if (n <= 1) {
    formatSummary(reply, _cnt, _prefs.enabled);
    return false;
  }

  const char* sub = parts[1];

  if (n == 2) {
    if (strcmp(sub, "help") == 0) {
      strcpy(reply, CFILTER_HELP);
    } else if (strcmp(sub, "on") == 0 || strcmp(sub, "off") == 0) {
      _prefs.enabled = strcmp(sub, "on") == 0;
      strcpy(reply, _prefs.enabled ? "> Filter: on" : "> Filter: off");
      changed = true;
    } else if (strcmp(sub, "reset") == 0) {
      _prefs.reset();
      clearThrottles();
      strcpy(reply, "> Filter: preferences reset");
      changed = true;
    } else if (strcmp(sub, "stats") == 0) {
      strcpy(reply, CFILTER_STATS_HELP);
    } else if (strcmp(sub, "malformed") == 0) {
      sprintf(reply, "> Filter: malformed text scan %s", _prefs.malformed ? "on" : "off");
    } else if (strcmp(sub, "age") == 0) {
      FilterStat::formatStatsAge(reply, CFILTER_REPLY_SIZE, _cnt, _prefs.age_mins, MessageAge::clockSet(now_secs));
    } else if (strcmp(sub, "path") == 0) {
      strcpy(reply, CFILTER_PATH_HELP);
    } else if (strcmp(sub, "sender") == 0 || strcmp(sub, "text") == 0) {
      strcpy(reply, CFILTER_RULE_HELP);
    } else {
      strcpy(reply, "> Filter: command error");
    }
    return changed;
  }

  const char* arg = parts[2];

  if (strcmp(sub, "stats") == 0) {
    if (strcmp(arg, "malformed") == 0) {
      FilterStat::formatStatsMalformed(reply, CFILTER_REPLY_SIZE, _cnt);
    } else if (strcmp(arg, "age") == 0) {
      FilterStat::formatStatsAge(reply, CFILTER_REPLY_SIZE, _cnt, _prefs.age_mins, MessageAge::clockSet(now_secs));
    } else if (strcmp(arg, "path") == 0) {
      FilterStat::formatPathList(reply, CFILTER_REPLY_SIZE, _prefs.path_block, &_cnt);
    } else if (strcmp(arg, "sender") == 0) {
      FilterStat::formatRuleList(reply, CFILTER_REPLY_SIZE, _prefs.sender_rules, FILTER_RULE_COUNT, _cnt.sender_slot, _cnt.sender_pass);
    } else if (strcmp(arg, "text") == 0) {
      FilterStat::formatRuleList(reply, CFILTER_REPLY_SIZE, _prefs.text_rules, FILTER_RULE_COUNT, _cnt.text_slot, _cnt.text_pass);
    } else {
      strcpy(reply, CFILTER_STATS_HELP);
    }

  } else if (strcmp(sub, "malformed") == 0) {
    if (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0) {
      _prefs.malformed = strcmp(arg, "on") == 0;
      strcpy(reply, _prefs.malformed ? "> Filter: malformed scan on" : "> Filter: malformed scan off");
      changed = true;
    } else {
      strcpy(reply, "> Filter: syntax error 'filter malformed <on | off>'");
    }

  } else if (strcmp(sub, "age") == 0) {
    uint16_t mins;
    if (MessageAge::parseMins(arg, &mins)) {
      _prefs.age_mins = mins;
      strcpy(reply, "> Filter: OK");
      changed = true;
    } else {
      strcpy(reply, "> Filter: error <minutes> range is 1-10080, or off");
    }

  } else if (strcmp(sub, "path") == 0) {
    char hex[2 * FILTER_PATH_MAX_LEN + 1];
    bool is_add = strcmp(arg, "add") == 0;
    if (strcmp(arg, "list") == 0) {
      FilterStat::formatPathList(reply, CFILTER_REPLY_SIZE, _prefs.path_block, nullptr);
    } else if (n >= 4 && (is_add || strcmp(arg, "remove") == 0)) {
      bool ok = is_add ? FilterPath::add(_prefs.path_block, FILTER_PATH_COUNT, parts[3], hex)
                       : FilterPath::remove(_prefs.path_block, FILTER_PATH_COUNT, parts[3], hex);
      if (ok) {
        sprintf(reply, "> Filter: path %s %s", hex, is_add ? "added" : "removed");
        changed = true;
      } else if (hex[0] == '\0') {
        strcpy(reply, "> Filter: error path prefix is 2-8 hex digits");
      } else {
        strcpy(reply, "Failed");
      }
    } else {
      strcpy(reply, CFILTER_PATH_HELP);
    }

  } else if (strcmp(sub, "sender") == 0 || strcmp(sub, "text") == 0) {
    bool is_sender = strcmp(sub, "sender") == 0;
    if (strcmp(arg, "list") == 0) {
      if (is_sender) {
        FilterStat::formatRuleList(reply, CFILTER_REPLY_SIZE, _prefs.sender_rules, FILTER_RULE_COUNT, nullptr, nullptr);
      } else {
        FilterStat::formatRuleList(reply, CFILTER_REPLY_SIZE, _prefs.text_rules, FILTER_RULE_COUNT, nullptr, nullptr);
      }
    } else if (n >= 4 && strcmp(arg, "add") == 0) {
      uint16_t secs;
      uint8_t prob;
      size_t max_len = is_sender ? FILTER_SENDER_LEN - 1 : FILTER_TEXT_LEN - 1;
      if (strlen(parts[3]) > max_len) {
        sprintf(reply, "> Filter: error <pattern> max %u chars", (unsigned)max_len);
      } else if (!FilterRules::parseArgs(n >= 5 ? parts[4] : nullptr, n >= 6 ? parts[5] : nullptr, &secs, &prob)) {
        strcpy(reply, CFILTER_RULE_ARGS_ERR);
      } else if (is_sender ? FilterRules::addRule(_prefs.sender_rules, FILTER_RULE_COUNT, parts[3], secs, prob)
                           : FilterRules::addRule(_prefs.text_rules, FILTER_RULE_COUNT, parts[3], secs, prob)) {
        snprintf(reply, CFILTER_REPLY_SIZE, "> Filter: %s %s added", sub, parts[3]);
        changed = true;
      } else {
        strcpy(reply, "Failed");
      }
    } else if (n >= 4 && strcmp(arg, "remove") == 0) {
      int slot = is_sender ? FilterRules::removeRule(_prefs.sender_rules, FILTER_RULE_COUNT, parts[3])
                           : FilterRules::removeRule(_prefs.text_rules, FILTER_RULE_COUNT, parts[3]);
      if (slot >= 0) {
        (is_sender ? _sender_last : _text_last)[slot] = 0;
        snprintf(reply, CFILTER_REPLY_SIZE, "> Filter: %s %s removed", sub, parts[3]);
        changed = true;
      } else {
        strcpy(reply, "Failed");
      }
    } else {
      strcpy(reply, CFILTER_RULE_HELP);
    }

  } else {
    strcpy(reply, "> Filter: command error");
  }
  return changed;
}
