#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <algorithm>
#include <set>

using namespace std;

const int MAX_KEY_LEN = 65;
const int ORDER = 100; // B+ tree order

// Key-Value pair
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

// B+ Tree Node
struct BPNode {
    bool is_leaf;
    int num_keys;
    KVPair keys[ORDER];
    int children[ORDER + 1]; // file positions for internal nodes
    int next_leaf; // for leaf nodes, linking to next leaf

    BPNode() : is_leaf(true), num_keys(0), next_leaf(-1) {
        memset(children, -1, sizeof(children));
    }
};

class BPlusTree {
private:
    string data_file;
    string index_file;
    int root_pos;
    int next_free_pos;

    // Read node from file
    void read_node(int pos, BPNode& node) {
        if (pos < 0) return;
        fstream fs(data_file, ios::in | ios::binary);
        if (!fs.is_open()) return;
        fs.seekg(pos * sizeof(BPNode));
        fs.read((char*)&node, sizeof(BPNode));
        fs.close();
    }

    // Write node to file
    int write_node(const BPNode& node, int pos = -1) {
        fstream fs(data_file, ios::in | ios::out | ios::binary);
        if (!fs.is_open()) {
            fs.open(data_file, ios::out | ios::binary);
            fs.close();
            fs.open(data_file, ios::in | ios::out | ios::binary);
        }

        if (pos < 0) {
            pos = next_free_pos++;
        }

        fs.seekp(pos * sizeof(BPNode));
        fs.write((const char*)&node, sizeof(BPNode));
        fs.close();

        return pos;
    }

    // Save metadata
    void save_metadata() {
        ofstream ofs(index_file, ios::binary);
        ofs.write((const char*)&root_pos, sizeof(int));
        ofs.write((const char*)&next_free_pos, sizeof(int));
        ofs.close();
    }

    // Load metadata
    void load_metadata() {
        ifstream ifs(index_file, ios::binary);
        if (ifs.is_open()) {
            ifs.read((char*)&root_pos, sizeof(int));
            ifs.read((char*)&next_free_pos, sizeof(int));
            ifs.close();
        }
    }

    // Find position to insert in sorted array
    int find_insert_pos(const KVPair* arr, int n, const KVPair& kv) {
        int left = 0, right = n;
        while (left < right) {
            int mid = (left + right) / 2;
            if (arr[mid] < kv) {
                left = mid + 1;
            } else {
                right = mid;
            }
        }
        return left;
    }

    // Split leaf node
    void split_leaf(BPNode& node, BPNode& new_node, KVPair& up_key) {
        int mid = (ORDER + 1) / 2;

        new_node.is_leaf = true;
        new_node.num_keys = node.num_keys - mid;
        for (int i = 0; i < new_node.num_keys; i++) {
            new_node.keys[i] = node.keys[mid + i];
        }

        node.num_keys = mid;
        new_node.next_leaf = node.next_leaf;

        up_key = new_node.keys[0];
    }

    // Split internal node
    void split_internal(BPNode& node, BPNode& new_node, KVPair& up_key) {
        int mid = ORDER / 2;

        new_node.is_leaf = false;
        new_node.num_keys = node.num_keys - mid - 1;
        for (int i = 0; i < new_node.num_keys; i++) {
            new_node.keys[i] = node.keys[mid + 1 + i];
            new_node.children[i] = node.children[mid + 1 + i];
        }
        new_node.children[new_node.num_keys] = node.children[node.num_keys];

        up_key = node.keys[mid];
        node.num_keys = mid;
    }

