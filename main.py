import requests
import re
import os
import time
import random
import yaml
from pymongo import MongoClient
from bs4 import BeautifulSoup
from urllib.parse import urlparse

CONFIG_FILE = "config.yaml"
HEADERS = {"User-Agent": "Mozilla/5.0 (compatible; StudentIRBot/1.0; +http://mai.ru)"}
MONGO_FIELDS = ["url", "raw_html", "source", "download_timestamp"]


def load_config():
    if not os.path.exists(CONFIG_FILE):
        raise FileNotFoundError(f"Файл конфигурации не найден: {CONFIG_FILE}")
    with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
        return yaml.safe_load(f)

def get_source_name(url):
    return urlparse(url).netloc.replace('www.', '')

def run_crawler():
    try:
        config = load_config()
    except Exception as e:
        print(f"Ошибка загрузки конфигурации: {e}")

    try:
        client = MongoClient(config['db']['host'])
        db = client[config['db']['database']]
        collection = db[config['db']['collection']]
        collection.create_index("url", unique=True)
        print("Успешное подключение к MongoDB.")
    except Exception as e:
        print(f"Ошибка подключения к Mongo.")
        return

    max_pages = config['logic']['max_pages']
    crawl_delay = config['logic']['crawl_delay']

    total_crawled = collection.count_documents({})
    print(f"В базе уже есть {total_crawled} документов. Цель: {max_pages}.")

    current_id = total_crawled

    for i in range(9, 14):
        for sitemap_url in config['sitemaps']:
            sitemap_url = sitemap_url + "?page=" + str(i)
            if current_id >= max_pages:
                break

            print(f"\nОбработка sitemap: {sitemap_url}")
            try:
                r = requests.get(sitemap_url, headers=HEADERS, timeout=15)
                if r.status_code != 200:
                    print(f"Ошибка sitemap {sitemap_url}: {r.status_code}")
                    continue

                soup = BeautifulSoup(r.content, 'lxml')
                urls = [loc.text.strip() for loc in soup.find_all('loc')]

                for page_url in urls:
                    if current_id >= max_pages:
                        break

                    if any(ext in page_url for ext in ['.jpg', '.png', '.pdf']):
                        continue

                    existing_doc = collection.find_one({"url": page_url})
                    headers_to_send = HEADERS.copy()

                    if existing_doc:
                        timestamp = existing_doc.get('download_timestamp')
                        if timestamp:
                            last_crawled_date = time.strftime('%a, %d %b %Y %H:%M:%S GMT', time.gmtime(timestamp))
                            headers_to_send['If-Modified-Since'] = last_crawled_date

                    try:
                        resp = requests.get(page_url, headers=headers_to_send, timeout=10)

                        if resp.status_code == 304:
                            print(f"[{current_id}] Не изменен: {page_url}")
                            continue

                        if resp.status_code == 200:
                            current_timestamp = time.time()

                            document_data = {
                                "url": page_url,
                                "raw_html": resp.text,
                                "source": get_source_name(page_url),
                                "download_timestamp": current_timestamp
                            }

                            if existing_doc:
                                collection.update_one(
                                    {"_id": existing_doc['_id']},
                                    {"$set": document_data}
                                )
                                print(f"[{existing_doc['_id']}] Обновлен: {page_url}")
                            else:
                                insert_result = collection.insert_one(document_data)
                                current_id += 1
                                print(f"[{current_id}] Скачан: {page_url}")

                        else:
                            print(f"[{current_id}] Ошибка доступа {resp.status_code}: {page_url}")

                    except Exception as e:
                        print(f"[{current_id}] Ошибка скачивания {page_url}: {e}")

                    time.sleep(random.uniform(crawl_delay, crawl_delay * 1.5))

            except Exception as e:
                print(f"Общая ошибка при обработке sitemap {sitemap_url}: {e}")

    print(f"\nВсего в базе: {collection.count_documents({})}.")


if __name__ == "__main__":
    run_crawler()