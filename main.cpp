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
#include <limits>  // Добавлено для numeric_limits

using namespace std;
namespace fs = std::filesystem;

const string DIR_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\dataset_txt";
const string DICT_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\resources\\lemmas.txt";
const string INV_INDEX_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\inverted_index.bin";
const string DOC_NAMES_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\doc_names.txt";
const string DOC_LENGTHS_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\doc_lengths.txt";

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
        if (check_sym(c)) cur.push_back(c);
        else if (!cur.empty()) {
            string low = str_lower(to_utf8(cur));
            string* lem_ptr = lemmas.find(low);
            res.push_back(lem_ptr ? *lem_ptr : low);
            cur.clear();
        }
    }
    if (!cur.empty()) {
        string low = str_lower(to_utf8(cur));
        string* lem_ptr = lemmas.find(low);
        res.push_back(lem_ptr ? *lem_ptr : low);
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

            if (!inv_index.count(word)) {
                if (prefix == '+') {
                    has_plus = true;
                }
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
            cout << "Results: No documents match.\n";
            continue;
        }

        vector<int> candidates;

        if (has_plus) {
            candidates.assign(must_have.begin(), must_have.end());
        } else if (has_regular) {
            candidates.assign(should_have.begin(), should_have.end());
        } else {
            for (int i = 1; i <= total_docs_count; ++i) {
                candidates.push_back(i);
            }
        }

        vector<int> final_results;
        for (int doc_id : candidates) {
            if (must_not.count(doc_id)) continue;
            final_results.push_back(doc_id);
        }

        cout << "Results: ";
        if (final_results.empty()) {
            cout << "No documents match.";
        } else {
            for (int doc_id : final_results) {
                if (doc_names.count(doc_id)) {
                    cout << doc_names[doc_id] << " ";
                }
            }
        }
        cout << endl;
    }
}

void tf_idf_search() {
    cout << "\n=== TF-IDF SEARCH ===\n";
    string query;

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
            results.emplace_back(p.second, p.first);
        }
        sort(results.begin(), results.end(), greater<pair<double, int>>());

        size_t top_k = 10;
        cout << "Top " << min(results.size(), top_k) << " results:\n";
        for (size_t i = 0; i < min(results.size(), top_k); ++i) {
            cout << "File: " << doc_names[results[i].second]
                 << " | Score: " << fixed << setprecision(6) << results[i].first << endl;
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

int main() {
    SetConsoleCP(CP_UTF8);     
    SetConsoleOutputCP(CP_UTF8);

    if (!load_dictionary()) {
        cerr << "Dictionary not found!\n";
        return 1;
    }

    index_data(); 

    check_zipf();
    
    int choice;
    cout << "\nChoose mode: 1-Boolean, 2-TF-IDF: ";
    cin >> choice;
    cin.ignore(9999999, '\n'); 

    if (choice == 1) boolean_search();
    else tf_idf_search();

    return 0;
}