    // Insert into node (returns true if split occurred)
    bool insert_into_node(int pos, const KVPair& kv, int& split_pos, KVPair& up_key) {
        BPNode node;
        read_node(pos, node);

        if (node.is_leaf) {
            // Check if already exists
            for (int i = 0; i < node.num_keys; i++) {
                if (node.keys[i] == kv) {
                    return false; // Already exists
                }
            }

            int insert_pos = find_insert_pos(node.keys, node.num_keys, kv);

            if (node.num_keys < ORDER) {
                // Simple insert
                for (int i = node.num_keys; i > insert_pos; i--) {
                    node.keys[i] = node.keys[i - 1];
                }
                node.keys[insert_pos] = kv;
                node.num_keys++;
                write_node(node, pos);
                return false;
            } else {
                // Need to split
                KVPair temp[ORDER + 1];
                int j = 0;
                for (int i = 0; i < node.num_keys; i++) {
                    if (i == insert_pos) temp[j++] = kv;
                    temp[j++] = node.keys[i];
                }
                if (insert_pos == node.num_keys) temp[j++] = kv;

                for (int i = 0; i < ORDER + 1; i++) {
                    node.keys[i] = temp[i];
                }
                node.num_keys = ORDER + 1;

                BPNode new_node;
                split_leaf(node, new_node, up_key);

                int new_pos = write_node(new_node);
                node.next_leaf = new_pos;
                write_node(node, pos);

                split_pos = new_pos;
                return true;
            }
        } else {
            // Internal node
            int child_idx = 0;
            while (child_idx < node.num_keys && kv.key[0] != '\0') {
                if (strcmp(kv.key, node.keys[child_idx].key) < 0) break;
                child_idx++;
            }

            int child_split_pos;
            KVPair child_up_key;
            bool child_split = insert_into_node(node.children[child_idx], kv, child_split_pos, child_up_key);

            if (!child_split) return false;

            // Need to insert child_up_key into this node
            if (node.num_keys < ORDER) {
                int insert_pos = find_insert_pos(node.keys, node.num_keys, child_up_key);

                for (int i = node.num_keys; i > insert_pos; i--) {
                    node.keys[i] = node.keys[i - 1];
                    node.children[i + 1] = node.children[i];
                }
                node.keys[insert_pos] = child_up_key;
                node.children[insert_pos + 1] = child_split_pos;
                node.num_keys++;
                write_node(node, pos);
                return false;
            } else {
                // Need to split internal node
                KVPair temp_keys[ORDER + 1];
                int temp_children[ORDER + 2];

                int insert_pos = find_insert_pos(node.keys, node.num_keys, child_up_key);
                int j = 0, k = 0;
                for (int i = 0; i < node.num_keys; i++) {
                    if (i == insert_pos) {
                        temp_keys[j++] = child_up_key;
                        temp_children[k++] = node.children[i];
                        temp_children[k++] = child_split_pos;
                    } else {
                        temp_keys[j++] = node.keys[i];
                        temp_children[k++] = node.children[i];
                    }
                }
                if (insert_pos == node.num_keys) {
                    temp_keys[j++] = child_up_key;
                    temp_children[k++] = node.children[node.num_keys];
                    temp_children[k++] = child_split_pos;
                } else {
                    temp_children[k++] = node.children[node.num_keys];
                }

                for (int i = 0; i < ORDER + 1; i++) {
                    node.keys[i] = temp_keys[i];
                    node.children[i] = temp_children[i];
                }
                node.children[ORDER + 1] = temp_children[ORDER + 1];
                node.num_keys = ORDER + 1;

                BPNode new_node;
                split_internal(node, new_node, up_key);

                split_pos = write_node(new_node);
                write_node(node, pos);
                return true;
            }
        }
    }

    // Find leaf node containing key
    int find_leaf(const char* key) {
        if (root_pos < 0) return -1;

        int pos = root_pos;
        BPNode node;

        while (true) {
            read_node(pos, node);
            if (node.is_leaf) return pos;

            int child_idx = 0;
            while (child_idx < node.num_keys) {
                if (strcmp(key, node.keys[child_idx].key) < 0) break;
                child_idx++;
            }
            pos = node.children[child_idx];
        }
    }

public:
    BPlusTree(const string& prefix) : root_pos(-1), next_free_pos(0) {
        data_file = prefix + ".dat";
        index_file = prefix + ".idx";

        // Try to load existing tree
        ifstream test(index_file);
        if (test.good()) {
            test.close();
            load_metadata();
        } else {
            // Create new tree
            BPNode root;
            root_pos = write_node(root);
            save_metadata();
        }
    }

    void insert(const char* key, int value) {
        KVPair kv(key, value);

        if (root_pos < 0) {
            BPNode root;
            root.is_leaf = true;
            root.num_keys = 1;
            root.keys[0] = kv;
            root_pos = write_node(root);
            save_metadata();
            return;
        }

        int split_pos;
        KVPair up_key;
        bool split = insert_into_node(root_pos, kv, split_pos, up_key);

        if (split) {
            // Create new root
            BPNode new_root;
            new_root.is_leaf = false;
            new_root.num_keys = 1;
            new_root.keys[0] = up_key;
            new_root.children[0] = root_pos;
            new_root.children[1] = split_pos;

            root_pos = write_node(new_root);
            save_metadata();
        }
    }

    vector<int> find(const char* key) {
        vector<int> result;

        int leaf_pos = find_leaf(key);
        if (leaf_pos < 0) return result;

        BPNode node;
        read_node(leaf_pos, node);

        // Find in current and subsequent leaves
        while (leaf_pos >= 0) {
            read_node(leaf_pos, node);

            bool found_any = false;
            for (int i = 0; i < node.num_keys; i++) {
                int cmp = strcmp(node.keys[i].key, key);
                if (cmp == 0) {
                    result.push_back(node.keys[i].value);
                    found_any = true;
                } else if (cmp > 0) {
                    break;
                }
            }

            if (!found_any && node.num_keys > 0 && strcmp(node.keys[0].key, key) > 0) {
                break;
            }

            leaf_pos = node.next_leaf;
        }

        sort(result.begin(), result.end());
        return result;
    }

    void remove(const char* key, int value) {
        // Simple implementation: mark as deleted or rebuild
        // For this problem, we'll use a simpler approach
        // Since deletion is complex in B+ tree, we can implement a lazy deletion
        // or rebuild when needed. For now, let's implement actual deletion.

        int leaf_pos = find_leaf(key);
        if (leaf_pos < 0) return;

        BPNode node;
        read_node(leaf_pos, node);

        KVPair target(key, value);
        int del_idx = -1;
        for (int i = 0; i < node.num_keys; i++) {
            if (node.keys[i] == target) {
                del_idx = i;
                break;
            }
        }

        if (del_idx >= 0) {
            for (int i = del_idx; i < node.num_keys - 1; i++) {
                node.keys[i] = node.keys[i + 1];
            }
            node.num_keys--;
            write_node(node, leaf_pos);
        }
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    BPlusTree tree("bptree_db");

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
