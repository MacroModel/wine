/*
 * Copyright 2009 Vincent Povirk
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <math.h>

#define COBJMACROS

#include "windef.h"
#include "objbase.h"
#include "wincodec.h"
#include "wine/test.h"

typedef struct bitmap_data {
    const WICPixelFormatGUID *format;
    UINT bpp;
    const BYTE *bits;
    UINT width;
    UINT height;
    double xres;
    double yres;
} bitmap_data;

typedef struct BitmapTestSrc {
    const IWICBitmapSourceVtbl *lpVtbl;
    LONG ref;
    const bitmap_data *data;
} BitmapTestSrc;

static HRESULT WINAPI BitmapTestSrc_QueryInterface(IWICBitmapSource *iface, REFIID iid,
    void **ppv)
{
    if (!ppv) return E_INVALIDARG;

    if (IsEqualIID(&IID_IUnknown, iid) ||
        IsEqualIID(&IID_IWICBitmapSource, iid))
        *ppv = iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI BitmapTestSrc_AddRef(IWICBitmapSource *iface)
{
    BitmapTestSrc *This = (BitmapTestSrc*)iface;
    ULONG ref = InterlockedIncrement(&This->ref);
    return ref;
}

static ULONG WINAPI BitmapTestSrc_Release(IWICBitmapSource *iface)
{
    BitmapTestSrc *This = (BitmapTestSrc*)iface;
    ULONG ref = InterlockedDecrement(&This->ref);
    return ref;
}

static HRESULT WINAPI BitmapTestSrc_GetSize(IWICBitmapSource *iface,
    UINT *puiWidth, UINT *puiHeight)
{
    BitmapTestSrc *This = (BitmapTestSrc*)iface;
    *puiWidth = This->data->width;
    *puiHeight = This->data->height;
    return S_OK;
}

static HRESULT WINAPI BitmapTestSrc_GetPixelFormat(IWICBitmapSource *iface,
    WICPixelFormatGUID *pPixelFormat)
{
    BitmapTestSrc *This = (BitmapTestSrc*)iface;
    memcpy(pPixelFormat, This->data->format, sizeof(GUID));
    return S_OK;
}

static HRESULT WINAPI BitmapTestSrc_GetResolution(IWICBitmapSource *iface,
    double *pDpiX, double *pDpiY)
{
    BitmapTestSrc *This = (BitmapTestSrc*)iface;
    *pDpiX = This->data->xres;
    *pDpiY = This->data->yres;
    return S_OK;
}

static HRESULT WINAPI BitmapTestSrc_CopyPalette(IWICBitmapSource *iface,
    IWICPalette *pIPalette)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI BitmapTestSrc_CopyPixels(IWICBitmapSource *iface,
    const WICRect *prc, UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer)
{
    BitmapTestSrc *This = (BitmapTestSrc*)iface;
    UINT bytesperrow;
    UINT srcstride;
    UINT row_offset;

    if (prc->X < 0 || prc->Y < 0 || prc->X+prc->Width > This->data->width || prc->Y+prc->Height > This->data->height)
        return E_INVALIDARG;

    bytesperrow = ((This->data->bpp * prc->Width)+7)/8;
    srcstride = ((This->data->bpp * This->data->width)+7)/8;

    if (cbStride < bytesperrow)
        return E_INVALIDARG;

    if ((cbStride * prc->Height) > cbBufferSize)
        return E_INVALIDARG;

    row_offset = prc->X * This->data->bpp;

    if (row_offset % 8 == 0)
    {
        UINT row;
        const BYTE *src;
        BYTE *dst;

        src = This->data->bits + (row_offset / 8) + prc->Y * srcstride;
        dst = pbBuffer;
        for (row=0; row < prc->Height; row++)
        {
            memcpy(dst, src, bytesperrow);
            src += srcstride;
            dst += cbStride;
        }
        return S_OK;
    }
    else
    {
        ok(0, "bitmap %p was asked to copy pixels not aligned on a byte boundary\n", iface);
        return E_FAIL;
    }
}

static const IWICBitmapSourceVtbl BitmapTestSrc_Vtbl = {
    BitmapTestSrc_QueryInterface,
    BitmapTestSrc_AddRef,
    BitmapTestSrc_Release,
    BitmapTestSrc_GetSize,
    BitmapTestSrc_GetPixelFormat,
    BitmapTestSrc_GetResolution,
    BitmapTestSrc_CopyPalette,
    BitmapTestSrc_CopyPixels
};

static void CreateTestBitmap(const bitmap_data *data, IWICBitmapSource **bitmap)
{
    BitmapTestSrc *This = HeapAlloc(GetProcessHeap(), 0, sizeof(BitmapTestSrc));

    if (This)
    {
        This->lpVtbl = &BitmapTestSrc_Vtbl;
        This->ref = 1;
        This->data = data;
        *bitmap = (IWICBitmapSource*)This;
    }
    else
        *bitmap = NULL;
}

static void DeleteTestBitmap(IWICBitmapSource *bitmap)
{
    BitmapTestSrc *This = (BitmapTestSrc*)bitmap;
    ok(This->lpVtbl == &BitmapTestSrc_Vtbl, "test bitmap %p deleted with incorrect vtable\n", bitmap);
    ok(This->ref == 1, "test bitmap %p deleted with %i references instead of 1\n", bitmap, This->ref);
    HeapFree(GetProcessHeap(), 0, This);
}

static void compare_bitmap_data(const struct bitmap_data *expect, IWICBitmapSource *source, const char *name)
{
    BYTE *converted_bits;
    UINT width, height;
    double xres, yres;
    WICRect prc;
    UINT stride, buffersize;
    GUID dst_pixelformat;
    HRESULT hr;

    hr = IWICBitmapSource_GetSize(source, &width, &height);
    ok(SUCCEEDED(hr), "GetSize(%s) failed, hr=%x\n", name, hr);
    ok(width == expect->width, "expecting %u, got %u (%s)\n", expect->width, width, name);
    ok(height == expect->height, "expecting %u, got %u (%s)\n", expect->height, height, name);

    hr = IWICBitmapSource_GetResolution(source, &xres, &yres);
    ok(SUCCEEDED(hr), "GetResolution(%s) failed, hr=%x\n", name, hr);
    ok(fabs(xres - expect->xres) < 0.02, "expecting %0.2f, got %0.2f (%s)\n", expect->xres, xres, name);
    ok(fabs(yres - expect->yres) < 0.02, "expecting %0.2f, got %0.2f (%s)\n", expect->yres, yres, name);

    hr = IWICBitmapSource_GetPixelFormat(source, &dst_pixelformat);
    ok(SUCCEEDED(hr), "GetPixelFormat(%s) failed, hr=%x\n", name, hr);
    ok(IsEqualGUID(&dst_pixelformat, expect->format), "got unexpected pixel format (%s)\n", name);

    prc.X = 0;
    prc.Y = 0;
    prc.Width = expect->width;
    prc.Height = expect->height;

    stride = (expect->bpp * expect->width + 7) / 8;
    buffersize = stride * expect->height;

    converted_bits = HeapAlloc(GetProcessHeap(), 0, buffersize);
    hr = IWICBitmapSource_CopyPixels(source, &prc, stride, buffersize, converted_bits);
    ok(SUCCEEDED(hr), "CopyPixels(%s) failed, hr=%x\n", name, hr);
    if (IsEqualGUID(expect->format, &GUID_WICPixelFormat32bppBGR))
    {
        /* ignore the padding byte when comparing data */
        UINT i;
        BOOL equal=TRUE;
        const DWORD *a=(const DWORD*)expect->bits, *b=(const DWORD*)converted_bits;
        for (i=0; i<(buffersize/4); i++)
            if ((a[i]&0xffffff) != (b[i]&0xffffff))
            {
                equal = FALSE;
                break;
            }
        ok(equal, "unexpected pixel data (%s)\n", name);
    }
    else
        ok(memcmp(expect->bits, converted_bits, buffersize) == 0, "unexpected pixel data (%s)\n", name);
    HeapFree(GetProcessHeap(), 0, converted_bits);
}

