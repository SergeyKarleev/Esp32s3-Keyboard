"""Словари для ввода свайпом: src/dict_en.c и src/dict_ru.c.

Источник — частотные списки FrequencyWords (OpenSubtitles 2018, CC BY-SA 4.0):
  https://github.com/hermitdave/FrequencyWords/tree/master/content/2018

  python3 tools/gen_dict.py <каталог с en_full.txt и ru_full.txt>

Слова идут по убыванию частоты, по одному на строку; ё заменена на е (на раскладке ё
вынесена в слой символов, свайпом ее не набрать).

IT-лексика и сленг из tools/words_<lang>.txt поднимаются в начало: базовые формы
вперемешку с самыми частыми словами, остальные формы — следом.
"""
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent

LANGS = {
    "en": (re.compile(r"^[a-z]+$"), 50000),
    "ru": (re.compile(r"^[а-я]+$"), 75000),
}

# место в словаре для слов из words_<lang>.txt: ранг FIRST, затем каждый STEP-й
BOOST_FIRST = 60
BOOST_STEP = 3

VOWELS_AFTER_SIBILANT = {"ы": "и", "я": "а", "ю": "у"}


def soften(stem, ending):
    """Правописание после к/г/х и ж/ш/ч/щ: кы -> ки, шя -> ша."""
    if stem and ending and stem[-1] in "кгхжшчщ" and ending[0] in "ыяю":
        if stem[-1] in "кгх" and ending[0] != "ы":
            return stem + ending
        return stem + VOWELS_AFTER_SIBILANT[ending[0]] + ending[1:]
    return stem + ending


def ru_forms(word, paradigm):
    """Формы русского слова по короткой пометке (см. tools/words_ru.txt)."""
    if paradigm in ("m", "mk"):
        ends = ["", "а", "у", "ом", "е", "ы", "ов", "ам", "ами", "ах"]
        return [soften(word, e) for e in ends]
    if paradigm in ("f", "fk", "fs"):
        stem = word[:-1]
        ends = ["а", "ы", "е", "у", "ой", "ам", "ами", "ах"]
        forms = [soften(stem, e) for e in ends]
        if paradigm == "fs":
            forms.append(stem + "ей")
        return forms
    if paradigm == "vi":
        stem = word[:-3]
        ends = ["ить", "ил", "ила", "ило", "или", "ит", "им", "ишь", "ите", "и", "ят"]
        return [soften(stem, e) for e in ends]
    if paradigm == "va":
        stem = word[:-2]
        ends = ["ть", "ю", "ешь", "ет", "ем", "ете", "ют", "л", "ла", "ли", "ло", "й", "йте"]
        return [stem + e for e in ends]
    if paradigm == "vnu":
        stem = word[:-4]
        ends = ["нуть", "ну", "нешь", "нет", "нем", "нете", "нут", "нул", "нула", "нули", "ни"]
        return [stem + e for e in ends]
    if paradigm == "adj":
        stem = word[:-2]
        ends = ["ый", "ая", "ое", "ые", "ого", "ому", "ым", "ом", "ой", "ую", "ых", "ыми"]
        return [soften(stem, e) for e in ends]
    raise ValueError(f"неизвестная парадигма {paradigm} у {word}")


def load_boost(lang, pattern):
    """Слова из words_<lang>.txt: (базовые формы, остальные формы) в порядке приоритета."""
    base, extra = [], []
    for line in (TOOLS / f"words_{lang}.txt").read_text(encoding="utf-8").splitlines():
        line = line.split("#")[0].strip()
        for token in line.split():
            word, _, paradigm = token.partition(":")
            word = word.lower().replace("ё", "е")
            forms = ru_forms(word, paradigm) if paradigm else [word]
            base.append(forms[0] if not paradigm else word)
            extra.extend(forms)
    keep = lambda words: [w for w in dict.fromkeys(words) if len(w) >= 2 and pattern.match(w)]
    base = keep(base)
    return base, [w for w in keep(extra) if w not in set(base)]


def load(path, pattern, limit):
    words, seen = [], set()
    with path.open(encoding="utf-8") as f:
        for line in f:
            word = line.split(" ")[0].lower().replace("ё", "е")
            if len(word) < 2 or not pattern.match(word) or word in seen:
                continue
            seen.add(word)
            words.append(word)
            if len(words) >= limit:
                break
    return words


def merge(general, base, extra):
    boosted = base + extra
    boosted_set = set(boosted)
    rest = [w for w in general if w not in boosted_set]
    out = rest[:BOOST_FIRST]
    i = BOOST_FIRST
    for w in boosted:
        out.extend(rest[i : i + BOOST_STEP - 1])
        i += BOOST_STEP - 1
        out.append(w)
    out.extend(rest[i:])
    return out[: len(general)]


def main():
    src = Path(sys.argv[1])
    out = TOOLS.parent / "src"
    for lang, (pattern, limit) in LANGS.items():
        path = src / f"{lang}_full.txt"
        if not path.exists():
            path = src / f"{lang}_50k.txt"
        base, extra = load_boost(lang, pattern)
        words = merge(load(path, pattern, limit), base, extra)
        lines = [f"// Сгенерировано tools/gen_dict.py: {len(words)} слов по убыванию частоты", f"const char DICT_{lang.upper()}[] ="]
        for i in range(0, len(words), 16):
            lines.append('    "' + "\\n".join(words[i : i + 16]) + '\\n"')
        lines[-1] += ";"
        (out / f"dict_{lang}.c").write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(lang, len(words), "words,", len(base), "+", len(extra), "boosted")


if __name__ == "__main__":
    main()
