#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <algorithm>

using namespace std;

const int MAX_KEY_LEN = 65;
const int ORDER = 50;  // B+ tree order - must support ORDER+1 elements during split

struct KVPair {
    char key[MAX_KEY_LEN];
    int value;

    KVPair() { memset(key, 0, MAX_KEY_LEN); value = 0; }
    KVPair(const char* k, int v) {
        strncpy(key, k, MAX_KEY_LEN - 1);
        key[MAX_KEY_LEN - 1] = '\0';
        value = v;
    }

    bool operator<(const KVPair& other) const {
        int cmp = strcmp(key, other.key);
        if (cmp != 0) return cmp < 0;
        return value < other.value;
    }

    bool operator==(const KVPair& other) const {
        return strcmp(key, other.key) == 0 && value == other.value;
    }
};

struct BPNode {
    bool is_leaf;
    int num_keys;
    KVPair keys[ORDER + 1];  // +1 to allow temporary overflow during split
    int children[ORDER + 2];  // +2 to allow temporary overflow during split
    int next_leaf;

    BPNode() : is_leaf(true), num_keys(0), next_leaf(-1) {
        memset(children, -1, sizeof(children));
    }
};

class BPlusTree {
private:
    string filename;
    int root_pos;
    int next_free_pos;

    void read_node(int pos, BPNode& node) {
        fstream fs(filename, ios::in | ios::binary);
        fs.seekg(sizeof(int) * 2 + pos * sizeof(BPNode));
        fs.read((char*)&node, sizeof(BPNode));
        fs.close();
    }

    void write_node(const BPNode& node, int pos) {
        fstream fs(filename, ios::in | ios::out | ios::binary);
        fs.seekp(sizeof(int) * 2 + pos * sizeof(BPNode));
        fs.write((const char*)&node, sizeof(BPNode));
        fs.close();
    }

    void save_metadata() {
        fstream fs(filename, ios::in | ios::out | ios::binary);
        fs.seekp(0);
        fs.write((const char*)&root_pos, sizeof(int));
        fs.write((const char*)&next_free_pos, sizeof(int));
        fs.close();
    }

    void load_metadata() {
        ifstream ifs(filename, ios::binary);
        ifs.read((char*)&root_pos, sizeof(int));
        ifs.read((char*)&next_free_pos, sizeof(int));
        ifs.close();
    }

    int alloc_node() {
        return next_free_pos++;
    }

    int lower_bound_pos(const KVPair* arr, int n, const KVPair& kv) {
        int left = 0, right = n;
        while (left < right) {
            int mid = (left + right) / 2;
            if (arr[mid] < kv) left = mid + 1;
            else right = mid;
        }
        return left;
    }

    void split_leaf(BPNode& node, BPNode& new_node, KVPair& up_key) {
        int mid = (ORDER + 1) / 2;
        new_node.is_leaf = true;
        new_node.num_keys = node.num_keys - mid;
        memcpy(new_node.keys, node.keys + mid, new_node.num_keys * sizeof(KVPair));
        node.num_keys = mid;
        new_node.next_leaf = node.next_leaf;
        up_key = new_node.keys[0];
    }

    void split_internal(BPNode& node, BPNode& new_node, KVPair& up_key) {
        int mid = ORDER / 2;
        new_node.is_leaf = false;
        new_node.num_keys = node.num_keys - mid - 1;
        memcpy(new_node.keys, node.keys + mid + 1, new_node.num_keys * sizeof(KVPair));
        memcpy(new_node.children, node.children + mid + 1, (new_node.num_keys + 1) * sizeof(int));
        up_key = node.keys[mid];
        node.num_keys = mid;
    }