static const BYTE bits_32bppBGR[] = {
    255,0,0,80, 0,255,0,80, 0,0,255,80, 0,0,0,80,
    0,255,255,80, 255,0,255,80, 255,255,0,80, 255,255,255,80};
static const struct bitmap_data testdata_32bppBGR = {
    &GUID_WICPixelFormat32bppBGR, 32, bits_32bppBGR, 4, 2, 96.0, 96.0};

static const BYTE bits_32bppBGRA[] = {
    255,0,0,255, 0,255,0,255, 0,0,255,255, 0,0,0,255,
    0,255,255,255, 255,0,255,255, 255,255,0,255, 255,255,255,255};
static const struct bitmap_data testdata_32bppBGRA = {
    &GUID_WICPixelFormat32bppBGRA, 32, bits_32bppBGRA, 4, 2, 96.0, 96.0};

static void test_conversion(const struct bitmap_data *src, const struct bitmap_data *dst, const char *name, BOOL todo)
{
    IWICBitmapSource *src_bitmap, *dst_bitmap;
    HRESULT hr;

    CreateTestBitmap(src, &src_bitmap);

    hr = WICConvertBitmapSource(dst->format, src_bitmap, &dst_bitmap);
    if (todo)
        todo_wine ok(SUCCEEDED(hr), "WICConvertBitmapSource(%s) failed, hr=%x\n", name, hr);
    else
        ok(SUCCEEDED(hr), "WICConvertBitmapSource(%s) failed, hr=%x\n", name, hr);

    if (SUCCEEDED(hr))
    {
        compare_bitmap_data(dst, dst_bitmap, name);

        IWICBitmapSource_Release(dst_bitmap);
    }

    DeleteTestBitmap(src_bitmap);
}

