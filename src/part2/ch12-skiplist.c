#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define MAX_LEVEL 16

typedef struct SkipNode {
    char *key;
    double score;
    struct SkipNode **next;
    int level;
} SkipNode;

typedef struct {
    SkipNode *head;
    int max_level;
    int count;
    int level_count[MAX_LEVEL + 1];
} SkipList;

int random_level() {
    int level = 1;
    while ((rand() & 1) && level < MAX_LEVEL) {
        level++;
    }
    return level;
}

SkipList* sl_create() {
    SkipList *sl = malloc(sizeof(SkipList));
    if (!sl) return NULL;
    
    sl->head = malloc(sizeof(SkipNode));
    sl->head->key = NULL;
    sl->head->score = 0;
    sl->head->level = MAX_LEVEL;
    sl->head->next = calloc(MAX_LEVEL + 1, sizeof(SkipNode*));
    
    sl->max_level = 1;
    sl->count = 0;
    memset(sl->level_count, 0, sizeof(sl->level_count));
    
    return sl;
}

void free_node(SkipNode *node) {
    if (node) {
        if (node->key) free(node->key);
        if (node->next) free(node->next);
        free(node);
    }
}

void sl_free(SkipList *sl) {
    if (!sl) return;
    
    SkipNode *current = sl->head->next[0];
    while (current) {
        SkipNode *next = current->next[0];
        free_node(current);
        current = next;
    }
    free_node(sl->head);
    free(sl);
}

SkipNode* sl_find(SkipList *sl, const char *key) {
    if (!sl || !key) return NULL;
    
    SkipNode *current = sl->head->next[0];
    while (current) {
        if (strcmp(current->key, key) == 0) {
            return current;
        }
        current = current->next[0];
    }
    return NULL;
}

void sl_insert(SkipList *sl, const char *key, double score) {
    SkipNode *existing = sl_find(sl, key);
    if (existing) {
        existing->score = score;
        printf("✅ Updated: %s = %.2f\n", key, score);
        return;
    }
    
    SkipNode *update[MAX_LEVEL + 1];
    SkipNode *current = sl->head;
    
    for (int i = sl->max_level - 1; i >= 0; i--) {
        while (current->next[i] && 
               (current->next[i]->score < score || 
                (current->next[i]->score == score && 
                 strcmp(current->next[i]->key, key) < 0))) {
            current = current->next[i];
        }
        update[i] = current;
    }
    
    int new_level = random_level();
    if (new_level > sl->max_level) {
        for (int i = sl->max_level; i < new_level; i++) {
            update[i] = sl->head;
        }
        sl->max_level = new_level;
    }
    
    SkipNode *new_node = malloc(sizeof(SkipNode));
    new_node->key = strdup(key);
    new_node->score = score;
    new_node->level = new_level;
    new_node->next = calloc(new_level + 1, sizeof(SkipNode*));
    
    for (int i = 0; i < new_level; i++) {
        new_node->next[i] = update[i]->next[i];
        update[i]->next[i] = new_node;
        sl->level_count[i]++;
    }
    
    sl->count++;
    printf("✅ Inserted: %s = %.2f (level: %d, count: %d)\n", 
           key, score, new_level, sl->count);
}

double sl_get_score(SkipList *sl, const char *key) {
    SkipNode *node = sl_find(sl, key);
    if (node) {
        return node->score;
    }
    return -1;
}

bool sl_delete(SkipList *sl, const char *key) {
    if (!sl || !key) return false;
    
    // First find the node to get its score
    SkipNode *target = sl_find(sl, key);
    if (!target) {
        printf("❌ Key not found: %s\n", key);
        return false;
    }
    
    double score = target->score;
    
    SkipNode *update[MAX_LEVEL + 1];
    SkipNode *current = sl->head;
    
    for (int i = sl->max_level - 1; i >= 0; i--) {
        while (current->next[i] && 
               (current->next[i]->score < score || 
                (current->next[i]->score == score && 
                 strcmp(current->next[i]->key, key) < 0))) {
            current = current->next[i];
        }
        update[i] = current;
    }
    
    current = current->next[0];
    if (!current || strcmp(current->key, key) != 0) {
        printf("❌ Key not found: %s\n", key);
        return false;
    }
    
    for (int i = 0; i < sl->max_level; i++) {
        if (update[i]->next[i] != current) break;
        update[i]->next[i] = current->next[i];
        sl->level_count[i]--;
    }
    
    while (sl->max_level > 1 && sl->head->next[sl->max_level - 1] == NULL) {
        sl->max_level--;
    }
    
    free_node(current);
    sl->count--;
    printf("🗑️ Deleted: %s (count: %d)\n", key, sl->count);
    return true;
}

