# Aseprite FLIC Library

> Distributed under [MIT license](LICENSE.txt)

Library to read/write Animator Pro FLI/FLC files.

Example:

```c++
#include "flic/flic.h"

#include <cstdio>
#include <vector>

int main(int argc, char* argv[])
{
  if (argc < 2)
    return 1;

  FILE* f = std::fopen(argv[1], "rb");
  flic::StdioFileInterface file(f);
  flic::Decoder decoder(&file);
  flic::Header header;
  if (!decoder.readHeader(header))
    return 2;

  std::vector<uint8_t> buffer(header.width * header.height);
  flic::Frame frame;
  frame.pixels = &buffer[0];
  frame.rowstride = header.width;

  for (int i=0; i<header.frames; ++i) {
    if (!decoder.readFrame(frame))
      return 3;
  }

  return 0;
}
```