static void test_invalid_conversion(void)
{
    IWICBitmapSource *src_bitmap, *dst_bitmap;
    HRESULT hr;

    CreateTestBitmap(&testdata_32bppBGRA, &src_bitmap);

    /* convert to a non-pixel-format GUID */
    hr = WICConvertBitmapSource(&GUID_VendorMicrosoft, src_bitmap, &dst_bitmap);
    ok(hr == WINCODEC_ERR_COMPONENTNOTFOUND, "WICConvertBitmapSource returned %x\n", hr);

    DeleteTestBitmap(src_bitmap);
}

static void test_default_converter(void)
{
    IWICBitmapSource *src_bitmap;
    IWICFormatConverter *converter;
    BOOL can_convert=1;
    HRESULT hr;

    CreateTestBitmap(&testdata_32bppBGRA, &src_bitmap);

    hr = CoCreateInstance(&CLSID_WICDefaultFormatConverter, NULL, CLSCTX_INPROC_SERVER,
        &IID_IWICFormatConverter, (void**)&converter);
    ok(SUCCEEDED(hr), "CoCreateInstance failed, hr=%x\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IWICFormatConverter_CanConvert(converter, &GUID_WICPixelFormat32bppBGRA,
            &GUID_WICPixelFormat32bppBGR, &can_convert);
        ok(SUCCEEDED(hr), "CanConvert returned %x\n", hr);
        ok(can_convert, "expected TRUE, got %i\n", can_convert);

        hr = IWICFormatConverter_Initialize(converter, src_bitmap,
            &GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, NULL, 0.0,
            WICBitmapPaletteTypeCustom);
        ok(SUCCEEDED(hr), "Initialize returned %x\n", hr);

        if (SUCCEEDED(hr))
            compare_bitmap_data(&testdata_32bppBGR, (IWICBitmapSource*)converter, "default converter");

        IWICFormatConverter_Release(converter);
    }

    DeleteTestBitmap(src_bitmap);
}

