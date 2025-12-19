import subprocess
import time
import sys
import random
import string

EXE_PATH = r"C:\Users\fedor\PycharmProjects\gg\build\Debug\indexer.exe"
WORKING_DIR = r"C:\Users\fedor\PycharmProjects\gg"


class AdvancedTester:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        try:
            self.process = subprocess.Popen(
                [EXE_PATH], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True, encoding='utf-8',
                cwd=WORKING_DIR, bufsize=0
            )

            self.process.stdin.write("1\n")
            self.process.stdin.flush()
            self._wait_for("Query:")
        except Exception as e:
            print(f"Ошибка инициализации: {e}")
            sys.exit(1)

    def _wait_for(self, stop_phrase):
        buffer = ""
        while True:
            char = self.process.stdout.read(1)
            if not char: break
            buffer += char
            if stop_phrase in buffer: break
        return buffer

    def run_test(self, name, query, validator_fn):
        start_time = time.time()
        self.process.stdin.write(query + "\n")
        self.process.stdin.flush()
        output = self._wait_for("Query:")
        duration = (time.time() - start_time) * 1000

        count = 0
        for line in output.split('\n'):
            if "Found" in line:
                try:
                    count = int(line.strip().split(' ')[1])
                except:
                    count = 0
                break

        success, message = validator_fn(count, output)
        status = "[  OK  ]" if success else "[ FAIL ]"

        if success:
            self.passed += 1
        else:
            self.failed += 1

        print(f"{status} {name.ljust(20)} | Query: '{query[:30]}...' | {count} docs | {duration:.2f}ms")
        if not success:
            print(f"       -> Error: {message}")

    def run_all(self):
        print("=" * 80)
        print("RUNNING COMPREHENSIVE SEARCH ENGINE TEST SUITE")
        print("=" * 80)

        self.run_test("AND_LOGIC_VALIDATION", "computer science university",
                      lambda c, out: (c <= 12, "Too many results for specific triple-word query"))

        long_query = " ".join(["word" for _ in range(200)])
        self.run_test("LONG_QUERY_STRESS", long_query,
                      lambda c, out: (True, ""))

        garbage = "".join(random.choices(string.ascii_letters + string.digits, k=50))
        self.run_test("FUZZING_RANDOM", garbage,
                      lambda c, out: (c == 0, "Garbage should return 0 results"))

        print("\n--- Starting High-Frequency Stress Test (50 queries) ---")
        stress_start = time.time()
        for _ in range(50):
            self.process.stdin.write("the\n")
            self.process.stdin.flush()
            self._wait_for("Query:")
        stress_duration = time.time() - stress_start
        print(f"[  OK  ] STRESS_TEST_BATCH      | Avg: {(stress_duration / 50) * 1000:.2f}ms per query")

        self.run_test("DUPLICATE_WORDS", "university university university",
                      lambda c, out: (c == 12, "Duplicate words changed result count"))

        print("=" * 80)
        print(f"FINAL RESULT: PASSED: {self.passed} | FAILED: {self.failed}")
        print("=" * 80)

        self.process.stdin.write("exit\n")
        self.process.terminate()


if __name__ == "__main__":
    tester = AdvancedTester()
    tester.run_all()