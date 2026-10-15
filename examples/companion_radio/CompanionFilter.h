#pragma once

#include <stdint.h>
#include <string.h>

#include <Packet.h>
#include <helpers/ConfigSerializer.h>

// Rule engine, list editing, malformed scan and reply formatting are shared
// with the repeater packet filter. All of these are header-only and free of
// radio, filesystem and clock, so this filter runs on `native` too.
#include "../simple_repeater/FilterStats.h"
#include "../simple_repeater/MessageAge.h"
#include "../simple_repeater/MessageContent.h"
#include "../simple_repeater/PathBlock.h"
#include "../simple_repeater/SenderRules.h"

// DMC receive-side filter for the companion firmware. A companion does not
// normally repeat, so this exists to keep junk and unwanted channel traffic
// away from the user. Two verdicts besides PASS:
//
//   DROP  junk (malformed text, too old, via a blocked repeater): not shown,
//         and not repeated either when the companion has repeat enabled.
//   HIDE  personal taste (sender / text rules): not shown, but still repeated,
//         so one user's mute list never censors the mesh for others.
//
// Settings persist as JSON in CFILTER_PREFS_FILE and are edited with the
// `filter ...` CLI, reachable over the companion protocol (CMD_RUN_CLI_COMMAND).

#define CFILTER_PREFS_FILE "/cfilter.json"

// The tightest reply buffer is remote CLI over the mesh: the reply sits at
// offset 5 of a 166-byte buffer (161 bytes), after an optional 3-byte "xx|"
// prefix echoed from the command. 158 fits every caller.
#define CFILTER_REPLY_SIZE 158

// Recently judged packets remembered, so the copies every nearby repeater
// relays are not judged and counted again.
#define CFILTER_JUDGED_COUNT 16

enum class CFilterVerdict : uint8_t { PASS, HIDE, DROP };

class CompanionFilterPrefs : public ConfigSerializer {
public:
  bool enabled = false;
  bool malformed = false;
  uint16_t age_mins = 0;                                // 0 = off
  PathPrefix path_block[FILTER_PATH_COUNT] = {};
  SenderRule sender_rules[FILTER_RULE_COUNT] = {};
  TextRule text_rules[FILTER_RULE_COUNT] = {};

  CompanionFilterPrefs() : _sender(sender_rules), _text(text_rules) { }
  CompanionFilterPrefs(const CompanionFilterPrefs&) = delete;   // the list serializers point into this object
  CompanionFilterPrefs& operator=(const CompanionFilterPrefs&) = delete;

  void reset();
  void sanitize();   // after a load: clamp anything a hand-edited file got wrong

private:
  // One rule as {"p":"pattern","secs":N,"prob":N}.
  template <typename Rule>
  class RuleSer : public ConfigSerializer {
  public:
    Rule* rule = nullptr;
  protected:
    void structure() override {
      def("p", FilterRules::patternBuf(*rule), FilterRules::patternMax(*rule) + 1);
      def("secs", rule->secs);
      def("prob", rule->prob);
    }
  };

  // A rule list as {"0":{...},"3":{...}}, keyed by slot; empty slots are not written.
  template <typename Rule>
  class RuleListSer : public ConfigSerializer {
    Rule* _rules;
    RuleSer<Rule> _items[FILTER_RULE_COUNT];
  public:
    explicit RuleListSer(Rule* rules) : _rules(rules) {
      for (int i = 0; i < FILTER_RULE_COUNT; i++) _items[i].rule = &_rules[i];
    }
  protected:
    void structure() override {
      static const char* const KEYS[] = { "0", "1", "2", "3", "4", "5", "6", "7" };
      static_assert(FILTER_RULE_COUNT <= sizeof(KEYS) / sizeof(KEYS[0]), "add keys for the extra rule slots");
      for (int i = 0; i < FILTER_RULE_COUNT; i++) {
        if (_context->op() == OP::WRITE && FilterRules::patternOf(_rules[i])[0] == '\0') continue;
        def(KEYS[i], _items[i]);
      }
    }
  };

  RuleListSer<SenderRule> _sender;
  RuleListSer<TextRule> _text;

protected:
  void structure() override {
    def("on", enabled);
    def("malformed", malformed);
    def("age", age_mins);
    def("path", (void*)path_block, sizeof(path_block));
    def("sender", _sender);
    def("text", _text);
  }
};

class CompanionFilter {
  CompanionFilterPrefs _prefs;
  Counters _cnt;
  uint32_t _sender_last[FILTER_RULE_COUNT];   // throttle state per sender rule (millis, 0 = never)
  uint32_t _text_last[FILTER_RULE_COUNT];     // throttle state per text rule
  uint8_t _judged[CFILTER_JUDGED_COUNT][MAX_HASH_SIZE];
  uint8_t _judged_count = 0;
  uint8_t _judged_next = 0;

public:
  CompanionFilter() { clearThrottles(); }

  CompanionFilterPrefs& prefs() { return _prefs; }
  const Counters& counters() const { return _cnt; }
  bool isEnabled() const { return _prefs.enabled; }

  // Whether checkGroupText() can say anything, i.e. whether decrypting a
  // flood group text up front is worth it.
  bool wantsGroupText() const;

  // Flood ADVERT / GRP_TXT / GRP_DATA whose path holds a blocked repeater
  // prefix: DROP. Traffic addressed to this node is never path-blocked.
  CFilterVerdict checkPath(const mesh::Packet* packet);

  // `data` is a decrypted GRP_TXT plaintext (timestamp, flags, "Sender: text").
  // `roll` is a random 0..99 for the rules' probability.
  CFilterVerdict checkGroupText(const uint8_t* data, int len, uint32_t now_secs, uint32_t now_ms, uint8_t roll);

  // True when this packet hash was judged recently; otherwise records it.
  // The caller lets such a copy through to the mesh's own duplicate check.
  bool alreadyJudged(const uint8_t hash[MAX_HASH_SIZE]);

  static bool isCommand(const char* command);

  // `filter ...`. `reply` must hold CFILTER_REPLY_SIZE bytes. Returns true when
  // the preferences changed and should be saved.
  bool handleCommand(const char* command, char* reply, uint32_t now_secs);

  void clearThrottles() {
    memset(_sender_last, 0, sizeof(_sender_last));
    memset(_text_last, 0, sizeof(_text_last));
  }
};

// Firmware-side persistence (CompanionFilterHook.cpp); not built on `native`.
class DataStore;
bool loadCompanionFilter(CompanionFilter& filter, DataStore& store);
bool saveCompanionFilter(CompanionFilter& filter, DataStore& store);