static void test_encoder(const struct bitmap_data *src, const CLSID* clsid_encoder,
    const struct bitmap_data *dst, const CLSID *clsid_decoder, const char *name)
{
    HRESULT hr;
    IWICBitmapEncoder *encoder;
    IWICBitmapSource *src_bitmap;
    HGLOBAL hglobal;
    IStream *stream;
    IWICBitmapFrameEncode *frameencode;
    IPropertyBag2 *options=NULL;
    IWICBitmapDecoder *decoder;
    IWICBitmapFrameDecode *framedecode;
    WICPixelFormatGUID pixelformat;

    CreateTestBitmap(src, &src_bitmap);

    hr = CoCreateInstance(clsid_encoder, NULL, CLSCTX_INPROC_SERVER,
        &IID_IWICBitmapEncoder, (void**)&encoder);
    ok(SUCCEEDED(hr), "CoCreateInstance failed, hr=%x\n", hr);
    if (SUCCEEDED(hr))
    {
        hglobal = GlobalAlloc(GMEM_MOVEABLE, 0);
        ok(hglobal != NULL, "GlobalAlloc failed\n");
        if (hglobal)
        {
            hr = CreateStreamOnHGlobal(hglobal, TRUE, &stream);
            ok(SUCCEEDED(hr), "CreateStreamOnHGlobal failed, hr=%x\n", hr);
        }

        if (hglobal && SUCCEEDED(hr))
        {
            hr = IWICBitmapEncoder_Initialize(encoder, stream, WICBitmapEncoderNoCache);
            ok(SUCCEEDED(hr), "Initialize failed, hr=%x\n", hr);

            hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frameencode, &options);
            ok(SUCCEEDED(hr), "CreateFrame failed, hr=%x\n", hr);
            if (SUCCEEDED(hr))
            {
                hr = IWICBitmapFrameEncode_Initialize(frameencode, options);
                ok(SUCCEEDED(hr), "Initialize failed, hr=%x\n", hr);

                memcpy(&pixelformat, src->format, sizeof(GUID));
                hr = IWICBitmapFrameEncode_SetPixelFormat(frameencode, &pixelformat);
                ok(SUCCEEDED(hr), "SetPixelFormat failed, hr=%x\n", hr);
                ok(IsEqualGUID(&pixelformat, src->format), "SetPixelFormat changed the format\n");

                hr = IWICBitmapFrameEncode_SetSize(frameencode, src->width, src->height);
                ok(SUCCEEDED(hr), "SetSize failed, hr=%x\n", hr);

                hr = IWICBitmapFrameEncode_WriteSource(frameencode, src_bitmap, NULL);
                ok(SUCCEEDED(hr), "WriteSource failed, hr=%x\n", hr);

                hr = IWICBitmapFrameEncode_Commit(frameencode);
                ok(SUCCEEDED(hr), "Commit failed, hr=%x\n", hr);

                hr = IWICBitmapEncoder_Commit(encoder);
                ok(SUCCEEDED(hr), "Commit failed, hr=%x\n", hr);

                IWICBitmapFrameEncode_Release(frameencode);
                IPropertyBag2_Release(options);
            }

            if (SUCCEEDED(hr))
            {
                hr = CoCreateInstance(clsid_decoder, NULL, CLSCTX_INPROC_SERVER,
                    &IID_IWICBitmapDecoder, (void**)&decoder);
                ok(SUCCEEDED(hr), "CoCreateInstance failed, hr=%x\n", hr);
            }

            if (SUCCEEDED(hr))
            {
                hr = IWICBitmapDecoder_Initialize(decoder, stream, WICDecodeMetadataCacheOnDemand);
                ok(SUCCEEDED(hr), "Initialize failed, hr=%x\n", hr);

                hr = IWICBitmapDecoder_GetFrame(decoder, 0, &framedecode);
                ok(SUCCEEDED(hr), "GetFrame failed, hr=%x\n", hr);

                if (SUCCEEDED(hr))
                {
                    compare_bitmap_data(dst, (IWICBitmapSource*)framedecode, name);

                    IWICBitmapFrameDecode_Release(framedecode);
                }

                IWICBitmapDecoder_Release(decoder);
            }

            IStream_Release(stream);
        }
    }

    DeleteTestBitmap(src_bitmap);
}

START_TEST(converter)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    test_conversion(&testdata_32bppBGRA, &testdata_32bppBGR, "BGRA -> BGR", 0);
    test_conversion(&testdata_32bppBGR, &testdata_32bppBGRA, "BGR -> BGRA", 0);
    test_conversion(&testdata_32bppBGRA, &testdata_32bppBGRA, "BGRA -> BGRA", 0);
    test_invalid_conversion();
    test_default_converter();

    test_encoder(&testdata_32bppBGR, &CLSID_WICBmpEncoder,
                 &testdata_32bppBGR, &CLSID_WICBmpDecoder, "BMP encoder 32bppBGR");

    CoUninitialize();
}
