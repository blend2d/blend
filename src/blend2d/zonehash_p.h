// // Blend2D - 2D Vector Graphics Powered by a JIT Compiler
//
//  * Official Blend2D Home Page: https://blend2d.com
//  * Official Github Repository: https://github.com/blend2d/blend2d
//
// Copyright (c) 2017-2020 The Blend2D Authors
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

#ifndef BLEND2D_ZONEHASH_P_H_INCLUDED
#define BLEND2D_ZONEHASH_P_H_INCLUDED

#include "./api-internal_p.h"
#include "./unicode_p.h"
#include "./zoneallocator_p.h"

//! \cond INTERNAL
//! \addtogroup blend2d_internal
//! \{

// ============================================================================
// [blHashString]
// ============================================================================

static BL_INLINE uint32_t blHashRound(uint32_t hash, uint32_t c) noexcept {
  return hash * 65599u + c;
}

static BL_INLINE uint32_t blHashRoundCI(uint32_t hash, uint32_t c) noexcept {
  return hash * 65599u + blAsciiToLower(c);
}

// Gets a hash of the given string `data` of size `size`. Size must be valid
// as this function doesn't check for a null terminator and allows it in the
// middle of the string.
static BL_INLINE uint32_t blHashString(const char* data, size_t size) noexcept {
  uint32_t hashCode = 0;
  for (uint32_t i = 0; i < size; i++)
    hashCode = blHashRound(hashCode, uint8_t(data[i]));
  return hashCode;
}

static BL_INLINE uint32_t blHashString(const BLStringView& view) noexcept {
  return blHashString(view.data, view.size);
}

// Gets a hash of the given string `data` of size `size`. Size must be valid
// as this function doesn't check for a null terminator and allows it in the
// middle of the string.
static BL_INLINE uint32_t blHashStringCI(const char* data, size_t size) noexcept {
  uint32_t hashCode = 0;
  for (uint32_t i = 0; i < size; i++)
    hashCode = blHashRoundCI(hashCode, uint8_t(data[i]));
  return hashCode;
}

static BL_INLINE uint32_t blHashStringCI(const BLStringView& view) noexcept {
  return blHashStringCI(view.data, view.size);
}

// ============================================================================
// [BLZoneHashNode]
// ============================================================================

//! Node used by `ZoneHash<>` template.
//!
//! You must provide function `bool eq(const Key& key)` in order to make
//! `ZoneHash::get()` working.
class BLZoneHashNode {
public:
  BL_NONCOPYABLE(BLZoneHashNode)

  BL_INLINE BLZoneHashNode(uint32_t hashCode = 0, uint32_t customData = 0) noexcept
    : _hashNext(nullptr),
      _hashCode(hashCode),
      _customData(customData) {}

  //! Next node in the chain, null if it terminates the chain.
  BLZoneHashNode* _hashNext;
  //! Precalculated hash-code of key.
  uint32_t _hashCode;
  //! Padding, can be reused by any Node that inherits `BLZoneHashNode`.
  union {
    uint32_t _customData;
    uint16_t _customDataU16[2];
    uint8_t _customDataU8[4];
  };
};

// ============================================================================
// [BLZoneHashBase]
// ============================================================================

//! Base class used by `BLZoneHashMap<>` template to share the common functionality.
class BLZoneHashBase {
public:
  BL_NONCOPYABLE(BLZoneHashBase)

  // NOTE: There must be at least 2 embedded buckets, otherwise we wouldn't be
  // able to implement division as multiplication and shift in 32-bit mode the
  // way we want. Additionally, if we know that there is always a valid buckets
  // array we won't have to perform null checks.
  enum NullConstants : uint32_t {
    kNullCount = 2,
    kNullGrow = 1,
    kNullRcpValue = 2147483648u, // 2^31
    kNullRcpShift = BL_TARGET_ARCH_BITS >= 64 ? 32 : 0
  };

  //! Buckets data.
  BLZoneHashNode** _data;
  //! Count of records inserted into the hash table.
  size_t _size;
  //! Count of hash buckets.
  uint32_t _bucketCount;
  //! When buckets array should grow (only checked after insertion).
  uint32_t _bucketGrow;
  //! Reciprocal value of `_bucketCount`.
  uint32_t _rcpValue;
  //! How many bits to shift right when hash is multiplied with `_rcpValue`.
  uint8_t _rcpShift;
  //! Prime value index in internal prime array.
  uint8_t _primeIndex;
  //! Padding...
  uint8_t _reserved[2];
  //! Embedded and initial hash data.
  BLZoneHashNode* _embedded[kNullCount];

  //! \name Construction & Destruction
  //! \{

  BL_INLINE BLZoneHashBase() noexcept
    : _data(_embedded),
      _size(0),
      _bucketCount(kNullCount),
      _bucketGrow(kNullGrow),
      _rcpValue(kNullRcpValue),
      _rcpShift(kNullRcpShift),
      _primeIndex(0),
      _embedded {} {}

  BL_INLINE BLZoneHashBase(BLZoneHashBase&& other) noexcept
    : _data(other._data),
      _size(other._size),
      _bucketCount(other._bucketCount),
      _bucketGrow(other._bucketGrow),
      _rcpValue(other._rcpValue),
      _rcpShift(other._rcpShift),
      _primeIndex(other._primeIndex) {
    other._data = nullptr;
    other._size = 0;
    other._bucketCount = kNullCount;
    other._bucketGrow = kNullGrow;
    other._rcpValue = kNullRcpValue;
    other._rcpShift = kNullRcpShift;
    other._primeIndex = 0;

    memcpy(_embedded, other._embedded, kNullCount * sizeof(BLZoneHashNode*));
    memset(other._embedded, 0, kNullCount * sizeof(BLZoneHashNode*));
  }

