/**************************************************************************/
/*  test_string.h                                                         */
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

#ifndef TEST_FUZZY_SEARCH_H
#define TEST_FUZZY_SEARCH_H

#include "core/string/fuzzy_search.h"
#include "tests/test_macros.h"
#include <iostream>
#include <chrono>

namespace TestFuzzySearch {

struct FuzzySearchTestCase {
	String query;
	String targets;
	String expected;
};

double calculateMean(const Vector<int>& numbers) {
    double sum = 0.0;
    for(int num : numbers) {
        sum += num;
    }
    return sum / numbers.size();
}

// Function to calculate standard deviation
double calculateStdDev(const Vector<int>& numbers) {
    double mean = calculateMean(numbers);
    double variance = 0.0;

    for(int num : numbers) {
        variance += (num - mean) * (num - mean);
    }
    variance /= numbers.size();  // Population standard deviation formula
    return std::sqrt(variance);
}

auto load_test_cases() {
	Ref<FileAccess> tests = FileAccess::open(TestUtils::get_data_path("fuzzy_search/fuzzy_search_tests.txt"), FileAccess::READ);
	REQUIRE(!tests.is_null());

	Vector<FuzzySearchTestCase> test_cases;
	while(true) {
		auto line = tests->get_csv_line();
		if (line.is_empty() || line.size() != 3) {
			break;
		}
		test_cases.append({ line[0], line[1], line[2] });
	}
	return test_cases;
}

auto load_test_data(String dataset_path) {
	Ref<FileAccess> fp = FileAccess::open(TestUtils::get_data_path("fuzzy_search/" + dataset_path), FileAccess::READ);
	REQUIRE(!fp.is_null());
	auto lines = fp->get_as_utf8_string().split("\n");
	CHECK(lines.size() > 0);
	return lines;
}

auto get_top_result(String query, Vector<String> lines) {
	Vector<Ref<FuzzySearchResult>> res = FuzzySearch::search_all(query, lines);
	if(res.size() > 0) {
		return res[0]->target;
	}
	return String("<no result>");
}

auto bench(FuzzySearchTestCase test_case, int repeat) {
	auto lines = load_test_data(test_case.targets);
	Vector<String> all_lines;
	while (repeat-- > 0) {
		all_lines.append_array(lines);
	}

	Vector<int> results;
	String top_result;

	// run twice for a warmp up
	for(int i = 0; i < 2; i++) {
		results.clear();
		
		for(int j = 0; j < 10; j++) {
			auto start = std::chrono::high_resolution_clock::now();
			top_result = get_top_result(test_case.query, all_lines);
			auto end = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
			results.push_back(duration);
		}
	}

	MESSAGE(test_case.query, ",", test_case.targets, ",", calculateStdDev(results), ",", calculateMean(results), ",", top_result);
}

TEST_CASE("[Stress][FuzzySearch] Benchmark fuzzy search") {
	for (auto test_case : load_test_cases()) {
		bench(test_case, 100);
	}
}

TEST_CASE("[FuzzySearch] Test fuzzy search results") {
	for (auto test_case : load_test_cases()) {
		auto lines = load_test_data(test_case.targets);
		CHECK_EQ(get_top_result(test_case.query, lines), test_case.expected);
	}
}

} // namespace TestString

#endif // TEST_FUZZY_SEARCH_H
