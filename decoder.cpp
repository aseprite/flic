// Aseprite FLIC Library
// Copyright (c) 2019-2020 Igara Studio S.A.
// Copyright (c) 2015 David Capello
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#include "flic.h"
#include "flic_details.h"

#undef assert
#define assert(...)

namespace flic {

Decoder::Decoder(FileInterface* file)
  : m_file(file)
  , m_frameCount(0)
  , m_offsetFrame1(0)
  , m_offsetFrame2(0)
{
}

bool Decoder::readHeader(Header& header)
{
  read32(); // file size
  uint16_t magic = read16();

  assert(magic == FLI_MAGIC_NUMBER || magic == FLC_MAGIC_NUMBER);
  if (magic != FLI_MAGIC_NUMBER &&
      magic != FLC_MAGIC_NUMBER)
    return false;

  header.frames = read16();
  header.width  = read16();
  header.height = read16();
  read16();                     // Color depth (it is interpreted as 8bpp anyway)
  read16();                     // Skip flags
  header.speed = read32();
  if (magic == FLI_MAGIC_NUMBER) {
    if (header.speed == 0)
      header.speed = 70;
    else
      header.speed = 1000 * header.speed / 70;
  }

  if (magic == FLC_MAGIC_NUMBER) {
    // Offset to the first and second frame
    m_file->seek(80);
    m_offsetFrame1 = read32();
    m_offsetFrame2 = read32();
  }

  if (header.width == 0) header.width = 320;
  if (header.height == 0) header.height = 200;

  m_width = header.width;
  m_height = header.height;

  // Skip padding
  m_file->seek(128);
  return true;
}

bool Decoder::readFrame(Frame& frame)
{
  switch (m_frameCount) {
    case 0:
      if (m_offsetFrame1)
        m_file->seek(m_offsetFrame1);
      break;
    case 1:
      if (m_offsetFrame2)
        m_file->seek(m_offsetFrame2);
      break;
  }

  uint32_t frameStartPos = m_file->tell();
  uint32_t frameSize = read32();
  uint16_t magic = read16();
  assert(magic == FLI_FRAME_MAGIC_NUMBER);
  (void)magic;

  uint16_t chunks = read16();
  for (int i=0; i<8; ++i)       // Padding
    m_file->read8();

  for (uint16_t i=0; i!=chunks; ++i)
    readChunk(frame);

  m_file->seek(frameStartPos+frameSize);
  ++m_frameCount;
  return true;
}

void Decoder::readChunk(Frame& frame)
{
  uint32_t chunkStartPos = m_file->tell();
  uint32_t chunkSize = read32();
  uint16_t type = read16();

  switch (type) {
    case FLI_COLOR_256_CHUNK: readColorChunk(frame, false); break;
    case FLI_DELTA_CHUNK:     readDeltaChunk(frame);        break;
    case FLI_COLOR_64_CHUNK:  readColorChunk(frame, true);  break;
    case FLI_LC_CHUNK:        readLcChunk(frame);           break;
    case FLI_BLACK_CHUNK:     readBlackChunk(frame);        break;
    case FLI_BRUN_CHUNK:      readBrunChunk(frame);         break;
    case FLI_COPY_CHUNK:      readCopyChunk(frame);         break;
    default:
      // Ignore all other kind of chunks
      break;
  }

  m_file->seek(chunkStartPos+chunkSize);
}

void Decoder::readBlackChunk(Frame& frame)
{
  std::fill(frame.pixels,
            frame.pixels+frame.rowstride*m_height, 0);
}

void Decoder::readCopyChunk(Frame& frame)
{
  assert(m_width == 320 && m_height == 200);
  if (m_width == 320 && m_height == 200) {
    for (int y=0; y<200; ++y) {
      uint8_t* it = frame.pixels + y*frame.rowstride;
      for (int x=0; x<320; ++x, ++it)
        *it = m_file->read8();
    }
  }
}

void Decoder::readColorChunk(Frame& frame, bool oldColorChunk)
{
  int npackets = read16();

  // For each packet
  int i = 0;
  while (npackets--) {
    i += m_file->read8();       // Colors to skip

    int colors = m_file->read8();
    if (colors == 0)
      colors = 256;

    for (int j=0; j<colors
           // If i+j >= 256 it means that the color chunk is invalid,
           // we check this to avoid an buffer overflow of frame.colormap[]
           && i+j < 256;
         ++j) {
      Color& color = frame.colormap[i+j];
      color.r = m_file->read8();
      color.g = m_file->read8();
      color.b = m_file->read8();
      if (oldColorChunk) {
        color.r = 255 * int(color.r) / 63;
        color.g = 255 * int(color.g) / 63;
        color.b = 255 * int(color.b) / 63;
      }
    }
  }
}

void Decoder::readBrunChunk(Frame& frame)
{
  for (int y=0; y<m_height; ++y) {
    uint8_t* it = frame.pixels+frame.rowstride*y;
    int x = 0;
    int npackets = m_file->read8(); // Use the number of packet to check integrity
    (void)npackets; // to be ignored according to latest standard (holdover from the FLI format)
    while (m_file->ok() && x < m_width) {
      int count = int(int8_t(m_file->read8()));
      if (count >= 0) {
        uint8_t color = m_file->read8();
        while (count-- != 0 && x < m_width) {
          *it = color;
          ++it;
          ++x;
        }
      }
      else {
        while (count++ != 0 && x < m_width) {
          *it = m_file->read8();
          ++it;
          ++x;
        }
      }
    }
  }
}

void Decoder::readLcChunk(Frame& frame)
{
  int skipLines = read16();
  int nlines = read16();

  for (int y=skipLines; y<skipLines+nlines; ++y) {
    // Break in case of invalid data
    if (y < 0 || y >= m_height)
      break;

    uint8_t* it = frame.pixels+frame.rowstride*y;
    int x = 0;
    int npackets = m_file->read8();
    while (npackets-- && x < m_width) {
      int skip = m_file->read8();

      x += skip;
      it += skip;

      int count = int(int8_t(m_file->read8()));
      if (count >= 0) {
        uint8_t* end = frame.pixels+frame.rowstride*m_height;
        while (count-- != 0 && it < end) {
          *it = m_file->read8();
          ++it;
          ++x;
        }
        // Broken file? More bytes than available buffer
        if (it == end)
          return;
      }
      else {
        uint8_t color = m_file->read8();
        while (count++ != 0 && x < m_width) {
          *it = color;
          ++it;
          ++x;
        }
      }
    }
  }
}

void Decoder::readDeltaChunk(Frame& frame)
{
  int nlines = read16();
  int y = 0;
  while (nlines-- != 0) {
    int npackets = 0;

    while (m_file->ok()) {
      int16_t word = read16();
      if (word < 0) {          // Has bit 15 (0x8000)
        if (word & 0x4000) {   // Has bit 14 (0x4000)
          y += -word;          // Skip lines
        }
        // Only last pixel has changed
        else {
          assert(y >= 0 && y < m_height);
          if (y >= 0 && y < m_height) {
            uint8_t* it = frame.pixels + y*frame.rowstride + m_width - 1;
            *it = (word & 0xff);
          }
          ++y;
          if (nlines-- == 0)
            return;             // We are done
        }
      }
      else {
        npackets = word;
        break;
      }
    }

    // Avoid invalid data to skip more lines than the availables.
    if (y >= m_height)
      break;

    int x = 0;
    while (npackets-- != 0) {
      x += m_file->read8();           // Skip pixels
      int8_t count = m_file->read8(); // Number of words

      assert(y >= 0 && y < m_height && x >= 0 && x < m_width);
      uint8_t* it = frame.pixels + y*frame.rowstride + x;

      if (count >= 0) {
        while (count-- != 0 && x < m_width) {
          int color1 = m_file->read8();
          int color2 = m_file->read8();

          *it = color1;
          ++it;
          ++x;

          if (x < m_width) {
            *it = color2;
            ++it;
            ++x;
          }
        }
      }
      else {
        int color1 = m_file->read8();
        int color2 = m_file->read8();

        while (count++ != 0 && x < m_width) {
          *it = color1;
          ++it;
          ++x;

          if (x < m_width) {
            *it = color2;
            ++it;
            ++x;
          }
        }
      }
    }

    ++y;
  }
}

uint16_t Decoder::read16()
{
  int b1 = m_file->read8();
  int b2 = m_file->read8();

  if (m_file->ok()) {
    return ((b2 << 8) | b1); // Little endian
  }
  else
    return 0;
}

uint32_t Decoder::read32()
{
  int b1 = m_file->read8();
  int b2 = m_file->read8();
  int b3 = m_file->read8();
  int b4 = m_file->read8();

  if (m_file->ok()) {
    // Little endian
    return ((b4 << 24) | (b3 << 16) | (b2 << 8) | b1);
  }
  else
    return 0;
}

} // namespace flic
