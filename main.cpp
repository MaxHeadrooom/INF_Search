#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstdint>
#include <cmath> 
#include <iomanip> 
#include <set>
#include <iterator>
#include <map>
#include <windows.h>
#include <limits> 

using namespace std;
namespace fs = std::filesystem;

const string DIR_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\dataset_txt";
const string DICT_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\resources\\lemmas.txt";
const string INV_INDEX_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\inverted_index.bin";
const string DOC_NAMES_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\doc_names.txt";
const string DOC_LENGTHS_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\doc_lengths.txt";
const string DOC_URLS_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\urls.txt"; 

const size_t HASH_SIZE = 10000; 

struct Hasher {
    size_t operator()(const string& key) const {
        size_t h = 0;
        for (unsigned char c : key) h = (h * 31 + c);
        return h % HASH_SIZE; 
    }
    size_t operator()(int key) const { return (size_t)abs(key) % HASH_SIZE; }
};

template <typename K, typename V>
struct CustomHashMap {
    vector<pair<K, V>> buckets[HASH_SIZE]; 
    Hasher hasher;

    void insert(const K& key, const V& value) {
        size_t index = hasher(key);
        for (auto& p : buckets[index]) { if (p.first == key) { p.second = value; return; } }
        buckets[index].push_back({key, value}); 
    }

    V* find(const K& key) {
        size_t index = hasher(key);
        for (auto& p : buckets[index]) { if (p.first == key) return &p.second; }
        return nullptr;
    }

    const V* find(const K& key) const {
        size_t index = hasher(key);
        for (const auto& p : buckets[index]) { if (p.first == key) return &p.second; }
        return nullptr;
    }

    bool count(const K& key) const { return find(key) != nullptr; }
    
    V& operator[](const K& key) {
        V* ptr = find(key);
        if (!ptr) { insert(key, V{}); return *find(key); }
        return *ptr;
    }

    size_t size() const {
        size_t total = 0;
        for (size_t i = 0; i < HASH_SIZE; ++i) total += buckets[i].size();
        return total;
    }

    struct Iterator {
        vector<pair<K, V>>* base;
        size_t b_idx;
        typename vector<pair<K, V>>::iterator it;
        
        Iterator(vector<pair<K, V>>* _base, size_t _idx) : base(_base), b_idx(_idx) {
            if (b_idx < HASH_SIZE) { it = base[b_idx].begin(); skip(); }
        }
        void skip() {
            while (b_idx < HASH_SIZE && it == base[b_idx].end()) {
                b_idx++;
                if (b_idx < HASH_SIZE) it = base[b_idx].begin();
            }
        }
        pair<K, V>& operator*() { return *it; }
        Iterator& operator++() { ++it; skip(); return *this; }
        bool operator!=(const Iterator& o) const { return b_idx != o.b_idx || (b_idx < HASH_SIZE && it != o.it); }
    };
    Iterator begin() { return Iterator(buckets, 0); }
    Iterator end() { return Iterator(buckets, HASH_SIZE); }
};

CustomHashMap<string, string> lemmas; 
CustomHashMap<string, vector<uint8_t>> inv_index; 
CustomHashMap<int, string> doc_names; 
CustomHashMap<int, int> doc_lengths;
CustomHashMap<int, string> doc_urls;
long long total_docs_count = 0;

bool is_cont(unsigned char b) { return (b & 0xC0) == 0x80; }

vector<uint32_t> to_codes(const string &s) {
    vector<uint32_t> res; size_t i = 0;
    const unsigned char *p = reinterpret_cast<const unsigned char*>(s.data());
    while (i < s.size()) {
        unsigned char c = p[i]; uint32_t val = 0; size_t n = 0;
        if (c < 0x80) { val = c; n = 0; } 
        else if ((c & 0xE0) == 0xC0) { val = c & 0x1F; n = 1; } 
        else if ((c & 0xF0) == 0xE0) { val = c & 0x0F; n = 2; } 
        else if ((c & 0xF8) == 0xF0) { val = c & 0x07; n = 3; } 
        else { ++i; continue; }
        if (i + n >= s.size()) break;
        bool fail = false;
        for (size_t k = 1; k <= n; ++k) { if (!is_cont(p[i + k])) { fail = true; break; } val = (val << 6) | (p[i + k] & 0x3F); }
        if (fail) { ++i; continue; } res.push_back(val); i += 1 + n;
    }
    return res;
}

