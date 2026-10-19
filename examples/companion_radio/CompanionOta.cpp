// DMC companion pull OTA from the DutchMeshCore OTA server (`ota check` /
// `ota update`). The decisions are in helpers/OtaManifest.h (native tests); this
// file does the network and flash side.
//
// The device reads <base>/<COMPANION_OTA_VARIANT>.json, the manifest the server
// builds from this repo's dmc-companion-* releases:
//   {"file":"https://.../companion/fw/<tag>/<asset>.bin","version":"v1.18.0","hash":"abc1234","partSig":"..."}
// `ota check` fetches it over plain HTTP (version info only, no TLS heap). An
// update fetches it again over HTTPS and streams the image into the inactive app
// slot, both verified against the roots in CompanionOtaCerts.h, then reboots.
// The flow follows the DMC observer's otaFromManifest (dmc-observer-dev).

#include "CompanionNet.h"

#ifdef DMC_COMPANION_OTA

#include "MyMesh.h"
#include "CompanionOtaCerts.h"
#include <helpers/OtaManifest.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define OTA_MANIFEST_MAX 1024   // a manifest is ~250 bytes

// Signature of the partition table flashed on this device, in the format of
// scripts/partition_signature.py (which build.sh runs to produce the manifest's partSig).
static void devicePartitionSignature(char* out, size_t out_sz) {
  OtaPartEntry e[24];
  int n = 0;
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (it != nullptr && n < (int)(sizeof(e) / sizeof(e[0]))) {
    const esp_partition_t* p = esp_partition_get(it);
    e[n].type = (uint8_t)p->type;
    e[n].subtype = (uint8_t)p->subtype;
    e[n].offset = p->address;
    e[n].size = p->size;
    n++;
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);  // safe on NULL (loop exhausted)
  ota_partsig_format(e, n, out, out_sz);
}

