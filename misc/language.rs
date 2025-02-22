/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

use std::ffi::CString;
use std::os::raw::c_void;

use isolang::Language;

extern "C" {
    fn ta_xmemdup(ctx: *mut c_void, p: *const c_void, size: usize) -> *mut c_void;
}

#[repr(C)]
pub struct BStr {
    ptr: *mut u8,
    len: usize,
}

#[no_mangle]
pub extern "C" fn mp_lang_canonicalize(talloc_ctx: *mut c_void, lang: BStr) -> BStr {
    let input_str = unsafe {
        let slice = std::slice::from_raw_parts(lang.ptr as *const u8, lang.len);
        match std::str::from_utf8(slice) {
            Ok(s) => s,
            Err(_) => {
                debug_assert!(false, "Invalid UTF-8 string");
                return BStr { ptr: std::ptr::null_mut(), len: 0 };
            }
        }
    };

    let canonical = Language::from_639_1(input_str)
        .or_else(|| Language::from_639_3(input_str))
        .map(|lang| lang.to_639_3())
        .unwrap_or(input_str);

    let result = canonical;

    let c_string = match CString::new(result) {
        Ok(cstr) => cstr,
        Err(_) => return BStr { ptr: std::ptr::null_mut(), len: 0 },
    };

    let bytes = c_string.into_bytes();
    let len = bytes.len();
    let ptr = unsafe {
        ta_xmemdup(talloc_ctx, bytes.as_ptr() as *const c_void, len) as *mut u8
    };

    BStr { ptr, len }
}