bool load_doc_urls() {
    ifstream f(DOC_URLS_PATH);
    if (!f.is_open()) {
        cerr << "Не удалось открыть файл с URL: " << DOC_URLS_PATH << endl;
        return false;
    }

    string line;
    int loaded = 0;
    while (getline(f, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        int id;
        string url;
        if (ss >> id && getline(ss, url)) {
            size_t start = url.find_first_not_of(" \t");
            if (start != string::npos) url = url.substr(start);
            doc_urls.insert(id, url);
            loaded++;
        }
    }
    cout << "Загружено " << loaded << " URL из " << DOC_URLS_PATH << endl;
    return true;
}

string to_utf8(const vector<uint32_t> &v) {
    string res; 
    for (uint32_t c : v) {
        if (c <= 0x7F) res.push_back((char)c);
        else if (c <= 0x7FF) { res.push_back((char)(0xC0 | ((c >> 6) & 0x1F))); res.push_back((char)(0x80 | (c & 0x3F))); }
        else if (c <= 0xFFFF) { res.push_back((char)(0xE0 | ((c >> 12) & 0x0F))); res.push_back((char)(0x80 | ((c >> 6) & 0x3F))); res.push_back((char)(0x80 | (c & 0x3F))); }
        else { res.push_back((char)(0xF0 | ((c >> 18) & 0x07))); res.push_back((char)(0xC0 | ((c >> 12) & 0x3F))); res.push_back((char)(0x80 | ((c >> 6) & 0x3F))); res.push_back((char)(0x80 | (c & 0x3F))); }
    } return res;
}

uint32_t char_lower(uint32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32; 
    if (c >= 0x0410 && c <= 0x042F) return c + 0x20; 
    if (c == 0x0401) return 0x0451; 
    return c;
}

string str_lower(const string &s) {
    vector<uint32_t> v = to_codes(s); 
    for (auto &c : v) c = char_lower(c); 
    return to_utf8(v);
}

bool check_sym(uint32_t c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return true; 
    if (c >= '0' && c <= '9') return true; 
    if (c >= 0x0400 && c <= 0x04FF) return true; 
    return false;
}

vector<string> parse(const string &text) {
    vector<string> res; 
    vector<uint32_t> codes = to_codes(text); 
    vector<uint32_t> cur;
    for (uint32_t c : codes) {
        if (check_sym(c)) {
            cur.push_back(char_lower(c));  
        } else if (!cur.empty()) {
            string low = to_utf8(cur);
            res.push_back(low);
            cur.clear();
        }
    }
    if (!cur.empty()) {
        string low = to_utf8(cur);
        res.push_back(low);
    }
    return res;
}

void vbyte_encode(int n, vector<uint8_t>& res) {
    while (n >= 128) { res.push_back((n & 0x7F)); n >>= 7; }
    res.push_back(n | 0x80);
}

int vbyte_decode(const vector<uint8_t>& data, size_t& offset) {
    int n = 0, shift = 0;
    while (offset < data.size()) {
        uint8_t b = data[offset++];
        n |= (b & 0x7F) << shift;
        if (b & 0x80) break;
        shift += 7;
    }
    return n;
}

vector<pair<int, int>> decompress_list(const vector<uint8_t>& data) {
    vector<pair<int, int>> res;
    size_t offset = 0; int last_id = 0;
    while (offset < data.size()) {
        int delta = vbyte_decode(data, offset);
        int freq = vbyte_decode(data, offset);
        last_id += delta;
        res.push_back({last_id, freq});
    }
    return res;
}

void check_zipf() {
    cout << "\n=== ZIPF'S LAW CHECK ===\n";
    vector<pair<string, int>> freqs;
    for (auto& p : inv_index) {
        int total_f = 0;
        auto postings = decompress_list(p.second);
        for (auto& entry : postings) total_f += entry.second;
        freqs.push_back({p.first, total_f});
    }
    sort(freqs.begin(), freqs.end(), [](auto& a, auto& b){ return a.second > b.second; });

    cout << left << setw(15) << "Word" << setw(10) << "Freq" << setw(10) << "Rank" << "Constant (F*R)\n";
    for (int i = 0; i < min((int)freqs.size(), 15); i++) {
        int rank = i + 1;
        cout << left << setw(15) << freqs[i].first 
             << setw(10) << freqs[i].second 
             << setw(10) << rank 
             << (long long)freqs[i].second * rank << endl;
    }
}

void boolean_search() {
    cout << "\n=== BOOLEAN SEARCH ===\n";
    string query;

    while (true) {
        cout << "Bool Query (+word, -word, word): ";
        cout.flush();

        if (!getline(cin, query)) {
            break;
        }
        if (query == "exit") {
            break;
        }
        if (query.empty()) {
            cout << "Results: No documents match.\n";
            continue;
        }

        stringstream ss(query);
        string token;

        set<int> must_have;  
        set<int> must_not;    
        set<int> should_have; 

        bool has_plus = false;
        bool has_regular = false;

        vector<string> plus_terms;

        while (ss >> token) {
            char prefix = ' ';
            string raw_word = token;

            if (token.size() > 1 && (token[0] == '+' || token[0] == '-')) {
                prefix = token[0];
                raw_word = token.substr(1);
            }

            vector<string> parsed = parse(raw_word);
            if (parsed.empty()) {
                if (prefix == '+') has_plus = true;
                continue;
            }
            string word = parsed[0];

            if (prefix == '+') {
                plus_terms.push_back(word);  
            }

            if (!inv_index.count(word)) {
                if (prefix == '+') has_plus = true;  
                continue;
            }

            auto postings = decompress_list(inv_index[word]);
            set<int> doc_ids;
            for (const auto& p : postings) {
                doc_ids.insert(p.first);
            }

            if (prefix == '+') {
                has_plus = true;
                if (must_have.empty()) {
                    must_have = std::move(doc_ids);
                } else {
                    set<int> intersect;
                    set_intersection(must_have.begin(), must_have.end(),
                                     doc_ids.begin(), doc_ids.end(),
                                     inserter(intersect, intersect.begin()));
                    must_have = std::move(intersect);
                }
            } else if (prefix == '-') {
                must_not.insert(doc_ids.begin(), doc_ids.end());
            } else {
                has_regular = true;
                should_have.insert(doc_ids.begin(), doc_ids.end());
            }
        }

        if (has_plus && must_have.empty()) {
            cout << "Results: No documents match (required terms not found).\n";
            continue;
        }

        cout << "Debug: has_plus = " << has_plus << ", must_have size = " << must_have.size()  << ", should_have size = " << should_have.size() << endl;

        vector<int> candidates;

        if (has_plus) {
            candidates.assign(must_have.begin(), must_have.end());
        } else if (has_regular) {
            candidates.assign(should_have.begin(), should_have.end());
        } else {
            cout << "Results: No documents match.\n";
            continue;
        }

        vector<int> final_results;
        for (int doc_id : candidates) {
            if (must_not.count(doc_id)) continue;
            final_results.push_back(doc_id);
        }

        if (has_plus && !final_results.empty() && !plus_terms.empty()) {
            vector<int> verified;
            for (int doc_id : final_results) {
                string filename = to_string(doc_id) + ".txt";
                string file_path = DIR_PATH + "\\" + filename;

                ifstream doc_file(file_path);
                if (!doc_file.is_open()) continue;

                string content((istreambuf_iterator<char>(doc_file)), istreambuf_iterator<char>());
                doc_file.close();

                string lower_content = str_lower(content);

                bool all_present = true;
                for (const string& term : plus_terms) {
                    if (lower_content.find(term) == string::npos) {
                        all_present = false;
                        break;
                    }
                }
                if (all_present) {
                    verified.push_back(doc_id);
                }
            }
            final_results = std::move(verified);
        }

        cout << "Results: ";
        if (final_results.empty()) {
            cout << "No documents match.";
        } else {
            for (int doc_id : final_results) {
                string* url_ptr = doc_urls.find(doc_id);
                string url = url_ptr ? *url_ptr : "[doc_" + to_string(doc_id) + "]";
                cout << url << " ";
            }
        }
        cout << endl;
    }
}

void tf_idf_search() {
    cout << "\n=== TF-IDF SEARCH ===\n";
    string query;

    const double MIN_SCORE = 0.05;  

    while (true) {
        cout << "TF-IDF Query: ";
        cout.flush(); 

        if (!getline(cin, query)) {
            break;
        }
        if (query == "exit") {
            break;
        }

        vector<string> q_words = parse(query);
        if (q_words.empty()) {
            cout << "No query terms.\n";
            continue;
        }

        map<int, double> scores;
        for (const string& w : q_words) {
            const vector<uint8_t>* data = inv_index.find(w);
            if (data) {
                auto postings = decompress_list(*data);
                double idf = log((double)total_docs_count / postings.size());
                for (const auto& e : postings) {
                    int doc_id = e.first;
                    int doc_len = doc_lengths[doc_id];
                    if (doc_len > 0) {
                        double tf = (double)e.second / doc_len;
                        scores[doc_id] += tf * idf;
                    }
                }
            }
        }

        if (scores.empty()) {
            cout << "No matching documents found.\n";
            continue;
        }

        vector<pair<double, int>> results;
        for (const auto& p : scores) {
            if (p.second >= MIN_SCORE) {  
                results.emplace_back(p.second, p.first);
            }
        }

        if (results.empty()) {
            cout << "No documents with sufficient relevance found.\n";
            continue;
        }

        sort(results.begin(), results.end(), greater<pair<double, int>>());

        size_t top_k = 10;
        cout << "Top " << min(results.size(), top_k);
        for (size_t i = 0; i < min(results.size(), top_k); ++i) {
            int doc_id = results[i].second;
            double score = results[i].first;

            string* url_ptr = doc_urls.find(doc_id);
            string url = url_ptr ? *url_ptr : "[URL не найден для doc_id " + to_string(doc_id) + "]";

            cout << url << " | Score: " << fixed << setprecision(6) << score << endl;
        }
    }
}

bool load_dictionary() {
    ifstream f(DICT_PATH);
    if (!f.is_open()) return false;
    string k, v;
    while (f >> k >> v) lemmas.insert(str_lower(k), str_lower(v));
    return true;
}

void index_data() {
    cout << "Indexing files in " << DIR_PATH << "...\n";
    
    CustomHashMap<string, vector<pair<int, int>>> temp_postings;
    
    int id = 0;
    for (const auto& entry : fs::directory_iterator(DIR_PATH)) {
        if (entry.path().extension() != ".txt") continue;
        
        id++;
        string filename = entry.path().filename().string();
        doc_names.insert(id, filename);
        
        ifstream f(entry.path());
        if (!f.is_open()) {
            cerr << "Cannot open: " << entry.path() << endl;
            continue;
        }
        
        string content((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
        vector<string> words = parse(content);
        doc_lengths.insert(id, static_cast<int>(words.size()));

        map<string, int> local_counts;
        for (const auto& w : words) local_counts[w]++;

        for (const auto& p : local_counts) {
            temp_postings[p.first].emplace_back(id, p.second);
        }
    }
    total_docs_count = id;

    for (auto& entry : temp_postings) {
        const string& word = entry.first;
        auto& postings = entry.second;

        sort(postings.begin(), postings.end());

        vector<uint8_t> compressed;
        int last_id = 0;
        for (const auto& p : postings) {
            int delta = p.first - last_id;
            vbyte_encode(delta, compressed);
            vbyte_encode(p.second, compressed);
            last_id = p.first;
        }

        inv_index.insert(word, move(compressed));
    }

    cout << "Indexing completed. Documents: " << total_docs_count 
         << ", Terms: " << inv_index.size() << endl;
}

void save_index() {
    ofstream inv_out(INV_INDEX_PATH, ios::binary);
    if (!inv_out.is_open()) {
        cerr << "Не могу сохранить inverted index!" << endl;
        return;
    }
    for (auto& entry : inv_index) {
        uint32_t term_len = entry.first.size();
        inv_out.write(reinterpret_cast<char*>(&term_len), sizeof(term_len));
        inv_out.write(entry.first.c_str(), term_len);

        uint32_t data_size = entry.second.size();
        inv_out.write(reinterpret_cast<char*>(&data_size), sizeof(data_size));
        inv_out.write(reinterpret_cast<char*>(entry.second.data()), data_size);
    }
    inv_out.close();

    ofstream len_out(DOC_LENGTHS_PATH);
    for (auto& p : doc_lengths) {
        len_out << p.first << " " << p.second << "\n";
    }

    ofstream names_out(DOC_NAMES_PATH);
    for (auto& p : doc_names) {
        names_out << p.first << " " << p.second << "\n";
    }

    cout << "Индекс сохранён." << endl;
}

bool load_index() {
    ifstream inv_in(INV_INDEX_PATH, ios::binary);
    if (!inv_in.is_open()) {
        cout << "Файл индекса не найден: " << INV_INDEX_PATH << endl;
        return false;
    }

    inv_index = CustomHashMap<string, vector<uint8_t>>();  
    while (inv_in.peek() != EOF) {
        uint32_t term_len;
        if (!inv_in.read(reinterpret_cast<char*>(&term_len), sizeof(term_len))) break;
        string term(term_len, '\0');
        inv_in.read(&term[0], term_len);

        uint32_t data_size;
        inv_in.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));
        vector<uint8_t> data(data_size);
        inv_in.read(reinterpret_cast<char*>(data.data()), data_size);

        inv_index.insert(term, move(data));
    }

    ifstream len_in(DOC_LENGTHS_PATH);
    int id, len;
    while (len_in >> id >> len) {
        doc_lengths.insert(id, len);
    }

    ifstream names_in(DOC_NAMES_PATH);
    string name;
    while (names_in >> id >> ws && getline(names_in, name)) {
        if (!name.empty()) doc_names.insert(id, name);
    }

    total_docs_count = doc_lengths.size();
    cout << "Индекс загружен: " << inv_index.size() << " терминов, " << total_docs_count << " документов." << endl;
    return true;
}

int main() {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    if (!load_dictionary()) {
        cerr << "Словарь лемм не найден!" << endl;
        return 1;
    }

    load_doc_urls(); 

    while (true) {
        cout << "\n=== ПОИСКОВЫЙ ДВИЖОК ===\n";
        cout << "1. Перестроить индекс\n";
        cout << "2. Boolean поиск\n";
        cout << "3. TF-IDF поиск\n";
        cout << "4. Выход\n";
        cout << "Выбор: ";

        int choice;
        cin >> choice;
        cin.ignore();

        if (choice == 4) break;

        if (choice == 1) {
            index_data();
            save_index();
            check_zipf();
        } else if (choice == 2 || choice == 3) {
            if (inv_index.size() == 0) {
                if (load_index()) {
                } else {
                    cout << "Нет готового индекс.\n";
                    continue;
                }
            }
            if (choice == 2) boolean_search();
            else tf_idf_search();
        }
    }

    return 0;

}
