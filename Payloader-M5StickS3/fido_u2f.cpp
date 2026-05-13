#include "fido_u2f.h"
#include "fido_hid.h"
#include <Arduino.h>
#include <Preferences.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#include <mbedtls/bignum.h>
#include <mbedtls/ecp.h>
#include <string.h>

// mbedTLS 3.x hides struct members behind MBEDTLS_PRIVATE; 2.x exposes them directly.
#ifndef MBEDTLS_PRIVATE
#define MBEDTLS_PRIVATE(x) x
#endif

// ---------------------------------------------------------------------------
// Persistent storage
// ---------------------------------------------------------------------------
static Preferences prefs;

// Master wrapping key (AES-256)
static uint8_t s_masterKey[32] = {};
static bool    s_masterKeyLoaded = false;

// Attestation certificate and key
static uint8_t  s_attestCert[1024] = {};
static uint16_t s_attestCertLen    = 0;
static uint8_t  s_attestPriv[32]   = {};
static bool     s_attestLoaded     = false;

// NVS sign counter
static uint32_t s_counter = 0;

// ---------------------------------------------------------------------------
// RNG context (shared)
// ---------------------------------------------------------------------------
static mbedtls_entropy_context   s_entropy;
static mbedtls_ctr_drbg_context  s_ctr_drbg;
static bool                      s_rngInit = false;

static void ensureRng() {
  if (s_rngInit) return;
  mbedtls_entropy_init(&s_entropy);
  mbedtls_ctr_drbg_init(&s_ctr_drbg);
  const char* pers = "fido_u2f";
  mbedtls_ctr_drbg_seed(&s_ctr_drbg, mbedtls_entropy_func, &s_entropy,
                         (const uint8_t*)pers, strlen(pers));
  s_rngInit = true;
}

// ---------------------------------------------------------------------------
// Master key — load or generate
// ---------------------------------------------------------------------------
static void ensureMasterKey() {
  if (s_masterKeyLoaded) return;
  ensureRng();

  prefs.begin("fido", false);
  size_t stored = prefs.getBytesLength("mkey");
  if (stored == 32) {
    prefs.getBytes("mkey", s_masterKey, 32);
  } else {
    // Generate new master key
    mbedtls_ctr_drbg_random(&s_ctr_drbg, s_masterKey, 32);
    prefs.putBytes("mkey", s_masterKey, 32);
  }
  s_counter = prefs.getUInt("ctr", 0);
  prefs.end();

  s_masterKeyLoaded = true;
}

// ---------------------------------------------------------------------------
// AES-256-CBC key wrapping
// IV = all zeros (the appId provides domain separation via the plaintext)
// ---------------------------------------------------------------------------
static void wrapKey(const uint8_t appId[32], const uint8_t privKey[32], uint8_t kh[64]) {
  ensureMasterKey();

  // plaintext = appId[32] || privKey[32]
  uint8_t plain[64];
  memcpy(plain,      appId,   32);
  memcpy(plain + 32, privKey, 32);

  uint8_t iv[16] = {};  // all-zero IV

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, s_masterKey, 256);
  mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, 64, iv, plain, kh);
  mbedtls_aes_free(&ctx);
}

static bool unwrapKey(const uint8_t kh[64], const uint8_t appId[32], uint8_t privKey[32]) {
  ensureMasterKey();

  uint8_t plain[64];
  uint8_t iv[16] = {};

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_dec(&ctx, s_masterKey, 256);
  mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, 64, iv, kh, plain);
  mbedtls_aes_free(&ctx);

  // Verify appId matches
  if (memcmp(plain, appId, 32) != 0) return false;

  memcpy(privKey, plain + 32, 32);
  return true;
}

