// Aseprite FLIC Library
// Copyright (c) 2015 David Capello
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#include "flic.h"
#include "flic_details.h"

namespace flic {

template<typename Iterator>
static int count_consecutive_values(Iterator begin, Iterator end)
{
  Iterator prev = nullptr;
  int same = 0;
  for (Iterator it=begin; it!=end; prev=it, ++it) {
    if (!prev || *prev == *it)
      ++same;
    else
      break;
  }
  return same;
}

template<typename Iterator>
static int count_max_consecutive_values(Iterator begin, Iterator end, Iterator* maxStart)
{
  Iterator prev = nullptr;
  Iterator curStart = nullptr;
  *maxStart = nullptr;
  int max = 0;
  int same = 0;
  for (Iterator it=begin; it!=end; prev=it, ++it) {
    if (!prev || *prev == *it) {
      if (!curStart)
        curStart = it;

      ++same;
      if (max < same) {
        max = same;
        *maxStart = curStart;
      }
    }
    else {
      same = 0;
      curStart = nullptr;
    }
  }
  return max;
}

template<typename Iterator1, typename Iterator2>
static int count_max_consecutive_equal_values(Iterator1 begin1, Iterator1 end1,
                                              Iterator2 begin2, Iterator2 end2,
                                              Iterator2* maxStart)
{
  Iterator1 it1 = begin1;
  Iterator2 it2 = begin2;
  Iterator2 curStart = nullptr;
  *maxStart = nullptr;
  int max = 0;
  int same = 0;
  for (; it1!=end1 && it2!=end2; ++it1, ++it2) {
    if (*it1 == *it2) {
      if (!curStart)
        curStart = it2;

      ++same;
      if (max < same) {
        max = same;
        *maxStart = curStart;
      }
    }
    else {
      same = 0;
      curStart = nullptr;
    }
  }
  return max;
}

Encoder::Encoder(FileInterface* file)
  : m_file(file)
  , m_frameCount(0)
  , m_offsetFrame1(0)
  , m_offsetFrame2(0)
{
}

Encoder::~Encoder()
{
  // Fill header information
  if (m_file->ok()) {
    uint32_t size = m_file->tell();
    m_file->seek(0);

    write32(size);              // Write file size
    write16(FLC_MAGIC_NUMBER);  // Always as FLC file
    write16(m_frameCount);      // Number of frames

    m_file->seek(80);
    write32(m_offsetFrame1);
    write32(m_offsetFrame2);
  }
}

void Encoder::writeHeader(const Header& header)
{
  write32(0);                // File size, to be completed in ~Encoder()
  write16(0);                // File type
  write16(0);                // Number of frames
  write16(m_width = header.width);
  write16(m_height = header.height);
  write16(8);
  write16(0);                // Flags
  write32(header.speed);
  m_file->seek(128);
}

void Encoder::writeFrame(const Frame& frame)
{
  uint32_t frameStartPos = m_file->tell();
  int nchunks = 0;

  switch (m_frameCount) {
    case 0: m_offsetFrame1 = frameStartPos; break;
    case 1: m_offsetFrame2 = frameStartPos; break;
  }

  write32(0);           // Frame size will be written at the end of this function
  write16(0);           // Magic number
  write16(0);           // Number of chunks
  write32(0);           // Padding
  write32(0);

  if (m_frameCount == 0 || m_prevColormap != frame.colormap) {
    writeColorChunk(frame);
    ++nchunks;
  }

  if (m_frameCount == 0) {
    writeBrunChunk(frame);
    ++nchunks;

    // Create the buffer to store previous frame pixels
    m_prevFrameData.resize(m_height*frame.rowstride);
    std::copy(frame.pixels,
              frame.pixels+m_height*frame.rowstride,
              m_prevFrameData.begin());
  }
  else {
    writeLcChunk(frame);
    ++nchunks;
  }

  size_t frameEndPos = m_file->tell();
  m_file->seek(frameStartPos);
  write32(frameEndPos - frameStartPos); // Frame size
  write16(FLI_FRAME_MAGIC_NUMBER);      // Chunk type
  write16(nchunks);                     // Number of chunks

  m_file->seek(frameEndPos);
  ++m_frameCount;
}

void Encoder::writeRingFrame(const Frame& frame)
{
  writeFrame(frame);
  --m_frameCount;
}

void Encoder::writeColorChunk(const Frame& frame)
{
  // Chunk header
  size_t chunkBeginPos = m_file->tell();
  write32(0);           // Chunk size (this will be re-written below)
  write16(0);           // Chunk type
  write16(0);           // Write number of packets in this chunk

  // Write packets
  int npackets = 0;
  int skip = 0;
  for (int i=0; i<256; ) {
    if (m_frameCount == 0 ||
        m_prevColormap[i] != frame.colormap[i]) {
      int ncolors;
      if (m_frameCount == 0) {
        ncolors = 256;
      }
      else {
        ncolors = 1;
        for (int j=i+1; j<256; ++j) {
          if (m_prevColormap[j] != frame.colormap[j])
            ++ncolors;
        }
      }

      assert(ncolors > 0);

      ++npackets;
      m_file->write8(skip); // How many colors to skip from previous packet
      m_file->write8(ncolors == 256 ? 0: ncolors); // 0 means 256 colors

      // Write colors
      for (int j=i; j<ncolors; ++j) {
        const Color a = frame.colormap[j];
        m_file->write8(a.r);
        m_file->write8(a.g);
        m_file->write8(a.b);
      }

      i += ncolors;
      skip = 0;
    }
    else {
      ++skip;
      ++i;
    }
  }

  assert(npackets > 0);

  // Update chunk size
  size_t chunkEndPos = m_file->tell();
  m_file->seek(chunkBeginPos);

  if ((chunkEndPos - chunkBeginPos) & 1) // Avoid odd chunk size
    ++chunkEndPos;

  write32(chunkEndPos - chunkBeginPos); // Chunk size
  write16(FLI_COLOR_256_CHUNK);         // Chunk type
  write16(npackets);                    // Number of packets
  m_file->seek(chunkEndPos);

  m_prevColormap = frame.colormap;
}

void Encoder::writeBrunChunk(const Frame& frame)
{
  // Chunk header
  size_t chunkBeginPos = m_file->tell();
  write32(0);           // Chunk size (this will be re-written below)
  write16(FLI_BRUN_CHUNK);

  for (int y=0; y<m_height; ++y)
    writeBrunLineChunk(frame, y);

  // Update chunk size
  size_t chunkEndPos = m_file->tell();
  m_file->seek(chunkBeginPos);

  if ((chunkEndPos - chunkBeginPos) & 1) // Avoid odd chunk size
    ++chunkEndPos;

  write32(chunkEndPos - chunkBeginPos);
  m_file->seek(chunkEndPos);
}

void Encoder::writeBrunLineChunk(const Frame& frame, int y)
{
  size_t npacketsPos = m_file->tell();
  m_file->write8(0); // Number of packets, it will be re-written later

  // Number of packets
  int npackets = 0;

  uint8_t* it = frame.pixels + y*frame.rowstride;
  for (int x=0; x<m_width; ) {
    int remain = (m_width-x);
    uint8_t* maxSameStart = nullptr;

    int samePixels = count_consecutive_values(it, it+remain);
    int maxSamePixels = count_max_consecutive_values(it, it+remain, &maxSameStart);

    // We can compress 127 equal pixels in one packet
    if (samePixels > 127)
      samePixels = 127;

    if (samePixels >= 4) {
      // One packet to compress "samePixels"
      ++npackets;
      m_file->write8(samePixels);
      m_file->write8(*it);

      it += samePixels;
      x += samePixels;
    }
    else {
      // We can include 128 pixels in one packet
      if (remain > 128)
        remain = 128;

      // Is it better to reduce this packet just to compress future same pixels?
      if (maxSamePixels >= 4 && remain > (maxSameStart-it))
        remain = (maxSameStart-it);

      assert(remain > 0);

      ++npackets;
      m_file->write8(-remain);
      for (int i=0; i<remain; ++i, ++it)
        m_file->write8(*it);

      x += remain;
    }
  }

  size_t restorePos = m_file->tell();
  m_file->seek(npacketsPos);
  m_file->write8(npackets < 255 ? npackets: 255);
  m_file->seek(restorePos);
}

void Encoder::writeLcChunk(const Frame& frame)
{
  int skipLines = 0;
  for (int y=0; y<m_height; ++y) {
    std::vector<uint8_t>::iterator prevIt =
      m_prevFrameData.begin() + y*frame.rowstride;
    uint8_t* it = frame.pixels + y*frame.rowstride;

    for (int x=0; x<m_width; ++x, ++it, ++prevIt) {
      if (*prevIt != *it)
        goto firstScanDone;
    }

    ++skipLines;
  }

firstScanDone:;

  int skipEndLines = 0;
  for (int y=m_height-1; y > skipLines; --y) {
    std::vector<uint8_t>::iterator prevIt =
      m_prevFrameData.begin() + y*frame.rowstride;
    uint8_t* it = frame.pixels + y*frame.rowstride;

    for (int x=0; x<m_width; ++x, ++it, ++prevIt) {
      if (*prevIt != *it)
        goto secondScanDone;
    }

    ++skipEndLines;
  }

secondScanDone:;

  int nlines = (m_height - skipEndLines - skipLines);

  // Chunk header
  size_t chunkBeginPos = m_file->tell();
  write32(0);            // Chunk size (this will be re-written below)
  write16(FLI_LC_CHUNK);
  write16(skipLines);    // How many lines to skip
  write16(nlines);

  for (int y=skipLines; y<skipLines+nlines; ++y)
    writeLcLineChunk(frame, y);

  // Update the previous frame data
  if (nlines > 0)
    std::copy(frame.pixels+(skipLines*frame.rowstride),
              frame.pixels+((skipLines+nlines)*frame.rowstride),
              m_prevFrameData.begin()+(skipLines*frame.rowstride));

  // Update chunk size
  size_t chunkEndPos = m_file->tell();
  m_file->seek(chunkBeginPos);

  if ((chunkEndPos - chunkBeginPos) & 1) // Avoid odd chunk size
    ++chunkEndPos;

  write32(chunkEndPos - chunkBeginPos);
  m_file->seek(chunkEndPos);
}

void Encoder::writeLcLineChunk(const Frame& frame, int y)
{
  size_t npacketsPos = m_file->tell();
  m_file->write8(0); // Number of packets, it will be re-written later

  // Number of packets
  int npackets = 0;
  int skipPixels = 0;

  std::vector<uint8_t>::iterator prevIt =
    m_prevFrameData.begin() + y*frame.rowstride;
  uint8_t* it = frame.pixels + y*frame.rowstride;

  for (int x=0; x<m_width; ) {
    if (*prevIt != *it) {
      while (skipPixels > 255) {
        // One empty packet to skip 255 pixels that are equal to the previous frame
        ++npackets;
        m_file->write8(255);
        m_file->write8(0);

        skipPixels -= 255;
      }

      // New packet
      ++npackets;
      m_file->write8(skipPixels);

      int remain = (m_width-x);
      if (remain > 128)
        remain = 128;

      // Calculate if there is a strip of equal pixels with the
      // previous frame in the following pixels
      uint8_t* maxUnchangedStart = nullptr;
      int maxUnchangedPixels =
        count_max_consecutive_equal_values(prevIt, prevIt+remain,
                                           it, it+remain,
                                           &maxUnchangedStart);
      if (maxUnchangedPixels > 4 && remain > (maxUnchangedStart-it))
        remain = (maxUnchangedStart-it);

      // Check if we can create a compressed packet
      uint8_t* maxSameStart = nullptr;
      int samePixels = count_consecutive_values(it, it+remain);
      int maxSamePixels = count_max_consecutive_values(it, it+remain, &maxSameStart);

      // We can compress 128 equal pixels in one packet
      if (samePixels > 128)
        samePixels = 128;

      if (samePixels >= 4) {
        // One packet to compress "samePixels"
        m_file->write8(-samePixels);
        m_file->write8(*it);

        prevIt += samePixels;
        it += samePixels;
        x += samePixels;
      }
      else {
        // We can include 127 pixels in one packet
        if (remain > 127)
          remain = 127;

        // Is it better to reduce this packet just to compress future same pixels?
        if (maxSamePixels >= 4 && remain > (maxSameStart-it))
          remain = (maxSameStart-it);

        assert(remain > 0);

        m_file->write8(remain);
        for (int i=0; i<remain; ++i, ++it)
          m_file->write8(*it);

        prevIt += remain;
        x += remain;
      }

      skipPixels = 0;
    }
    else {
      ++skipPixels;
      ++prevIt;
      ++it;
      ++x;
    }
  }

  if (skipPixels != m_width) {
    assert(npackets != 0);

    size_t restorePos = m_file->tell();
    m_file->seek(npacketsPos);
    m_file->write8(npackets < 255 ? npackets: 255);
    m_file->seek(restorePos);
  }
  else {
    assert(npackets == 0);
  }
}

void Encoder::write16(uint16_t value)
{
  // Little endian
  m_file->write8(value & 0x00FF);
  m_file->write8((value & 0xFF00) >> 8);
}

void Encoder::write32(uint32_t value)
{
  // Little endian
  m_file->write8((int)value & 0x00FF);
  m_file->write8((int)((value & 0x0000FF00L) >> 8));
  m_file->write8((int)((value & 0x00FF0000L) >> 16));
  m_file->write8((int)((value & 0xFF000000L) >> 24));
}

} // namespace flic
