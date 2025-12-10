import os
import yaml
from pymongo import MongoClient
from bs4 import BeautifulSoup
import re

CONFIG_FILE = "config.yaml"
OUTPUT_DIR = "dataset_txt"
REGISTRY_FILE = "urls.txt"


def load_config():
    if not os.path.exists(CONFIG_FILE):
        return {'db': {'host': 'mongodb://localhost:27017/', 'database': 'ir_search_engine', 'collection': 'pages'}}
    with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
        return yaml.safe_load(f)


def clean_text(html_content):
    if not html_content:
        return ""

    try:
        soup = BeautifulSoup(html_content, 'lxml')
    except:
        soup = BeautifulSoup(html_content, 'html.parser')

    for script in soup(["script", "style", "header", "footer", "nav", "aside", "form", "iframe", "noscript"]):
        script.extract()

    text = soup.get_text(separator=' ', strip=True)

    text = re.sub(r'\s+', ' ', text)

    return text


def run_export():
    config = load_config()

    try:
        client = MongoClient(config['db']['host'])
        db = client[config['db']['database']]
        collection = db[config['db']['collection']]
        total_docs = collection.count_documents({})
        print(f"Подключено к MongoDB. Найдено документов: {total_docs}")
    except Exception as e:
        print(f"Ошибка подключения к БД: {e}")
        return

    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)
        print(f"Создана папка {OUTPUT_DIR}")

    with open(REGISTRY_FILE, "w", encoding="utf-8") as reg:
        cursor = collection.find({}, {"url": 1, "raw_html": 1})

        doc_id = 0
        exported_count = 0

        for doc in cursor:
            try:
                raw_html = doc.get("raw_html", "")
                url = doc.get("url", "")

                clean_content = clean_text(raw_html)

                filename = os.path.join(OUTPUT_DIR, f"{doc_id}.txt")

                with open(filename, "w", encoding="utf-8") as f:
                    f.write(clean_content)

                reg.write(f"{doc_id}\t{url}\n")

                doc_id += 1
                exported_count += 1

                if exported_count % 1000 == 0:
                    print(f"Экспортировано: {exported_count}")

            except Exception as e:
                print(f"Ошибка при обработке документа {doc.get('url', 'unknown')}: {e}")

    print(f"Тексты лежат в папке: {OUTPUT_DIR}")
    print(f"Список ссылок: {REGISTRY_FILE}")


if __name__ == "__main__":
    run_export()