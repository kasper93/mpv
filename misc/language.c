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

#include "language.h"

#include <limits.h>
#include <stdint.h>

#include "common/common.h"
#include "misc/ctype.h"

bstr mp_lang_canonicalize(void *talloc_ctx, bstr lang);

int mp_match_lang(char **langs, const char *lang)
{
    if (!lang)
        return 0;

    void *ta_ctx = talloc_new(NULL);
    int lang_parts_n = 0;
    bstr *lang_parts = NULL;
    bstr rest = bstr0(lang);
    while (rest.len) {
        bstr s = bstr_split(rest, "-", &rest);
        MP_TARRAY_APPEND(ta_ctx, lang_parts, lang_parts_n, s);
    }

    int best_score = 0;
    if (!lang_parts_n)
        goto done;

    for (int idx = 0; langs && langs[idx]; idx++) {
        rest = bstr0(langs[idx]);
        int part = 0;
        int score = 0;
        while (rest.len) {
            bstr s = bstr_split(rest, "-", &rest);
            if (!part) {
                if (bstrcasecmp(mp_lang_canonicalize(ta_ctx, lang_parts[0]), mp_lang_canonicalize(ta_ctx, s)))
                    break;
                score = INT_MAX - idx;
                part++;
                continue;
            }

            if (part >= lang_parts_n)
                break;

            if (bstrcasecmp(lang_parts[part], s))
                score -= 1000;

            part++;
        }
        score -= (lang_parts_n - part) * 1000;
        best_score = MPMAX(best_score, score);
    }

done:
    talloc_free(ta_ctx);
    return best_score;
}

bstr mp_guess_lang_from_filename(bstr name, int *lang_start)
{
    name = bstr_strip(bstr_strip_ext(name));

    if (name.len < 2)
        return (bstr){0};

    int lang_length = 0;
    int i = name.len - 1;
    int suffixes_length = 0;

    char delimiter = '.';
    if (name.start[i] == ')') {
        delimiter = '(';
        i--;
    }
    if (name.start[i] == ']') {
        delimiter = '[';
        i--;
    }

    while (true) {
        while (i >= 0 && mp_isalpha(name.start[i])) {
            lang_length++;
            i--;
        }

        // According to
        // https://en.wikipedia.org/wiki/IETF_language_tag#Syntax_of_language_tags
        // subtags after the first are composed of 1 to 8 letters.
        if (lang_length < suffixes_length + 1 || lang_length > suffixes_length + 8)
            return (bstr){0};

        if (i >= 0 && name.start[i] == '-') {
            lang_length++;
            i--;
            suffixes_length = lang_length;
        } else {
            break;
        }
    }

    // The primary subtag can have 2 or 3 letters.
    if (lang_length < suffixes_length + 2 || lang_length > suffixes_length + 3 ||
        i <= 0 || name.start[i] != delimiter)
        return (bstr){0};

    if (lang_start)
        *lang_start = i;

    return (bstr){name.start + i + 1, lang_length};
}
