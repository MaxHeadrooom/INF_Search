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
#include <stdexcept>
#include <memory> 
#include <limits> 

using namespace std;
namespace fs = std::filesystem;

const string DIR_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\dataset_txt"; 
const string DICT_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\resources\\lemmas.txt"; 
const string INV_INDEX_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\inverted_index_vbyte.txt"; 
const string DOC_NAMES_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\doc_names.txt";
const string DOC_LENGTHS_PATH = "C:\\Users\\fedor\\PycharmProjects\\gg\\doc_lengths.txt"; 

const size_t HASH_SIZE = 10000; 

struct Hasher {
    size_t operator()(const string& key) const {
        size_t h = 0;
        for (char c : key) h = (h * 31 + c);
        return h % HASH_SIZE; 
    }
    size_t operator()(int key) const {
        return (size_t)abs(key) % HASH_SIZE; 
    }
};

template <typename K, typename V>
struct CustomHashMap {

    vector<pair<K, V>> buckets[HASH_SIZE]; 
    Hasher hasher;

    void insert(const K& key, const V& value) {
        size_t index = hasher(key);
        for (auto& pair : buckets[index]) {
            if (pair.first == key) {
                pair.second = value; 
                return;
            }
        }
        buckets[index].push_back({key, value}); 
    }

    V* find(const K& key) {
        size_t index = hasher(key);
        for (auto& pair : buckets[index]) {
            if (pair.first == key) {
                return &pair.second;
            }
        }
        return nullptr;
    }

    const V* find(const K& key) const {
        size_t index = hasher(key);
        for (const auto& pair : buckets[index]) {
            if (pair.first == key) {
                return &pair.second;
            }
        }
        return nullptr;
    }

    bool count(const K& key) const {
        return find(key) != nullptr;
    }
    
    V& at(const K& key) {
        V* value_ptr = find(key);
        if (value_ptr == nullptr) {
            cerr << "ERROR: Key not found in CustomHashMap." << endl;
            exit(1); 
        }
        return *value_ptr;
    }
    
    V& operator[](const K& key) {
        V* value_ptr = find(key);
        if (value_ptr == nullptr) {
            insert(key, V{}); 
            return at(key); 
        }
        return *value_ptr;
    }

    size_t size() const {
        size_t total = 0;
        for (size_t i = 0; i < HASH_SIZE; ++i) {
            total += buckets[i].size();
        }
        return total;
    }
    
    struct Iterator {
        vector<pair<K, V>>* buckets_base_ptr;
        vector<pair<K, V>>* current_bucket_ptr;
        typename vector<pair<K, V>>::iterator inner_it;
        size_t array_size;
        
        Iterator(vector<pair<K, V>>* buckets_array, size_t start_index, size_t size) : 
            buckets_base_ptr(buckets_array),
            current_bucket_ptr(buckets_array + start_index), 
            array_size(size) 
        {
            if (start_index < array_size) {
                 inner_it = current_bucket_ptr->begin();
                 skip_empty_buckets();
            } else {
                 current_bucket_ptr = buckets_array + array_size; 
            }
        }
        
        void skip_empty_buckets() {
            while (current_bucket_ptr < (buckets_base_ptr + array_size) && inner_it == current_bucket_ptr->end()) {
                current_bucket_ptr++;
                if (current_bucket_ptr < (buckets_base_ptr + array_size)) {
                    inner_it = current_bucket_ptr->begin();
                }
            }
        }

        pair<K, V>& operator*() const { return *inner_it; }
        pair<K, V>* operator->() const { return &(*inner_it); }

        Iterator& operator++() {
            inner_it++;
            if (inner_it == current_bucket_ptr->end()) {
                 skip_empty_buckets();
            }
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return current_bucket_ptr != other.current_bucket_ptr;
        }
    };
    
    Iterator begin() {
        return Iterator(buckets, 0, HASH_SIZE);
    }
    
    Iterator end() {
        return Iterator(buckets, HASH_SIZE, HASH_SIZE);
    }
};

CustomHashMap<string, string>* lemmas = nullptr; 
CustomHashMap<string, vector<uint8_t>>* inv_index = nullptr; 
CustomHashMap<int, string>* doc_names = nullptr; 
CustomHashMap<int, int>* doc_lengths = nullptr; 

long long total_docs_count = 0;

