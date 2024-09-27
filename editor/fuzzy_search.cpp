/**************************************************************************/
/*  fuzzy_search.cpp                                                      */
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

#include "fuzzy_search.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"
#include "scene/gui/tree.h"

const int max_results = 100;
const int max_misses = 2;
const int short_query_cutoff = 3;
const float cull_factor = 0.5f;
const String boundary_chars = "/\\-_.";

Ref<FuzzySearchResult> new_search_result(const String &p_target) {
	Ref<FuzzySearchResult> result;
	result.instantiate();
	result->target = p_target;
	result->score = 0;
	result->bonus_index = p_target.rfind_char('/');
	return result;
}

bool is_on_boundary(const String& str, const int index) {
	if (index == -1 || index == str.size()) {
		return true;
	}
	return boundary_chars.find_char(str[index]) >= 0;
}

void FuzzySearchResult::add_and_score_substring(const int p_start, const int p_length, const int p_query_length) {
	matched_substring_pairs.append(p_start);
	matched_substring_pairs.append(p_length);
	// Score longer substrings higher than short substrings
	int substring_score = p_length * p_length * p_length;
	// Score matches deeper in path higher than shallower matches
	if (p_start > bonus_index) {
		substring_score *= 2;
	}
	// Score matches on a word boundary higher than matches within a word
	if (is_on_boundary(target, p_start - 1) || is_on_boundary(target, p_start + p_length)) {
		substring_score += 2;
	}
	// Score exact query matches higher than non-compact subsequence matches
	if (p_length == p_query_length) {
		substring_score *= 3;
	}
	score += substring_score;
}

Vector<Ref<FuzzySearchResult>> sort_and_filter(const Vector<Ref<FuzzySearchResult>> &p_results) {
	Vector<Ref<FuzzySearchResult>> results;

	if (p_results.is_empty()) {
		return results;
	}

	float avg_score = 0;

	for (const Ref<FuzzySearchResult> &result : p_results) {
		if (result->score > avg_score) {
			avg_score += result->score;
		}
	}

	avg_score /= p_results.size();
	float cull_score = avg_score * cull_factor;

	struct FuzzySearchResultComparator {
		bool operator()(const Ref<FuzzySearchResult> &A, const Ref<FuzzySearchResult> &B) const {
			// Sort on (score, length, alphanumeric) to ensure consistent ordering
			if (A->score == B->score) {
				if (A->target.length() == B->target.length()) {
					return A->target < B->target;
				}
				return A->target.length() < B->target.length();
			}
			return A->score > B->score;
		}
	};

	// Prune low score entries before even sorting
	for (Ref<FuzzySearchResult> i : p_results) {
		if (i->score >= cull_score) {
			results.push_back(i);
		}
	}

	SortArray<Ref<FuzzySearchResult>, FuzzySearchResultComparator> sorter;

	if (results.size() > max_results) {
		sorter.partial_sort(0, results.size(), max_results, results.ptrw());
		results.resize(max_results);
	} else {
		sorter.sort(results.ptrw(), results.size());
	}

	return results;
}

Ref<FuzzySearchResult> fuzzy_search(const PackedStringArray &p_query, const String &p_target, bool case_sensitive) {
	if (p_query.size() == 0) {
		return nullptr;
	}

	if (p_target.is_empty()) {
		return nullptr;
	}

	String adjusted_target = case_sensitive ? p_target : p_target.to_lower();
	Ref<FuzzySearchResult> result = new_search_result(p_target);
	bool any_match = false;
	int offset = 0;
	int misses = 0;

	// Special case exact matches on very short queries
	if (p_query.size() == 1 && p_query[0].length() <= short_query_cutoff) {
		int index = adjusted_target.rfind(p_query[0]);
		if (index >= 0) {
			result->add_and_score_substring(index, p_query[0].length(), p_query[0].length());
			return result;
		}
	}

	for (int i = p_query.size() - 1; i >= 0; i--) {
		const String &part = p_query[i];
		int part_len = part.length();

		// Finds the starting offset for the latest instance of subsequence query_part in p_target.
		for (int j = part_len - 1; j >= 0; j--) {
			int new_offset = adjusted_target.rfind_char(part[j], offset - 1);
			if (new_offset < 0 || (new_offset == 0 && j > 0)) {
				if (++misses > max_misses) {
					return nullptr;
				}
			} else {
				offset = new_offset;
				any_match = true;
			}
		}

		// Disallow "matching" only on missed characters
		if (!any_match) {
			return nullptr;
		}

		int forward_offset = offset;
		int run_start = offset;
		int run_length = 1;

		// Forward-searches the same subsequence from that offset to bias for more compact representation
		// in cases with multiple subsequences. Inspired by FuzzyMatchV1 in fzf. Additionally, record each
		// substring in the subsequence for scoring and display.
		for (int j = 1; j < part_len; j++) {
			int new_offset = adjusted_target.find_char(part[j], forward_offset + 1);
			if (new_offset > 0) {
				forward_offset = new_offset;
				if (run_start + run_length != forward_offset) {
					result->add_and_score_substring(run_start, run_length, part_len);
					run_start = forward_offset;
					run_length = 1;
				} else {
					run_length += 1;
				}
			}
		}
		result->add_and_score_substring(run_start, run_length, part_len);
	}

	return result;
}

