#define DLL_EXPORT extern "C" __declspec(dllexport)

extern "C" BOOL WINAPI DllMain(const HINSTANCE /* thisDllModule */, const DWORD /* reason */, const LPVOID /*reserved*/) {
  return TRUE;
}

static bool LoadFile(LPTSTR filename, std::vector<uint8_t>* out) {
  FILE* file = nullptr;
  if (_wfopen_s(&file, filename, L"rb") != 0) {
    return false;
  }
  if (!file) {
    return false;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return false;
  }

  long size = ftell(file);
  // Avoid invalid file or directory.
  if (size >= LONG_MAX || size < 0) {
    fclose(file);
    return false;
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return false;
  }

  out->resize(size);
  size_t readsize = fread(out->data(), 1, size, file);
  if (fclose(file) != 0) {
    return false;
  }

  return readsize == static_cast<size_t>(size);
}

// decode JPEG XL file
static bool DecodeJpegXlOneShot(const uint8_t* jxl, size_t size, std::vector<uint8_t>* pixels, size_t* xsize, size_t* ysize, std::vector<uint8_t>* /* icc_profile */, bool thumbnail) {
  // Multi-threaded parallel runner.
  auto runner = JxlResizableParallelRunnerMake(nullptr);

  auto dec = JxlDecoderMake(nullptr);
  if (pixels != NULL) {
    if (JXL_DEC_SUCCESS != JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE)) {
      fprintf(stderr, "JxlDecoderSubscribeEvents failed\n");
      return false;
    }
  } else // just want the basic image info
  {
    if (JXL_DEC_SUCCESS != JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO)) {
      fprintf(stderr, "JxlDecoderSubscribeEvents failed\n");
      return false;
    }
  }

  if (JXL_DEC_SUCCESS != JxlDecoderSetParallelRunner(dec.get(), JxlResizableParallelRunner, runner.get())) {
    fprintf(stderr, "JxlDecoderSetParallelRunner failed\n");
    return false;
  }

  JxlBasicInfo info;
  JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};

  JxlDecoderSetInput(dec.get(), jxl, size);
  JxlDecoderCloseInput(dec.get());

  for (;;) {
    JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());

    if (status == JXL_DEC_ERROR) {
      fprintf(stderr, "Decoder error\n");
      return false;
    } else if (status == JXL_DEC_NEED_MORE_INPUT) {
      fprintf(stderr, "Error, already provided all input\n");
      return false;
    } else if (status == JXL_DEC_BASIC_INFO) {
      if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec.get(), &info)) {
        fprintf(stderr, "JxlDecoderGetBasicInfo failed\n");
        return false;
      }
      *xsize = info.xsize;
      *ysize = info.ysize;
      if (!thumbnail) {
        JxlResizableParallelRunnerSetThreads(runner.get(), JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize));
      } else
        JxlResizableParallelRunnerSetThreads(runner.get(), 1);
    } else if ((status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) && (pixels != NULL)) {
      size_t buffer_size;
      if (JXL_DEC_SUCCESS != JxlDecoderImageOutBufferSize(dec.get(), &format, &buffer_size)) {
        fprintf(stderr, "JxlDecoderImageOutBufferSize failed\n");
        return false;
      }
      constexpr size_t kBytesPerPixel = 4;
      if (*xsize == 0 || *ysize == 0 || *xsize > SIZE_MAX / *ysize || (*xsize * *ysize) > SIZE_MAX / kBytesPerPixel) {
        fprintf(stderr, "Invalid image dimensions %" PRIu64 " %" PRIu64 "\n", static_cast<uint64_t>(*xsize), static_cast<uint64_t>(*ysize));
        return false;
      }
      const size_t expected_buffer_size = *xsize * *ysize * kBytesPerPixel;
      if (buffer_size != expected_buffer_size) {
        fprintf(stderr, "Invalid out buffer size %" PRIu64 " %" PRIu64 "\n", static_cast<uint64_t>(buffer_size), static_cast<uint64_t>(expected_buffer_size));
        return false;
      }
      pixels->resize(buffer_size);
      void* pixels_buffer = pixels->data();
      size_t pixels_buffer_size = pixels->size();
      if (JXL_DEC_SUCCESS != JxlDecoderSetImageOutBuffer(dec.get(), &format, pixels_buffer, pixels_buffer_size)) {
        fprintf(stderr, "JxlDecoderSetImageOutBuffer failed\n");
        return false;
      }
    } else if (status == JXL_DEC_FULL_IMAGE) {
      // Nothing to do. Do not yet return. If the image is an animation, more
      // full frames may be decoded. This example only keeps the last one.
    } else if (status == JXL_DEC_SUCCESS) {
      // All decoding successfully finished.
      // It's not required to call JxlDecoderReleaseInput(dec.get()) here since
      // the decoder will be destroyed.
      return true;
    } else {
      fprintf(stderr, "Unknown decoder status\n");
      return false;
    }
  }
}

