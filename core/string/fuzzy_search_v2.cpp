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

#include "fuzzy_search_v2.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"
#include "scene/gui/tree.h"

const int max_results = 100;
const int max_misses = 2;
const float cull_factor = 0.8f;
const String boundary_chars = "/\\-_.";

bool is_valid_interval(Vector2i p_interval) {
	// Empty intervals are represented as (-1, -1).
	return p_interval.x >= 0 && p_interval.y >= p_interval.x;
}

Vector2i extend_interval(Vector2i p_a, Vector2i p_b) {
	if (!is_valid_interval(p_a)) {
		return p_b;
	}
	if (!is_valid_interval(p_b)) {
		return p_a;
	}
	return Vector2(MIN(p_a.x, p_b.x), MAX(p_a.y, p_b.y));
}

Ref<FuzzyTokenMatch> new_token_match(const String& p_token) {
	Ref<FuzzyTokenMatch> match;
	match.instantiate();
	match->token_length = p_token.length();
	return match;
}

void FuzzyTokenMatch::add_substring(const int substring_start, const int substring_length) {
	substrings.append(Vector2i(substring_start, substring_length));
	matched_length += substring_length;
	int substring_end = substring_start + substring_length - 1;
	interval = extend_interval(interval, Vector2i(substring_start, substring_end));
}

bool FuzzyTokenMatch::intersects(const Vector2i other_interval) {
	if (!is_valid_interval(interval) || !is_valid_interval(other_interval)) {
		return false;
	}
	return interval.y >= other_interval.x && interval.x <= other_interval.y;
}

Ref<FuzzySearchResultV2> new_search_result(const String &p_target) {
	Ref<FuzzySearchResultV2> result;
	result.instantiate();
	result->target = p_target;
	result->bonus_index = p_target.rfind_char('/');
	result->miss_budget = max_misses;
	return result;
}

bool is_word_boundary(const String& str, const int index) {
	if (index == -1 || index == str.size()) {
		return true;
	}
	return boundary_chars.find_char(str[index]) >= 0;
}

bool FuzzySearchResultV2::can_add_token_match(Ref<FuzzyTokenMatch> &p_match) {
	if (p_match.is_null() || p_match->misses() > miss_budget) {
		return false;
	}

	if (p_match->intersects(match_interval)) {
		if (token_matches.size() == 1) {
			return false;
		}
		for (Ref<FuzzyTokenMatch> existing_match : token_matches) {
			if (existing_match->intersects(p_match->interval)) {
				return false;
			}
		}
	}

	return true;
}

void FuzzySearchResultV2::score_token_match(Ref<FuzzyTokenMatch> &p_match) {
	// This can always be tweaked more. The intuition is that exact matches should almost always
	// be prioritized over broken up matches, and other criteria more or less act as tie breakers.

	p_match->score = 0;

	for (Vector2i substring : p_match->substrings) {
		// Score longer substrings higher than short substrings
		int substring_score = substring.y * substring.y;
		// Score matches deeper in path higher than shallower matches
		if (substring.x > bonus_index) {
			substring_score *= 2;
		}
		// Score matches on a word boundary higher than matches within a word
		if (is_word_boundary(target, substring.x - 1) || is_word_boundary(target, substring.x + substring.y)) {
			substring_score += 4;
		}
		// Score exact query matches higher than non-compact subsequence matches
		if (substring.y == p_match->token_length) {
			substring_score += 100;
		}
		p_match->score += substring_score;
	}
}

void FuzzySearchResultV2::add_token_match(Ref<FuzzyTokenMatch>& p_match) {
	score += p_match->score;
	match_interval = extend_interval(match_interval, p_match->interval);
	miss_budget -= p_match->misses();
	token_matches.append(p_match);
}