bool is_cont(unsigned char b) { return (b & 0xC0) == 0x80; }
vector<uint32_t> to_codes(const string &s) {
    vector<uint32_t> res; size_t i = 0;
    const unsigned char *p = reinterpret_cast<const unsigned char*>(s.data());
    while (i < s.size()) {
        unsigned char c = p[i]; uint32_t val = 0; size_t n = 0;
        if (c < 0x80) { val = c; n = 0; } else if ((c & 0xE0) == 0xC0) { val = c & 0x1F; n = 1; } else if ((c & 0xF0) == 0xE0) { val = c & 0x0F; n = 2; } else if ((c & 0xF8) == 0xF0) { val = c & 0x07; n = 3; } else { ++i; continue; }
        if (i + n >= s.size()) break; bool fail = false;
        for (size_t k = 1; k <= n; ++k) { if (!is_cont(p[i + k])) { fail = true; break; } val = (val << 6) | (p[i + k] & 0x3F); }
        if (fail) { ++i; continue; } res.push_back(val); i += 1 + n;
    }
    return res;
}
string to_utf8(const vector<uint32_t> &v) {
    string res; for (uint32_t c : v) {
        if (c <= 0x7F) res.push_back((char)c);
        else if (c <= 0x7FF) { res.push_back((char)(0xC0 | ((c >> 6) & 0x1F))); res.push_back((char)(0x80 | (c & 0x3F))); }
        else if (c <= 0xFFFF) { res.push_back((char)(0xE0 | ((c >> 12) & 0x0F))); res.push_back((char)(0x80 | ((c >> 6) & 0x3F))); res.push_back((char)(0x80 | (c & 0x3F))); }
        else { res.push_back((char)(0xF0 | ((c >> 18) & 0x07))); res.push_back((char)(0xC0 | ((c >> 12) & 0x3F))); res.push_back((char)(0x80 | ((c >> 6) & 0x3F))); res.push_back((char)(0x80 | (c & 0x3F))); }
    } return res;
}
uint32_t char_lower(uint32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32; if (c >= 0x0410 && c <= 0x042F) return c + 0x20; if (c == 0x0401) return 0x0451; return c;
}
bool check_sym(uint32_t c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return true; if (c >= '0' && c <= '9') return true; if (c >= 0x0400 && c <= 0x04FF) return true; return false;
}
string str_lower(const string &s) {
    vector<uint32_t> v = to_codes(s); for (auto &c : v) c = char_lower(c); return to_utf8(v);
}

vector<string> parse(const string &text) {
    vector<string> res; 
    vector<uint32_t> codes = to_codes(text); 
    vector<uint32_t> cur;

    for (size_t i = 0; i < codes.size(); ++i) {
        if (check_sym(codes[i])) {
            cur.push_back(codes[i]);
        }
        else { 
            if (!cur.empty()) { 
                string raw = to_utf8(cur); 
                string low = str_lower(raw); 
                
                string* lemma_ptr = lemmas->find(low);
                
                if (lemma_ptr != nullptr) {
                    res.push_back(*lemma_ptr); 
                } else {
                    res.push_back(low);
                }
                cur.clear(); 
            } 
        }
    }
    if (!cur.empty()) { 
        string raw = to_utf8(cur); 
        string low = str_lower(raw); 
        
        string* lemma_ptr = lemmas->find(low);
        
        if (lemma_ptr != nullptr) {
            res.push_back(*lemma_ptr);
        } else {
            res.push_back(low);
        }
    }
    return res;
}

bool load_dict() {
    cout << "Loading custom dictionary (Hash Table)..." << endl; 
    ifstream f(DICT_PATH, ios::binary);
    if (!f.is_open()) {
        cerr << "ERROR: Failed to open dictionary at " << DICT_PATH << endl;
        return false;
    }
    string line;
    while (getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back(); 
        if (line.empty()) continue;
        
        stringstream ss(line); 
        string key, val; 
        
        if (ss >> key) {
            if (ss >> val) {
                lemmas->insert(str_lower(key), str_lower(val));
            }
        }
    }
    cout << "Dictionary loaded. Total entries: " << lemmas->size() << endl;
    return true;
}

void vbyte_encode(int n, vector<uint8_t>& result) {
    while (true) {
        if (n < 128) {
            result.push_back(n | 0x80); 
            break;
        } else {
            result.push_back(n & 0x7F); 
            n >>= 7;
        }
    }
}

int vbyte_decode(const vector<uint8_t>& data, size_t& offset) {
    int n = 0;
    int shift = 0;
    while (true) {
        if (offset >= data.size()) {
            return 0; 
        }
        uint8_t byte = data[offset++];
        n |= (byte & 0x7F) << shift;
        if (byte & 0x80) break; 
        shift += 7;
    }
    return n;
}

struct SearchResult {
    int doc_id;
    double score;
};

