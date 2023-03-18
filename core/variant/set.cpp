/**************************************************************************/
/*  dictionary.cpp                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "set.h"

#include "core/templates/hash_set.h"
#include "core/templates/safe_refcount.h"
#include "core/variant/variant.h"
// required in this order by VariantInternal, do not remove this comment.
#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/variant/type_info.h"
#include "core/variant/variant_internal.h"

struct SetPrivate {
	SafeRefCount refcount;
	Variant *read_only = nullptr; // If enabled, a pointer is used to a temporary value that is used to return read-only values.
	HashSet<Variant, VariantHasher, StringLikeVariantComparator> variant_map;
};

Variant Set::get_value_at_index(int p_index) const {
	int index = 0;
	for (const Variant &E : _p->variant_map) {
		if (index == p_index) {
			return E;
		}
		index++;
	}

	return Variant();
}

const Variant *Set::getptr(const Variant &p_key) const {
	auto E(_p->variant_map.find(p_key));
	if (!E) {
		return nullptr;
	}
	return &(*E);
}

Variant *Set::getptr(const Variant &p_key) {
	auto E(_p->variant_map.find(p_key));
	if (!E) {
		return nullptr;
	}
	if (unlikely(_p->read_only != nullptr)) {
		*_p->read_only = *E;
		return _p->read_only;
	} else {
		return const_cast<Variant*>(&(*E));
	}
}

int Set::size() const {
	return _p->variant_map.size();
}

bool Set::is_empty() const {
	return !_p->variant_map.size();
}

bool Set::has(const Variant &p_key) const {
	return _p->variant_map.has(p_key);
}

bool Set::has_all(const Array &p_keys) const {
	for (int i = 0; i < p_keys.size(); i++) {
		if (!has(p_keys[i])) {
			return false;
		}
	}
	return true;
}

bool Set::erase(const Variant &p_key) {
	ERR_FAIL_COND_V_MSG(_p->read_only, false, "Dictionary is in read-only state.");
	return _p->variant_map.erase(p_key);
}

bool Set::operator==(const Set &p_dictionary) const {
	return recursive_equal(p_dictionary, 0);
}

bool Set::operator!=(const Set &p_dictionary) const {
	return !recursive_equal(p_dictionary, 0);
}

bool Set::recursive_equal(const Set &p_dictionary, int recursion_count) const {
	// Cheap checks
	if (_p == p_dictionary._p) {
		return true;
	}
	if (_p->variant_map.size() != p_dictionary._p->variant_map.size()) {
		return false;
	}

	// Heavy O(n) check
	if (recursion_count > MAX_RECURSION) {
		ERR_PRINT("Max recursion reached");
		return true;
	}
	recursion_count++;
	for (const Variant &this_E : _p->variant_map) {
		HashSet<Variant, VariantHasher, StringLikeVariantComparator>::Iterator other_E(p_dictionary._p->variant_map.find(this_E));
		if (!other_E || !this_E.hash_compare(*other_E, recursion_count)) {
			return false;
		}
	}
	return true;
}

void Set::_ref(const Set &p_from) const {
	//make a copy first (thread safe)
	if (!p_from._p->refcount.ref()) {
		return; // couldn't copy
	}

	//if this is the same, unreference the other one
	if (p_from._p == _p) {
		_p->refcount.unref();
		return;
	}
	if (_p) {
		_unref();
	}
	_p = p_from._p;
}

void Set::clear() {
	ERR_FAIL_COND_MSG(_p->read_only, "Dictionary is in read-only state.");
	_p->variant_map.clear();
}

void Set::merge(const Set &p_dictionary, bool p_overwrite) {
	
}

void Set::_unref() const {
	ERR_FAIL_COND(!_p);
	if (_p->refcount.unref()) {
		if (_p->read_only) {
			memdelete(_p->read_only);
		}
		memdelete(_p);
	}
	_p = nullptr;
}

uint32_t Set::hash() const {
	return recursive_hash(0);
}

uint32_t Set::recursive_hash(int recursion_count) const {
	if (recursion_count > MAX_RECURSION) {
		ERR_PRINT("Max recursion reached");
		return 0;
	}

	uint32_t h = hash_murmur3_one_32(Variant::SET);

	recursion_count++;
	for (const Variant &E : _p->variant_map) {
		h = hash_murmur3_one_32(E.recursive_hash(recursion_count), h);
		h = hash_murmur3_one_32(E.recursive_hash(recursion_count), h);
	}

	return hash_fmix32(h);
}

Array Set::values() const {
	Array varr;
	if (_p->variant_map.is_empty()) {
		return varr;
	}

	varr.resize(size());

	int i = 0;
	for (const Variant &E : _p->variant_map) {
		varr[i] = E;
		i++;
	}

	return varr;
}

const Variant *Set::next(const Variant *p_key) const {
	if (p_key == nullptr) {
		// caller wants to get the first element
		if (_p->variant_map.begin()) {
			return &(*_p->variant_map.begin());
		}
		return nullptr;
	}
	HashSet<Variant, VariantHasher, StringLikeVariantComparator>::Iterator E = _p->variant_map.find(*p_key);

	if (!E) {
		return nullptr;
	}

	++E;

	if (E) {
		return &(*E);
	}

	return nullptr;
}

Set Set::duplicate(bool p_deep) const {
	return recursive_duplicate(p_deep, 0);
}

void Set::make_read_only() {
	if (_p->read_only == nullptr) {
		_p->read_only = memnew(Variant);
	}
}
bool Set::is_read_only() const {
	return _p->read_only != nullptr;
}

Set Set::recursive_duplicate(bool p_deep, int recursion_count) const {
	Set n;

	if (recursion_count > MAX_RECURSION) {
		ERR_PRINT("Max recursion reached");
		return n;
	}

/*
	if (p_deep) {
		recursion_count++;
		for (const KeyValue<Variant, Variant> &E : _p->variant_map) {
			n[E.key.recursive_duplicate(true, recursion_count)] = E.value.recursive_duplicate(true, recursion_count);
		}
	} else {
		for (const KeyValue<Variant, Variant> &E : _p->variant_map) {
			n[E.key] = E.value;
		}
	}
*/
	return n;
}

void Set::operator=(const Set &p_dictionary) {
	if (this == &p_dictionary) {
		return;
	}
	_ref(p_dictionary);
}

const void *Set::id() const {
	return _p;
}

Set::Set(const Set &p_from) {
	_p = nullptr;
	_ref(p_from);
}

Set::Set() {
	_p = memnew(SetPrivate);
	_p->refcount.init();
}

Set::~Set() {
	_unref();
}
