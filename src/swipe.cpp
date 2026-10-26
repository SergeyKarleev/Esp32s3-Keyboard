#include "swipe.h"

#include <Arduino.h>
#include <Preferences.h>

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" const char DICT_EN[];
extern "C" const char DICT_RU[];

namespace swipe {

namespace {

constexpr int N = 32;              // точек после ресемплинга
constexpr int MAX_WORD_KEYS = 24;  // длиннее слова свайпом не набирают
constexpr size_t MAX_CHECKS = 12000;
// вес частоты: score = среднее расстояние / ширина клавиши + FREQ_WEIGHT * ln(ранг + 1)
constexpr float FREQ_WEIGHT = 0.08f;
// буква слова дальше MISS_FREE ширин клавиши от пути — штраф, дальше MISS_MAX — слово отбрасывается
constexpr float MISS_FREE = 0.3f;
constexpr float MISS_MAX = 1.6f;
constexpr float MISS_WEIGHT = 0.2f;

struct Dict {
    const char* text = nullptr;
    uint32_t base = 0;     // codepoint первой буквы алфавита
    uint8_t letters = 0;   // букв в алфавите
    uint32_t words = 0;
    uint32_t* wordOff = nullptr;    // начало слова по рангу
    uint32_t* bucketStart = nullptr;  // CSR: [первая буква][последняя буква] -> диапазон в bucketWords
    uint32_t* bucketWords = nullptr;  // ранги слов
};

Dict dicts[2];

// выученные слова: выбранные из подсказок и набранные по буквам
constexpr size_t MAX_USER_WORDS = 128;
constexpr uint32_t NO_RANK = UINT32_MAX;
// слово не из словаря считается таким же редким, как слово с этим рангом
constexpr uint32_t UNKNOWN_RANK = 20000;
// бонус выученного слова: USER_WEIGHT * ln(1 + сколько раз выбрано)
constexpr float USER_WEIGHT = 0.08f;
constexpr uint32_t SAVE_DELAY_MS = 5000;

// во flash
struct StoredWord {
    char text[30];  // UTF-8, с нулем
    uint8_t lang;
    uint8_t count;
};
struct UserWord {
    char text[30] = {};
    uint8_t lang = 0;
    uint8_t count = 0;  // 0 — пустая запись
    uint32_t rank = NO_RANK;
};
UserWord userWords[MAX_USER_WORDS];
bool dirty = false;
uint32_t changedAt = 0;

struct FPoint {
    float x, y;
};

int letterIndex(const Dict& d, uint32_t cp) {
    if (cp < d.base || cp >= d.base + d.letters) return -1;
    return int(cp - d.base);
}

void* psram(size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

// первая и последняя буква слова [p, end)
bool edges(const Dict& d, const char* p, const char* end, int& first, int& last) {
    first = last = -1;
    while (p < end) {
        int i = letterIndex(d, nextCp(p, end));
        if (i < 0) return false;
        if (first < 0) first = i;
        last = i;
    }
    return first >= 0;
}

void build(Dict& d, const char* text, uint32_t base, uint8_t letters) {
    d.text = text;
    d.base = base;
    d.letters = letters;
    size_t buckets = size_t(letters) * letters;
    size_t total = strlen(text);
    for (size_t i = 0; i < total; i++)
        if (text[i] == '\n') d.words++;
    d.wordOff = static_cast<uint32_t*>(psram(d.words * 4));
    d.bucketStart = static_cast<uint32_t*>(psram((buckets + 1) * 4));
    d.bucketWords = static_cast<uint32_t*>(psram(d.words * 4));
    uint32_t* fill = static_cast<uint32_t*>(psram((buckets + 1) * 4));
    std::fill(d.bucketStart, d.bucketStart + buckets + 1, 0);

    // два прохода: размеры корзин, затем раскладка рангов по корзинам
    uint32_t w = 0;
    for (const char* p = text; *p; w++) {
        const char* e = strchr(p, '\n');
        d.wordOff[w] = p - text;
        int f, l;
        if (edges(d, p, e, f, l)) d.bucketStart[f * letters + l + 1]++;
        p = e + 1;
    }
    for (size_t b = 0; b < buckets; b++) d.bucketStart[b + 1] += d.bucketStart[b];
    std::copy(d.bucketStart, d.bucketStart + buckets + 1, fill);
    for (w = 0; w < d.words; w++) {
        const char* p = text + d.wordOff[w];
        int f, l;
        if (edges(d, p, strchr(p, '\n'), f, l)) d.bucketWords[fill[f * letters + l]++] = w;
    }
    free(fill);
    Serial.printf("[swipe] dict %u words, psram free %u\n", d.words, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

float dist(FPoint a, FPoint b) { return std::hypot(a.x - b.x, a.y - b.y); }

// равномерно по длине ломаной
void resample(const FPoint* in, int n, FPoint* out, int m) {
    if (n == 1) {
        std::fill(out, out + m, in[0]);
        return;
    }
    float total = 0;
    for (int i = 1; i < n; i++) total += dist(in[i - 1], in[i]);
    if (total < 1e-3f) {
        std::fill(out, out + m, in[0]);
        return;
    }
    float step = total / (m - 1);
    out[0] = in[0];
    int k = 1;
    float acc = 0;
    for (int i = 1; i < n && k < m; i++) {
        FPoint a = in[i - 1], b = in[i];
        float seg = dist(a, b);
        while (seg > 0 && acc + seg >= step * k - 1e-3f && k < m) {
            float t = (step * k - acc) / seg;
            out[k++] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
        }
        acc += seg;
    }
    while (k < m) out[k++] = in[n - 1];
}

// ближайшие к точке клавиши в радиусе (до maxOut), по возрастанию расстояния
int nearKeys(const FPoint& p, const FPoint* centers, const bool* has, int letters, float radius, int* out, int maxOut) {
    int idx[32], count = 0;
    for (int i = 0; i < letters; i++)
        if (has[i] && dist(p, centers[i]) <= radius) idx[count++] = i;
    std::sort(idx, idx + count, [&](int a, int b) { return dist(p, centers[a]) < dist(p, centers[b]); });
    count = std::min(count, maxOut);
    std::copy(idx, idx + count, out);
    return count;
}

// разбор пути, общий для всех слов-кандидатов
struct Match {
    const Dict* d = nullptr;
    FPoint centers[32];
    bool has[32] = {};
    FPoint user[N], dense[64];
    float kw = 1;

    // расстояние пути до ломаной через буквы слова [p, e), в ширинах клавиши; false — не подходит
    bool score(const char* p, const char* e, float& out) const {
        FPoint tmpl[MAX_WORD_KEYS], tmplN[N];
        int L = 0, prev = -1;
        while (p < e) {
            int li = letterIndex(*d, nextCp(p, e));
            if (li < 0 || !has[li]) return false;
            if (li == prev) continue;  // двойные буквы — одна клавиша
            if (L == MAX_WORD_KEYS) return false;
            tmpl[L++] = centers[li];
            prev = li;
        }
        // путь должен пройти рядом с каждой буквой слова: далеко — отказ, чуть мимо — штраф
        float miss = 0;
        for (int i = 1; i < L - 1; i++) {
            float m = 1e9f;
            for (int j = 0; j < 64; j++) m = std::min(m, dist(tmpl[i], dense[j]));
            if (m >= kw * MISS_MAX) return false;
            miss += std::max(0.0f, m - kw * MISS_FREE);
        }
        resample(tmpl, L, tmplN, N);
        float sum = 0;
        for (int i = 0; i < N; i++) sum += dist(user[i], tmplN[i]);
        out = sum / N / kw + MISS_WEIGHT * miss / kw;
        return true;
    }
};

// лучшие кандидаты по возрастанию score, без повторов слова
struct Best {
    struct Item {
        float score;
        const char* text;
        uint8_t len;
    };
    Item items[8];
    size_t n = 0, max = 8;

    bool full() const { return n == max; }
    float worst() const { return items[n - 1].score; }

    void add(float score, const char* text, size_t len) {
        for (size_t i = 0; i < n; i++) {
            if (items[i].len != len || memcmp(items[i].text, text, len)) continue;
            if (items[i].score <= score) return;
            // то же слово с лучшим счетом: убираем старое
            std::copy(items + i + 1, items + n, items + i);
            n--;
            break;
        }
        if (full() && score >= worst()) return;
        size_t j = n < max ? n++ : max - 1;
        while (j > 0 && items[j - 1].score > score) {
            items[j] = items[j - 1];
            j--;
        }
        items[j] = {score, text, uint8_t(len)};
    }
};

// ранг слова в словаре или NO_RANK
uint32_t findRank(const Dict& d, const char* word, size_t len) {
    int f, l;
    if (!edges(d, word, word + len, f, l)) return NO_RANK;
    size_t b = size_t(f) * d.letters + l;
    for (uint32_t k = d.bucketStart[b]; k < d.bucketStart[b + 1]; k++) {
        const char* p = d.text + d.wordOff[d.bucketWords[k]];
        if (!strncmp(p, word, len) && p[len] == '\n') return d.bucketWords[k];
    }
    return NO_RANK;
}

void loadUserWords() {
    StoredWord stored[MAX_USER_WORDS] = {};
    Preferences prefs;
    if (prefs.begin("words", true)) {
        if (prefs.getBytesLength("list") == sizeof stored) prefs.getBytes("list", stored, sizeof stored);
        prefs.end();
    }
    size_t n = 0;
    for (size_t i = 0; i < MAX_USER_WORDS; i++) {
        UserWord& u = userWords[i];
        memcpy(u.text, stored[i].text, sizeof u.text);
        u.text[sizeof u.text - 1] = 0;
        u.lang = stored[i].lang;
        u.count = u.lang <= settings::RU ? stored[i].count : 0;
        if (!u.count) continue;
        u.rank = findRank(dicts[u.lang], u.text, strlen(u.text));
        n++;
    }
    Serial.printf("[swipe] %u learned words\n", n);
}

}  // namespace

uint32_t nextCp(const char*& p, const char* end) {
    uint8_t c = *p++;
    if (c < 0x80) return c;
    uint32_t cp;
    int more;
    if ((c & 0xE0) == 0xC0) {
        cp = c & 0x1F;
        more = 1;
    } else if ((c & 0xF0) == 0xE0) {
        cp = c & 0x0F;
        more = 2;
    } else {
        cp = c & 0x07;
        more = 3;
    }
    while (more-- && p < end) cp = (cp << 6) | (*p++ & 0x3F);
    return cp;
}

void begin() {
    build(dicts[settings::EN], DICT_EN, 'a', 26);
    build(dicts[settings::RU], DICT_RU, 0x430, 32);
    loadUserWords();
}

size_t decode(settings::Lang lang, const Point* path, size_t n, const LetterKey* keys, size_t keyCount, int keyW, Word* out,
              size_t maxOut) {
    const Dict& d = dicts[lang];
    if (!d.words || n < 2) return 0;

    Match m;
    m.d = &d;
    for (size_t i = 0; i < keyCount; i++) {
        int li = letterIndex(d, keys[i].cp);
        if (li < 0) continue;
        m.centers[li] = {float(keys[i].center.x), float(keys[i].center.y)};
        m.has[li] = true;
    }

    static FPoint raw[256];
    size_t rn = std::min<size_t>(n, 256);
    for (size_t i = 0; i < rn; i++) raw[i] = {float(path[i * n / rn].x), float(path[i * n / rn].y)};
    raw[rn - 1] = {float(path[n - 1].x), float(path[n - 1].y)};
    resample(raw, rn, m.user, N);
    resample(raw, rn, m.dense, 64);

    m.kw = float(keyW);
    int starts[4], ends[4];
    int sn = nearKeys(m.user[0], m.centers, m.has, d.letters, m.kw * 1.3f, starts, 4);
    int en = nearKeys(m.user[N - 1], m.centers, m.has, d.letters, m.kw * 1.3f, ends, 4);

    Best best;
    best.max = std::min<size_t>(maxOut, 8);
    size_t checks = 0;
    for (int si = 0; si < sn; si++) {
        for (int ei = 0; ei < en; ei++) {
            size_t b = size_t(starts[si]) * d.letters + ends[ei];
            for (uint32_t k = d.bucketStart[b]; k < d.bucketStart[b + 1] && checks < MAX_CHECKS; k++, checks++) {
                uint32_t rank = d.bucketWords[k];
                float freqPenalty = FREQ_WEIGHT * std::log(float(rank) + 1.0f);
                // даже идеальное совпадение не попадет в лучшие — дальше слова только реже
                if (best.full() && freqPenalty >= best.worst()) break;
                const char* p = d.text + d.wordOff[rank];
                const char* e = strchr(p, '\n');
                float score;
                if (m.score(p, e, score)) best.add(score + freqPenalty, p, e - p);
            }
        }
    }

    // выученные слова: частота употребления поднимает их выше словарной
    for (const UserWord& u : userWords) {
        if (!u.count || u.lang != lang) continue;
        // слово не из словаря учитываем со второго раза: одиночная опечатка не мешает
        if (u.rank == NO_RANK && u.count < 2) continue;
        const char* p = u.text;
        const char* e = p + strlen(p);
        int f, l;
        if (!edges(d, p, e, f, l)) continue;
        bool near = false;
        for (int si = 0; si < sn; si++)
            for (int ei = 0; ei < en; ei++) near |= starts[si] == f && ends[ei] == l;
        float score;
        if (!near || !m.score(p, e, score)) continue;
        float freqPenalty = FREQ_WEIGHT * std::log(float(u.rank == NO_RANK ? UNKNOWN_RANK : u.rank) + 1.0f);
        best.add(score + freqPenalty - USER_WEIGHT * std::log(1.0f + u.count), p, e - p);
    }

    for (size_t i = 0; i < best.n; i++) out[i] = {best.items[i].text, best.items[i].len, best.items[i].score};
    return best.n;
}

void learn(settings::Lang lang, const char* word, size_t len, uint8_t weight) {
    const Dict& d = dicts[lang];
    if (len < 2 || len >= sizeof(UserWord::text) || !d.words) return;
    int f, l;
    if (!edges(d, word, word + len, f, l)) return;
    UserWord* slot = nullptr;
    for (UserWord& u : userWords)
        if (u.count && u.lang == lang && strlen(u.text) == len && !memcmp(u.text, word, len)) slot = &u;
    if (!slot) {
        // нет места — вытесняем самое редкое
        slot = &userWords[0];
        for (UserWord& u : userWords)
            if (u.count < slot->count) slot = &u;
        memcpy(slot->text, word, len);
        slot->text[len] = 0;
        slot->lang = lang;
        slot->count = 0;
        slot->rank = findRank(d, word, len);
    }
    slot->count = std::min(255, slot->count + weight);
    dirty = true;
    changedAt = millis();
}

void poll() {
    if (!dirty || millis() - changedAt < SAVE_DELAY_MS) return;
    dirty = false;
    Preferences prefs;
    prefs.begin("words", false);
    StoredWord stored[MAX_USER_WORDS];
    for (size_t i = 0; i < MAX_USER_WORDS; i++) {
        memcpy(stored[i].text, userWords[i].text, sizeof stored[i].text);
        stored[i].lang = userWords[i].lang;
        stored[i].count = userWords[i].count;
    }
    prefs.putBytes("list", stored, sizeof stored);
    prefs.end();
}

void forget() {
    for (UserWord& u : userWords) u = UserWord{};
    dirty = true;
    changedAt = millis() - SAVE_DELAY_MS;
}

}  // namespace swipe
