/*
 * Copyright 2018 Nikolay Sivov
 * Copyright 2018 Zhiyi Zhang
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

#include "windef.h"
#include "winbase.h"
#include "pathcch.h"
#include "strsafe.h"

#include "wine/debug.h"
#include "wine/unicode.h"

WINE_DEFAULT_DEBUG_CHANNEL(path);

static SIZE_T strnlenW(const WCHAR *string, SIZE_T maxlen)
{
    SIZE_T i;

    for (i = 0; i < maxlen; i++)
        if (!string[i]) break;
    return i;
}

static BOOL is_prefixed_unc(const WCHAR *string)
{
    static const WCHAR prefixed_unc[] = {'\\', '\\', '?', '\\', 'U', 'N', 'C', '\\'};
    return !strncmpiW(string, prefixed_unc, ARRAY_SIZE(prefixed_unc));
}

static BOOL is_prefixed_disk(const WCHAR *string)
{
    static const WCHAR prefix[] = {'\\', '\\', '?', '\\'};
    return !strncmpW(string, prefix, ARRAY_SIZE(prefix)) && isalphaW(string[4]) && string[5] == ':';
}

HRESULT WINAPI PathCchAddBackslash(WCHAR *path, SIZE_T size)
{
    return PathCchAddBackslashEx(path, size, NULL, NULL);
}

HRESULT WINAPI PathCchAddBackslashEx(WCHAR *path, SIZE_T size, WCHAR **endptr, SIZE_T *remaining)
{
    BOOL needs_termination;
    SIZE_T length;

    TRACE("%s, %lu, %p, %p\n", debugstr_w(path), size, endptr, remaining);

    length = strlenW(path);
    needs_termination = size && length && path[length - 1] != '\\';

    if (length >= (needs_termination ? size - 1 : size))
    {
        if (endptr) *endptr = NULL;
        if (remaining) *remaining = 0;
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }

    if (!needs_termination)
    {
        if (endptr) *endptr = path + length;
        if (remaining) *remaining = size - length;
        return S_FALSE;
    }

    path[length++] = '\\';
    path[length] = 0;

    if (endptr) *endptr = path + length;
    if (remaining) *remaining = size - length;

    return S_OK;
}

HRESULT WINAPI PathCchAddExtension(WCHAR *path, SIZE_T size, const WCHAR *extension)
{
    const WCHAR *existing_extension, *next;
    SIZE_T path_length, extension_length, dot_length;
    BOOL has_dot;
    HRESULT hr;

    TRACE("%s %lu %s\n", wine_dbgstr_w(path), size, wine_dbgstr_w(extension));

    if (!path || !size || size > PATHCCH_MAX_CCH || !extension) return E_INVALIDARG;

    next = extension;
    while (*next)
    {
        if ((*next == '.' && next > extension) || *next == ' ' || *next == '\\') return E_INVALIDARG;
        next++;
    }

    has_dot = extension[0] == '.' ? TRUE : FALSE;

    hr = PathCchFindExtension(path, size, &existing_extension);
    if (FAILED(hr)) return hr;
    if (*existing_extension) return S_FALSE;

    path_length = strnlenW(path, size);
    dot_length = has_dot ? 0 : 1;
    extension_length = strlenW(extension);

    if (path_length + dot_length + extension_length + 1 > size) return STRSAFE_E_INSUFFICIENT_BUFFER;

    /* If extension is empty or only dot, return S_OK with path unchanged */
    if (!extension[0] || (extension[0] == '.' && !extension[1])) return S_OK;

    if (!has_dot)
    {
        path[path_length] = '.';
        path_length++;
    }

    strcpyW(path + path_length, extension);
    return S_OK;
}

HRESULT WINAPI PathCchFindExtension(const WCHAR *path, SIZE_T size, const WCHAR **extension)
{
    const WCHAR *lastpoint = NULL;
    SIZE_T counter = 0;

    TRACE("%s %lu %p\n", wine_dbgstr_w(path), size, extension);

    if (!path || !size || size > PATHCCH_MAX_CCH)
    {
        *extension = NULL;
        return E_INVALIDARG;
    }

    while (*path)
    {
        if (*path == '\\' || *path == ' ')
            lastpoint = NULL;
        else if (*path == '.')
            lastpoint = path;

        path++;
        counter++;
        if (counter == size || counter == PATHCCH_MAX_CCH)
        {
            *extension = NULL;
            return E_INVALIDARG;
        }
    }

    *extension = lastpoint ? lastpoint : path;
    return S_OK;
}

HRESULT WINAPI PathCchRemoveExtension(WCHAR *path, SIZE_T size)
{
    const WCHAR *extension;
    WCHAR *next;
    HRESULT hr;

    TRACE("%s %lu\n", wine_dbgstr_w(path), size);

    if (!path || !size || size > PATHCCH_MAX_CCH) return E_INVALIDARG;

    hr = PathCchFindExtension(path, size, &extension);
    if (FAILED(hr)) return hr;

    next = path + (extension - path);
    while (next - path < size && *next) *next++ = 0;

    return next == extension ? S_FALSE : S_OK;
}

HRESULT WINAPI PathCchRenameExtension(WCHAR *path, SIZE_T size, const WCHAR *extension)
{
    HRESULT hr;

    TRACE("%s %lu %s\n", wine_dbgstr_w(path), size, wine_dbgstr_w(extension));

    hr = PathCchRemoveExtension(path, size);
    if (FAILED(hr)) return hr;

    hr = PathCchAddExtension(path, size, extension);
    return FAILED(hr) ? hr : S_OK;
}

HRESULT WINAPI PathCchStripPrefix(WCHAR *path, SIZE_T size)
{
    TRACE("%s %lu\n", wine_dbgstr_w(path), size);

    if (!path || !size || size > PATHCCH_MAX_CCH) return E_INVALIDARG;

    if (is_prefixed_unc(path))
    {
        /* \\?\UNC\a -> \\a */
        if (size < strlenW(path + 8) + 3) return E_INVALIDARG;
        strcpyW(path + 2, path + 8);
        return S_OK;
    }
    else if (is_prefixed_disk(path))
    {
        /* \\?\C:\ -> C:\ */
        if (size < strlenW(path + 4) + 1) return E_INVALIDARG;
        strcpyW(path, path + 4);
        return S_OK;
    }
    else
        return S_FALSE;
}

BOOL WINAPI PathIsUNCEx(const WCHAR *path, const WCHAR **server)
{
    const WCHAR *result = NULL;

    TRACE("%s %p\n", wine_dbgstr_w(path), server);

    if (is_prefixed_unc(path))
        result = path + 8;
    else if (path[0] == '\\' && path[1] == '\\' && path[2] != '?')
        result = path + 2;

    if (server) *server = result;
    return result ? TRUE : FALSE;
}
