import os
import re
import sys
import pymorphy3

DATASET_DIR = "dataset_txt"
RESOURCES_DIR = "resources"
DICT_FILE = os.path.join(RESOURCES_DIR, "lemmas.txt")

def create_lemma_dict():
    morph = pymorphy3.MorphAnalyzer(lang="ru")

    unique_words = set()

    print(" начало")

    files = sorted(os.listdir(DATASET_DIR))
    total_files = len(files)
    processed_files = 0

    for i, filename in enumerate(files, start=1):
        if not filename.lower().endswith(".txt"):
            continue

        path = os.path.join(DATASET_DIR, filename)
        try:
            with open(path, "r", encoding="utf-8") as f:
                text = f.read().lower()
                words = re.findall(r'[а-яё]+', text)
                unique_words.update(words)
        except Exception as e:
            print(f"ошибка чтения {filename}: {e}")

        processed_files += 1
        if processed_files % 100 == 0 or i == total_files:
            print(f"просканировано {processed_files}/{total_files} файлов. найдено слов: {len(unique_words)}")

    print(f"\n уникальных слов: {len(unique_words)}")

    sorted_words = sorted(unique_words)

    with open(DICT_FILE, "w", encoding="utf-8") as f:
        count = 0
        for word in sorted_words:
            try:
                parsed = morph.parse(word)
                if not parsed:
                    lemma = word
                else:
                    lemma = parsed[0].normal_form
            except Exception as e:
                lemma = word

            f.write(f"{word}\t{lemma}\n")
            count += 1
            if count % 10000 == 0:
                print(f"записано {count} лемм...")

if __name__ == "__main__":
    create_lemma_dict()

