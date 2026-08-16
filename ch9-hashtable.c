#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

// ============================================
// PART 1: Hash Table Core
// ============================================

// Entry in the hash table (linked list for chaining)
typedef struct HEntry {
    char *key;
    char *value;
    struct HEntry *next;  // Next entry in chain
} HEntry;

// Hash table structure
typedef struct {
    HEntry **buckets;     // Array of bucket pointers
    int size;             // Number of buckets
    int count;            // Number of entries stored
    int threshold;        // When to resize (size * load_factor)
    float load_factor;    // Usually 0.75
} HashTable;

// Hash function (djb2 - very common and fast)
unsigned long hash_function(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }
    return hash;
}

// Create a new hash table
HashTable* ht_create(int initial_size) {
    HashTable *ht = malloc(sizeof(HashTable));
    if (!ht) return NULL;
    
    ht->size = initial_size > 0 ? initial_size : 16;
    ht->count = 0;
    ht->load_factor = 0.75;
    ht->threshold = ht->size * ht->load_factor;
    ht->buckets = calloc(ht->size, sizeof(HEntry*));
    
    if (!ht->buckets) {
        free(ht);
        return NULL;
    }
    
    return ht;
}

// ============================================
// PART 2: Basic Operations (SET, GET, DEL)
// ============================================

// Set a key-value pair (insert or update)
void ht_set(HashTable *ht, const char *key, const char *value) {
    // 1. Check if we need to resize
    if (ht->count >= ht->threshold) {
        printf("🔄 Resizing hash table (count=%d, size=%d)\n", ht->count, ht->size);
        // We'll implement resize later
    }
    
    // 2. Calculate hash and bucket index
    unsigned long hash = hash_function(key);
    int index = hash % ht->size;
    
    // 3. Check if key already exists
    HEntry *entry = ht->buckets[index];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            // Update existing key
            free(entry->value);
            entry->value = strdup(value);
            printf("✅ Updated: %s = %s\n", key, value);
            return;
        }
        entry = entry->next;
    }
    
    // 4. Insert new key at head of chain
    HEntry *new_entry = malloc(sizeof(HEntry));
    new_entry->key = strdup(key);
    new_entry->value = strdup(value);
    new_entry->next = ht->buckets[index];
    ht->buckets[index] = new_entry;
    ht->count++;
    
    printf("✅ Inserted: %s = %s (bucket: %d)\n", key, value, index);
}

// Get a value by key
char* ht_get(HashTable *ht, const char *key) {
    unsigned long hash = hash_function(key);
    int index = hash % ht->size;
    
    HEntry *entry = ht->buckets[index];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    return NULL;  // Key not found
}

// Delete a key-value pair
bool ht_delete(HashTable *ht, const char *key) {
    unsigned long hash = hash_function(key);
    int index = hash % ht->size;
    
    HEntry *entry = ht->buckets[index];
    HEntry *prev = NULL;
    
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            // Found it - remove from chain
            if (prev) {
                prev->next = entry->next;
            } else {
                ht->buckets[index] = entry->next;
            }
            free(entry->key);
            free(entry->value);
            free(entry);
            ht->count--;
            printf("🗑️ Deleted: %s\n", key);
            return true;
        }
        prev = entry;
        entry = entry->next;
    }
    printf("❌ Key not found: %s\n", key);
    return false;
}

// ============================================
// PART 3: Resizing (Chapter 10)
// ============================================

// Resize the hash table
void ht_resize(HashTable *ht, int new_size) {
    if (new_size < 16) new_size = 16;
    
    printf("🔄 Resizing from %d to %d buckets...\n", ht->size, new_size);
    
    // 1. Save old buckets
    HEntry **old_buckets = ht->buckets;
    int old_size = ht->size;
    
    // 2. Create new buckets
    ht->size = new_size;
    ht->threshold = ht->size * ht->load_factor;
    ht->buckets = calloc(ht->size, sizeof(HEntry*));
    if (!ht->buckets) {
        // Restore on failure
        ht->buckets = old_buckets;
        ht->size = old_size;
        ht->threshold = ht->size * ht->load_factor;
        printf("❌ Resize failed!\n");
        return;
    }
    
    // 3. Rehash all entries
    ht->count = 0;
    for (int i = 0; i < old_size; i++) {
        HEntry *entry = old_buckets[i];
        while (entry) {
            HEntry *next = entry->next;
            
            // Rehash to new bucket
            unsigned long hash = hash_function(entry->key);
            int index = hash % ht->size;
            
            // Insert at head
            entry->next = ht->buckets[index];
            ht->buckets[index] = entry;
            ht->count++;
            
            entry = next;
        }
    }
    
    free(old_buckets);
    printf("✅ Resize complete! Size: %d, Count: %d\n", ht->size, ht->count);
}