// Fetch the manifest body into buf. tls: verified HTTPS, else plain HTTP.
static bool fetchManifest(const char* base, bool tls, char* buf, size_t buf_sz, char* reply) {
  char murl[192];
  if (tls) {
    int n = snprintf(murl, sizeof(murl), "%s/%s.json", base, COMPANION_OTA_VARIANT);
    if (n <= 0 || (size_t)n >= sizeof(murl) || !ota_url_is_https(murl)) {
      strcpy(reply, "Error: OTA manifest URL");
      return false;
    }
  } else if (!ota_manifest_url(base, COMPANION_OTA_VARIANT, murl, sizeof(murl))) {
    strcpy(reply, "Error: OTA manifest URL");
    return false;
  }

  WiFiClient plain;
  WiFiClientSecure secure;
  HTTPClient http;
  http.setTimeout(15000);
  http.setUserAgent("MeshCore-DMC-Companion-OTA/1.0");
  // HTTP/1.0: a CDN answers 1.1 with a chunked body, which is not read raw here.
  http.useHTTP10(true);
  bool begun;
  if (tls) {
    secure.setCACert(COMPANION_OTA_ROOT_CERTS);
    secure.setTimeout(15);
    begun = http.begin(secure, murl);
  } else {
    begun = http.begin(plain, murl);
  }
  if (!begun) {
    strcpy(reply, "Error: OTA manifest connect failed");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    if (code == 404)    snprintf(reply, CNET_REPLY_SIZE, "Error: no published build for %s", COMPANION_OTA_VARIANT);
    else if (code < 0)  snprintf(reply, CNET_REPLY_SIZE, "Error: OTA manifest %s", http.errorToString(code).c_str());
    else                snprintf(reply, CNET_REPLY_SIZE, "Error: OTA manifest HTTP %d", code);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  size_t len = 0;
  uint32_t deadline = millis() + 15000;
  while (len + 1 < buf_sz && (stream->connected() || stream->available()) && (int32_t)(millis() - deadline) < 0) {
    int avail = stream->available();
    if (avail <= 0) { delay(5); continue; }
    size_t want = buf_sz - 1 - len;
    if ((size_t)avail < want) want = (size_t)avail;
    len += stream->readBytes(buf + len, want);
  }
  buf[len] = 0;
  bool overflow = (len + 1 >= buf_sz) && stream->available() > 0;
  http.end();
  if (len == 0 || overflow) {
    strcpy(reply, overflow ? "Error: OTA manifest too large" : "Error: OTA manifest empty");
    return false;
  }
  return true;
}

static bool otaRunImpl(const char* base, bool dry_run, char* reply) {
  if (WiFi.status() != WL_CONNECTED) {
    strcpy(reply, "Error: Wi-Fi not connected");
    return false;
  }

  char body[OTA_MANIFEST_MAX];
  if (!fetchManifest(base, !dry_run, body, sizeof(body), reply)) return false;

  char url[256], version[32], hash[24], partsig[256];
  ota_json_string(body, "file", url, sizeof(url));
  ota_json_string(body, "version", version, sizeof(version));
  ota_json_string(body, "hash", hash, sizeof(hash));
  ota_json_string(body, "partSig", partsig, sizeof(partsig));
  if (!url[0]) {
    strcpy(reply, "Error: OTA manifest has no file");
    return false;
  }
  // The manifest may only send the device to this product's firmware path, over TLS.
  if (!ota_firmware_url_allowed(url, base) || !ota_url_is_https(url)) {
    strcpy(reply, "Error: OTA manifest file URL not allowed");
    return false;
  }

  char avail[64];
  if (version[0]) snprintf(avail, sizeof(avail), "%s (%s)", version, hash);
  else            snprintf(avail, sizeof(avail), "%s", hash);

  OtaVerdict verdict = ota_compare(FIRMWARE_VERSION, version, hash);
  if (verdict == OTA_VERDICT_CURRENT) {
    snprintf(reply, CNET_REPLY_SIZE, "> up to date: %s", avail);
    return false;
  }
  if (verdict == OTA_VERDICT_OLDER) {
    snprintf(reply, CNET_REPLY_SIZE, "> running %s, newer than published %s", FIRMWARE_VERSION, avail);
    return false;
  }

  // OTA writes the app slot only: an image built for another partition layout,
  // or a build without a second app slot, needs a USB flash. A manifest without
  // partSig skips the layout guard.
  if (partsig[0]) {
    char own_sig[256];
    devicePartitionSignature(own_sig, sizeof(own_sig));
    if (strcmp(own_sig, partsig) != 0) {
      snprintf(reply, CNET_REPLY_SIZE, "> update %s needs a USB flash (partition layout changed)", avail);
      return false;
    }
  }
  if (esp_ota_get_next_update_partition(nullptr) == nullptr) {
    snprintf(reply, CNET_REPLY_SIZE, "> update %s needs a USB flash (single app slot)", avail);
    return false;
  }

  const char* kind = (verdict == OTA_VERDICT_REBUILD) ? " (same version, other build)" : "";
  if (dry_run) {
    snprintf(reply, CNET_REPLY_SIZE, "> update available: %s%s", avail, kind);
    return true;
  }

  // --- flash ---------------------------------------------------------------
  Serial.printf("OTA: %s -> %s, downloading %s\n", FIRMWARE_VERSION, avail, url);
  board.setInhibitSleep(true);   // stay awake through the flash

  WiFiClientSecure client;
  client.setCACert(COMPANION_OTA_ROOT_CERTS);
  client.setTimeout(20);

  // Non-capturing lambdas + a file-static decile, so the global httpUpdate never
  // holds a dangling reference after this function returns.
  static int progress_decile;
  progress_decile = -1;
  httpUpdate.onProgress([](int cur, int total) {
    if (total <= 0) return;
    int d = (int)((int64_t)cur * 10 / total);
    if (d != progress_decile) { progress_decile = d; Serial.printf("OTA: %d%%\n", d * 10); }
  });
  httpUpdate.onEnd([]() { Serial.println("OTA: write complete, rebooting"); });
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(true);
  t_httpUpdate_return ret = httpUpdate.update(client, url);

  // Only reached on failure: a good image reboots inside update().
  board.setInhibitSleep(false);
  if (ret == HTTP_UPDATE_NO_UPDATES) {
    snprintf(reply, CNET_REPLY_SIZE, "Error: OTA server sent no image for %s", avail);
  } else {
    snprintf(reply, CNET_REPLY_SIZE, "Error: OTA failed (%d): %s", (int)ret, httpUpdate.getLastErrorString().c_str());
  }
  return false;
}

// The TLS handshake and the update need far more stack than the loop task has,
// especially when reached through the mesh receive path. Run the work in its own
// 24 KB task and wait for it; the stack is freed when it ends.
struct OtaTaskArgs {
  const char* base;
  bool dry_run;
  char* reply;
  volatile bool result;
  volatile bool done;
};

static void otaTask(void* param) {
  OtaTaskArgs* a = static_cast<OtaTaskArgs*>(param);
  a->result = otaRunImpl(a->base, a->dry_run, a->reply);
  a->done = true;
  vTaskDelete(nullptr);
}

bool companionOtaRun(const char* base, bool dry_run, char* reply) {
  reply[0] = 0;
  OtaTaskArgs args = { base, dry_run, reply, false, false };
  // No core pinning: the ESP32-C3 has only one.
  if (xTaskCreate(otaTask, "ota", 24576, &args, 5, nullptr) != pdPASS) {
    strcpy(reply, "Error: OTA task could not start (low memory)");
    return false;
  }
  while (!args.done) delay(20);   // delay() yields to the OTA task
  return args.result;
}

#endif  // DMC_COMPANION_OTA
