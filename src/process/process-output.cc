// Copyright (c) 2025 R.J. (kencube@hotmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "process-output.h"

namespace process_lib {

ProcessLineOutputReader::ProcessLineOutputReader(size_t buffer_size)
: buffer_size_(buffer_size)
{
  buffer_ = std::unique_ptr<char[]>(new char[buffer_size_]);
}

void *ProcessLineOutputReader::allocateReadBuffer(size_t &buffer_size) {
  buffer_size = buffer_size_ - buf_offset_ - 1;
  return buffer_.get() + buf_offset_;
}

void ProcessLineOutputReader::commitReadBuffer(size_t bytes_transferred) {
  if (bytes_transferred == 0) {
    return;
  }

  // try to consume all lines
  const size_t buffer_size = buf_offset_ + bytes_transferred;

  char *ptr_start = buffer_.get();
  char *ptr_end = ptr_start;
  size_t remains = 0;
  int line_counts = 0;

  for (size_t n = 0; n < buffer_size; n++, ptr_end++) {
    if (*ptr_end == '\n') {
      size_t charcount = ptr_end - ptr_start; // exclude \n
      bool cr_flag = (charcount > 0 && *(ptr_end - 1) == '\r');
      if (cr_flag) { // exclude \r
        charcount--;
      }

      if (charcount > 0) {
        // make it null-terminated
        *ptr_end = 0;
        if (cr_flag) {
          *(ptr_end - 1) = 0;
        }
        onLineReceived(ptr_start, charcount, true);
      }

      ptr_start = ptr_end + 1;
      remains = 0;
      line_counts++;
    } else {
      remains++;
    }
  }

  if (remains > 0) {
    // move incompleted line to offset 0
    if (line_counts > 0) {
      memmove(buffer_.get(), ptr_start, remains);
      buf_offset_ = remains;
    } else {
      // no line found
      // remains == buffer_size
      if (buffer_size >= buffer_size_ / 2) {
        // but buffer is to big
        // treat it a newline
        // make it null-terminated
        *(ptr_start + buffer_size) = 0;
        onLineReceived(ptr_start, buffer_size, false);
        buf_offset_ = 0;
      } else {
        buf_offset_ = buffer_size;
      }
    }
  } else {
    // all buffer parsed, reset buffer offset
    buf_offset_ = 0;
  }
}

} // namespace process_lib