// Set with automatic resizing
void ht_set_with_resize(HashTable *ht, const char *key, const char *value) {
    // Resize if needed
    if (ht->count >= ht->threshold) {
        int new_size = ht->size * 2;
        ht_resize(ht, new_size);
    }
    
    // Now insert
    ht_set(ht, key, value);
}

// ============================================
// PART 4: Utility Functions
// ============================================

// Print the hash table (for debugging)
void ht_print(HashTable *ht) {
    printf("\n========== Hash Table ==========\n");
    printf("Size: %d, Count: %d, Threshold: %d\n", 
           ht->size, ht->count, ht->threshold);
    printf("Load factor: %.2f\n", (float)ht->count / ht->size);
    
    for (int i = 0; i < ht->size; i++) {
        HEntry *entry = ht->buckets[i];
        if (entry) {
            printf("Bucket %d: ", i);
            while (entry) {
                printf("[%s = %s] -> ", entry->key, entry->value);
                entry = entry->next;
            }
            printf("NULL\n");
        }
    }
    printf("================================\n\n");
}

// Get all keys (for KEYS command)
char** ht_get_keys(HashTable *ht, int *count) {
    char **keys = malloc(ht->count * sizeof(char*));
    if (!keys) return NULL;
    
    int idx = 0;
    for (int i = 0; i < ht->size; i++) {
        HEntry *entry = ht->buckets[i];
        while (entry) {
            keys[idx++] = entry->key;
            entry = entry->next;
        }
    }
    *count = ht->count;
    return keys;
}

// Free the hash table
void ht_free(HashTable *ht) {
    for (int i = 0; i < ht->size; i++) {
        HEntry *entry = ht->buckets[i];
        while (entry) {
            HEntry *next = entry->next;
            free(entry->key);
            free(entry->value);
            free(entry);
            entry = next;
        }
    }
    free(ht->buckets);
    free(ht);
    printf("🗑️ Hash table freed\n");
}

// ============================================
// TESTING
// ============================================

int main() {
    printf("📚 Chapter 9-10: Hash Table Implementation\n\n");
    
    // Create hash table
    HashTable *ht = ht_create(8);
    printf("✅ Created hash table with %d buckets\n\n", ht->size);
    
    // Test 1: Insert some data
    printf("=== TEST 1: Inserting data ===\n");
    ht_set(ht, "name", "John");
    ht_set(ht, "age", "30");
    ht_set(ht, "city", "New York");
    ht_set(ht, "country", "USA");
    ht_set(ht, "job", "Engineer");
    ht_print(ht);
    
    // Test 2: Get data
    printf("=== TEST 2: Retrieving data ===\n");
    printf("name: %s\n", ht_get(ht, "name"));
    printf("age: %s\n", ht_get(ht, "age"));
    printf("city: %s\n", ht_get(ht, "city"));
    printf("job: %s\n", ht_get(ht, "job"));
    printf("unknown: %s\n", ht_get(ht, "unknown"));
    printf("\n");
    
    // Test 3: Update data
    printf("=== TEST 3: Updating data ===\n");
    ht_set(ht, "name", "Jane");
    ht_set(ht, "age", "31");
    ht_print(ht);
    
    // Test 4: Delete data
    printf("=== TEST 4: Deleting data ===\n");
    ht_delete(ht, "country");
    ht_delete(ht, "job");
    ht_print(ht);
    
    // Test 5: Resizing
    printf("=== TEST 5: Resizing (automatic) ===\n");
    printf("Adding more data to trigger resize...\n");
    ht_set(ht, "key1", "value1");
    ht_set(ht, "key2", "value2");
    ht_set(ht, "key3", "value3");
    ht_set(ht, "key4", "value4");
    ht_set(ht, "key5", "value5");
    ht_set(ht, "key6", "value6");
    ht_set(ht, "key7", "value7");
    ht_set(ht, "key8", "value8");
    ht_print(ht);
    
    // Test 6: Get all keys
    printf("=== TEST 6: Getting all keys ===\n");
    int key_count;
    char **keys = ht_get_keys(ht, &key_count);
    printf("All keys (%d): ", key_count);
    for (int i = 0; i < key_count; i++) {
        printf("%s ", keys[i]);
    }
    printf("\n\n");
    free(keys);
    
    // Clean up
    ht_free(ht);
    
    return 0;
}