Vector<Ref<FuzzySearchResultV2>> sort_and_filter(const Vector<Ref<FuzzySearchResultV2>> &p_results) {
	Vector<Ref<FuzzySearchResultV2>> results;

	if (p_results.is_empty()) {
		return results;
	}

	float avg_score = 0;

	for (const Ref<FuzzySearchResultV2> &result : p_results) {
		if (result->score > avg_score) {
			avg_score += result->score;
		}
	}

	// TODO: Tune scoring and culling here to display fewer subsequence soup matches when good matches
	//  are available.
	avg_score /= p_results.size();
	float cull_score = avg_score * cull_factor;

	struct FuzzySearchResultComparator {
		bool operator()(const Ref<FuzzySearchResultV2> &A, const Ref<FuzzySearchResultV2> &B) const {
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

	// Prune low score entries before sorting
	for (Ref<FuzzySearchResultV2> i : p_results) {
		if (i->score >= cull_score) {
			results.push_back(i);
		}
	}

	SortArray<Ref<FuzzySearchResultV2>, FuzzySearchResultComparator> sorter;

	if (results.size() > max_results) {
		sorter.partial_sort(0, results.size(), max_results, results.ptrw());
		results.resize(max_results);
	} else {
		sorter.sort(results.ptrw(), results.size());
	}

	return results;
}

Ref<FuzzyTokenMatch> try_match_token(
	const String &p_token,
	const String &p_target,
	int p_offset,
	int p_miss_budget
) {
	Ref<FuzzyTokenMatch> match = new_token_match(p_token);
	int run_start = -1;
	int run_len = 0;

	// Search for the subsequence p_token in p_target starting from p_offset, recording each substring for
	// later scoring and display.
	for (int i = 0; i < p_token.length(); i++) {
		int new_offset = p_target.find_char(p_token[i], p_offset);
		if (new_offset < 0) {
			if (--p_miss_budget < 0) {
				return nullptr;
			}
		} else {
			if (run_start == -1 || p_offset != new_offset) {
				if (run_start != -1) {
					match->add_substring(run_start, run_len);
				}
				run_start = new_offset;
				run_len = 1;
			} else {
				run_len += 1;
			}
			p_offset = new_offset + 1;
		}
	}

	if (run_start != -1) {
		match->add_substring(run_start, run_len);
	}

	return match;
}

Ref<FuzzySearchResultV2> fuzzy_search(const PackedStringArray &p_query, const String &p_target, bool case_sensitive) {
	if (p_query.size() == 0) {
		return nullptr;
	}

	if (p_target.is_empty()) {
		return nullptr;
	}

	String adjusted_target = case_sensitive ? p_target : p_target.to_lower();
	Ref<FuzzySearchResultV2> result = new_search_result(p_target);

	// For each token, eagerly generate subsequences starting from index 0 and keep the best scoring one
	// which does not conflict with prior token matches. This is not ensured to find the highest scoring
	// combination of matches, or necessarily the highest scoring single subsequence, as it only considers
	// eager subsequences for a given index, and likewise eagerly finds matches for each token in sequence.
	for (const String &token : p_query) {
		int offset = 0;
		// TODO : Consider avoiding the FuzzyTokenMatch allocation by either passing a reference to reuse or
		//  otherwise tracking scores/intervals witout a RefCounted construct.
		Ref<FuzzyTokenMatch> best_match = nullptr, match = nullptr;

		while (true) {
			match = try_match_token(token, adjusted_target, offset, result->miss_budget);
			if (match.is_null()) {
				break;
			}
			if (result->can_add_token_match(match)) {
				result->score_token_match(match);
				if (best_match.is_null() || best_match->score < match->score) {
					best_match = match;
				}
			}
			if (is_valid_interval(match->interval)) {
				offset = match->interval.x + 1;
			} else {
				break;
			}
		}

		if (best_match.is_null()) {
			return nullptr;
		}

		result->add_token_match(best_match);
	}

	return result;
}

Vector<Ref<FuzzySearchResultV2>> FuzzySearchV2::search_all(const String &p_query, const PackedStringArray &p_targets) {
	Vector<Ref<FuzzySearchResultV2>> res;

	// Just spit out the results list if no query is given.
	if (p_query.is_empty()) {
		for (int i = 0; (i < max_results) && (i < p_targets.size()); i++) {
			res.push_back(new_search_result(p_targets[i]));
		}

		return res;
	}

	bool case_sensitive = !p_query.is_lowercase();
	PackedStringArray query_parts = p_query.split(" ", false);

	for (const String &target : p_targets) {
		Ref<FuzzySearchResultV2> r = fuzzy_search(query_parts, target, case_sensitive);
		if (!r.is_null()) {
			res.append(r);
		}
	}

	return sort_and_filter(res);
}

void FuzzySearchV2::draw_matches(Tree *p_tree) {
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

	Vector2 margin_and_scroll_offset = -p_tree->get_scroll();
	margin_and_scroll_offset.x += p_tree->get_theme_constant("item_margin");
	margin_and_scroll_offset.y += font->get_string_size("A", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).y;

	Ref<Texture2D> icon = head->get_icon(0);
	if (icon.is_valid()) {
		margin_and_scroll_offset.x += icon->get_width();
	}

	while (head != nullptr && head->is_visible()) {
		Ref<FuzzySearchResultV2> fuzzy_search_result = head->get_metadata(0);
		if (fuzzy_search_result.is_valid()) {
			for (Ref<FuzzyTokenMatch> match : fuzzy_search_result->token_matches) {
				for (Vector2i substring : match->substrings) {
					String str_left_of_match = fuzzy_search_result->target.substr(0, substring.x);
					String match_str = fuzzy_search_result->target.substr(substring.x, substring.y);

					Vector2 position = font->get_string_size(str_left_of_match, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
					position.y = 0;
					position += p_tree->get_item_rect(head, 0).position;
					position += margin_and_scroll_offset;

					Vector2 size = font->get_string_size(match_str, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);

					p_tree->draw_rect(Rect2(position, size), Color(1, 1, 1, 0.07), true);
					p_tree->draw_rect(Rect2(position, size), Color(0.5, 0.7, 1.0, 0.4), false, 1);
				}
			}
		}

		head = head->get_next_visible();
	}
}