// ---------------------------------------------------------------------------
// EC P-256 key pair generation
// pubKey65: uncompressed point 0x04 || X[32] || Y[32]
// privKey32: raw 32-byte scalar
// ---------------------------------------------------------------------------
static bool generateKeyPair(uint8_t pubKey65[65], uint8_t privKey32[32]) {
  ensureRng();

  mbedtls_ecp_keypair kp;
  mbedtls_ecp_keypair_init(&kp);

  int ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,
                                 &kp,
                                 mbedtls_ctr_drbg_random,
                                 &s_ctr_drbg);
  if (ret != 0) {
    mbedtls_ecp_keypair_free(&kp);
    return false;
  }

  // Export private key (big-endian 32 bytes)
  ret = mbedtls_mpi_write_binary(&kp.MBEDTLS_PRIVATE(d), privKey32, 32);
  if (ret != 0) {
    mbedtls_ecp_keypair_free(&kp);
    return false;
  }

  // Export public key — uncompressed
  pubKey65[0] = 0x04;
  ret  = mbedtls_mpi_write_binary(&kp.MBEDTLS_PRIVATE(Q).MBEDTLS_PRIVATE(X), pubKey65 + 1,  32);
  ret |= mbedtls_mpi_write_binary(&kp.MBEDTLS_PRIVATE(Q).MBEDTLS_PRIVATE(Y), pubKey65 + 33, 32);

  mbedtls_ecp_keypair_free(&kp);
  return (ret == 0);
}

// ---------------------------------------------------------------------------
// ECDSA sign — returns DER-encoded SEQUENCE { INTEGER r, INTEGER s }
// derOut must be at least 72 bytes
// ---------------------------------------------------------------------------
static bool ecdsaSign(const uint8_t privKey32[32], const uint8_t hash32[32],
                      uint8_t* derOut, size_t* derLen) {
  ensureRng();

  mbedtls_ecp_keypair kp;
  mbedtls_ecp_keypair_init(&kp);
  mbedtls_ecp_group_load(&kp.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_SECP256R1);
  mbedtls_mpi_read_binary(&kp.MBEDTLS_PRIVATE(d), privKey32, 32);
  mbedtls_ecp_mul(&kp.MBEDTLS_PRIVATE(grp), &kp.MBEDTLS_PRIVATE(Q),
                  &kp.MBEDTLS_PRIVATE(d), &kp.MBEDTLS_PRIVATE(grp).G,
                  mbedtls_ctr_drbg_random, &s_ctr_drbg);

  mbedtls_mpi r, s;
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);

  int ret = mbedtls_ecdsa_sign(&kp.MBEDTLS_PRIVATE(grp), &r, &s, &kp.MBEDTLS_PRIVATE(d),
                                hash32, 32,
                                mbedtls_ctr_drbg_random, &s_ctr_drbg);
  mbedtls_ecp_keypair_free(&kp);

  if (ret != 0) {
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    return false;
  }

  // Encode as DER SEQUENCE { INTEGER r, INTEGER s }
  // Each INTEGER may need a leading 0x00 if the high bit is set
  uint8_t rBuf[33], sBuf[33];
  size_t  rLen = 32, sLen = 32;

  mbedtls_mpi_write_binary(&r, rBuf + 1, 32);
  mbedtls_mpi_write_binary(&s, sBuf + 1, 32);
  mbedtls_mpi_free(&r);
  mbedtls_mpi_free(&s);

  // Check if leading zero needed
  bool rPad = (rBuf[1] & 0x80) != 0;
  bool sPad = (sBuf[1] & 0x80) != 0;
  rBuf[0] = 0x00;
  sBuf[0] = 0x00;

  const uint8_t* rPtr = rPad ? rBuf       : rBuf + 1;
  const uint8_t* sPtr = sPad ? sBuf       : sBuf + 1;
  rLen = rPad ? 33 : 32;
  sLen = sPad ? 33 : 32;

  // Trim leading zeros but keep at least 1 byte and preserve sign
  while (rLen > 1 && rPtr[0] == 0x00 && (rPtr[1] & 0x80) == 0) { rPtr++; rLen--; }
  while (sLen > 1 && sPtr[0] == 0x00 && (sPtr[1] & 0x80) == 0) { sPtr++; sLen--; }

  size_t seqBody = 2 + rLen + 2 + sLen;
  size_t total   = 2 + seqBody;

  if (total > 72) { return false; } // safety

  uint8_t* p = derOut;
  *p++ = 0x30;                          // SEQUENCE
  *p++ = (uint8_t)seqBody;
  *p++ = 0x02;                          // INTEGER r
  *p++ = (uint8_t)rLen;
  memcpy(p, rPtr, rLen); p += rLen;
  *p++ = 0x02;                          // INTEGER s
  *p++ = (uint8_t)sLen;
  memcpy(p, sPtr, sLen); p += sLen;

  *derLen = (size_t)(p - derOut);
  return true;
}

