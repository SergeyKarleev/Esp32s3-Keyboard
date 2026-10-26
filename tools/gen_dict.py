"""Словари для ввода свайпом: src/dict_en.c и src/dict_ru.c.

Источник — частотные списки FrequencyWords (OpenSubtitles 2018, CC BY-SA 4.0):
  https://github.com/hermitdave/FrequencyWords/tree/master/content/2018

  python3 tools/gen_dict.py <каталог с en_50k.txt и ru_50k.txt>

Слова идут по убыванию частоты, по одному на строку; ё заменена на е (на раскладке ё
вынесена в слой символов, свайпом ее не набрать).
"""
import re
import sys
from pathlib import Path

LANGS = {
    "en": (re.compile(r"^[a-z]+$"), 30000),
    "ru": (re.compile(r"^[а-я]+$"), 50000),
}


def load(path, pattern, limit):
    words, seen = [], set()
    for line in path.read_text(encoding="utf-8").splitlines():
        word = line.split(" ")[0].lower().replace("ё", "е")
        if len(word) < 2 or not pattern.match(word) or word in seen:
            continue
        seen.add(word)
        words.append(word)
        if len(words) >= limit:
            break
    return words


def main():
    src = Path(sys.argv[1])
    out = Path(__file__).resolve().parent.parent / "src"
    for lang, (pattern, limit) in LANGS.items():
        words = load(src / f"{lang}_50k.txt", pattern, limit)
        lines = [f"// Сгенерировано tools/gen_dict.py: {len(words)} слов по убыванию частоты", f"const char DICT_{lang.upper()}[] ="]
        for i in range(0, len(words), 16):
            lines.append('    "' + "\\n".join(words[i : i + 16]) + '\\n"')
        lines[-1] += ";"
        (out / f"dict_{lang}.c").write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(lang, len(words), "words")


if __name__ == "__main__":
    main()