    bool insert_into_node(int pos, const KVPair& kv, int& split_pos, KVPair& up_key) {
        BPNode node;
        read_node(pos, node);

        if (node.is_leaf) {
            // Check for duplicate
            for (int i = 0; i < node.num_keys; i++) {
                if (node.keys[i] == kv) return false;
            }

            int insert_pos = lower_bound_pos(node.keys, node.num_keys, kv);

            if (node.num_keys < ORDER) {
                memmove(node.keys + insert_pos + 1, node.keys + insert_pos,
                        (node.num_keys - insert_pos) * sizeof(KVPair));
                node.keys[insert_pos] = kv;
                node.num_keys++;
                write_node(node, pos);
                return false;
            } else {
                // Split leaf
                KVPair temp[ORDER + 1];
                int j = 0;
                for (int i = 0; i < node.num_keys; i++) {
                    if (i == insert_pos) temp[j++] = kv;
                    temp[j++] = node.keys[i];
                }
                if (insert_pos == node.num_keys) temp[j++] = kv;

                memcpy(node.keys, temp, (ORDER + 1) * sizeof(KVPair));
                node.num_keys = ORDER + 1;

                BPNode new_node;
                split_leaf(node, new_node, up_key);

                split_pos = alloc_node();
                node.next_leaf = split_pos;
                write_node(node, pos);
                write_node(new_node, split_pos);
                save_metadata();
                return true;
            }
        } else {
            // Internal node: find correct child
            int child_idx = 0;
            for (int i = 0; i < node.num_keys; i++) {
                if (strcmp(kv.key, node.keys[i].key) >= 0) {
                    child_idx = i + 1;
                }
            }

            int child_split_pos;
            KVPair child_up_key;
            bool child_split = insert_into_node(node.children[child_idx], kv, child_split_pos, child_up_key);

            if (!child_split) return false;

            // Insert the split result
            if (node.num_keys < ORDER) {
                int insert_pos = lower_bound_pos(node.keys, node.num_keys, child_up_key);
                memmove(node.keys + insert_pos + 1, node.keys + insert_pos,
                        (node.num_keys - insert_pos) * sizeof(KVPair));
                memmove(node.children + insert_pos + 2, node.children + insert_pos + 1,
                        (node.num_keys - insert_pos) * sizeof(int));
                node.keys[insert_pos] = child_up_key;
                node.children[insert_pos + 1] = child_split_pos;
                node.num_keys++;
                write_node(node, pos);
                return false;
            } else {
                // Split internal node
                KVPair temp_keys[ORDER + 1];
                int temp_children[ORDER + 2];
                int insert_pos = lower_bound_pos(node.keys, node.num_keys, child_up_key);

                int j = 0, k = 0;
                for (int i = 0; i <= node.num_keys; i++) {
                    if (i == insert_pos) {
                        temp_keys[j++] = child_up_key;
                        temp_children[k++] = node.children[i];
                        temp_children[k++] = child_split_pos;
                    } else {
                        if (i < node.num_keys) temp_keys[j++] = node.keys[i];
                        temp_children[k++] = node.children[i];
                    }
                }

                memcpy(node.keys, temp_keys, (ORDER + 1) * sizeof(KVPair));
                memcpy(node.children, temp_children, (ORDER + 2) * sizeof(int));
                node.num_keys = ORDER + 1;

                BPNode new_node;
                split_internal(node, new_node, up_key);

                split_pos = alloc_node();
                write_node(node, pos);
                write_node(new_node, split_pos);
                save_metadata();
                return true;
            }
        }
    }

    int find_leaf(const char* key) {
        if (root_pos < 0) return -1;
        int pos = root_pos;
        BPNode node;

        while (true) {
            read_node(pos, node);
            if (node.is_leaf) return pos;

            // Find leftmost child that could contain the key
            int child_idx = 0;
            for (int i = 0; i < node.num_keys; i++) {
                if (strcmp(key, node.keys[i].key) > 0) {
                    child_idx = i + 1;
                } else {
                    break;
                }
            }
            pos = node.children[child_idx];
        }
    }

public:
    BPlusTree(const string& fname) : filename(fname), root_pos(0), next_free_pos(1) {
        ifstream test(filename);
        if (test.good()) {
            test.close();
            load_metadata();
        } else {
            ofstream create(filename, ios::binary);
            create.close();
            BPNode root;
            save_metadata();
            write_node(root, root_pos);
        }
    }

    void insert(const char* key, int value) {
        KVPair kv(key, value);
        int split_pos;
        KVPair up_key;
        bool split = insert_into_node(root_pos, kv, split_pos, up_key);

        if (split) {
            BPNode new_root;
            new_root.is_leaf = false;
            new_root.num_keys = 1;
            new_root.keys[0] = up_key;
            new_root.children[0] = root_pos;
            new_root.children[1] = split_pos;
            root_pos = alloc_node();
            write_node(new_root, root_pos);
            save_metadata();
        }
    }

    vector<int> find(const char* key) {
        vector<int> result;
        int leaf_pos = find_leaf(key);
        if (leaf_pos < 0) return result;

        BPNode node;
        while (leaf_pos >= 0) {
            read_node(leaf_pos, node);
            bool found_any = false;
            for (int i = 0; i < node.num_keys; i++) {
                int cmp = strcmp(node.keys[i].key, key);
                if (cmp == 0) {
                    result.push_back(node.keys[i].value);
                    found_any = true;
                } else if (cmp > 0) {
                    if (!found_any) return result;  // Wrong leaf
                    leaf_pos = -1;
                    break;
                }
            }
            if (leaf_pos >= 0) leaf_pos = node.next_leaf;
        }

        sort(result.begin(), result.end());
        return result;
    }

    void remove(const char* key, int value) {
        int leaf_pos = find_leaf(key);
        if (leaf_pos < 0) return;

        BPNode node;
        read_node(leaf_pos, node);

        KVPair target(key, value);
        for (int i = 0; i < node.num_keys; i++) {
            if (node.keys[i] == target) {
                memmove(node.keys + i, node.keys + i + 1,
                        (node.num_keys - i - 1) * sizeof(KVPair));
                node.num_keys--;
                write_node(node, leaf_pos);
                return;
            }
        }
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    BPlusTree tree("bptree.dat");

    int n;
    cin >> n;

    for (int i = 0; i < n; i++) {
        string cmd;
        cin >> cmd;

        if (cmd == "insert") {
            string index;
            int value;
            cin >> index >> value;
            tree.insert(index.c_str(), value);
        } else if (cmd == "delete") {
            string index;
            int value;
            cin >> index >> value;
            tree.remove(index.c_str(), value);
        } else if (cmd == "find") {
            string index;
            cin >> index;
            vector<int> results = tree.find(index.c_str());
            if (results.empty()) {
                cout << "null\n";
            } else {
                for (size_t j = 0; j < results.size(); j++) {
                    if (j > 0) cout << " ";
                    cout << results[j];
                }
                cout << "\n";
            }
        }
    }

    return 0;
}