DLL_EXPORT BOOL DVP_Identify(const LPVIEWERPLUGININFO info) {
  info->dwFlags = DVPFIF_ExtensionsOnly | DVPFIF_NeedRandomSeek;

  // The version format: MAJOR.MINOR.BUILD.REVISION
  info->dwVersionHigh = MAKELONG(/* MINOR */ VERSION_MINOR, /* MAJOR */ VERSION_MAJOR);
  info->dwVersionLow = MAKELONG(/* REVISION */ 0, /* BUILD */ 0);

  info->lpszName = const_cast<wchar_t*>(L"JPEG XL");
  info->lpszHandleExts = const_cast<wchar_t*>(L".jxl");
  info->lpszDescription = const_cast<wchar_t*>(L"JPEG XL Viewer Plugin");
  info->lpszCopyright = const_cast<wchar_t*>(L"(c) Copyright 2024 Kuro68k");

  info->dwlMinFileSize = 100;
  info->dwlMaxFileSize = 0 /* infinite */;
  info->uiMajorFileType = DVPMajorType_Image;
  info->idPlugin = {0xfa959248, 0xab91, 0x4363, {0x88, 0xb8, 0x5, 0x9c, 0x5d, 0x84, 0x88, 0x24}}; // {FA959248-AB91-4363-88B8-059C5D848824}

  return TRUE;
}

DLL_EXPORT BOOL DVP_IdentifyFile(const HWND /* parentControlHandle */, const LPTSTR filePath, const LPVIEWERPLUGINFILEINFO fileInfo, const HANDLE /* abortEvent */) {
  std::vector<uint8_t> jxl;
  if (!LoadFile(filePath, &jxl)) {
    return FALSE;
  }

  size_t xsize = 0, ysize = 0;
  if (!DecodeJpegXlOneShot(jxl.data(), jxl.size(), NULL, &xsize, &ysize, NULL, false)) {
    return FALSE;
  }

  // Fill out file information and return success
  fileInfo->dwFlags = DVPFIF_CanReturnBitmap | DVPFIF_CanReturnViewer | DVPFIF_CanReturnThumbnail;
  fileInfo->wMajorType = DVPMajorType_Image;
  fileInfo->wMinorType = 0;
  fileInfo->szImageSize.cx = (LONG)xsize;
  fileInfo->szImageSize.cy = (LONG)ysize;
  fileInfo->iNumBits = 32;
  if (fileInfo->lpszInfo && fileInfo->cchInfoMax > 0) {
    (void)StringCchPrintf(fileInfo->lpszInfo, fileInfo->cchInfoMax, L"%ld x %ld JPEG XL Image", fileInfo->szImageSize.cx, fileInfo->szImageSize.cy);
  }

  return TRUE;
}

// Create a bitmap from a disk-based TGA file
DLL_EXPORT HBITMAP DVP_LoadBitmap(const HWND /* parentControlHandle */, LPWSTR filePath, LPVIEWERPLUGINFILEINFO /* lpVPFileInfo */, LPSIZE /* lpszDesiredSize */, HANDLE /* hAbortEvent */) {
  std::vector<uint8_t> jxl;
  if (!LoadFile(filePath, &jxl)) {
    fwprintf(stderr, L"couldn't load %s\n", filePath);
    return NULL;
  }

  std::vector<uint8_t> pixels;
  std::vector<uint8_t> icc_profile;
  size_t xsize = 0, ysize = 0;
  bool thumbnail = false;
  if (!DecodeJpegXlOneShot(jxl.data(), jxl.size(), &pixels, &xsize, &ysize, &icc_profile, thumbnail)) {
    fwprintf(stderr, L"Error while decoding %s\n", filePath);
    return NULL;
  }

  HBITMAP hBitmap = NULL;
  BITMAPINFOHEADER bmih;
  bmih.biSize = sizeof(BITMAPINFOHEADER);
  bmih.biWidth = (long)xsize;
  bmih.biHeight = (long)(0 - ysize);
  bmih.biPlanes = 1;
  bmih.biBitCount = 32;
  bmih.biCompression = BI_RGB;
  bmih.biSizeImage = 0;
  bmih.biXPelsPerMeter = 10;
  bmih.biYPelsPerMeter = 10;
  bmih.biClrUsed = 0;
  bmih.biClrImportant = 0;

  BITMAPINFO dbmi;
  ZeroMemory(&dbmi, sizeof(dbmi));
  dbmi.bmiHeader = bmih;
  dbmi.bmiColors->rgbBlue = 0;
  dbmi.bmiColors->rgbGreen = 0;
  dbmi.bmiColors->rgbRed = 0;
  dbmi.bmiColors->rgbReserved = 0;

  // convert RGBA to BGRA
  size_t count = xsize * ysize;
  uint8_t* p = pixels.data();
  for (size_t i = 0; i < count; i++) {
    uint8_t r = *p;
    uint8_t b = *(p + 2);
    *p = b;
    *(p + 2) = r;
    p += 4;
  }

  // create DIB
  HDC hdc = ::GetDC(NULL);
  hBitmap = CreateDIBitmap(hdc, &bmih, CBM_INIT, pixels.data(), &dbmi, DIB_RGB_COLORS);
  ::ReleaseDC(NULL, hdc);
  if (hBitmap == NULL) {
    fwprintf(stderr, L"Error while creating DIB section\n");
    return NULL;
  }

  return hBitmap;
}
