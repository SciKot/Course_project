#pragma once

#include <istream>
#include <ostream>
#include <set>
#include <list>
#include <vector>
#include <map>
#include <string>
#include <mutex>
using namespace std;

vector<string> SplitIntoWords(const string& line);
vector<string_view> SplitIntoWordsView(const string& s);

class InvertedIndex {
public:
  void Add(const string& document, mutex& m_add);
  vector<size_t> Lookup(const string& word) const;

  size_t GetMaxDocsId() const { return max_docs_id; }

private:
  map<string, vector<size_t>> index;
  size_t max_docs_id = 0;
};

class SearchServer {
public:
  SearchServer() = default;
  explicit SearchServer(istream& document_input);
  void UpdateDocumentBase(istream& document_input);
  void AddQueriesStream(istream& query_input, ostream& search_results_output);
private:
  InvertedIndex index;
  vector<mutex>* mutexes_ref = nullptr;
};
