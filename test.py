import subprocess
import time
import sys

EXE_PATH = r"C:\Users\fedor\PycharmProjects\gg\build\Debug\indexer.exe"
WORKING_DIR = r"C:\Users\fedor\PycharmProjects\gg"


class SearchEngineTester:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.results = []
        try:
            self.process = subprocess.Popen(
                [EXE_PATH],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding='utf-8',
                cwd=WORKING_DIR,
                bufsize=0
            )
            self._send("1")
            self._wait_for("Query:")
        except Exception as e:
            print(f"Failed to start engine: {e}")
            sys.exit(1)

    def _send(self, text):
        self.process.stdin.write(text + "\n")
        self.process.stdin.flush()

    def _wait_for(self, stop_phrase):
        buffer = ""
        while True:
            char = self.process.stdout.read(1)
            if not char: break
            buffer += char
            if stop_phrase in buffer: break
        return buffer

    def assert_query(self, suite, query, min_results=0, max_latency=50.0, expected_count=None):
        start = time.perf_counter()
        self._send(query)
        output = self._wait_for("Query:")
        end = time.perf_counter()

        latency = (end - start) * 1000

        count = 0
        for line in output.split('\n'):
            if "Found" in line:
                try:
                    count = int(line.strip().split(' ')[1])
                except:
                    count = 0
                break
        is_passed = True
        reason = ""

        if count < min_results:
            is_passed = False
            reason = f"Too few results (got {count}, expected min {min_results})"
        elif expected_count is not None and count != expected_count:
            is_passed = False
            reason = f"Count mismatch (got {count}, expected {expected_count})"
        elif latency > max_latency:
            is_passed = False
            reason = f"Timeout ({latency:.2f}ms > {max_latency}ms)"

        if is_passed:
            self.passed += 1
            status = " [ PASSED ] "
        else:
            self.failed += 1
            status = f" [ FAILED ] ({reason})"

        print(f"{status} {suite} :: '{query}' ({count} docs, {latency:.2f}ms)")

    def run_all(self):
        print(f"{'=' * 70}\nRUNNING SEARCH ENGINE TEST SUITE\n{'=' * 70}")

        self.assert_query("BASIC", "home", min_results=30)
        self.assert_query("BASIC", "university", min_results=10)

        self.assert_query("NORMALIZE", "UNIVERSITY", expected_count=12)
        self.assert_query("NORMALIZE", "uNiVeRsItY", expected_count=12)

        self.assert_query("PERF", "the", min_results=500, max_latency=15.0)
        self.assert_query("PERF", "and", min_results=50, max_latency=10.0)

        # Проверяем, что поиск по фразе не падает и логически корректен
        self.assert_query("MULTI", "data science", min_results=1)
        self.assert_query("MULTI", "computer engineering technology", min_results=0)

        self.assert_query("EDGE", "", expected_count=0)  # Пустой ввод
        self.assert_query("EDGE", " ", expected_count=0)  # Пробел
        self.assert_query("EDGE", "a" * 100, expected_count=0)  # Очень длинная строка
        self.assert_query("EDGE", "./.,!@#$%^", expected_count=0)  # Мусорные символы

        self.assert_query("MISS", "quantum_mechanics_in_my_kitchen", expected_count=0)
        self.assert_query("MISS", "zzyzx", expected_count=0)

        print(f"\n{'=' * 70}\nTEST RESULTS Summary:")
        print(f" TOTAL:  {self.passed + self.failed}")
        print(f" PASSED: {self.passed}")
        print(f" FAILED: {self.failed}")
        print(f"{'=' * 70}")

        self._send("exit")
        self.process.terminate()


if __name__ == "__main__":
    tester = SearchEngineTester()
    tester.run_all()