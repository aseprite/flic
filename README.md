# Aseprite FLIC Library

[![build](https://github.com/aseprite/flic/actions/workflows/build.yml/badge.svg)](https://github.com/aseprite/flic/actions/workflows/build.yml)
[![MIT Licensed](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.txt)

Library to read/write [Animator Pro FLI/FLC files](https://en.wikipedia.org/wiki/FLIC_(file_format)).
Tested with [libfuzzer](https://github.com/aseprite/fuzz).

## Read File Example

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

## Write File Example

```c++
#include "flic/flic.h"

#include <cstdio>
#include <vector>

void render_frame(const flic::Header& header, flic::Frame& frame,
                  const int frameNumber)
{
  // Palettes can change frame by frame
  for (int c=0; c<256; ++c)
    frame.colormap[c] = flic::Color(c, c, 255*frameNumber/header.frames);

  // Frame image
  for (int y=0; y<header.height; ++y)
    for (int x=0; x<header.width; ++x)
      frame.pixels[y*frame.rowstride + x] = ((x*y*frameNumber) % 256);
}

int main(int argc, char* argv[])
{
  if (argc < 2)
    return 1;

  FILE* f = std::fopen(argv[1], "wb");
  flic::StdioFileInterface file(f);
  flic::Encoder encoder(&file);
  flic::Header header;
  header.frames = 10;
  header.width = 32;
  header.height = 32;
  header.speed = 50;              // Speed is in milliseconds per frame
  encoder.writeHeader(header);

  std::vector<uint8_t> buffer(header.width * header.height);
  flic::Frame frame;
  frame.pixels = &buffer[0];
  frame.rowstride = header.width;

  for (int i=0; i<header.frames; ++i) {
    render_frame(header, frame, i);
    encoder.writeFrame(frame);
  }

  // Render first frame and call writeRingFrame()
  render_frame(header, frame, 0);
  encoder.writeRingFrame(frame);

  return 0;
}
```
