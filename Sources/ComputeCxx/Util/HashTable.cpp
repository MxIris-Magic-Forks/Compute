#include "HashTable.h"

#include <memory>

#include "Heap.h"

namespace util {

uint64_t pointer_hash(void const *pointer) {
    int64_t result = (-1 ^ (int64_t)(pointer) << 0x20) + (int64_t)pointer;
    result = result ^ result >> 0x16;
    result = result + (-1 ^ result << 0xd);
    result = (result ^ result >> 8) * 9;
    result = result ^ result >> 0xf;
    result = result + (-1 ^ result << 0x1b);
    return result ^ result >> 0x1f;
}

bool pointer_compare(void const *a, void const *b) { return a == b; }

uint64_t string_hash(char const *str) {
    int64_t result = 0;
    for (char const *c = str; *c; c += 1) {
        result = result * 33 + *c;
    }
    return result;
}

UntypedTable::UntypedTable() {
    _hash = pointer_hash;
    _compare = pointer_compare;
    _did_remove_key = nullptr;
    _did_remove_value = nullptr;
    _heap = nullptr;
    _spare_node = 0;
    _buckets = nullptr;
    _count = 0;
    _bucket_mask = 0;
    _bucket_mask_width = 0;
    _is_heap_owner = true;
    _compare_by_pointer = true;
}

UntypedTable::UntypedTable(hasher custom_hash, key_equal custom_compare, key_callback_t did_remove_key,
                           value_callback_t did_remove_value, Heap *heap) {
    _hash = custom_hash != nullptr ? custom_hash : pointer_hash;
    _compare = custom_compare != nullptr ? custom_compare : pointer_compare;
    _did_remove_key = did_remove_key;
    _did_remove_value = did_remove_value;
    _heap = heap;
    _spare_node = 0;
    _buckets = nullptr;
    _count = 0;
    _bucket_mask = 0;
    _bucket_mask_width = 0;
    _is_heap_owner = heap == nullptr;
    _compare_by_pointer = custom_compare == nullptr || custom_compare == pointer_compare;
}

UntypedTable::~UntypedTable() {
    if ((_did_remove_key || _did_remove_value) && _count) {
        for (uint32_t bucket = 0; !(bucket >> _bucket_mask_width); bucket++) {
            for (HashNode *node = _buckets[bucket]; node != nullptr; node = node->next) {
                if (_did_remove_key) {
                    _did_remove_key(node->key);
                }
                if (_did_remove_value) {
                    _did_remove_value(node->value);
                }
            }
        }
    }
    if (_bucket_mask > 4) {
        free(_buckets);
    }
    if (_is_heap_owner && _heap) {
        _heap->reset(nullptr, 0);
        delete _heap;
    }
}

#pragma mark - Managing buckets

constexpr uint32_t initial_bucket_mask_width = 4;

void UntypedTable::create_buckets() {

    if (_buckets == nullptr) {
        _bucket_mask_width = initial_bucket_mask_width;
        _bucket_mask = (1 << initial_bucket_mask_width) - 1;

        if (_heap == nullptr) {
            _heap = new Heap(nullptr, 0, Heap::minimum_increment);
        }

        size_t size = sizeof(Bucket *) * (1 << initial_bucket_mask_width);
        _buckets = (Bucket *)_heap->alloc_(size);
        std::memset(_buckets, 0, size);
    }
}

void UntypedTable::grow_buckets() {
    if (_bucket_mask_width > 0x1E) {
        return;
    }

    uint32_t old_width = _bucket_mask_width;
    _bucket_mask_width = old_width + 1;

    Bucket *old_buckets = _buckets;
    Bucket *new_buckets = nullptr;

    uint64_t size = sizeof(Bucket *) * (1 << _bucket_mask_width);
    if (old_width >= initial_bucket_mask_width) {
        new_buckets = new Bucket[1 << _bucket_mask_width];
    } else {
        Heap *heap = _heap;
        if (heap == nullptr) {
            _heap = new Heap(nullptr, 0, Heap::minimum_increment);
        }
        new_buckets = (Bucket *)_heap->alloc_(size);
    }
    std::memset(new_buckets, 0, size);

    // redistribute old buckets into new
    if (new_buckets) {
        _bucket_mask = (1 << _bucket_mask_width) - 1;
        for (uint32_t i = 0; !(i >> old_width); i++) {
            for (UntypedTable::HashNode *node = old_buckets[i]; node != nullptr; node = node->next) {
                uint64_t new_bucket = _bucket_mask & node->hash_value;
                node->next = new_buckets[new_bucket];
                new_buckets[new_bucket] = node;
            }
        }
        _buckets = new_buckets;

        if (old_width > initial_bucket_mask_width) {
            delete old_buckets;
        }
    }
}

#pragma mark - Lookup

UntypedTable::nullable_value_type UntypedTable::lookup(key_type key, nullable_key_type *found_key_out) {
    if (_count) {
        uint64_t hash_value = _hash(key);
        HashNode *node = _buckets[_bucket_mask & hash_value];
        if (_compare_by_pointer) {
            for (; node != nullptr; node = node->next) {
                if (node->key == key) {
                    if (found_key_out) {
                        *found_key_out = node->key;
                    }
                    return node->value;
                }
            }
        } else if (node) {
            for (; node != nullptr; node = node->next) {
                if (node->hash_value == hash_value && _compare(node->key, key)) {
                    if (found_key_out) {
                        *found_key_out = node->key;
                    }
                    return node->value;
                }
            }
        }
    }
    if (found_key_out) {
        *found_key_out = nullptr;
    }
    return nullptr;
}

#pragma mark - Modifying entries

bool UntypedTable::insert(key_type key, value_type value) {
    if (_buckets == nullptr) {
        this->create_buckets();
    }

    void *result;

    uint64_t hash_value = _hash(key);
    HashNode *node = _buckets[hash_value & _bucket_mask];

    // replace existing if match
    for (; node != nullptr; node = node->next) {
        if (node->hash_value == hash_value && _compare(node->key, key)) {
            if (_did_remove_key) {
                _did_remove_key(node->key);
            }
            if (_did_remove_value) {
                _did_remove_value(node->value);
            }
            node->key = key;
            node->value = value;
            return false;
        }
    }

    // insert new
    if (_count + 1 > 4 << _bucket_mask_width) {
        this->grow_buckets();
    }
    if (!_heap) {
        _heap = new Heap(nullptr, 0, Heap::minimum_increment);
    }

    HashNode *inserted_node = _spare_node;
    if (inserted_node) {
        _spare_node = _spare_node->next;
    } else {
        inserted_node = (HashNode *)_heap->alloc_(sizeof(HashNode));
    }

    inserted_node->key = key;
    inserted_node->value = value;
    inserted_node->hash_value = hash_value;

    uint64_t bucket = _bucket_mask & hash_value;
    inserted_node->next = _buckets[bucket];
    _buckets[bucket] = inserted_node;

    _count += 1;

    return true;
}

bool UntypedTable::remove(key_type key) {
    if (_count == 0) {
        return false;
    }
    if (_compare_by_pointer) {
        return this->remove_ptr(key);
    }
    uint64_t hash_value = _hash(key);
    HashNode *node = _buckets[_bucket_mask & hash_value];

    for (HashNode *candidate = node; candidate != nullptr; candidate = candidate->next) {
        if (candidate->hash_value == hash_value && _compare(candidate->key, key)) {
            node->next = candidate->next;
            if (_did_remove_key) {
                _did_remove_key(candidate->key);
            }
            if (_did_remove_value) {
                _did_remove_value(candidate->value);
            }
            candidate->next = _spare_node;
            _spare_node = candidate;
            _count -= 1;
            return true;
        }
        node = candidate;
    }

    return false;
}

bool UntypedTable::remove_ptr(key_type key) {
    if (_count == 0) {
        return false;
    }

    HashNode *node = _buckets[_bucket_mask & _hash(key)];
    for (HashNode *candidate = node; candidate != nullptr; candidate = candidate->next) {
        if (candidate->key == key) {
            node->next = candidate->next;
            if (_did_remove_key) {
                _did_remove_key(candidate->key);
            }
            if (_did_remove_value) {
                _did_remove_value(candidate->value);
            }
            candidate->next = _spare_node;
            _spare_node = candidate;
            _count -= 1;
            return true;
        }
        node = candidate;
    }
}

void UntypedTable::for_each(entry_callback_t body, const void *context) {
    if (_count) {
        for (uint32_t i = 0; !(i >> _bucket_mask_width); i++) {
            for (UntypedTable::HashNode *node = _buckets[i]; node != nullptr; node = node->next) {
                body(node->key, node->value, context);
            }
        }
    }
}

} // namespace util