// ---------------------------------------------------------------------------
// Self-signed attestation certificate — hand-crafted DER (no x509write needed)
//
// TBSCertificate structure is fixed except for the 65-byte EC public key.
// Layout (199 bytes content inside outer SEQUENCE):
//   [0] v3, serial=1, sigAlg=ecdsaWithSHA256,
//   issuer/subject = CN=M5 Security Key,
//   validity 2020-2040, SPKI = EC P-256 uncompressed point
// ---------------------------------------------------------------------------

// Fixed TBS prefix: everything up to (but not including) the 65-byte pubkey point.
// TBS SEQUENCE header claims content length = 199 (0xC7).
static const uint8_t TBS_PREFIX[] = {
  // TBS SEQUENCE  (tag=0x30, length=0x81,0xC7 = 199)
  0x30, 0x81, 0xC7,
  // [0] version v3
  0xA0, 0x03, 0x02, 0x01, 0x02,
  // serial INTEGER 1
  0x02, 0x01, 0x01,
  // signatureAlgorithm SEQUENCE { OID ecdsaWithSHA256 }
  0x30, 0x0A,
    0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02,
  // issuer SEQUENCE { SET { SEQUENCE { OID commonName, UTF8 "M5 Security Key" } } }
  0x30, 0x1A,
    0x31, 0x18,
      0x30, 0x16,
        0x06, 0x03, 0x55, 0x04, 0x03,
        0x0C, 0x0F,
          'M','5',' ','S','e','c','u','r','i','t','y',' ','K','e','y',
  // validity SEQUENCE { UTCTime "200101000000Z", UTCTime "400101000000Z" }
  0x30, 0x1E,
    0x17, 0x0D, '2','0','0','1','0','1','0','0','0','0','0','0','Z',
    0x17, 0x0D, '4','0','0','1','0','1','0','0','0','0','0','0','Z',
  // subject (same as issuer)
  0x30, 0x1A,
    0x31, 0x18,
      0x30, 0x16,
        0x06, 0x03, 0x55, 0x04, 0x03,
        0x0C, 0x0F,
          'M','5',' ','S','e','c','u','r','i','t','y',' ','K','e','y',
  // subjectPublicKeyInfo SEQUENCE (89 bytes)
  0x30, 0x59,
    // algorithm SEQUENCE { OID ecPublicKey, OID prime256v1 }
    0x30, 0x13,
      0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01,
      0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07,
    // subjectPublicKey BIT STRING (66 bytes: 0x00 + pubKey65)
    0x03, 0x42, 0x00
    // ← pubKey65[65] appended immediately after this array
};