const PackedStringArray _split_query(const String& p_query) {
	return p_query.split(" ", false);
}

Ref<FuzzySearchResult> FuzzySearch::search(const String &p_query, const String &p_target) {
	if (p_query.is_empty()) {
		return new_search_result(p_target);
	}

	return fuzzy_search(_split_query(p_query), p_target, !p_query.is_lowercase());
}

Vector<Ref<FuzzySearchResult>> FuzzySearch::search_all(const String &p_query, const PackedStringArray &p_targets) {
	Vector<Ref<FuzzySearchResult>> res;

	// Just spit out the results list if no query is given.
	if (p_query.is_empty()) {
		for (int i = 0; (i < max_results) && (i < p_targets.size()); i++) {
			res.push_back(new_search_result(p_targets[i]));
		}

		return res;
	}

	bool case_sensitive = !p_query.is_lowercase();
	PackedStringArray query_parts = _split_query(p_query);

	for (const String &target : p_targets) {
		Ref<FuzzySearchResult> r = fuzzy_search(query_parts, target, case_sensitive);
		if (!r.is_null()) {
			res.append(r);
		}
	}

	return sort_and_filter(res);
}

void FuzzySearch::draw_matches(Tree *p_tree) {
	if (p_tree == nullptr) {
		return;
	}

	TreeItem *head = p_tree->get_root();
	if (head == nullptr) {
		return;
	}

	Ref<Font> font = p_tree->get_theme_font("font");
	if (!font.is_valid()) {
		return;
	}

	int font_size = p_tree->get_theme_font_size("font_size");
	Vector2 position_adjust = Vector2(0, -3);
	Vector2 size_adjust = Vector2(1, 0);

	Vector2 margin_and_scroll_offset = -p_tree->get_scroll() + position_adjust;
	margin_and_scroll_offset.x += p_tree->get_theme_constant("item_margin");
	margin_and_scroll_offset.y += font->get_string_size("A", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).y;

	Vector2 magic_numbers = Vector2(23, -5);
	margin_and_scroll_offset += magic_numbers;

	Ref<Texture2D> icon = head->get_icon(0);
	if (icon.is_valid()) {
		margin_and_scroll_offset.x += icon->get_width();
	}

	while (head != nullptr && head->is_visible()) {
		Ref<FuzzySearchResult> fuzzy_search_result = head->get_metadata(0);
		if (fuzzy_search_result.is_valid()) {
			const Vector<int> &substr_sequences = fuzzy_search_result->matched_substring_pairs;

			for (int i = 0; i < substr_sequences.size(); i += 2) {
				String str_left_of_match = fuzzy_search_result->target.substr(0, substr_sequences[i]);
				String match_str = fuzzy_search_result->target.substr(substr_sequences[i], substr_sequences[i + 1]);

				Vector2 position = font->get_string_size(str_left_of_match, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
				position.y = 0;
				position += p_tree->get_item_rect(head, 0).position;
				position += margin_and_scroll_offset;

				Vector2 size = font->get_string_size(match_str, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size) + size_adjust;

				p_tree->draw_rect(Rect2(position, size), Color(1, 1, 1, 0.07), true);
				p_tree->draw_rect(Rect2(position, size), Color(0.5, 0.7, 1.0, 0.4), false, 1);
			}
		}

		head = head->get_next_visible();
	}
}
