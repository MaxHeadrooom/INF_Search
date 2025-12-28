import os
import yaml
import re
import shutil  # уже импортировали для очистки
from pymongo import MongoClient
from bs4 import BeautifulSoup

CONFIG_FILE = "config.yaml"
OUTPUT_DIR = "dataset_txt"
REGISTRY_FILE = "urls.txt"

BAD_URL_PATTERNS = [
    "/tegi/", "/tags/", "/category/", "/author/",
    "/podcasts/", "/search/", "/archive/", "/amp/"
]

def load_config():
    if not os.path.exists(CONFIG_FILE):
        return {'db': {'host': 'mongodb://localhost:27017/', 'database': 'ir_search_engine', 'collection': 'pages'}}
    with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
        return yaml.safe_load(f)


def clean_html(html_content):
    if not html_content:
        return ""

    # Ключевой фикс: принудительно работаем с bytes и UTF-8
    if isinstance(html_content, str):
        # Если уже строка — кодируем в bytes с заменой ошибок
        html_bytes = html_content.encode('utf-8', errors='replace')
    else:
        # Если bytes — декодируем/кодируем с заменой
        html_bytes = html_content.decode('utf-8', errors='replace').encode('utf-8')

    try:
        soup = BeautifulSoup(html_bytes, 'lxml')
    except:
        soup = BeautifulSoup(html_bytes, 'html.parser')

    # Удаляем ненужные теги
    for tag in soup(["script", "style", "header", "footer", "nav", "aside", "form", "iframe", "noscript", "button"]):
        tag.extract()

    text = soup.get_text(separator=' ', strip=True)
    # Нормализуем пробелы
    text = re.sub(r'\s+', ' ', text)
    return text.strip()


def run_export():
    config = load_config()
    try:
        client = MongoClient(config['db']['host'])
        db = client[config['db']['database']]
        collection = db[config['db']['collection']]
        print(f"Подключено к MongoDB. Всего документов: {collection.count_documents({})}")
    except Exception as e:
        print(f"Ошибка БД: {e}")
        return

    # Очистка перед экспортом
    if os.path.exists(OUTPUT_DIR):
        shutil.rmtree(OUTPUT_DIR)
    os.makedirs(OUTPUT_DIR)

    if os.path.exists(REGISTRY_FILE):
        os.remove(REGISTRY_FILE)

    with open(REGISTRY_FILE, "w", encoding="utf-8") as reg:
        cursor = collection.find({}, {"url": 1, "raw_html": 1})

        doc_id = 0
        exported_count = 0

        for doc in cursor:
            url = doc.get("url", "")

            if any(pattern in url for pattern in BAD_URL_PATTERNS):
                continue

            try:
                raw_html = doc.get("raw_html", "")

                clean_text = clean_html(raw_html)

                # Фильтр по минимальной длине
                if len(clean_text.split()) < 10:
                    continue

                filename = os.path.join(OUTPUT_DIR, f"{doc_id}.txt")
                with open(filename, "w", encoding="utf-8") as f:
                    f.write(clean_text)

                reg.write(f"{doc_id}\t{url}\n")

                doc_id += 1
                exported_count += 1
                if exported_count % 500 == 0:
                    print(f"Обработано: {exported_count} документов...")

            except Exception as e:
                print(f"Ошибка на {url}: {e}")

    print(f"\nГотово! Экспортировано {exported_count} чистых документов.")


if __name__ == "__main__":
    run_export()