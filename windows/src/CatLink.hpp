#pragma once
// Cat Link (Windows): two KuroCats show each other's cat. Mirrors Sources/CatLink.swift; the wire
// format is docs/cat-link-protocol.md. Only built-in Windows APIs are used: BCrypt (HKDF via HMAC,
// AES-GCM), WinHTTP (secure WebSocket) and a tiny JSON reader/writer, so KuroCat.exe stays one file.
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <optional>
#include <functional>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <random>
#include <algorithm>
#include "KeyMap.hpp"
#include "InputEngine.hpp"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")

namespace catlink {

using Bytes = std::vector<uint8_t>;

inline std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

inline std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

inline std::string toHex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

inline bool randomBytes(uint8_t* out, size_t n) {
    return BCryptGenRandom(nullptr, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

// MARK: - Crypto (HKDF-SHA256, AES-256-GCM)

inline Bytes hmacSha256(const Bytes& key, const Bytes& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    Bytes out(32);
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) return {};
    bool ok = BCryptCreateHash(alg, &hash, nullptr, 0, (PUCHAR)key.data(), (ULONG)key.size(), 0) == 0
           && BCryptHashData(hash, (PUCHAR)data.data(), (ULONG)data.size(), 0) == 0
           && BCryptFinishHash(hash, out.data(), 32, 0) == 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok ? out : Bytes{};
}

/// RFC 5869 with one expand block (enough for <= 32 bytes).
inline Bytes hkdf32(const std::string& ikm, const std::string& salt, const std::string& info, size_t length) {
    Bytes prk = hmacSha256(Bytes(salt.begin(), salt.end()), Bytes(ikm.begin(), ikm.end()));
    Bytes block(info.begin(), info.end());
    block.push_back(0x01);
    Bytes okm = hmacSha256(prk, block);
    if (okm.size() < length) return {};
    okm.resize(length);
    return okm;
}

class AesGcm {
public:
    AesGcm() = default;
    AesGcm(const AesGcm&) = delete;
    AesGcm& operator=(const AesGcm&) = delete;
    ~AesGcm() { reset(); }

    bool setKey(const Bytes& key) {
        std::lock_guard<std::mutex> lock(m_);
        resetLocked();
        if (key.size() != 32) return false;
        if (BCryptOpenAlgorithmProvider(&alg_, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) { alg_ = nullptr; return false; }
        if (BCryptSetProperty(alg_, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0 ||
            BCryptGenerateSymmetricKey(alg_, &key_, nullptr, 0, (PUCHAR)key.data(), 32, 0) != 0) {
            resetLocked();
            return false;
        }
        return true;
    }

    /// nonce (12) | ciphertext | tag (16). `fixedNonce` is only for the self-test.
    Bytes seal(const Bytes& plain, const uint8_t* fixedNonce = nullptr) {
        std::lock_guard<std::mutex> lock(m_);
        if (!key_) return {};
        Bytes out(12 + plain.size() + 16);
        if (fixedNonce) memcpy(out.data(), fixedNonce, 12); else if (!randomBytes(out.data(), 12)) return {};
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = out.data();
        info.cbNonce = 12;
        info.pbTag = out.data() + 12 + plain.size();
        info.cbTag = 16;
        ULONG written = 0;
        if (BCryptEncrypt(key_, (PUCHAR)plain.data(), (ULONG)plain.size(), &info, nullptr, 0,
                          out.data() + 12, (ULONG)plain.size(), &written, 0) != 0) return {};
        return out;
    }

    std::optional<Bytes> open(const uint8_t* p, size_t n) {
        std::lock_guard<std::mutex> lock(m_);
        if (!key_ || n < 28) return std::nullopt;
        size_t len = n - 28;
        Bytes nonce(p, p + 12), tag(p + 12 + len, p + n), out(len);
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = nonce.data();
        info.cbNonce = 12;
        info.pbTag = tag.data();
        info.cbTag = 16;
        ULONG written = 0;
        if (BCryptDecrypt(key_, (PUCHAR)(p + 12), (ULONG)len, &info, nullptr, 0,
                          out.data(), (ULONG)len, &written, 0) != 0) return std::nullopt;
        return out;
    }

    void reset() { std::lock_guard<std::mutex> lock(m_); resetLocked(); }

private:
    void resetLocked() {
        if (key_) BCryptDestroyKey(key_);
        if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
        key_ = nullptr;
        alg_ = nullptr;
    }
    std::mutex m_;
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_KEY_HANDLE key_ = nullptr;
};

// MARK: - Pairing code

struct Code {
    char server = 'C';      // 'C' = broker-cn.emqx.io (Shanghai), 'G' = broker.emqx.io (US)
    std::string secret;     // 20 characters from kAlphabet

    static constexpr const char* kAlphabet = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";   // no 0/O, 1/I/L

    static Code generate(char server) {
        Code c;
        c.server = server == 'G' ? 'G' : 'C';
        const size_t n = strlen(kAlphabet);   // 31
        while (c.secret.size() < 20) {
            uint8_t b = 0;
            if (!randomBytes(&b, 1)) b = (uint8_t)(GetTickCount64() * 2654435761u >> 24);
            if (b < 248) c.secret += kAlphabet[b % n];   // 248 = 31 * 8: no modulo bias
        }
        return c;
    }

    /// Accepts the code with or without dashes/spaces, in any case.
    static std::optional<Code> parse(const std::wstring& text) {
        std::string s;
        for (wchar_t ch : text) {
            if (ch >= L'a' && ch <= L'z') ch = (wchar_t)(ch - 32);
            if ((ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')) s += (char)ch;
        }
        if (s.size() != 23 || s.compare(0, 2, "KC") != 0 || (s[2] != 'C' && s[2] != 'G')) return std::nullopt;
        Code c;
        c.server = s[2];
        c.secret = s.substr(3);
        for (char ch : c.secret) if (!strchr(kAlphabet, ch)) return std::nullopt;
        return c;
    }

    std::wstring display() const {
        std::string s = std::string("KC-") + server;
        for (size_t i = 0; i < 20; i += 4) s += "-" + secret.substr(i, 4);
        return widen(s);
    }

    const wchar_t* host() const { return server == 'G' ? L"broker.emqx.io" : L"broker-cn.emqx.io"; }
    Bytes key() const { return hkdf32(secret, "kurocat-link-v1", "aes-gcm-key", 32); }
    std::string topic() const {
        Bytes t = hkdf32(secret, "kurocat-link-v1", "mqtt-topic", 16);
        return "kurocat/v1/" + toHex(t.data(), t.size());
    }
};

// MARK: - Events & JSON

struct Event {
    std::string k;                  // hello, kd, ku, m, c, w, pet, bye
    int o = 0;                      // ms after the batch's first event
    std::optional<int> id, b;
    std::optional<std::string> s, n, coat, kb, eye;
    std::optional<double> x, y, vx, vy, d;
    std::optional<bool> h, hp, pl;
};

struct Batch {
    int v = 1;
    std::string f;
    int q = 0;
    std::vector<Event> e;
};

inline void jsonString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
        else if (c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
        else out += (char)c;
    }
    out += '"';
}

inline void jsonNumber(std::string& out, double v) {
    if (!std::isfinite(v)) v = 0;
    char buf[32];
    snprintf(buf, sizeof buf, "%.3f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    out += s;
}

inline std::string encodeBatch(const Batch& batch) {
    std::string out = "{\"v\":" + std::to_string(batch.v) + ",\"f\":";
    jsonString(out, batch.f);
    out += ",\"q\":" + std::to_string(batch.q) + ",\"e\":[";
    for (size_t i = 0; i < batch.e.size(); ++i) {
        const Event& e = batch.e[i];
        if (i) out += ',';
        out += "{\"k\":";
        jsonString(out, e.k);
        out += ",\"o\":" + std::to_string(e.o);
        auto str = [&](const char* name, const std::optional<std::string>& v) {
            if (v) { out += ",\""; out += name; out += "\":"; jsonString(out, *v); } };
        auto num = [&](const char* name, const std::optional<double>& v) {
            if (v) { out += ",\""; out += name; out += "\":"; jsonNumber(out, *v); } };
        auto integer = [&](const char* name, const std::optional<int>& v) {
            if (v) { out += ",\""; out += name; out += "\":" + std::to_string(*v); } };
        auto boolean = [&](const char* name, const std::optional<bool>& v) {
            if (v) { out += ",\""; out += name; out += "\":"; out += *v ? "true" : "false"; } };
        integer("id", e.id); str("s", e.s);
        num("x", e.x); num("y", e.y); num("vx", e.vx); num("vy", e.vy);
        integer("b", e.b); num("d", e.d); boolean("h", e.h);
        str("n", e.n); str("coat", e.coat); str("kb", e.kb); str("eye", e.eye);
        boolean("hp", e.hp); boolean("pl", e.pl);
        out += '}';
    }
    out += "]}";
    return out;
}

/// Minimal JSON reader: enough for batches (objects, arrays, strings with escapes, numbers, bools, null).
class JsonReader {
public:
    struct Value {
        enum Type { Null, Bool, Number, String, Array, Object } type = Null;
        bool boolean = false;
        double number = 0;
        std::string str;
        std::vector<Value> items;          // array items, or object values (names in `keys`)
        std::vector<std::string> keys;
        const Value* get(const char* name) const {
            for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == name) return &items[i];
            return nullptr;
        }
    };

    static std::optional<Value> parse(const std::string& text) {
        JsonReader r(text);
        Value v;
        if (!r.value(v, 0)) return std::nullopt;
        r.ws();
        if (r.i_ != r.t_.size()) return std::nullopt;
        return v;
    }

private:
    explicit JsonReader(const std::string& t) : t_(t) {}
    const std::string& t_;
    size_t i_ = 0;

    void ws() { while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\n' || t_[i_] == '\r' || t_[i_] == '\t')) ++i_; }
    bool lit(const char* s) {
        size_t n = strlen(s);
        if (t_.compare(i_, n, s) != 0) return false;
        i_ += n;
        return true;
    }

    bool value(Value& v, int depth) {
        if (depth > 16) return false;
        ws();
        if (i_ >= t_.size()) return false;
        char c = t_[i_];
        if (c == '{') {
            v.type = Value::Object; ++i_; ws();
            if (i_ < t_.size() && t_[i_] == '}') { ++i_; return true; }
            while (true) {
                ws();
                std::string name;
                if (!string(name)) return false;
                ws();
                if (i_ >= t_.size() || t_[i_] != ':') return false;
                ++i_;
                Value item;
                if (!value(item, depth + 1)) return false;
                v.keys.push_back(std::move(name));
                v.items.push_back(std::move(item));
                ws();
                if (i_ < t_.size() && t_[i_] == ',') { ++i_; continue; }
                if (i_ < t_.size() && t_[i_] == '}') { ++i_; return true; }
                return false;
            }
        }
        if (c == '[') {
            v.type = Value::Array; ++i_; ws();
            if (i_ < t_.size() && t_[i_] == ']') { ++i_; return true; }
            while (true) {
                Value item;
                if (!value(item, depth + 1)) return false;
                v.items.push_back(std::move(item));
                ws();
                if (i_ < t_.size() && t_[i_] == ',') { ++i_; continue; }
                if (i_ < t_.size() && t_[i_] == ']') { ++i_; return true; }
                return false;
            }
        }
        if (c == '"') { v.type = Value::String; return string(v.str); }
        if (lit("true")) { v.type = Value::Bool; v.boolean = true; return true; }
        if (lit("false")) { v.type = Value::Bool; v.boolean = false; return true; }
        if (lit("null")) { v.type = Value::Null; return true; }
        const char* start = t_.c_str() + i_;
        char* end = nullptr;
        double num = strtod(start, &end);
        if (end == start) return false;
        i_ += (size_t)(end - start);
        v.type = Value::Number;
        v.number = num;
        return true;
    }

    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }

    bool hex4(uint32_t& out) {
        if (i_ + 4 > t_.size()) return false;
        out = 0;
        for (int k = 0; k < 4; ++k) {
            char c = t_[i_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') out |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= (uint32_t)(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool string(std::string& out) {
        if (i_ >= t_.size() || t_[i_] != '"') return false;
        ++i_;
        while (i_ < t_.size()) {
            char c = t_[i_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i_ >= t_.size()) return false;
            char e = t_[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 6 <= t_.size() && t_[i_] == '\\' && t_[i_ + 1] == 'u') {
                        i_ += 2;
                        uint32_t lo = 0;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
};

inline std::optional<Batch> decodeBatch(const std::string& text) {
    auto root = JsonReader::parse(text);
    if (!root || root->type != JsonReader::Value::Object) return std::nullopt;
    using V = JsonReader::Value;
    Batch b;
    const V* v = root->get("v");
    const V* f = root->get("f");
    const V* q = root->get("q");
    const V* e = root->get("e");
    if (!v || v->type != V::Number || !f || f->type != V::String || !e || e->type != V::Array) return std::nullopt;
    b.v = (int)v->number;
    b.f = f->str;
    b.q = q && q->type == V::Number ? (int)q->number : 0;
    for (const V& item : e->items) {
        if (item.type != V::Object) continue;
        Event ev;
        const V* k = item.get("k");
        if (!k || k->type != V::String) continue;
        ev.k = k->str;
        auto num = [&](const char* name) -> std::optional<double> {
            const V* x = item.get(name);
            if (x && x->type == V::Number) return x->number;
            return std::nullopt;
        };
        auto str = [&](const char* name) -> std::optional<std::string> {
            const V* x = item.get(name);
            if (x && x->type == V::String) return x->str;
            return std::nullopt;
        };
        auto boolean = [&](const char* name) -> std::optional<bool> {
            const V* x = item.get(name);
            if (x && x->type == V::Bool) return x->boolean;
            return std::nullopt;
        };
        if (auto o = num("o")) ev.o = (int)*o;
        if (auto id = num("id")) ev.id = (int)*id;
        if (auto bb = num("b")) ev.b = (int)*bb;
        ev.s = str("s"); ev.n = str("n"); ev.coat = str("coat"); ev.kb = str("kb"); ev.eye = str("eye");
        ev.x = num("x"); ev.y = num("y"); ev.vx = num("vx"); ev.vy = num("vy"); ev.d = num("d");
        ev.h = boolean("h"); ev.hp = boolean("hp"); ev.pl = boolean("pl");
        b.e.push_back(std::move(ev));
    }
    return b;
}

// MARK: - MQTT 5 packets (QoS 0 only)

inline void putVarint(Bytes& out, size_t v) {
    do {
        uint8_t byte = (uint8_t)(v % 128);
        v /= 128;
        if (v) byte |= 0x80;
        out.push_back(byte);
    } while (v);
}

inline void putStr(Bytes& out, const std::string& s) {
    out.push_back((uint8_t)(s.size() >> 8));
    out.push_back((uint8_t)(s.size() & 0xFF));
    out.insert(out.end(), s.begin(), s.end());
}

inline Bytes packet(uint8_t header, const Bytes& body) {
    Bytes out{header};
    putVarint(out, body.size());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

inline Bytes mqttConnect(const std::string& clientId) {
    Bytes body;
    putStr(body, "MQTT");
    body.insert(body.end(), {0x05, 0x02, 0x00, 60, 0x00});   // v5, clean start, keep-alive 60 s, no properties
    putStr(body, clientId);
    return packet(0x10, body);
}

inline Bytes mqttSubscribe(const std::string& topic) {
    Bytes body{0x00, 0x01, 0x00};   // packet id 1, no properties
    putStr(body, topic);
    body.push_back(0x04);           // QoS 0 + No Local
    return packet(0x82, body);
}

inline Bytes mqttPublish(const std::string& topic, const Bytes& payload) {
    Bytes body;
    putStr(body, topic);
    body.push_back(0x00);           // no properties
    body.insert(body.end(), payload.begin(), payload.end());
    return packet(0x30, body);
}

// MARK: - Session

enum class Status { Off, Connecting, WaitingForFriend, Linked, FriendOffline, Reconnecting };

class Session {
public:
    struct Due { Event e; double applyAt; };

    /// What we tell the friend about our cat.
    std::function<Event()> profile = [] { Event e; e.k = "hello"; return e; };

    Session() {
        uint8_t id[4] = {};
        randomBytes(id, 4);
        peerId_ = toHex(id, 4);
    }
    ~Session() { stop(false); }

    const std::string& peerId() const { return peerId_; }
    std::optional<Code> code() const { return code_; }

    Status status() const {
        std::lock_guard<std::mutex> lock(sh_->m);
        return sh_->status;
    }
    std::wstring lastError() const {
        std::lock_guard<std::mutex> lock(sh_->m);
        return sh_->lastError;
    }
    std::string friendName() const {
        std::lock_guard<std::mutex> lock(sh_->m);
        return sh_->friendName;
    }

    void start(const Code& code) {
        stop(false);
        code_ = code;
        sh_ = std::make_shared<Shared>();
        sh_->topic = code.topic();
        sh_->peerId = peerId_;
        sh_->host = code.host();
        sh_->aes.setKey(code.key());
        sh_->status = Status::Connecting;
        pending_.clear();
        pressIds_.clear();
        auto sh = sh_;
        std::thread([sh] { ioLoop(sh); }).detach();
        std::thread([sh] { sendLoop(sh); }).detach();
    }

    void stop(bool sendBye = true) {
        if (!code_) return;
        if (sendBye && connected()) {
            Event bye; bye.k = "bye";
            queue(bye);
            flush();
        }
        auto sh = sh_;
        {
            std::lock_guard<std::mutex> lock(sh->m);
            sh->stopping = true;
            sh->status = Status::Off;
            sh->friendPeer.clear();
            sh->friendName.clear();
            sh->incoming.clear();
        }
        sh->cv.notify_all();
        // Give the sender a moment to deliver "bye", then close the socket (aborts the blocking receive).
        std::thread([sh] {
            Sleep(300);
            std::lock_guard<std::mutex> lock(sh->sendMutex);
            if (sh->ws) { WinHttpCloseHandle(sh->ws); sh->ws = nullptr; }
        }).detach();
        sh_ = std::make_shared<Shared>();
        code_.reset();
        pending_.clear();
        pressIds_.clear();
    }

    bool connected() const { return sh_->connected.load(); }

    // Called on the UI thread.
    void noteKeyDown(PhysicalKey key, bool viewerLeftPaw, bool passwordFocused) {
        if (!connected() || passwordFocused) return;
        int id = nextPressId_;
        nextPressId_ = nextPressId_ % 9999 + 1;
        pressIds_[(int)key] = id;
        Event e; e.k = "kd"; e.id = id; e.s = viewerLeftPaw ? "l" : "r";
        queue(e);
    }

    void noteKeyUp(PhysicalKey key) {
        auto it = pressIds_.find((int)key);
        if (it == pressIds_.end()) return;
        int id = it->second;
        pressIds_.erase(it);
        if (!connected()) return;
        Event e; e.k = "ku"; e.id = id;
        queue(e);
    }

    /// Scrolling, merged to one event per 50 ms (touchpads send many small steps).
    void noteScroll(float delta) {
        if (!connected()) return;
        double now = nowSeconds();
        if (!pending_.empty() && pending_.back().k == "w" && pending_.back().o >= (now - batchStart_) * 1000.0 - 50.0) {
            pending_.back().d = pending_.back().d.value_or(0.0) + delta;
            return;
        }
        Event e; e.k = "w"; e.d = delta;
        queue(e);
    }

    void noteClick(int button, bool down) {
        if (!connected()) return;
        Event e; e.k = "c"; e.b = button; e.d = down ? 1.0 : 0.0;
        queue(e);
    }

    /// We petted the friend's cat on our screen: their own cat feels it too.
    void notePet(Pt canvasPoint, float delta, bool isHead) {
        if (!connected()) return;
        Event e; e.k = "pet";
        e.x = std::round(canvasPoint.x * 10.0) / 10.0;
        e.y = std::round(canvasPoint.y * 10.0) / 10.0;
        e.d = std::round(delta * 10.0) / 10.0;
        e.h = isHead;
        queue(e);
    }

    void profileChanged() { helloDue_ = 0; }

    /// Once per frame on the UI thread: mouse samples, batching, hello, keep-alive, presence.
    void update(double now, const InputEngine& input) {
        if (!code_) return;
        auto sh = sh_;
        bool isConnected = sh->connected.load();
        if (isConnected && sh->justConnected.exchange(false)) {
            helloDue_ = 0;
            nextPing_ = now + 25;
        }
        if (isConnected && sh->friendJustArrived.exchange(false)) helloDue_ = 0;
        if (isConnected) {
            if (now >= nextMouse_) { nextMouse_ = now + 0.1; sampleMouse(input); }
            if (now >= helloDue_) { helloDue_ = now + 10; queue(profile()); flush(); }
            if (now >= nextFlush_) { nextFlush_ = now + 0.1; flush(); }
            if (now >= nextPing_) { nextPing_ = now + 25; enqueueRaw(Bytes{0xC0, 0x00}); }
            if (now - sh->lastReceive.load() > 90) {   // server stopped answering: force a reconnect
                std::lock_guard<std::mutex> lock(sh->sendMutex);
                if (sh->ws) { WinHttpCloseHandle(sh->ws); sh->ws = nullptr; }
            }
        }
        std::lock_guard<std::mutex> lock(sh->m);
        if (sh->status == Status::Linked && now - sh->lastHeard > 25) sh->status = Status::FriendOffline;
    }

    /// Friend events whose time has come (UI thread).
    std::vector<Due> takeDue(double now) {
        std::vector<Due> out;
        std::lock_guard<std::mutex> lock(sh_->m);
        auto& in = sh_->incoming;
        while (!in.empty() && in.front().applyAt <= now) {
            out.push_back(std::move(in.front()));
            in.pop_front();
        }
        return out;
    }

    // MARK: Self-test helpers
    static std::string sealForTest(const Code& code, const std::string& json, const uint8_t nonce[12]) {
        AesGcm aes;
        aes.setKey(code.key());
        Bytes sealed = aes.seal(Bytes(json.begin(), json.end()), nonce);
        return toHex(sealed.data(), sealed.size());
    }
    static std::optional<std::string> openForTest(const Code& code, const Bytes& sealed) {
        AesGcm aes;
        aes.setKey(code.key());
        auto plain = aes.open(sealed.data(), sealed.size());
        if (!plain) return std::nullopt;
        return std::string(plain->begin(), plain->end());
    }

private:
    struct Shared {
        mutable std::mutex m;
        std::condition_variable cv;
        std::deque<Bytes> outbox;
        std::deque<Due> incoming;
        Status status = Status::Off;
        std::wstring lastError;
        std::string friendPeer, friendName;
        double lastHeard = 0;
        bool stopping = false;

        std::mutex sendMutex;
        HINTERNET ws = nullptr;
        std::atomic<bool> connected{false};
        std::atomic<bool> justConnected{false};
        std::atomic<bool> friendJustArrived{false};
        std::atomic<double> lastReceive{0.0};

        std::string topic, peerId;
        std::wstring host;
        AesGcm aes;
    };

    std::shared_ptr<Shared> sh_ = std::make_shared<Shared>();
    std::optional<Code> code_;
    std::string peerId_;
    std::vector<Event> pending_;
    double batchStart_ = 0;
    int seq_ = 0;
    std::map<int, int> pressIds_;
    int nextPressId_ = 1;
    double helloDue_ = 0, nextFlush_ = 0, nextMouse_ = 0, nextPing_ = 0;
    POINT lastMouseSent_ = {-1, -1};
    bool mouseWasMoving_ = false;

    void queue(Event e) {
        double now = nowSeconds();
        if (pending_.empty()) batchStart_ = now;
        e.o = (int)((now - batchStart_) * 1000.0);
        pending_.push_back(std::move(e));
        if (pending_.size() >= 40) flush();
    }

    void flush() {
        if (pending_.empty() || !connected()) return;
        Batch b;
        b.f = peerId_;
        b.q = ++seq_;
        b.e.swap(pending_);
        std::string json = encodeBatch(b);
        Bytes sealed = sh_->aes.seal(Bytes(json.begin(), json.end()));
        if (sealed.empty()) return;
        enqueueRaw(mqttPublish(sh_->topic, sealed));
    }

    void enqueueRaw(Bytes bytes) {
        {
            std::lock_guard<std::mutex> lock(sh_->m);
            if (sh_->outbox.size() > 64) sh_->outbox.pop_front();
            sh_->outbox.push_back(std::move(bytes));
        }
        sh_->cv.notify_one();
    }

    void sampleMouse(const InputEngine& input) {
        POINT cur = input.lastCursorPos;
        HMONITOR mon = MonitorFromPoint(cur, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(mon, &mi)) return;
        double w = std::max(1L, mi.rcMonitor.right - mi.rcMonitor.left);
        double h = std::max(1L, mi.rcMonitor.bottom - mi.rcMonitor.top);
        bool moving = std::hypot(input.mouseVelocity.x, input.mouseVelocity.y) > 20.0f;
        bool moved = std::abs(cur.x - lastMouseSent_.x) + std::abs(cur.y - lastMouseSent_.y) > 0;
        if (!moving && !moved && !mouseWasMoving_) return;
        mouseWasMoving_ = moving;
        lastMouseSent_ = cur;
        Event e; e.k = "m";
        e.x = std::round((cur.x - mi.rcMonitor.left) / w * 1000.0) / 1000.0;
        e.y = std::round((1.0 - (cur.y - mi.rcMonitor.top) / h) * 1000.0) / 1000.0;     // y up on the wire
        e.vx = moving ? std::round(input.mouseVelocity.x / h * 1000.0) / 1000.0 : 0.0;
        e.vy = moving ? std::round(-input.mouseVelocity.y / h * 1000.0) / 1000.0 : 0.0;  // screen y is down
        queue(e);
    }

    static void setStatus(const std::shared_ptr<Shared>& sh, Status s, const std::wstring& error = L"") {
        std::lock_guard<std::mutex> lock(sh->m);
        if (sh->stopping) return;
        sh->status = s;
        if (!error.empty()) sh->lastError = error;
    }

    static bool sendNow(const std::shared_ptr<Shared>& sh, const Bytes& bytes) {
        std::lock_guard<std::mutex> lock(sh->sendMutex);
        if (!sh->ws) return false;
        return WinHttpWebSocketSend(sh->ws, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
                                    (PVOID)bytes.data(), (DWORD)bytes.size()) == NO_ERROR;
    }

    static void sendLoop(std::shared_ptr<Shared> sh) {
        while (true) {
            Bytes next;
            {
                std::unique_lock<std::mutex> lock(sh->m);
                sh->cv.wait_for(lock, std::chrono::milliseconds(500), [&] { return sh->stopping || !sh->outbox.empty(); });
                if (sh->stopping && sh->outbox.empty()) return;
                if (sh->outbox.empty()) continue;
                if (!sh->connected.load()) { sh->outbox.clear(); continue; }
                next = std::move(sh->outbox.front());
                sh->outbox.pop_front();
            }
            sendNow(sh, next);
        }
    }

    static HINTERNET openWebSocket(const std::shared_ptr<Shared>& sh, HINTERNET& session, std::wstring& error) {
        session = WinHttpOpen(L"KuroCat", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) session = WinHttpOpen(L"KuroCat", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) { error = L"WinHttpOpen failed"; return nullptr; }
        WinHttpSetTimeouts(session, 10000, 10000, 10000, 10000);
        HINTERNET connect = WinHttpConnect(session, sh->host.c_str(), 8084, 0);
        if (!connect) { error = L"cannot reach " + sh->host; return nullptr; }
        HINTERNET request = WinHttpOpenRequest(connect, L"GET", L"/mqtt", nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        HINTERNET ws = nullptr;
        if (request && WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) &&
            WinHttpSendRequest(request, L"Sec-WebSocket-Protocol: mqtt\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD code = 0, size = sizeof(code);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
            if (code == 101) ws = WinHttpWebSocketCompleteUpgrade(request, 0);
            else error = L"server answered HTTP " + std::to_wstring(code);
        } else {
            error = L"connection failed (error " + std::to_wstring(GetLastError()) + L")";
        }
        if (request) WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        return ws;
    }

    static void ioLoop(std::shared_ptr<Shared> sh) {
        int retry = 2;
        bool first = true;
        while (true) {
            { std::lock_guard<std::mutex> lock(sh->m); if (sh->stopping) break; }
            setStatus(sh, first ? Status::Connecting : Status::Reconnecting);
            first = false;
            std::wstring error;
            HINTERNET session = nullptr;
            HINTERNET ws = openWebSocket(sh, session, error);
            if (ws) {
                {
                    std::lock_guard<std::mutex> lock(sh->sendMutex);
                    sh->ws = ws;
                }
                sh->lastReceive = nowSeconds();
                uint8_t id[2] = {};
                randomBytes(id, 2);
                sendNow(sh, mqttConnect("kurocat-" + sh->peerId + "-" + toHex(id, 2)));
                error = receiveLoop(sh, ws, retry);
                sh->connected = false;
                std::lock_guard<std::mutex> lock(sh->sendMutex);
                if (sh->ws) { WinHttpCloseHandle(sh->ws); sh->ws = nullptr; }
            }
            if (session) WinHttpCloseHandle(session);
            { std::lock_guard<std::mutex> lock(sh->m); if (sh->stopping) break; }
            setStatus(sh, Status::Reconnecting, error);
            for (int waited = 0; waited < retry * 10; ++waited) {   // sleep `retry` seconds, waking for stop
                Sleep(100);
                std::lock_guard<std::mutex> lock(sh->m);
                if (sh->stopping) return;
            }
            retry = std::min(60, retry * 2);
        }
    }

    static std::wstring receiveLoop(const std::shared_ptr<Shared>& sh, HINTERNET ws, int& retry) {
        Bytes buffer;
        std::vector<uint8_t> chunk(16384);
        while (true) {
            DWORD read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
            DWORD err = WinHttpWebSocketReceive(ws, chunk.data(), (DWORD)chunk.size(), &read, &type);
            if (err != NO_ERROR) return L"connection lost (error " + std::to_wstring(err) + L")";
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) return L"server closed the connection";
            sh->lastReceive = nowSeconds();
            buffer.insert(buffer.end(), chunk.begin(), chunk.begin() + read);
            // Parse complete MQTT packets.
            while (buffer.size() >= 2) {
                size_t length = 0, multiplier = 1, index = 1;
                uint8_t byte = 0;
                bool complete = false;
                do {
                    if (index >= buffer.size() || index > 4) break;
                    byte = buffer[index++];
                    length += (byte & 0x7F) * multiplier;
                    multiplier *= 128;
                    if (!(byte & 0x80)) complete = true;
                } while (!complete);
                if (!complete || buffer.size() < index + length) break;
                uint8_t type4 = buffer[0] >> 4, flags = buffer[0] & 0x0F;
                Bytes body(buffer.begin() + index, buffer.begin() + index + length);
                buffer.erase(buffer.begin(), buffer.begin() + index + length);
                if (type4 == 2) {   // CONNACK
                    if (body.size() < 2 || body[1] != 0) return L"server refused the connection";
                    sh->connected = true;
                    sh->justConnected = true;
                    retry = 2;
                    sendNow(sh, mqttSubscribe(sh->topic));
                    std::lock_guard<std::mutex> lock(sh->m);
                    if (!sh->stopping) {
                        sh->lastError.clear();
                        if (sh->friendPeer.empty()) sh->status = Status::WaitingForFriend;
                        else sh->status = nowSeconds() - sh->lastHeard < 25 ? Status::Linked : Status::FriendOffline;
                    }
                } else if (type4 == 3) {   // PUBLISH
                    handlePublish(sh, body, flags);
                } else if (type4 == 14) {
                    return L"server closed the connection";
                }
            }
        }
    }

    static void handlePublish(const std::shared_ptr<Shared>& sh, const Bytes& body, uint8_t flags) {
        if (body.size() < 2) return;
        size_t topicLen = ((size_t)body[0] << 8) | body[1];
        size_t i = 2 + topicLen;
        if (body.size() < i) return;
        if ((flags >> 1) & 0x03) i += 2;
        size_t propLen = 0, multiplier = 1;
        while (i < body.size()) {
            uint8_t b = body[i++];
            propLen += (b & 0x7F) * multiplier;
            multiplier *= 128;
            if (!(b & 0x80)) break;
        }
        i += propLen;
        if (i > body.size()) return;
        auto plain = sh->aes.open(body.data() + i, body.size() - i);
        if (!plain) return;
        auto batch = decodeBatch(std::string(plain->begin(), plain->end()));
        if (!batch || batch->v != 1 || batch->f == sh->peerId) return;
        double now = nowSeconds();
        std::lock_guard<std::mutex> lock(sh->m);
        if (sh->stopping) return;
        for (const Event& e : batch->e) {
            if (e.k == "bye") {
                if (batch->f == sh->friendPeer) { sh->lastHeard = 0; sh->status = Status::FriendOffline; }
                return;
            }
        }
        if (sh->friendPeer != batch->f) {
            if (sh->friendPeer.empty()) sh->friendJustArrived = true;
            sh->friendPeer = batch->f;
        }
        sh->lastHeard = now;
        sh->status = Status::Linked;
        for (const Event& e : batch->e) {
            if (e.k == "hello") sh->friendName = e.n.value_or("");
            if (sh->incoming.size() > 400) sh->incoming.pop_front();
            sh->incoming.push_back({e, now + 0.12 + e.o / 1000.0});
        }
    }
};

// MARK: - Remote cat driver

/// Turns the friend's events into input for the second cat. Keys arrive anonymized ("left paw" /
/// "right paw"), so each press lands on a random key on that side.
class RemoteDriver {
public:
    InputEngine input;          // a feed, not real hardware (externalFeed = true)
    bool hasProfile = false;
    std::optional<std::string> coat, kb, eye;

    RemoteDriver() {
        input.externalFeed = true;
        input.kpmOverride = 0;
        for (int i = 0; i < (int)PhysicalKey::None; ++i) {
            PhysicalKey k = (PhysicalKey)i;
            if (visualKeyOf(k) != k || isModifierOrEdge(k)) continue;
            (prefersViewerLeftPaw(k) ? leftKeys_ : rightKeys_).push_back(k);
        }
    }

    /// Applies one event; returns the key to animate for "kd" (PhysicalKey::None otherwise).
    PhysicalKey apply(const Event& e, double now, int& clickButton, bool& clickDown) {
        clickButton = -1;
        if (e.k == "hello") {
            coat = e.coat; kb = e.kb; eye = e.eye;
            hasProfile = true;
        } else if (e.k == "kd" && e.id) {
            const auto& pool = e.s && *e.s == "l" ? leftKeys_ : rightKeys_;
            std::vector<PhysicalKey> free;
            for (PhysicalKey k : pool) if (!input.activeKeys.count(k)) free.push_back(k);
            const auto& from = free.empty() ? pool : free;
            PhysicalKey key = from[(size_t)(rng() % from.size())];
            held_[*e.id] = {key, now};
            input.activeKeys.insert(key);
            input.pressedOrder.push_back(key);
            input.lastKeyActivityTime = now;
            keyTimes_.push_back(now);
            return key;
        } else if (e.k == "ku" && e.id) {
            release(*e.id);
            input.lastKeyActivityTime = now;
        } else if (e.k == "m") {
            normX_ = e.x.value_or(normX_);
            normY_ = e.y.value_or(normY_);
            velX_ = e.vx.value_or(0);
            velY_ = e.vy.value_or(0);
            lastSample_ = now;
            input.lastMouseMoveTime = now;
        } else if (e.k == "c") {
            bool down = e.d.value_or(0) > 0.5;
            int b = e.b.value_or(0);
            if (b == 0) input.leftMouseDown = down; else if (b == 1) input.rightMouseDown = down;
            input.lastMouseMoveTime = now;
            if (down) input.lastMouseClickTime = now;
            clickButton = b;
            clickDown = down;
        } else if (e.k == "w") {
            input.lastMouseMoveTime = now;
        }
        return PhysicalKey::None;
    }

    void releaseAll() {
        held_.clear();
        input.activeKeys.clear();
        input.pressedOrder.clear();
        input.leftMouseDown = input.rightMouseDown = false;
        velX_ = velY_ = 0;
    }

    /// Before each physics step: cursor/velocity in this PC's screen space, stuck-key safety, KPM.
    void prepareFrame(double now) {
        std::vector<int> stale;
        for (auto& kv : held_) if (now - kv.second.second > 20) stale.push_back(kv.first);
        for (int id : stale) release(id);
        if (now - lastSample_ > 0.3) { velX_ *= 0.8; velY_ *= 0.8; }
        while (!keyTimes_.empty() && now - keyTimes_.front() > 10) keyTimes_.pop_front();
        input.kpmOverride = (int)keyTimes_.size() * 6;
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
        double w = mi.rcMonitor.right - mi.rcMonitor.left, h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        input.lastCursorPos = { (LONG)(mi.rcMonitor.left + normX_ * w), (LONG)(mi.rcMonitor.top + (1.0 - normY_) * h) };
        input.mouseVelocity = { (float)(velX_ * h), (float)(-velY_ * h) };
    }

private:
    std::map<int, std::pair<PhysicalKey, double>> held_;
    std::deque<double> keyTimes_;
    std::vector<PhysicalKey> leftKeys_, rightKeys_;
    double normX_ = 0.5, normY_ = 0.5, velX_ = 0, velY_ = 0, lastSample_ = 0;
    std::minstd_rand rng{(unsigned)GetTickCount64()};

    void release(int id) {
        auto it = held_.find(id);
        if (it == held_.end()) return;
        PhysicalKey key = it->second.first;
        held_.erase(it);
        bool stillHeld = false;
        for (auto& kv : held_) if (kv.second.first == key) stillHeld = true;
        if (!stillHeld) {
            input.activeKeys.erase(key);
            input.pressedOrder.erase(std::remove(input.pressedOrder.begin(), input.pressedOrder.end(), key), input.pressedOrder.end());
        }
    }

    static bool isModifierOrEdge(PhysicalKey k) {
        switch (k) {
            case PhysicalKey::LeftShift: case PhysicalKey::RightShift: case PhysicalKey::LeftControl:
            case PhysicalKey::LeftOption: case PhysicalKey::LeftCommand: case PhysicalKey::RightCommand:
            case PhysicalKey::RightOption: case PhysicalKey::Fn: case PhysicalKey::CapsLock: case PhysicalKey::Tab:
            case PhysicalKey::Escape: case PhysicalKey::Enter: case PhysicalKey::Backspace:
            case PhysicalKey::ArrowUp: case PhysicalKey::ArrowDown: case PhysicalKey::ArrowLeft: case PhysicalKey::ArrowRight:
                return true;
            default:
                return false;
        }
    }
};

} // namespace catlink