SkipNode** sl_range_by_rank(SkipList *sl, int start, int end, int *count) {
    if (start < 0) start = 0;
    if (end >= sl->count) end = sl->count - 1;
    if (start > end || sl->count == 0) {
        *count = 0;
        return NULL;
    }
    
    *count = end - start + 1;
    SkipNode **result = malloc(*count * sizeof(SkipNode*));
    if (!result) return NULL;
    
    SkipNode *current = sl->head->next[0];
    int pos = 0;
    while (current && pos < start) {
        current = current->next[0];
        pos++;
    }
    
    int idx = 0;
    while (current && idx < *count) {
        result[idx++] = current;
        current = current->next[0];
    }
    
    return result;
}

void sl_print(SkipList *sl) {
    printf("\n========== Skip List ==========\n");
    printf("Count: %d, Max Level: %d\n", sl->count, sl->max_level);
    printf("Level distribution: ");
    for (int i = 0; i < sl->max_level; i++) {
        printf("L%d:%d ", i, sl->level_count[i]);
    }
    printf("\n");
    
    for (int i = sl->max_level - 1; i >= 0; i--) {
        printf("Level %d: ", i);
        SkipNode *current = sl->head->next[i];
        while (current) {
            printf("[%s:%.2f] -> ", current->key, current->score);
            current = current->next[i];
        }
        printf("NULL\n");
    }
    printf("================================\n\n");
}

int main() {
    srand(time(NULL));
    
    printf("📚 Chapter 12-13: Skip List (Sorted Set)\n\n");
    
    SkipList *sl = sl_create();
    printf("✅ Created skip list\n\n");
    
    printf("=== TEST 1: Inserting elements ===\n");
    sl_insert(sl, "Alice", 50);
    sl_insert(sl, "Bob", 75);
    sl_insert(sl, "Charlie", 30);
    sl_insert(sl, "David", 90);
    sl_insert(sl, "Eve", 60);
    sl_insert(sl, "Frank", 40);
    sl_insert(sl, "Grace", 85);
    sl_print(sl);
    
    printf("=== TEST 2: Searching ===\n");
    char *search_keys[] = {"Alice", "Bob", "Unknown", "Eve"};
    for (int i = 0; i < 4; i++) {
        double score = sl_get_score(sl, search_keys[i]);
        if (score >= 0) {
            printf("✅ %s -> %.2f\n", search_keys[i], score);
        } else {
            printf("❌ %s -> not found\n", search_keys[i]);
        }
    }
    printf("\n");
    
    printf("=== TEST 3: Updating ===\n");
    sl_insert(sl, "Alice", 55);
    sl_insert(sl, "Bob", 80);
    sl_print(sl);
    
    printf("=== TEST 4: Range by rank (ZRANGE) ===\n");
    int count;
    SkipNode **range = sl_range_by_rank(sl, 1, 4, &count);
    if (range) {
        printf("Elements 1-4: ");
        for (int i = 0; i < count; i++) {
            printf("[%s:%.2f] ", range[i]->key, range[i]->score);
        }
        printf("\n");
        free(range);
    }
    printf("\n");
    
    printf("=== TEST 5: Deleting ===\n");
    sl_delete(sl, "David");
    sl_delete(sl, "Frank");
    sl_delete(sl, "Unknown");
    sl_print(sl);
    
    printf("=== TEST 6: Verify deletion ===\n");
    printf("David score: %.2f (should be -1.00)\n", sl_get_score(sl, "David"));
    printf("Frank score: %.2f (should be -1.00)\n", sl_get_score(sl, "Frank"));
    printf("Grace score: %.2f (should still exist)\n", sl_get_score(sl, "Grace"));
    printf("\n");
    
    sl_free(sl);
    printf("✅ Skip list freed\n");
    
    return 0;
}
