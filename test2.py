import unittest
import subprocess
import os
from pathlib import Path

EXE_PATH = r"C:\Users\fedor\PycharmProjects\gg\build\Debug\indexer.exe"
WORKING_DIR = r"C:\Users\fedor\PycharmProjects\gg"

DATASET_DIR = Path(WORKING_DIR) / "dataset_txt"
DICT_PATH = Path(WORKING_DIR) / "resources" / "lemmas.txt"


class TestIndexer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.exists(EXE_PATH):
            raise FileNotFoundError(f"Исполняемый файл не найден: {EXE_PATH}")

        if not DICT_PATH.exists():
            raise FileNotFoundError(f"Словарь лемм не найден: {DICT_PATH}")

        txt_files = list(DATASET_DIR.glob("*.txt"))
        if len(txt_files) < 100:  # у тебя 51000, так что ок
            raise ValueError(f"Ожидается много txt-файлов, найдено: {len(txt_files)}")

    def run_program(self, input_text: str, timeout: int = 300) -> str:
        """
        Запускает программу с заданным вводом.
        По умолчанию timeout=300 секунд (5 минут), т.к. индексация 51k документов занимает время.
        """
        proc = subprocess.run(
            [EXE_PATH],
            cwd=WORKING_DIR,
            input=input_text.encode('utf-8', errors='ignore'),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout  # Увеличенный таймаут
        )

        stderr = proc.stderr.decode('cp1251', errors='ignore')
        stdout = proc.stdout.decode('cp1251', errors='ignore')

        if proc.returncode != 0:
            self.fail(f"Программа упала с кодом {proc.returncode}\nSTDERR:\n{stderr}")

        if "Dictionary not found" in stderr or "Dictionary not found" in stdout:
            self.fail("Не найден словарь лемм")

        return stdout

    def test_01_indexing_and_zipf(self):
        """Проверка индексации и вывода Zipf's law"""
        output = self.run_program("1\nexit\n", timeout=400)

        self.assertIn("Indexing files in", output)
        self.assertIn("Indexing completed", output)
        self.assertIn("Documents: 51000", output)
        self.assertIn("Terms:", output)
        self.assertIn("=== ZIPF'S LAW CHECK ===", output)
        self.assertIn("Constant (F*R)", output)
        self.assertIn("forbes", output)

    def test_02_boolean_search_basic(self):
        """Простые булевы запросы (реальные данные корпуса)"""
        input_data = (
            "1\n"
            "+home\n"
            "+home +dog\n"
            "+home -dog\n"
            "exit\n"
        )

        output = self.run_program(input_data, timeout=300)

        self.assertIn("=== BOOLEAN SEARCH ===", output)
        self.assertIn("Results:", output)

        self.assertIn("4506.txt", output)

        self.assertIn("14875.txt", output)

    def test_03_boolean_logic(self):
        """Проверка логики + и - на реальных русских словах"""
        input_data = (
            "1\n"
            "+собака +дом +мяч\n"
            "+собака -дом\n"
            "exit\n"
        )

        output = self.run_program(input_data, timeout=500)

        self.assertIn("Results: 18642.txt", output)

        self.assertIn("Results:", output)

        self.assertNotIn("No documents match", output)

    def test_04_tf_idf_search(self):
        """TF-IDF поиск"""
        input_data = (
            "2\n"
            "home\n"
            "dog\n"
            "home dog\n"
            "exit\n"
        )

        output = self.run_program(input_data, timeout=300)

        self.assertIn("File:", output)
        self.assertIn("| Score:", output)

        self.assertIn("31849.txt | Score: 0.190228", output)
        self.assertIn("6231.txt | Score: 0.038583", output)

    def test_05_tf_idf_no_results(self):
        """Запрос без результатов"""
        input_data = (
            "2\n"
            "оченьредкоесловокоторогонетвкорпусе12345\n"
            "exit\n"
        )

        output = self.run_program(input_data, timeout=300)
        self.assertIn("No matching documents found", output)


if __name__ == '__main__':
    unittest.main(verbosity=2)