vector<pair<int, int>> decompress_posting_list(const vector<uint8_t>& compressed_data) {
    vector<pair<int, int>> postings;
    size_t offset = 0;
    int last_doc_id = 0;

    while (offset < compressed_data.size()) {
        int delta_id = vbyte_decode(compressed_data, offset);
        if (offset >= compressed_data.size() && delta_id != 0) break;
        
        int count = vbyte_decode(compressed_data, offset);

        int doc_id = last_doc_id + delta_id;
        postings.push_back({doc_id, count});
        last_doc_id = doc_id;
    }
    return postings;
}

void save_doc_data() {
    ofstream out_names(DOC_NAMES_PATH, ios::binary);
    for (const auto& pair : *doc_names) out_names << pair.first << "\t" << pair.second << "\n";
    out_names.close();

    ofstream out_lens(DOC_LENGTHS_PATH, ios::binary);
    for (const auto& pair : *doc_lengths) out_lens << pair.first << "\t" << pair.second << "\n";
    out_lens.close();
}

void save_inverted_index(long long total_docs) {
    ofstream out(INV_INDEX_PATH, ios::binary);
    out << total_docs << "\n";
    
    long long total_bytes = 0;

    for (const auto& pair : *inv_index) {
        const string& lemma = pair.first;
        const vector<uint8_t>& compressed_data = pair.second;
        
        out << lemma << " ";
        out << compressed_data.size() << " ";
        out.write(reinterpret_cast<const char*>(compressed_data.data()), compressed_data.size());
        out << "\n";
        
        total_bytes += compressed_data.size();
    }
    out.close();
    cout << "Total compressed index size (bytes): " << total_bytes << endl;
}

bool load_data() {
    cout << "Loading existing index and data..." << endl;
    
    ifstream f_names(DOC_NAMES_PATH, ios::binary);
    if (!f_names.is_open()) return false;
    string line;
    while (getline(f_names, line)) {
        stringstream ss(line); int id; string name;
        if (ss >> id) { 
            getline(ss >> std::ws, name); 
            doc_names->insert(id, name); 
        }
    }
    
    ifstream f_lens(DOC_LENGTHS_PATH, ios::binary);
    if (!f_lens.is_open()) return false;
    while (getline(f_lens, line)) {
        stringstream ss(line); int id, len;
        if (ss >> id >> len) doc_lengths->insert(id, len);
    }

    ifstream f_inv(INV_INDEX_PATH, ios::binary);
    if (!f_inv.is_open()) return false;
 
    if (!getline(f_inv, line)) return false;
    try {
        total_docs_count = stoll(line);
    } catch (const std::exception& e) {
        cerr << "Error parsing total_docs_count: " << e.what() << endl;
        return false;
    }

    while (true) { 
        string lemma;
        size_t compressed_size;
        
        if (!(f_inv >> lemma)) break;
        if (!(f_inv >> compressed_size)) break; 

        char separator;
        f_inv.get(separator); 

        vector<uint8_t> compressed_data(compressed_size);
        if (!f_inv.read(reinterpret_cast<char*>(compressed_data.data()), compressed_size)) {
             cerr << "Failed to read compressed data for lemma: " << lemma << endl;
             break;
        }
        
        f_inv.get(separator);
        
        inv_index->insert(lemma, compressed_data);
    }
    
    if (total_docs_count == 0 || inv_index->size() == 0) {
        cout << "Index file loaded but is empty or corrupted." << endl;
        return false;
    }
    cout << "Index loaded. Documents: " << total_docs_count << ", Unique lemmas: " << inv_index->size() << endl;
    return true;
}

