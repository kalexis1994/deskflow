/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsImage.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace deskflow::platform::image {

namespace {

//! COM for this thread while converting; a thread that already has it keeps its own
class ComScope
{
public:
  ComScope() : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
  {
  }
  ~ComScope()
  {
    if (SUCCEEDED(m_result))
      CoUninitialize();
  }
  ComScope(const ComScope &) = delete;
  ComScope &operator=(const ComScope &) = delete;

private:
  HRESULT m_result;
};

ComPtr<IWICImagingFactory> factory()
{
  ComPtr<IWICImagingFactory> result;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&result))))
    return nullptr;
  return result;
}

uint32_t rowBytes(uint32_t width, uint32_t bitsPerPixel)
{
  return ((width * bitsPerPixel + 31) / 32) * 4;
}

} // namespace

std::optional<std::string> bitmapToPng(std::string_view bitmap)
{
  BITMAPINFOHEADER header;
  if (bitmap.size() < sizeof(header))
    return std::nullopt;
  std::memcpy(&header, bitmap.data(), sizeof(header));
  if (header.biSize < sizeof(header) || header.biPlanes != 1 || header.biCompression != BI_RGB ||
      (header.biBitCount != 24 && header.biBitCount != 32) || header.biWidth <= 0 || header.biHeight == 0)
    return std::nullopt;

  const auto width = static_cast<uint32_t>(header.biWidth);
  const bool bottomUp = header.biHeight > 0;
  const auto height = static_cast<uint32_t>(bottomUp ? header.biHeight : -static_cast<int64_t>(header.biHeight));
  if (static_cast<unsigned long long>(width) * height > kMaxPixels)
    return std::nullopt;

  const auto stride = rowBytes(width, header.biBitCount);
  const std::size_t pixelsAt = header.biSize + std::size_t{header.biClrUsed} * sizeof(RGBQUAD);
  const std::size_t pixelBytes = std::size_t{stride} * height;
  if (bitmap.size() < pixelsAt || bitmap.size() - pixelsAt < pixelBytes)
    return std::nullopt;

  // WIC wants the rows top first
  std::vector<BYTE> rows(pixelBytes);
  const auto *source = reinterpret_cast<const BYTE *>(bitmap.data() + pixelsAt);
  for (uint32_t y = 0; y < height; ++y) {
    const auto from = bottomUp ? height - 1 - y : y;
    std::memcpy(rows.data() + std::size_t{y} * stride, source + std::size_t{from} * stride, stride);
  }

  ComScope com;
  const auto wic = factory();
  if (!wic)
    return std::nullopt;

  WICPixelFormatGUID format = header.biBitCount == 32 ? GUID_WICPixelFormat32bppBGR : GUID_WICPixelFormat24bppBGR;
  ComPtr<IWICBitmap> wicBitmap;
  ComPtr<IStream> stream;
  ComPtr<IWICBitmapEncoder> encoder;
  ComPtr<IWICBitmapFrameEncode> frame;
  if (FAILED(wic->CreateBitmapFromMemory(
          width, height, format, stride, static_cast<UINT>(rows.size()), rows.data(), &wicBitmap
      )) ||
      FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
      FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
      FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
      FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
      FAILED(frame->SetSize(width, height)) || FAILED(frame->SetPixelFormat(&format)) ||
      FAILED(frame->WriteSource(wicBitmap.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit()))
    return std::nullopt;

  STATSTG stat = {};
  HGLOBAL memory = nullptr;
  if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || FAILED(GetHGlobalFromStream(stream.Get(), &memory)))
    return std::nullopt;
  const auto *bytes = static_cast<const char *>(GlobalLock(memory));
  if (bytes == nullptr)
    return std::nullopt;
  std::string png(bytes, static_cast<std::size_t>(stat.cbSize.QuadPart));
  GlobalUnlock(memory);
  return png;
}

std::optional<std::string> pngToBitmap(std::string_view png)
{
  if (png.empty() || png.size() > MAXDWORD)
    return std::nullopt;

  ComScope com;
  const auto wic = factory();
  if (!wic)
    return std::nullopt;

  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> converter;
  // WIC only reads the buffer, whatever the signature says
  auto *data = reinterpret_cast<BYTE *>(const_cast<char *>(png.data()));
  if (FAILED(wic->CreateStream(&stream)) ||
      FAILED(stream->InitializeFromMemory(data, static_cast<DWORD>(png.size()))) ||
      FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
      FAILED(decoder->GetFrame(0, &frame)) || FAILED(wic->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(
          frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom
      )))
    return std::nullopt;

  UINT width = 0;
  UINT height = 0;
  if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0 ||
      static_cast<unsigned long long>(width) * height > kMaxPixels)
    return std::nullopt;

  const UINT stride = width * 4;
  std::vector<BYTE> pixels(std::size_t{stride} * height);
  if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data())))
    return std::nullopt;

  // over white, opaque: a bitmap's fourth byte means nothing to those who paste it
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    const unsigned alpha = pixels[i + 3];
    if (alpha == 255)
      continue;
    for (std::size_t c = 0; c < 3; ++c)
      pixels[i + c] = static_cast<BYTE>((pixels[i + c] * alpha + 255 * (255 - alpha)) / 255);
    pixels[i + 3] = 255;
  }

  BITMAPINFOHEADER header = {};
  header.biSize = sizeof(header);
  header.biWidth = static_cast<LONG>(width);
  header.biHeight = static_cast<LONG>(height); // bottom-up, as most programs expect
  header.biPlanes = 1;
  header.biBitCount = 32;
  header.biCompression = BI_RGB;
  header.biSizeImage = static_cast<DWORD>(pixels.size());

  std::string bitmap(reinterpret_cast<const char *>(&header), sizeof(header));
  bitmap.reserve(sizeof(header) + pixels.size());
  for (UINT y = height; y-- > 0;)
    bitmap.append(reinterpret_cast<const char *>(pixels.data()) + std::size_t{y} * stride, stride);
  return bitmap;
}

} // namespace deskflow::platform::image