  BL_INLINE ~BLZoneHashBase() noexcept {
    if (_data != _embedded)
      free(_data);
  }

  BL_INLINE void reset() noexcept {
    if (_data != _embedded)
      free(_data);

    _data = _embedded;
    _size = 0;
    _bucketCount = kNullCount;
    _bucketGrow = kNullGrow;
    _rcpValue = kNullRcpValue;
    _rcpShift = kNullRcpShift;
    _primeIndex = 0;
    memset(_embedded, 0, kNullCount * sizeof(BLZoneHashNode*));
  }

  //! \}

  //! \name Accessors
  //! \{

  BL_INLINE bool empty() const noexcept { return _size == 0; }
  BL_INLINE size_t size() const noexcept { return _size; }

  //! \}

  //! \name Internals
  //! \{

  BL_INLINE void _swap(BLZoneHashBase& other) noexcept {
    std::swap(_data, other._data);
    std::swap(_size, other._size);
    std::swap(_bucketCount, other._bucketCount);
    std::swap(_bucketGrow, other._bucketGrow);
    std::swap(_rcpValue, other._rcpValue);
    std::swap(_rcpShift, other._rcpShift);
    std::swap(_primeIndex, other._primeIndex);

    for (uint32_t i = 0; i < kNullCount; i++)
      std::swap(_embedded[i], other._embedded[i]);

    if (_data == other._embedded) _data = _embedded;
    if (other._data == _embedded) other._data = other._embedded;
  }

  BL_INLINE uint32_t _calcMod(uint32_t hash) const noexcept {
    uint32_t divided =
      BL_TARGET_ARCH_BITS >= 64
        ? uint32_t((uint64_t(hash) * _rcpValue) >> _rcpShift)
        : uint32_t((uint64_t(hash) * _rcpValue) >> 32) >> _rcpShift;

    uint32_t result = hash - divided * _bucketCount;
    BL_ASSERT(result < _bucketCount);
    return result;
  }

  void _rehash(uint32_t newCount) noexcept;
  void _insert(BLZoneHashNode* node) noexcept;
  bool _remove(BLZoneHashNode* node) noexcept;

  //! \}
};

// ============================================================================
// [BLZoneHashMap]
// ============================================================================

//! Low-level hash table specialized for storing string keys and POD values.
//!
//! This hash table allows duplicates to be inserted (the API is so low
//! level that it's up to you if you allow it or not, as you should first
//! `get()` the node and then modify it or insert a new node by using `insert()`,
//! depending on the intention).
template<typename NodeT>
class BLZoneHashMap : public BLZoneHashBase {
public:
  BL_NONCOPYABLE(BLZoneHashMap<NodeT>)

  typedef NodeT Node;

  //! \name Construction & Destruction
  //! \{

  BL_INLINE BLZoneHashMap() noexcept
    : BLZoneHashBase() {}

  BL_INLINE BLZoneHashMap(BLZoneHashMap&& other) noexcept
    : BLZoneHashMap(other) {}

  BL_INLINE ~BLZoneHashMap() noexcept {
    if (!std::is_trivially_destructible<NodeT>::value)
      _destroy();
  }

  //! \}

  //! \name Utilities
  //! \{

  BL_INLINE void swap(BLZoneHashMap& other) noexcept {
    BLZoneHashBase::_swap(other);
  }

  void _destroy() noexcept {
    for (size_t i = 0; i < _bucketCount; i++) {
      NodeT* node = static_cast<NodeT*>(_data[i]);
      if (node) {
        do {
          NodeT* next = static_cast<NodeT*>(node->_hashNext);
          blCallDtor(*node);
          node = next;
        } while (node);
        _data[i] = nullptr;
      }
    }
  }

  //! \}

  //! \name Functionality
  //! \{

  BL_INLINE NodeT* nodesByHashCode(uint32_t hashCode) const noexcept {
    uint32_t hashMod = _calcMod(hashCode);
    return static_cast<NodeT*>(_data[hashMod]);
  }

  template<typename KeyT>
  BL_INLINE NodeT* get(const KeyT& key) const noexcept {
    NodeT* node = nodesByHashCode(key.hashCode());
    while (node && !key.matches(node))
      node = static_cast<NodeT*>(node->_hashNext);
    return node;
  }

  BL_INLINE void insert(NodeT* node) noexcept { _insert(node); }
  BL_INLINE bool remove(NodeT* node) noexcept { return _remove(node); }

  template<typename Lambda>
  BL_INLINE void forEach(Lambda&& f) const noexcept {
    BLZoneHashNode** buckets = _data;
    uint32_t bucketCount = _bucketCount;

    for (uint32_t i = 0; i < bucketCount; i++) {
      Node* node = static_cast<Node*>(buckets[i]);
      while (node) {
        Node* next = static_cast<Node*>(node->_hashNext);
        f(node);
        node = next;
      }
    }
  }

  //! \}
};

//! \}
//! \endcond

#endif // BLEND2D_ZONEHASH_P_H_INCLUDED