static bool generateAttestCert(const uint8_t* privKey32, const uint8_t* pubKey65) {
  ensureRng();

  // Build TBS = TBS_PREFIX || pubKey65
  uint8_t tbs[202];
  memcpy(tbs, TBS_PREFIX, sizeof(TBS_PREFIX));
  memcpy(tbs + sizeof(TBS_PREFIX), pubKey65, 65);
  uint16_t tbsLen = (uint16_t)(sizeof(TBS_PREFIX) + 65);  // 202

  // SHA-256 the TBS
  uint8_t tbsHash[32];
  {
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    mbedtls_sha256_update(&sha, tbs, tbsLen);
    mbedtls_sha256_finish(&sha, tbsHash);
    mbedtls_sha256_free(&sha);
  }

  // ECDSA sign the TBS hash with the attestation private key
  uint8_t sig[72];
  size_t  sigLen = 0;
  if (!ecdsaSign(privKey32, tbsHash, sig, &sigLen)) return false;

  // Build outer Certificate:
  //   SEQUENCE {
  //     TBSCertificate (202 bytes)
  //     signatureAlgorithm SEQUENCE { OID ecdsaWithSHA256 }  (12 bytes)
  //     signatureValue BIT STRING { 0x00 || sig_der }
  //   }
  static const uint8_t SIG_ALG[] = {
    0x30, 0x0A, 0x06, 0x08,
    0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02
  };
  uint16_t bitStrContentLen = (uint16_t)(1 + sigLen);       // 0x00 + sig
  uint16_t bitStrTotalLen   = (uint16_t)(3 + bitStrContentLen); // tag + len + content (len < 128)
  uint16_t outerContent     = tbsLen + sizeof(SIG_ALG) + bitStrTotalLen;

  uint8_t* p = s_attestCert;
  *p++ = 0x30;           // outer SEQUENCE
  if (outerContent < 128) {
    *p++ = (uint8_t)outerContent;
  } else {
    *p++ = 0x82;
    *p++ = (uint8_t)(outerContent >> 8);
    *p++ = (uint8_t)outerContent;
  }
  memcpy(p, tbs, tbsLen); p += tbsLen;
  memcpy(p, SIG_ALG, sizeof(SIG_ALG)); p += sizeof(SIG_ALG);
  *p++ = 0x03;
  *p++ = (uint8_t)bitStrContentLen;
  *p++ = 0x00;  // no unused bits
  memcpy(p, sig, sigLen); p += sigLen;

  s_attestCertLen = (uint16_t)(p - s_attestCert);
  return true;
}

static void ensureAttestation() {
  if (s_attestLoaded) return;
  ensureMasterKey();
  ensureRng();

  prefs.begin("fido", false);

  size_t aKeyLen  = prefs.getBytesLength("akey");
  size_t aCertLen = prefs.getBytesLength("acert");

  if (aKeyLen == 32 && aCertLen > 0 && aCertLen <= sizeof(s_attestCert)) {
    prefs.getBytes("akey",  s_attestPriv, 32);
    s_attestCertLen = (uint16_t)aCertLen;
    prefs.getBytes("acert", s_attestCert, aCertLen);
  } else {
    // Generate attestation key pair
    uint8_t pubKey65[65];
    generateKeyPair(pubKey65, s_attestPriv);

    // Generate self-signed cert
    if (generateAttestCert(s_attestPriv, pubKey65)) {
      prefs.putBytes("akey",  s_attestPriv, 32);
      prefs.putBytes("acert", s_attestCert, s_attestCertLen);
    }
  }

  prefs.end();
  s_attestLoaded = true;
}

// ---------------------------------------------------------------------------
// Pending operation storage for deferred response
// ---------------------------------------------------------------------------
// We store enough info to complete the response in fidoCompleteOp()

static struct {
  bool    isRegister;
  uint8_t appId[32];
  uint8_t challenge[32];
  uint8_t keyHandle[64];   // only for authenticate
  uint8_t khLen;
  uint8_t p1;
  // Generated during setup (for REGISTER), used in completion
  uint8_t pubKey65[65];
  uint8_t privKey32[32];
  uint8_t newKh[64];       // wrapped key handle for register response
} s_pending;

