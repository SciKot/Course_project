#include "search_server.h"
#include "iterator_range.h"
#include "profile.h"

#include <algorithm>
#include <iterator>
#include <sstream>
#include <iostream>
#include <future>
#include <list>
#include <string_view>

// D (50'000) - max number of documents in base
// Dwords (1000) - max number of words in document
// U (15'000) - max number of unique words in all documents
// Q (500'000) - max number of queries
// Qwords (10) - max number of words in query
// W (100) - max number of chars in word

vector<string> SplitIntoWords(const string& line) {
  istringstream words_input(line);
  return { istream_iterator<string>(words_input), istream_iterator<string>() };
}

vector<string_view> SplitIntoWordsView(const string& s) {
  string_view str = s;
  vector <string_view> result;
  while (true) {
    size_t space = str.find(' ');
    if (space != 0) { result.push_back(str.substr(0, space)); }
    if (space == str.npos) {
      break;
    }
    else {
      str.remove_prefix(space + 1);
      if (str.empty()) { break; }
    }
  }
  return result;
}

vector<string_view> SplitIntoWordsView(const string& s, TotalDuration& dest) {
  ADD_DURATION(dest);
  string_view str = s;
  vector <string_view> result;
  while (true) {
    size_t space = str.find(' ');
    result.push_back(str.substr(0, space));
    if (space == str.npos) {
      break;
    }
    else {
      str.remove_prefix(space + 1);
    }
  }
  return result;
}

bool IsFirstDocMorePop(pair<size_t, size_t> lhs, pair<size_t, size_t> rhs) {
  int64_t lhs_docid = lhs.first;
  auto lhs_hit_count = lhs.second;
  int64_t rhs_docid = rhs.first;
  auto rhs_hit_count = rhs.second;
  return make_pair(lhs_hit_count, -lhs_docid) > make_pair(rhs_hit_count, -rhs_docid);
}

SearchServer::SearchServer(istream& document_input)
{
  UpdateDocumentBase(document_input);
}

void SearchServer::UpdateDocumentBase(istream& document_input) {
  mutex m_add;
  InvertedIndex new_index;
  vector<future<void>> futures;
  vector<string> doc_lines;
  for (string current_doc; getline(document_input, current_doc); ) {
    new_index.Add(current_doc, m_add);
  }

  if (mutexes_ref) {
    for (auto& m : *mutexes_ref) {
      m.lock();
    }
    index = move(new_index);
    for (auto& m : *mutexes_ref) {
      m.unlock();
    }
  }
  else {
    index = move(new_index);
  }
}

istream& ReadLine(istream& input, string& s, TotalDuration& dest) {
  ADD_DURATION(dest);
  return getline(input, s);
}

void LogOutRes(vector<pair<size_t, size_t>> output_results, string message = "") {
  for (auto p : output_results) {
    cout << "{" << p.first << ", " << p.second << "} ";
  }
  cout << message << '\n';
}

void SearchServer::AddQueriesStream(
  istream& query_input, ostream& search_results_output
) {

  const size_t MAX_DOCS_NUM = index.GetMaxDocsId();

  vector<future<string>> futures;
  vector<string> queries;
  for (string current_query; getline(query_input, current_query); ) {
    queries.push_back(move(current_query));
  }

  const size_t page_size = 1500;
  size_t pages_num = queries.size() / page_size + ((queries.size() % page_size) == 0 ? 0 : 1);
  vector<mutex> mutexes(pages_num);
  mutexes_ref = &mutexes;

  for (size_t counter = 0; counter < pages_num && counter < queries.size(); counter++) {
    auto It_begin = queries.begin() + page_size * counter;
    auto It_end = (queries.end() - (queries.begin() + page_size * counter) < page_size ?
      queries.end() : (queries.begin() + page_size * (counter + 1)));
    IteratorRange range(It_begin, It_end);
    InvertedIndex& index_ref = index;
    auto mutex_It = mutexes.begin() + counter;
    futures.push_back(async([range, MAX_DOCS_NUM, index_ref, mutex_It] {
      ostringstream os;
      vector<pair<size_t, size_t>> docid_count(MAX_DOCS_NUM);
      for (auto& current_query : range) {
        const auto words = SplitIntoWordsView(current_query);

        
        for (size_t i = 0; i < MAX_DOCS_NUM; i++) {
          docid_count[i] = pair(i, 0);
        }

        {
          for (const auto& word : words) {
            lock_guard<mutex> g(*mutex_It);
            for (const auto docid : index_ref.Lookup(string(word))) {
              docid_count[docid].second++;
            }
          }
        }

        {
          partial_sort(docid_count.begin(), Head(docid_count, 5).end(), docid_count.end(), IsFirstDocMorePop);
        }

        os << current_query << ':';
        for (auto [docid, hitcount] : Head(docid_count, 5)) {
          if (hitcount > 0) {
            os << " {"
              << "docid: " << docid << ", "
              << "hitcount: " << hitcount << '}';
          }
        }
        os << '\n';
      }
      return os.str();
      }));
  }

  for (auto& f : futures) {
      search_results_output << f.get();
  }

  mutexes_ref = nullptr;
}

void InvertedIndex::Add(const string& document, mutex& m_add) {
  const size_t docid = max_docs_id++;

  for (const auto word : SplitIntoWordsView(document)) {
    index[string(word)].push_back(docid);
  }
}

vector<size_t> InvertedIndex::Lookup(const string& word) const {
  if (auto it = index.find(word); it != index.end()) {
    return it->second;
  }
  else {
    return {};
  }
}