#pragma once

#include <string>
#include <vector>

#include <zlib.h>

namespace lob {

// Reads a .jsonl.gz recording line by line.
//
// If the recorder was killed hard, the file ends mid-stream. The reader then
// returns every complete line before the cut, drops the partial one, and sets
// truncated() instead of throwing.
class GzLineReader {
 public:
  explicit GzLineReader(const std::string& path);
  ~GzLineReader();
  GzLineReader(const GzLineReader&) = delete;
  GzLineReader& operator=(const GzLineReader&) = delete;

  // Next line without its '\n'. Returns false at end of file.
  bool next(std::string& line);
  bool truncated() const { return truncated_; }

 private:
  gzFile file_ = nullptr;
  std::vector<char> buf_;
  bool truncated_ = false;
};

}  // namespace lob