// ---------------------------------------------------------------------------
// u2fBuildResponse — called from fidoCompleteOp() after user action
// ---------------------------------------------------------------------------
void u2fBuildResponse(uint8_t* resp, uint16_t* respLen, uint16_t* sw) {
  FidoState state;
  {
    // Read state atomically — by the time fidoCompleteOp calls us the state
    // is already CONFIRMED or DECLINED (set before calling fidoCompleteOp)
    state = g_fidoState;
  }

  if (state == FIDO_DECLINED) {
    *respLen = 0;
    *sw      = SW_CONDITIONS_NOT_SAT;
    return;
  }

  // CONFIRMED
  ensureAttestation();

  if (s_pending.isRegister) {
    // Build registration response:
    // 0x05 | pubKey[65] | khLen[1] | kh[64] | cert_der | sig_der
    uint8_t* p = resp;
    *p++ = 0x05;
    memcpy(p, s_pending.pubKey65, 65); p += 65;
    *p++ = 64; // key handle length
    memcpy(p, s_pending.newKh, 64);   p += 64;

    // Attestation cert
    memcpy(p, s_attestCert, s_attestCertLen); p += s_attestCertLen;

    // Signature over SHA-256(0x00 || appId || challenge || kh || pubKey)
    uint8_t sigData[1 + 32 + 32 + 64 + 65];
    sigData[0] = 0x00;
    memcpy(sigData + 1,       s_pending.appId,    32);
    memcpy(sigData + 33,      s_pending.challenge, 32);
    memcpy(sigData + 65,      s_pending.newKh,    64);
    memcpy(sigData + 129,     s_pending.pubKey65, 65);

    uint8_t hash[32];
    { mbedtls_sha256_context _s; mbedtls_sha256_init(&_s); mbedtls_sha256_starts(&_s,0); mbedtls_sha256_update(&_s,sigData,sizeof(sigData)); mbedtls_sha256_finish(&_s,hash); mbedtls_sha256_free(&_s); }

    uint8_t sig[72];
    size_t  sigLen = 0;
    ecdsaSign(s_attestPriv, hash, sig, &sigLen);
    memcpy(p, sig, sigLen); p += sigLen;

    *respLen = (uint16_t)(p - resp);
    *sw      = SW_NO_ERROR;

  } else {
    // Authenticate
    // Unwrap private key
    uint8_t privKey[32];
    if (!unwrapKey(s_pending.keyHandle, s_pending.appId, privKey)) {
      *respLen = 0;
      *sw      = SW_WRONG_DATA;
      return;
    }

    // Increment counter
    ensureMasterKey();
    s_counter++;
    prefs.begin("fido", false);
    prefs.putUInt("ctr", s_counter);
    prefs.end();

    uint8_t flags     = 0x01; // user presence
    uint32_t ctr      = s_counter;
    uint8_t ctrBe[4]  = {
      (uint8_t)(ctr >> 24),
      (uint8_t)(ctr >> 16),
      (uint8_t)(ctr >>  8),
      (uint8_t)(ctr      )
    };

    // Signature over SHA-256(appId || flags || counter_be || challenge)
    uint8_t sigData[32 + 1 + 4 + 32];
    memcpy(sigData,      s_pending.appId,    32);
    sigData[32] = flags;
    memcpy(sigData + 33, ctrBe,             4);
    memcpy(sigData + 37, s_pending.challenge, 32);

    uint8_t hash[32];
    { mbedtls_sha256_context _s; mbedtls_sha256_init(&_s); mbedtls_sha256_starts(&_s,0); mbedtls_sha256_update(&_s,sigData,sizeof(sigData)); mbedtls_sha256_finish(&_s,hash); mbedtls_sha256_free(&_s); }

    uint8_t sig[72];
    size_t  sigLen = 0;
    ecdsaSign(privKey, hash, sig, &sigLen);

    uint8_t* p = resp;
    *p++ = flags;
    memcpy(p, ctrBe, 4); p += 4;
    memcpy(p, sig, sigLen); p += sigLen;

    *respLen = (uint16_t)(p - resp);
    *sw      = SW_NO_ERROR;
  }
}