void search_mode() {
    cout << "\n=== TF-IDF SEARCH MODE (V-Byte Compressed) ===" << endl;
    
    cin.clear();

    
    string query;
    while (true) {
        cout << "Query: " << flush; 

        if (!getline(cin, query)) {
            break; 
        }

        if (query == "exit") {
            cout << "Exiting search mode..." << endl;
            break;
        } 
        if (query.empty()) {
            continue;
        }
        
        vector<string> q_words = parse(query); 
        if (q_words.empty()) {
            cout << "Query resulted in no searchable words." << endl;
            continue;
        }
        
        CustomHashMap<int, double> doc_scores;
        CustomHashMap<int, int> doc_match_count;
        
        for (const string& w : q_words) {
             if (inv_index->count(w)) {
                const vector<uint8_t>& compressed_data = inv_index->at(w);
                vector<pair<int, int>> postings = decompress_posting_list(compressed_data);

                double idf = log((double)total_docs_count / postings.size());
                
                for (const auto& entry : postings) {
                    int doc_id = entry.first;
                    int count = entry.second; 

                    if (!doc_lengths->count(doc_id)) continue; 
                    
                    double tf = (double)count / doc_lengths->at(doc_id);
                    
                    doc_scores[doc_id] += tf * idf;
                    doc_match_count[doc_id]++;
                }
            }
        }

        vector<SearchResult> results;
        int required_matches = q_words.size();

        for (const auto& pair : doc_scores) {
            int doc_id = pair.first;
            double score = pair.second;
            
            if (doc_match_count.count(doc_id) && doc_match_count.at(doc_id) == required_matches) {
                results.push_back({doc_id, score});
            }
        }

        sort(results.begin(), results.end(), [](const SearchResult& a, const SearchResult& b) {
            return a.score > b.score;
        });

        if (results.empty()) {
            cout << "Nothing found (no documents contain all query words)." << endl;
        }
        else {
            cout << "Found " << results.size() << " documents (sorted by relevance):" << endl;
            cout << fixed << setprecision(6); 
            for (size_t i = 0; i < min(results.size(), (size_t)10); ++i) {
                cout << "[" << i+1 << "] Score: " << results[i].score << " | File: " << doc_names->at(results[i].doc_id) << endl;
            }
        }
        cout.flush(); 
    }
}

void start_process() {
    lemmas = new CustomHashMap<string, string>(); 
    inv_index = new CustomHashMap<string, vector<uint8_t>>(); 
    doc_names = new CustomHashMap<int, string>(); 
    doc_lengths = new CustomHashMap<int, int>(); 

    cout << "--- Starting Inverted Indexer ---" << endl; 
    setlocale(LC_ALL, "Russian"); 

    if (!load_dict()) {
        cerr << "FATAL: Dictionary loading failed. Exiting." << endl;
        return; 
    }

    if (load_data()) {
        cout << "Index and data loaded successfully. Starting search." << endl;
        search_mode();
        return;
    }

    cout << "Index not found or corrupted. Re-indexing with V-Byte compression..." << endl;
    
    CustomHashMap<string, vector<pair<int, int>>>* temp_inv_index_ptr = 
        new CustomHashMap<string, vector<pair<int, int>>>();
    
    int doc_id = 0;
    
    try {
        if (!fs::exists(DIR_PATH) || !fs::is_directory(DIR_PATH)) {
            cerr << "FILESYSTEM FATAL: Dataset directory not found or is not a directory: " << DIR_PATH << endl;
            delete temp_inv_index_ptr; 
            return;
        }

        for (const auto &entry : fs::directory_iterator(DIR_PATH)) {
            if (entry.path().extension() == ".txt") {
                doc_id++;
                doc_names->insert(doc_id, entry.path().filename().string());
                
                ifstream f(entry.path(), ios::binary);
                if (!f.is_open()) {
                    cerr << "Error opening file: " << entry.path().string() << endl;
                    continue;
                }
                string s((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
                
                vector<string> v = parse(s);
                doc_lengths->insert(doc_id, v.size()); 

                CustomHashMap<string, int> local_counts;
                for (const auto& w : v) {
                    local_counts[w]++; 
                }

                for (const auto& pair : local_counts) {
                    (*temp_inv_index_ptr)[pair.first].push_back({doc_id, pair.second});
                }
                
                if (doc_id % 1000 == 0) cout << "Indexed: " << doc_id << " documents." << endl;
            }
        }
    } catch (const fs::filesystem_error& e) {
        cerr << "Filesystem Error: " << e.what() << endl;
        cerr << "Check if the path " << DIR_PATH << " is correct." << endl;
        delete temp_inv_index_ptr;
        return;
    }

    total_docs_count = doc_id;
    save_doc_data();
    
    cout << "Starting V-Byte compression..." << endl;
    
    for (const auto& pair : *temp_inv_index_ptr) {
        const string& lemma = pair.first;
        const auto& postings = pair.second;
        
        vector<uint8_t> compressed_data;
        int last_doc_id = 0;
        
        for (const auto& entry : postings) {
            int current_doc_id = entry.first;
            int count = entry.second;
            
            int delta = current_doc_id - last_doc_id; 
            
            vbyte_encode(delta, compressed_data); 
            vbyte_encode(count, compressed_data); 
            
            last_doc_id = current_doc_id;
        }
        inv_index->insert(lemma, compressed_data);
    }

    delete temp_inv_index_ptr;

    save_inverted_index(doc_id);
    
    cout << "Re-indexing complete. Starting search." << endl;
    search_mode();
}

int main() {
    start_process();

    delete lemmas;
    delete inv_index;
    delete doc_names;
    delete doc_lengths;

    return 0;

}
