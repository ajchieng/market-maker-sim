#include "lob/gz_lines.hpp"

#include <cstring>
#include <stdexcept>

namespace lob {

GzLineReader::GzLineReader(const std::string& path) : buf_(1 << 16) {
  file_ = gzopen(path.c_str(), "rb");
  if (file_ == nullptr) throw std::runtime_error("cannot open " + path);
  gzbuffer(file_, 1 << 18);
}

GzLineReader::~GzLineReader() {
  if (file_ != nullptr) gzclose(file_);
}

bool GzLineReader::next(std::string& line) {
  line.clear();
  while (true) {
    if (gzgets(file_, buf_.data(), static_cast<int>(buf_.size())) == nullptr) {
      int err = Z_OK;
      gzerror(file_, &err);
      if (err != Z_OK) {  // cut off mid-stream: drop the partial line
        truncated_ = true;
        line.clear();
        return false;
      }
      return !line.empty();  // clean EOF; last line may lack a '\n'
    }
    const std::size_t n = std::strlen(buf_.data());
    line.append(buf_.data(), n);
    if (n > 0 && line.back() == '\n') {
      line.pop_back();
      return true;
    }
    // Line longer than the buffer (or EOF without '\n'): keep reading.
  }
}

}  // namespace lob