// ---------------------------------------------------------------------------
// u2fProcessApdu — non-blocking APDU handler
// ---------------------------------------------------------------------------
uint16_t u2fProcessApdu(const uint8_t* apdu, uint16_t apduLen,
                        uint8_t* resp, uint16_t* respLen,
                        uint32_t cid, uint8_t hidCmd) {
  *respLen = 0;

  // Silently decline all requests when no key app is on screen
  if (!g_usbKeyActive && !g_btKeyActive) return SW_CONDITIONS_NOT_SAT;

  if (apduLen < 4) return SW_WRONG_DATA;

  // uint8_t cla = apdu[0]; // always 0x00 for U2F
  uint8_t ins = apdu[1];
  uint8_t p1  = apdu[2];
  // uint8_t p2  = apdu[3];

  // Parse Lc / Le — handle extended length (Lc[0]==0x00 means 3-byte length)
  uint16_t lc    = 0;
  uint16_t dataOff = 5;

  if (apduLen < 5) {
    // No data; might be just INS
  } else if (apdu[4] == 0x00 && apduLen >= 7) {
    // Extended length
    lc      = ((uint16_t)apdu[5] << 8) | apdu[6];
    dataOff = 7;
  } else {
    lc      = apdu[4];
    dataOff = 5;
  }

  const uint8_t* data = (apduLen > dataOff) ? (apdu + dataOff) : nullptr;

  switch (ins) {
    // -----------------------------------------------------------------------
    // VERSION (0x03)
    // -----------------------------------------------------------------------
    case 0x03: {
      const char ver[] = "U2F_V2";
      memcpy(resp, ver, 6);
      *respLen = 6;
      return SW_NO_ERROR;
    }

    // -----------------------------------------------------------------------
    // REGISTER (0x01)
    // -----------------------------------------------------------------------
    case 0x01: {
      if (lc < 64 || !data) return SW_WRONG_DATA;

      // Parse challenge[32] || appId[32]
      const uint8_t* challenge = data;
      const uint8_t* appId     = data + 32;

      // Set up pending state
      s_pending.isRegister = true;
      memcpy(s_pending.challenge, challenge, 32);
      memcpy(s_pending.appId,     appId,     32);

      // Pre-generate key pair (deterministic, before user prompt)
      if (!generateKeyPair(s_pending.pubKey65, s_pending.privKey32)) {
        return SW_WRONG_DATA;
      }
      wrapKey(appId, s_pending.privKey32, s_pending.newKh);

      // Copy to g_fidoPending for the UI
      g_fidoPending.isRegister = true;
      memcpy(g_fidoPending.appId,     appId,     32);
      memcpy(g_fidoPending.challenge, challenge, 32);
      g_fidoPending.cid = cid;
      g_fidoPending.cmd = hidCmd;
      g_fidoPending.p1  = p1;

      // Signal UI
      g_fidoState = FIDO_WAITING_UP;

      return SW_PENDING;
    }

    // -----------------------------------------------------------------------
    // AUTHENTICATE (0x02)
    // -----------------------------------------------------------------------
    case 0x02: {
      if (lc < 65 || !data) return SW_WRONG_DATA;

      const uint8_t* challenge = data;
      const uint8_t* appId     = data + 32;
      uint8_t        khLen     = data[64];

      if (lc < (uint16_t)(65 + khLen)) return SW_WRONG_DATA;
      const uint8_t* kh = data + 65;

      // Check-only (0x07): verify key handle is ours
      if (p1 == 0x07) {
        uint8_t dummy[32];
        bool ok = unwrapKey(kh, appId, dummy);
        // Per spec: SW_CONDITIONS_NOT_SAT means key exists (counter-intuitive but correct)
        return ok ? SW_CONDITIONS_NOT_SAT : SW_WRONG_DATA;
      }

      // Enforce (0x03 or 0x00): need user presence
      uint8_t testPriv[32];
      if (!unwrapKey(kh, appId, testPriv)) return SW_WRONG_DATA;

      s_pending.isRegister = false;
      memcpy(s_pending.challenge, challenge, 32);
      memcpy(s_pending.appId,     appId,     32);
      memcpy(s_pending.keyHandle, kh, 64);
      s_pending.khLen = 64;
      s_pending.p1    = p1;

      g_fidoPending.isRegister = false;
      memcpy(g_fidoPending.appId,      appId,     32);
      memcpy(g_fidoPending.challenge,  challenge, 32);
      memcpy(g_fidoPending.keyHandle,  kh,        64);
      g_fidoPending.khLen = 64;
      g_fidoPending.cid   = cid;
      g_fidoPending.cmd   = hidCmd;
      g_fidoPending.p1    = p1;

      g_fidoState = FIDO_WAITING_UP;

      return SW_PENDING;
    }

    default:
      return SW_INS_NOT_SUPPORTED;
  }
}
