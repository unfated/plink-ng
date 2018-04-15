// This file is part of PLINK 2.00, copyright (C) 2005-2018 Shaun Purcell,
// Christopher Chang.
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.


#include "plink2_filter.h"
#include "plink2_random.h"
#include "plink2_stats.h"  // HweThresh(), etc.

#ifdef __cplusplus
namespace plink2 {
#endif

PglErr FromToFlag(const char* const* variant_ids, const uint32_t* variant_id_htable, const char* varid_from, const char* varid_to, uint32_t raw_variant_ct, uintptr_t max_variant_id_slen, uintptr_t variant_id_htable_size, uintptr_t* variant_include, ChrInfo* cip, uint32_t* variant_ct_ptr) {
  PglErr reterr = kPglRetSuccess;
  {
    if (!(*variant_ct_ptr)) {
      goto FromToFlag_ret_1;
    }
    const uint32_t* htable_dup_base = &(variant_id_htable[RoundUpPow2(variant_id_htable_size, kInt32PerCacheline)]);
    uint32_t chr_fo_idx = UINT32_MAX;
    uint32_t variant_uidx_start = UINT32_MAX;
    if (varid_from) {
      uint32_t cur_llidx;
      variant_uidx_start = VariantIdDupHtableFind(varid_from, variant_ids, variant_id_htable, htable_dup_base, strlen(varid_from), variant_id_htable_size, max_variant_id_slen, &cur_llidx);
      if (variant_uidx_start == UINT32_MAX) {
        snprintf(g_logbuf, kLogbufSize, "Error: --from variant '%s' not found.\n", varid_from);
        goto FromToFlag_ret_INCONSISTENT_INPUT_WW;
      }
      // do *not* check variant_include here.  variant ID uniqueness should not
      // be dependent on the order in which filters are applied.
      if (cur_llidx != UINT32_MAX) {
        snprintf(g_logbuf, kLogbufSize, "Error: --from variant ID '%s' appears multiple times.\n", varid_from);
        goto FromToFlag_ret_INCONSISTENT_INPUT_WW;
      }
      chr_fo_idx = GetVariantChrFoIdx(cip, variant_uidx_start);
    }
    uint32_t variant_uidx_end = 0;
    if (varid_to) {
      uint32_t cur_llidx;
      variant_uidx_end = VariantIdDupHtableFind(varid_to, variant_ids, variant_id_htable, htable_dup_base, strlen(varid_to), variant_id_htable_size, max_variant_id_slen, &cur_llidx);
      if (variant_uidx_end == UINT32_MAX) {
        snprintf(g_logbuf, kLogbufSize, "Error: --to variant '%s' not found.\n", varid_to);
        goto FromToFlag_ret_INCONSISTENT_INPUT_WW;
      }
      if (cur_llidx != UINT32_MAX) {
        snprintf(g_logbuf, kLogbufSize, "Error: --to variant ID '%s' appears multiple times.\n", varid_to);
        goto FromToFlag_ret_INCONSISTENT_INPUT_WW;
      }
      uint32_t chr_fo_idx2 = GetVariantChrFoIdx(cip, variant_uidx_end);
      if (variant_uidx_start == UINT32_MAX) {
        chr_fo_idx = chr_fo_idx2;
        variant_uidx_start = cip->chr_fo_vidx_start[chr_fo_idx];
      } else {
        if (chr_fo_idx != chr_fo_idx2) {
          logerrputs("Error: --from and --to variants are not on the same chromosome.\n");
          goto FromToFlag_ret_INCONSISTENT_INPUT;
        }
        if (variant_uidx_start > variant_uidx_end) {
          // permit order to be reversed
          uint32_t uii = variant_uidx_start;
          variant_uidx_start = variant_uidx_end;
          variant_uidx_end = uii;
        }
      }
      ++variant_uidx_end;  // convert to half-open interval
    } else {
      variant_uidx_end = cip->chr_fo_vidx_start[chr_fo_idx + 1];
    }
    if (variant_uidx_start) {
      ClearBitsNz(0, variant_uidx_start, variant_include);
    }
    if (variant_uidx_end < raw_variant_ct) {
      ClearBitsNz(variant_uidx_end, raw_variant_ct, variant_include);
    }
    ZeroWArr(kChrMaskWords, cip->chr_mask);
    SetBit(cip->chr_file_order[chr_fo_idx], cip->chr_mask);
    const uint32_t new_variant_ct = PopcountBitRange(variant_include, variant_uidx_start, variant_uidx_end);
    logprintf("--from/--to: %u variant%s remaining.\n", new_variant_ct, (new_variant_ct == 1)? "" : "s");
    *variant_ct_ptr = new_variant_ct;
  }
  while (0) {
  FromToFlag_ret_INCONSISTENT_INPUT_WW:
    WordWrapB(0);
    logerrputsb();
  FromToFlag_ret_INCONSISTENT_INPUT:
    reterr = kPglRetInconsistentInput;
    break;
  }
 FromToFlag_ret_1:
  return reterr;
}

PglErr SnpFlag(const uint32_t* variant_bps, const char* const* variant_ids, const uint32_t* variant_id_htable, const char* varid_snp, uint32_t raw_variant_ct, uintptr_t max_variant_id_slen, uintptr_t variant_id_htable_size, uint32_t do_exclude, int32_t window_bp, uintptr_t* variant_include, ChrInfo* cip, uint32_t* variant_ct_ptr) {
  unsigned char* bigstack_mark = g_bigstack_base;
  PglErr reterr = kPglRetSuccess;
  {
    if (!(*variant_ct_ptr)) {
      goto SnpFlag_ret_1;
    }
    const uint32_t* htable_dup_base = &(variant_id_htable[RoundUpPow2(variant_id_htable_size, kInt32PerCacheline)]);
    const uint32_t raw_variant_ctl = BitCtToWordCt(raw_variant_ct);
    uint32_t cur_llidx;
    uint32_t variant_uidx = VariantIdDupHtableFind(varid_snp, variant_ids, variant_id_htable, htable_dup_base, strlen(varid_snp), variant_id_htable_size, max_variant_id_slen, &cur_llidx);
    if (variant_uidx == UINT32_MAX) {
      snprintf(g_logbuf, kLogbufSize, "Error: --%ssnp variant '%s' not found.\n", do_exclude? "exclude-" : "", varid_snp);
      goto SnpFlag_ret_INCONSISTENT_INPUT_WW;
    }
    if (window_bp == -1) {
      // duplicates ok

      uintptr_t* seen_uidxs;
      // not actually necessary in --exclude-snp case, but this is still fast
      // enough relative to hash table construction that there's no point in
      // complicating the code further to conditionally optimize this out
      if (bigstack_calloc_w(raw_variant_ctl, &seen_uidxs)) {
        goto SnpFlag_ret_NOMEM;
      }
      while (1) {
        SetBit(variant_uidx, seen_uidxs);
        if (cur_llidx == UINT32_MAX) {
          break;
        }
        variant_uidx = htable_dup_base[cur_llidx];
        cur_llidx = htable_dup_base[cur_llidx + 1];
      }
      if (do_exclude) {
        BitvecAndNot(seen_uidxs, raw_variant_ctl, variant_include);
      } else {
        BitvecAnd(seen_uidxs, raw_variant_ctl, variant_include);
      }
    } else {
      if (cur_llidx != UINT32_MAX) {
        snprintf(g_logbuf, kLogbufSize, "Error: --%ssnp + --window central variant ID '%s' appears multiple times.\n", do_exclude? "exclude-" : "", varid_snp);
        goto SnpFlag_ret_INCONSISTENT_INPUT_WW;
      }
      const uint32_t chr_fo_idx = GetVariantChrFoIdx(cip, variant_uidx);
      const uint32_t chr_vidx_end = cip->chr_fo_vidx_start[chr_fo_idx + 1];
      const uint32_t center_bp = variant_bps[variant_uidx];
      const uint32_t window_bp_u = window_bp;
      uint32_t vidx_start = cip->chr_fo_vidx_start[chr_fo_idx];
      if (center_bp > window_bp_u) {
        vidx_start += CountSortedSmallerU32(&(variant_bps[vidx_start]), chr_vidx_end - vidx_start, center_bp - window_bp_u);
      }
      const uint32_t bp_end = 1 + center_bp + window_bp_u;
      const uint32_t vidx_end = vidx_start + CountSortedSmallerU32(&(variant_bps[vidx_start]), chr_vidx_end - vidx_start, bp_end);
      if (do_exclude) {
        ClearBitsNz(vidx_start, vidx_end, variant_include);
      } else {
        if (vidx_start) {
          ClearBitsNz(0, vidx_start, variant_include);
        }
        if (vidx_end < raw_variant_ct) {
          ClearBitsNz(vidx_end, raw_variant_ct, variant_include);
        }
        ZeroWArr(kChrMaskWords, cip->chr_mask);
        SetBit(cip->chr_file_order[chr_fo_idx], cip->chr_mask);
      }
    }
    const uint32_t new_variant_ct = PopcountWords(variant_include, raw_variant_ctl);
    logprintf("--%ssnp%s: %u variant%s remaining.\n", do_exclude? "exclude-" : "", (window_bp == -1)? "" : " + --window", new_variant_ct, (new_variant_ct == 1)? "" : "s");
    *variant_ct_ptr = new_variant_ct;
  }
  while (0) {
  SnpFlag_ret_NOMEM:
    reterr = kPglRetNomem;
    break;
  SnpFlag_ret_INCONSISTENT_INPUT_WW:
    WordWrapB(0);
    logerrputsb();
    reterr = kPglRetInconsistentInput;
    break;
  }
 SnpFlag_ret_1:
  BigstackReset(bigstack_mark);
  return reterr;
}

PglErr SnpsFlag(const char* const* variant_ids, const uint32_t* variant_id_htable, const RangeList* snps_range_list_ptr, uint32_t raw_variant_ct, uintptr_t max_variant_id_slen, uintptr_t variant_id_htable_size, uint32_t do_exclude, uintptr_t* variant_include, uint32_t* variant_ct_ptr) {
  unsigned char* bigstack_mark = g_bigstack_base;
  PglErr reterr = kPglRetSuccess;
  {
    if (!(*variant_ct_ptr)) {
      goto SnpsFlag_ret_1;
    }
    const uint32_t* htable_dup_base = &(variant_id_htable[RoundUpPow2(variant_id_htable_size, kInt32PerCacheline)]);
    const char* varid_strbox = snps_range_list_ptr->names;
    const unsigned char* starts_range = snps_range_list_ptr->starts_range;
    const uint32_t varid_ct = snps_range_list_ptr->name_ct;
    const uintptr_t varid_max_blen = snps_range_list_ptr->name_max_blen;
    const uint32_t raw_variant_ctl = BitCtToWordCt(raw_variant_ct);
    uintptr_t* seen_uidxs;
    if (bigstack_calloc_w(raw_variant_ctl, &seen_uidxs)) {
      goto SnpsFlag_ret_NOMEM;
    }
    uint32_t range_start_vidx = UINT32_MAX;
    for (uint32_t varid_idx = 0; varid_idx < varid_ct; ++varid_idx) {
      const char* cur_varid = &(varid_strbox[varid_idx * varid_max_blen]);
      uint32_t cur_llidx;
      uint32_t variant_uidx = VariantIdDupHtableFind(cur_varid, variant_ids, variant_id_htable, htable_dup_base, strlen(cur_varid), variant_id_htable_size, max_variant_id_slen, &cur_llidx);
      if (variant_uidx == UINT32_MAX) {
        snprintf(g_logbuf, kLogbufSize, "Error: --%ssnps variant '%s' not found.\n", do_exclude? "exclude-" : "", cur_varid);
        goto SnpsFlag_ret_INCONSISTENT_INPUT_WW;
      }
      if (starts_range[varid_idx]) {
        if (cur_llidx != UINT32_MAX) {
          snprintf(g_logbuf, kLogbufSize, "Error: --%ssnps range-starting variant ID '%s' appears multiple times.\n", do_exclude? "exclude-" : "", cur_varid);
          goto SnpsFlag_ret_INCONSISTENT_INPUT_WW;
        }
        range_start_vidx = variant_uidx;
      } else {
        if (range_start_vidx != UINT32_MAX) {
          if (cur_llidx != UINT32_MAX) {
            snprintf(g_logbuf, kLogbufSize, "Error: --%ssnps range-ending variant ID '%s' appears multiple times.\n", do_exclude? "exclude-" : "", cur_varid);
            goto SnpsFlag_ret_INCONSISTENT_INPUT_WW;
          }
          if (variant_uidx < range_start_vidx) {
            const uint32_t uii = variant_uidx;
            variant_uidx = range_start_vidx;
            range_start_vidx = uii;
          }
          FillBitsNz(range_start_vidx, variant_uidx + 1, seen_uidxs);
        } else {
          while (1) {
            SetBit(variant_uidx, seen_uidxs);
            if (cur_llidx == UINT32_MAX) {
              break;
            }
            variant_uidx = htable_dup_base[cur_llidx];
            cur_llidx = htable_dup_base[cur_llidx + 1];
          }
        }
        range_start_vidx = UINT32_MAX;
      }
    }
    if (do_exclude) {
      BitvecAndNot(seen_uidxs, raw_variant_ctl, variant_include);
    } else {
      BitvecAnd(seen_uidxs, raw_variant_ctl, variant_include);
    }
    const uint32_t new_variant_ct = PopcountWords(variant_include, raw_variant_ctl);
    logprintf("--%ssnps: %u variant%s remaining.\n", do_exclude? "exclude-" : "", new_variant_ct, (new_variant_ct == 1)? "" : "s");
    *variant_ct_ptr = new_variant_ct;
  }
  while (0) {
  SnpsFlag_ret_NOMEM:
    reterr = kPglRetNomem;
    break;
  SnpsFlag_ret_INCONSISTENT_INPUT_WW:
    WordWrapB(0);
    logerrputsb();
    reterr = kPglRetInconsistentInput;
    break;
  }
 SnpsFlag_ret_1:
  BigstackReset(bigstack_mark);
  return reterr;
}

void ExtractExcludeProcessToken(const char* const* variant_ids, const uint32_t* variant_id_htable, const uint32_t* htable_dup_base, const char* tok_start, uint32_t variant_id_htable_size, uintptr_t max_variant_id_slen, uint32_t token_slen, uintptr_t* already_seen, uintptr_t* duplicate_ct_ptr) {
  uint32_t cur_llidx;
  uint32_t variant_uidx = VariantIdDupHtableFind(tok_start, variant_ids, variant_id_htable, htable_dup_base, token_slen, variant_id_htable_size, max_variant_id_slen, &cur_llidx);
  if (variant_uidx == UINT32_MAX) {
    return;
  }
  if (IsSet(already_seen, variant_uidx)) {
    *duplicate_ct_ptr += 1;
  } else {
    while (1) {
      SetBit(variant_uidx, already_seen);
      if (cur_llidx == UINT32_MAX) {
        return;
      }
      variant_uidx = htable_dup_base[cur_llidx];
      cur_llidx = htable_dup_base[cur_llidx + 1];
    }
  }
}

PglErr ExtractExcludeFlagNorange(const char* const* variant_ids, const uint32_t* variant_id_htable, const char* fnames, uint32_t raw_variant_ct, uintptr_t max_variant_id_slen, uintptr_t variant_id_htable_size, uint32_t do_exclude, uintptr_t* variant_include, uint32_t* variant_ct_ptr) {
  unsigned char* bigstack_mark = g_bigstack_base;
  GzTokenStream gts;
  PreinitGzTokenStream(&gts);
  PglErr reterr = kPglRetSuccess;
  {
    if (!(*variant_ct_ptr)) {
      goto ExtractExcludeFlagNorange_ret_1;
    }
    // possible todo: multithreaded read/htable lookup
    const uint32_t raw_variant_ctl = BitCtToWordCt(raw_variant_ct);
    uintptr_t* already_seen;
    if (bigstack_calloc_w(raw_variant_ctl, &already_seen)) {
      goto ExtractExcludeFlagNorange_ret_NOMEM;
    }
    const uint32_t* htable_dup_base = &(variant_id_htable[RoundUpPow2(variant_id_htable_size, kInt32PerCacheline)]);
    const char* fnames_iter = fnames;
    uintptr_t duplicate_ct = 0;
    do {
      reterr = InitGzTokenStream(fnames_iter, &gts, g_textbuf);
      if (reterr) {
        goto ExtractExcludeFlagNorange_ret_1;
      }
      uint32_t token_slen;
      while (1) {
        const char* token_start = AdvanceGzTokenStream(&gts, &token_slen);
        if (!token_start) {
          break;
        }
        ExtractExcludeProcessToken(variant_ids, variant_id_htable, htable_dup_base, token_start, variant_id_htable_size, max_variant_id_slen, token_slen, already_seen, &duplicate_ct);
      }
      if (token_slen) {
        // error code
        if (token_slen == UINT32_MAX) {
          snprintf(g_logbuf, kLogbufSize, "Error: Excessively long ID in --%s file.\n", do_exclude? "exclude" : "extract");
          goto ExtractExcludeFlagNorange_ret_MALFORMED_INPUT_2;
        }
        goto ExtractExcludeFlagNorange_ret_READ_FAIL;
      }
      if (CloseGzTokenStream(&gts)) {
        goto ExtractExcludeFlagNorange_ret_READ_FAIL;
      }
      fnames_iter = strnul(fnames_iter);
      ++fnames_iter;
    } while (*fnames_iter);
    if (do_exclude) {
      BitvecAndNot(already_seen, raw_variant_ctl, variant_include);
    } else {
      BitvecAnd(already_seen, raw_variant_ctl, variant_include);
    }
    const uint32_t new_variant_ct = PopcountWords(variant_include, raw_variant_ctl);
    logprintf("--%s: %u variant%s remaining.\n", do_exclude? "exclude" : "extract", new_variant_ct, (new_variant_ct == 1)? "" : "s");
    *variant_ct_ptr = new_variant_ct;
    if (duplicate_ct) {
      logerrprintf("Warning: At least %" PRIuPTR " duplicate ID%s in --%s file(s).\n", duplicate_ct, (duplicate_ct == 1)? "" : "s", do_exclude? "exclude" : "extract");
    }
  }
  while (0) {
  ExtractExcludeFlagNorange_ret_NOMEM:
    reterr = kPglRetNomem;
    break;
  ExtractExcludeFlagNorange_ret_READ_FAIL:
    reterr = kPglRetReadFail;
    break;
  ExtractExcludeFlagNorange_ret_MALFORMED_INPUT_2:
    logerrputsb();
    reterr = kPglRetMalformedInput;
    break;
  }
 ExtractExcludeFlagNorange_ret_1:
  CloseGzTokenStream(&gts);
  BigstackReset(bigstack_mark);
  return reterr;
}

void RandomThinProb(const char* flagname_p, const char* unitname, double thin_keep_prob, uint32_t raw_item_ct, uintptr_t* item_include, uint32_t* item_ct_ptr) {
  // possible todo: try using truncated geometric distribution, like --dummy
  // can also parallelize this
  const uint32_t orig_item_ct = *item_ct_ptr;
  if (!orig_item_ct) {
    return;
  }
  const uint32_t uint32_thresh = S_CAST(uint32_t, thin_keep_prob * 4294967296.0 + 0.5);
  uint32_t item_uidx = 0;
  for (uint32_t item_idx = 0; item_idx < orig_item_ct; ++item_idx, ++item_uidx) {
    MovU32To1Bit(item_include, &item_uidx);
    if (sfmt_genrand_uint32(&g_sfmt) >= uint32_thresh) {
      ClearBit(item_uidx, item_include);
    }
  }
  const uint32_t new_item_ct = PopcountWords(item_include, BitCtToWordCt(raw_item_ct));
  *item_ct_ptr = new_item_ct;
  const uint32_t removed_ct = orig_item_ct - new_item_ct;
  logprintf("--%s: %u %s%s removed (%u remaining).\n", flagname_p, removed_ct, unitname, (removed_ct == 1)? "" : "s", new_item_ct);
  return;
}

PglErr RandomThinCt(const char* flagname_p, const char* unitname, uint32_t thin_keep_ct, uint32_t raw_item_ct, uintptr_t* item_include, uint32_t* item_ct_ptr) {
  unsigned char* bigstack_mark = g_bigstack_base;
  PglErr reterr = kPglRetSuccess;
  {
    const uint32_t orig_item_ct = *item_ct_ptr;
    if (thin_keep_ct >= orig_item_ct) {
      logerrprintf("Warning: --%s parameter exceeds # of remaining %ss; skipping.\n", flagname_p, unitname);
      goto RandomThinCt_ret_1;
    }
    const uint32_t removed_ct = orig_item_ct - thin_keep_ct;
    const uint32_t raw_item_ctl = BitCtToWordCt(raw_item_ct);
    uintptr_t* perm_buf;
    uintptr_t* new_item_include;
    if (bigstack_alloc_w(BitCtToWordCt(orig_item_ct), &perm_buf) ||
        bigstack_alloc_w(raw_item_ctl, &new_item_include)) {
      goto RandomThinCt_ret_NOMEM;
    }
    // no actual interleaving here, but may as well use this function
    // note that this requires marker_ct >= 2
    GeneratePerm1Interleaved(orig_item_ct, thin_keep_ct, 0, 1, perm_buf, &g_sfmt);
    ExpandBytearr(perm_buf, item_include, raw_item_ctl, orig_item_ct, 0, new_item_include);
    memcpy(item_include, new_item_include, raw_item_ctl * sizeof(intptr_t));
    *item_ct_ptr = thin_keep_ct;
    logprintf("--%s: %u %s%s removed (%u remaining).\n", flagname_p, removed_ct, unitname, (removed_ct == 1)? "" : "s", thin_keep_ct);
  }
  while (0) {
  RandomThinCt_ret_NOMEM:
    reterr = kPglRetNomem;
    break;
  }
 RandomThinCt_ret_1:
  BigstackReset(bigstack_mark);
  return reterr;
}

static const char keep_remove_flag_strs[4][11] = {"keep", "remove", "keep-fam", "remove-fam"};

PglErr KeepOrRemove(const char* fnames, const SampleIdInfo* siip, uint32_t raw_sample_ct, KeepFlags flags, uintptr_t* sample_include, uint32_t* sample_ct_ptr) {
  unsigned char* bigstack_mark = g_bigstack_base;
  const char* flag_name = keep_remove_flag_strs[flags % 4];
  uintptr_t line_idx = 0;
  PglErr reterr = kPglRetSuccess;
  ReadLineStream rls;
  PreinitRLstream(&rls);
  {
    const uint32_t orig_sample_ct = *sample_ct_ptr;
    if (!orig_sample_ct) {
      goto KeepOrRemove_ret_1;
    }
    const uint32_t raw_sample_ctl = BitCtToWordCt(raw_sample_ct);
    uintptr_t* seen_uidxs;
    if (bigstack_calloc_w(raw_sample_ctl, &seen_uidxs)) {
      goto KeepOrRemove_ret_NOMEM;
    }

    const uint32_t families_only = flags & kfKeepFam;
    const char* sample_ids = siip->sample_ids;
    const uintptr_t max_sample_id_blen = siip->max_sample_id_blen;
    char* idbuf = nullptr;
    uint32_t* xid_map = nullptr;
    char* sorted_xidbox = nullptr;
    uintptr_t max_xid_blen = max_sample_id_blen - 1;
    if (families_only) {
      // only need to do this once
      if (bigstack_alloc_u32(orig_sample_ct, &xid_map) ||
          bigstack_alloc_c(orig_sample_ct * max_xid_blen, &sorted_xidbox)) {
        goto KeepOrRemove_ret_NOMEM;
      }
      uint32_t sample_uidx = 0;
      for (uint32_t sample_idx = 0; sample_idx < orig_sample_ct; ++sample_idx, ++sample_uidx) {
        MovU32To1Bit(sample_include, &sample_uidx);
        const char* fidt_ptr = &(sample_ids[sample_uidx * max_sample_id_blen]);
        const char* fidt_end = AdvPastDelim(fidt_ptr, '\t');
        const uint32_t cur_fidt_slen = fidt_end - fidt_ptr;
        // include trailing tab, to simplify bsearch_str_lb() usage
        memcpyx(&(sorted_xidbox[sample_idx * max_xid_blen]), fidt_ptr, cur_fidt_slen, '\0');
        xid_map[sample_idx] = sample_uidx;
      }
      if (SortStrboxIndexed(orig_sample_ct, max_xid_blen, 0, sorted_xidbox, xid_map)) {
        goto KeepOrRemove_ret_NOMEM;
      }
    }
    unsigned char* bigstack_mark2 = nullptr;
    const char* fnames_iter = fnames;
    char* line_iter = nullptr;
    uintptr_t duplicate_ct = 0;
    do {
      if (!line_iter) {
        reterr = SizeAndInitRLstreamRaw(fnames_iter, bigstack_left() - (bigstack_left() / 4), &rls, &line_iter);
        bigstack_mark2 = g_bigstack_base;
      } else {
        reterr = RetargetRLstreamRaw(fnames_iter, &rls, &line_iter);
        // previous file always read to eof, so no need to call
        // RLstreamErrPrint().
      }
      if (reterr) {
        goto KeepOrRemove_ret_1;
      }
      char* linebuf_first_token;
      XidMode xid_mode;
      line_idx = 0;
      if (!families_only) {
        reterr = LoadXidHeader(flag_name, (siip->sids || (siip->flags & kfSampleIdStrictSid0))? kfXidHeader0 : kfXidHeaderIgnoreSid, &line_iter, &line_idx, &linebuf_first_token, &rls, &xid_mode);
        if (reterr) {
          if (reterr == kPglRetEof) {
            reterr = kPglRetSuccess;
            goto KeepOrRemove_empty_file;
          }
          goto KeepOrRemove_ret_READ_RLSTREAM;
        }
        const uint32_t allow_dups = siip->sids && (!(xid_mode & kfXidModeFlagSid));
        reterr = SortedXidboxInitAlloc(sample_include, siip, orig_sample_ct, allow_dups, xid_mode, 0, &sorted_xidbox, &xid_map, &max_xid_blen);
        if (reterr) {
          goto KeepOrRemove_ret_1;
        }
        if (bigstack_alloc_c(max_xid_blen, &idbuf)) {
          goto KeepOrRemove_ret_NOMEM;
        }
        if (*linebuf_first_token == '#') {
          *linebuf_first_token = '\0';
        }
      } else {
        line_iter = AdvToDelim(line_iter, '\n');
        linebuf_first_token = line_iter;
      }
      while (1) {
        if (!IsEolnKns(*linebuf_first_token)) {
          if (!families_only) {
            const char* linebuf_iter = linebuf_first_token;
            uint32_t xid_idx_start;
            uint32_t xid_idx_end;
            if (!SortedXidboxReadMultifind(sorted_xidbox, max_xid_blen, orig_sample_ct, 0, xid_mode, &linebuf_iter, &xid_idx_start, &xid_idx_end, idbuf)) {
              uint32_t sample_uidx = xid_map[xid_idx_start];
              if (IsSet(seen_uidxs, sample_uidx)) {
                ++duplicate_ct;
              } else {
                while (1) {
                  SetBit(sample_uidx, seen_uidxs);
                  if (++xid_idx_start == xid_idx_end) {
                    break;
                  }
                  sample_uidx = xid_map[xid_idx_start];
                }
              }
            } else if (!linebuf_iter) {
              goto KeepOrRemove_ret_MISSING_TOKENS;
            }
            line_iter = K_CAST(char*, linebuf_iter);
          } else {
            char* token_end = CurTokenEnd(linebuf_first_token);
            *token_end = '\t';
            const uint32_t slen = 1 + S_CAST(uintptr_t, token_end - linebuf_first_token);
            uint32_t lb_idx = bsearch_str_lb(linebuf_first_token, sorted_xidbox, slen, max_xid_blen, orig_sample_ct);
            *token_end = ' ';
            const uint32_t ub_idx = bsearch_str_lb(linebuf_first_token, sorted_xidbox, slen, max_xid_blen, orig_sample_ct);
            if (ub_idx != lb_idx) {
              uint32_t sample_uidx = xid_map[lb_idx];
              if (IsSet(seen_uidxs, sample_uidx)) {
                ++duplicate_ct;
              } else {
                while (1) {
                  SetBit(sample_uidx, seen_uidxs);
                  if (++lb_idx == ub_idx) {
                    break;
                  }
                  sample_uidx = xid_map[lb_idx];
                }
              }
            }
            line_iter = token_end;
          }
        }
        ++line_idx;
        reterr = RlsNextLstrip(&rls, &line_iter);
        if (reterr) {
          if (reterr == kPglRetEof) {
            // reterr = kPglRetSuccess;
            break;
          }
          goto KeepOrRemove_ret_READ_RLSTREAM;
        }
        linebuf_first_token = line_iter;
      }
    KeepOrRemove_empty_file:
      BigstackReset(bigstack_mark2);
      fnames_iter = strnul(fnames_iter);
      ++fnames_iter;
    } while (*fnames_iter);
    reterr = kPglRetSuccess;
    if (flags & kfKeepRemove) {
      BitvecAndNot(seen_uidxs, raw_sample_ctl, sample_include);
    } else {
      memcpy(sample_include, seen_uidxs, raw_sample_ctl * sizeof(intptr_t));
    }
    const uint32_t sample_ct = PopcountWords(sample_include, raw_sample_ctl);
    *sample_ct_ptr = sample_ct;
    logprintf("--%s: %u sample%s remaining.\n", flag_name, sample_ct, (sample_ct == 1)? "" : "s");
    if (duplicate_ct) {
      // "At least" since this does not count duplicate IDs absent from the
      // .fam.
      logerrprintf("Warning: At least %" PRIuPTR " duplicate ID%s in --%s file(s).\n", duplicate_ct, (duplicate_ct == 1)? "" : "s", flag_name);
    }
  }
  while (0) {
  KeepOrRemove_ret_NOMEM:
    reterr = kPglRetNomem;
    break;
  KeepOrRemove_ret_READ_RLSTREAM:
    {
      char file_descrip_buf[32];
      snprintf(file_descrip_buf, 32, "--%s file", flag_name);
      RLstreamErrPrint(file_descrip_buf, &rls, &reterr);
    }
    break;
  KeepOrRemove_ret_MISSING_TOKENS:
    logerrprintf("Error: Line %" PRIuPTR " of --%s file has fewer tokens than expected.\n", line_idx, flag_name);
    reterr = kPglRetMalformedInput;
    break;
  }
 KeepOrRemove_ret_1:
  BigstackReset(bigstack_mark);
  CleanupRLstream(&rls);
  return reterr;
}

// Minor extension of PLINK 1.x --filter.  (Renamed since --filter is not
// sufficiently self-describing; PLINK has lots of other filters on both
// samples and variants.  --filter is automatically be converted to --keep-fcol
// for backward compatibility, though.)
PglErr KeepFcol(const char* fname, const SampleIdInfo* siip, const char* strs_flattened, const char* col_name, uint32_t raw_sample_ct, uint32_t col_num, uintptr_t* sample_include, uint32_t* sample_ct_ptr) {
  unsigned char* bigstack_mark = g_bigstack_base;
  uintptr_t line_idx = 0;
  PglErr reterr = kPglRetSuccess;
  ReadLineStream rls;
  PreinitRLstream(&rls);
  {
    const uint32_t orig_sample_ct = *sample_ct_ptr;
    if (!orig_sample_ct) {
      goto KeepFcol_ret_1;
    }
    const uint32_t raw_sample_ctl = BitCtToWordCt(raw_sample_ct);
    uintptr_t* seen_xid_idxs;
    uintptr_t* keep_uidxs;
    if (bigstack_calloc_w(BitCtToWordCt(orig_sample_ct), &seen_xid_idxs) ||
        bigstack_calloc_w(raw_sample_ctl, &keep_uidxs)) {
      goto KeepFcol_ret_NOMEM;
    }

    char* sorted_strbox;
    uintptr_t max_str_blen;
    uint32_t str_ct;
    if (MultistrToStrboxDedupAlloc(strs_flattened, &sorted_strbox, &str_ct, &max_str_blen)) {
      goto KeepFcol_ret_NOMEM;
    }

    char* line_iter;
    reterr = SizeAndInitRLstreamRaw(fname, bigstack_left() - (bigstack_left() / 4), &rls, &line_iter);
    if (reterr) {
      goto KeepFcol_ret_1;
    }
    char* linebuf_first_token;
    XidMode xid_mode;
    reterr = LoadXidHeader("keep-fcol", (siip->sids || (siip->flags & kfSampleIdStrictSid0))? kfXidHeaderFixedWidth : kfXidHeaderFixedWidthIgnoreSid, &line_iter, &line_idx, &linebuf_first_token, &rls, &xid_mode);
    if (reterr) {
      if (reterr == kPglRetEof) {
        logerrputs("Error: Empty --keep-fcol file.\n");
        goto KeepFcol_ret_MALFORMED_INPUT;
      }
      goto KeepFcol_ret_READ_RLSTREAM;
    }
    const uint32_t id_col_ct = GetXidColCt(xid_mode);
    uint32_t postid_col_idx = 0;
    if (!col_name) {
      if (!col_num) {
        if (id_col_ct == 3) {
          logerrputs("Error: You must specify a --keep-fcol column with --keep-fcol-name or\n--keep-fcol-num.\n");
          goto KeepFcol_ret_INCONSISTENT_INPUT;
        }
        col_num = 3;
      }
      if (id_col_ct >= col_num) {
        logerrputs("Error: --keep-fcol-num parameter too small (it refers to a sample ID column in\nthe --keep-fcol file).\n");
        goto KeepFcol_ret_INCONSISTENT_INPUT;
      }
      postid_col_idx = col_num - id_col_ct;
    } else {
      if (*linebuf_first_token != '#') {
        logerrputs("Error: --keep-fcol-name requires the --keep-fcol file to have a header line\nstarting with #FID or #IID.\n");
        goto KeepFcol_ret_INCONSISTENT_INPUT;
      }
      const char* linebuf_iter = NextTokenMult(linebuf_first_token, id_col_ct);
      if (!linebuf_iter) {
        logerrputs("Error: --keep-fcol-name column not found in --keep-fcol file.\n");
        goto KeepFcol_ret_INCONSISTENT_INPUT;
      }
      const uint32_t col_name_slen = strlen(col_name);
      uint32_t already_found = 0;
      do {
        ++postid_col_idx;
        const char* token_end = CurTokenEnd(linebuf_iter);
        if ((S_CAST(uintptr_t, token_end - linebuf_iter) == col_name_slen) && (!memcmp(linebuf_iter, col_name, col_name_slen))) {
          if (already_found) {
            snprintf(g_logbuf, kLogbufSize, "Error: Multiple columns in --keep-fcol file are named '%s'.\n", col_name);
            goto KeepFcol_ret_INCONSISTENT_INPUT_WW;
          }
          already_found = 1;
        }
        linebuf_iter = FirstNonTspace(token_end);
      } while (!IsEolnKns(*linebuf_iter));
      if (!already_found) {
        logerrputs("Error: --keep-fcol-name column not found in --keep-fcol file.\n");
        goto KeepFcol_ret_INCONSISTENT_INPUT;
      }
      line_iter = K_CAST(char*, linebuf_iter);
    }
    uint32_t* xid_map = nullptr;
    char* sorted_xidbox = nullptr;
    const uint32_t allow_dups = siip->sids && (!(xid_mode & kfXidModeFlagSid));
    uintptr_t max_xid_blen;
    reterr = SortedXidboxInitAlloc(sample_include, siip, orig_sample_ct, allow_dups, xid_mode, 0, &sorted_xidbox, &xid_map, &max_xid_blen);
    if (reterr) {
      goto KeepFcol_ret_1;
    }
    char* idbuf = nullptr;
    if (bigstack_alloc_c(max_xid_blen, &idbuf)) {
      goto KeepFcol_ret_NOMEM;
    }
    if (*linebuf_first_token == '#') {
      *linebuf_first_token = '\0';
    }
    while (1) {
      if (!IsEolnKns(*linebuf_first_token)) {
        const char* linebuf_iter = linebuf_first_token;
        uint32_t xid_idx_start;
        uint32_t xid_idx_end;
        if (!SortedXidboxReadMultifind(sorted_xidbox, max_xid_blen, orig_sample_ct, 0, xid_mode, &linebuf_iter, &xid_idx_start, &xid_idx_end, idbuf)) {
          if (IsSet(seen_xid_idxs, xid_idx_start)) {
            logerrprintfww("Error: Sample ID on line %" PRIuPTR " of --keep-fcol file duplicates one earlier in the file.\n", line_idx);
            goto KeepFcol_ret_MALFORMED_INPUT;
          }
          SetBit(xid_idx_start, seen_xid_idxs);
          linebuf_iter = NextTokenMult(linebuf_iter, postid_col_idx);
          if (!linebuf_iter) {
            goto KeepFcol_ret_MISSING_TOKENS;
          }
          const char* token_end = CurTokenEnd(linebuf_iter);
          const int32_t ii = bsearch_str(linebuf_iter, sorted_strbox, token_end - linebuf_iter, max_str_blen, str_ct);
          if (ii != -1) {
            for (; xid_idx_start < xid_idx_end; ++xid_idx_start) {
              const uint32_t sample_uidx = xid_map[xid_idx_start];
              SetBit(sample_uidx, keep_uidxs);
            }
          }
        } else if (!linebuf_iter) {
          goto KeepFcol_ret_MISSING_TOKENS;
        }
        line_iter = K_CAST(char*, linebuf_iter);
      }
      ++line_idx;
      reterr = RlsNextLstrip(&rls, &line_iter);
      if (reterr) {
        if (reterr == kPglRetEof) {
          reterr = kPglRetSuccess;
          break;
        }
        goto KeepFcol_ret_READ_RLSTREAM;
      }
      linebuf_first_token = line_iter;
    }
    memcpy(sample_include, keep_uidxs, raw_sample_ctl * sizeof(intptr_t));
    const uint32_t sample_ct = PopcountWords(sample_include, raw_sample_ctl);
    *sample_ct_ptr = sample_ct;
    logprintf("--keep-fcol: %u sample%s remaining.\n", sample_ct, (sample_ct == 1)? "" : "s");
  }
  while (0) {
  KeepFcol_ret_NOMEM:
    reterr = kPglRetNomem;
    break;
  KeepFcol_ret_READ_RLSTREAM:
    RLstreamErrPrint("--keep-fcol file", &rls, &reterr);
    break;
  KeepFcol_ret_MALFORMED_INPUT:
    reterr = kPglRetMalformedInput;
    break;
  KeepFcol_ret_MISSING_TOKENS:
    snprintf(g_logbuf, kLogbufSize, "Error: Line %" PRIuPTR " of --keep-fcol file has fewer tokens than expected.\n", line_idx);
  KeepFcol_ret_INCONSISTENT_INPUT_WW:
    WordWrapB(0);
    logerrputsb();
  KeepFcol_ret_INCONSISTENT_INPUT:
    reterr = kPglRetInconsistentInput;
    break;
  }
 KeepFcol_ret_1:
  BigstackReset(bigstack_mark);
  CleanupRLstream(&rls);
  return reterr;
}

PglErr RequirePheno(const PhenoCol* pheno_cols, const char* pheno_names, const char* require_pheno_flattened, uint32_t raw_sample_ct, uint32_t pheno_ct, uintptr_t max_pheno_name_blen, uint32_t is_covar, uintptr_t* sample_include, uint32_t* sample_ct_ptr) {
  unsigned char* bigstack_mark = g_bigstack_base;
  unsigned char* bigstack_end_mark = g_bigstack_end;
  PglErr reterr = kPglRetSuccess;
  {
    const uint32_t orig_sample_ct = *sample_ct_ptr;
    if (!orig_sample_ct) {
      goto RequirePheno_ret_1;
    }
    uint32_t required_pheno_ct = 0;
    uintptr_t max_required_pheno_blen = 2;
    uintptr_t* matched_phenos = nullptr;
    char* sorted_required_pheno_names = nullptr;
    if (require_pheno_flattened) {
      if (MultistrToStrboxDedupAlloc(require_pheno_flattened, &sorted_required_pheno_names, &required_pheno_ct, &max_required_pheno_blen)) {
        goto RequirePheno_ret_NOMEM;
      }
      if (bigstack_calloc_w(1 + (required_pheno_ct / kBitsPerWord), &matched_phenos)) {
        goto RequirePheno_ret_NOMEM;
      }
    } else {
      if (!pheno_ct) {
        logerrputs(is_covar? "Warning: No covariates loaded; ignoring --require-covar.\n" : "Warning: No phenotypes loaded; ignoring --require-pheno.\n");
        goto RequirePheno_ret_1;
      }
      required_pheno_ct = pheno_ct;
    }
    const uint32_t raw_sample_ctl = BitCtToWordCt(raw_sample_ct);
    for (uint32_t pheno_idx = 0; pheno_idx < pheno_ct; ++pheno_idx) {
      if (sorted_required_pheno_names) {
        const char* cur_pheno_name = &(pheno_names[pheno_idx * max_pheno_name_blen]);
        const int32_t ii = bsearch_str(cur_pheno_name, sorted_required_pheno_names, strlen(cur_pheno_name), max_required_pheno_blen, required_pheno_ct);
        if (ii == -1) {
          continue;
        }
        SetBitI(ii, matched_phenos);
      }
      BitvecAnd(pheno_cols[pheno_idx].nonmiss, raw_sample_ctl, sample_include);
    }
    if (matched_phenos) {
      const uint32_t first_unmatched_idx = AdvTo0Bit(matched_phenos, 0);
      if (first_unmatched_idx < required_pheno_ct) {
        logerrprintfww("Error: --require-%s '%s' not loaded.\n", is_covar? "covar covariate" : "pheno phenotype", &(sorted_required_pheno_names[first_unmatched_idx * max_required_pheno_blen]));
        goto RequirePheno_ret_INCONSISTENT_INPUT;
      }
    }
    const uint32_t new_sample_ct = PopcountWords(sample_include, raw_sample_ctl);
    const uint32_t removed_sample_ct = orig_sample_ct - new_sample_ct;
    logprintf("--require-%s: %u sample%s removed.\n", is_covar? "covar" : "pheno", removed_sample_ct, (removed_sample_ct == 1)? "" : "s");
    *sample_ct_ptr = new_sample_ct;
  }
  while (0) {
  RequirePheno_ret_NOMEM:
    reterr = kPglRetNomem;
    break;
  RequirePheno_ret_INCONSISTENT_INPUT:
    reterr = kPglRetInconsistentInput;
    break;
  }
 RequirePheno_ret_1:
  BigstackDoubleReset(bigstack_mark, bigstack_end_mark);
  return reterr;
}

PglErr KeepRemoveIf(const CmpExpr* cmp_expr, const PhenoCol* pheno_cols, const char* pheno_names, const PhenoCol* covar_cols, const char* covar_names, uint32_t raw_sample_ct, uint32_t pheno_ct, uintptr_t max_pheno_name_blen, uint32_t covar_ct, uintptr_t max_covar_name_blen, uint32_t affection_01, uint32_t is_remove, uintptr_t* sample_include, uint32_t* sample_ct_ptr) {
  unsigned char* bigstack_mark = g_bigstack_base;
  PglErr reterr = kPglRetSuccess;
  {
    const uint32_t orig_sample_ct = *sample_ct_ptr;
    if (!orig_sample_ct) {
      goto KeepRemoveIf_ret_1;
    }
    const char* cur_name = cmp_expr->pheno_name;
    const uintptr_t name_blen = 1 + strlen(cur_name);
    const PhenoCol* cur_pheno_col = nullptr;
    if (name_blen <= max_pheno_name_blen) {
      for (uint32_t pheno_idx = 0; pheno_idx < pheno_ct; ++pheno_idx) {
        if (!memcmp(cur_name, &(pheno_names[pheno_idx * max_pheno_name_blen]), name_blen)) {
          cur_pheno_col = &(pheno_cols[pheno_idx]);
          break;
        }
      }
    }
    if (!cur_pheno_col) {
      if (name_blen <= max_covar_name_blen) {
        for (uint32_t covar_idx = 0; covar_idx < covar_ct; ++covar_idx) {
          if (!memcmp(cur_name, &(covar_names[covar_idx * max_covar_name_blen]), name_blen)) {
            cur_pheno_col = &(covar_cols[covar_idx]);
            break;
          }
        }
      }
    }
    if (!cur_pheno_col) {
      // could no-op for --remove-if?  don't implement that unless/until
      // someone asks for it, though.
      snprintf(g_logbuf, kLogbufSize, "Error: --%s-if phenotype/covariate not loaded.\n", is_remove? "remove" : "keep");
      goto KeepRemoveIf_ret_INCONSISTENT_INPUT_2;
    }
    const uint32_t raw_sample_ctl = BitCtToWordCt(raw_sample_ct);
    CmpBinaryOp binary_op = cmp_expr->binary_op;
    const uint32_t pheno_must_exist = is_remove ^ (binary_op != kCmpOperatorNoteq);
    const uintptr_t* pheno_nm = cur_pheno_col->nonmiss;
    if (pheno_must_exist) {
      BitvecAnd(pheno_nm, raw_sample_ctl, sample_include);
    }
    uintptr_t* sample_include_intersect;
    if (bigstack_alloc_w(raw_sample_ctl, &sample_include_intersect)) {
      goto KeepRemoveIf_ret_NOMEM;
    }
    memcpy(sample_include_intersect, sample_include, raw_sample_ctl * sizeof(intptr_t));
    if (!pheno_must_exist) {
      BitvecAnd(pheno_nm, raw_sample_ctl, sample_include_intersect);
    }
    const uint32_t sample_intersect_ct = PopcountWords(sample_include_intersect, raw_sample_ctl);
    const char* cur_val_str = &(cur_name[name_blen]);
    const uint32_t val_slen = strlen(cur_val_str);
    if (cur_pheno_col->type_code == kPhenoDtypeQt) {
      double val;
      if (!ScanadvDouble(cur_val_str, &val)) {
        snprintf(g_logbuf, kLogbufSize, "Error: Invalid --%s-if value (finite number expected).\n", is_remove? "remove" : "keep");
        goto KeepRemoveIf_ret_INCONSISTENT_INPUT_2;
      }
      if (is_remove) {
        binary_op = S_CAST(CmpBinaryOp, kCmpOperatorEq - S_CAST(uint32_t, binary_op));
      }
      const double* pheno_vals = cur_pheno_col->data.qt;
      uint32_t sample_uidx = 0;
      switch (binary_op) {
        case kCmpOperatorNoteq:
          for (uint32_t sample_idx = 0; sample_idx < sample_intersect_ct; ++sample_idx, ++sample_uidx) {
            MovU32To1Bit(sample_include_intersect, &sample_uidx);
            if (pheno_vals[sample_uidx] == val) {
              ClearBit(sample_uidx, sample_include);
            }
          }
          break;
        case kCmpOperatorLe:
          for (uint32_t sample_idx = 0; sample_idx < sample_intersect_ct; ++sample_idx, ++sample_uidx) {
            MovU32To1Bit(sample_include_intersect, &sample_uidx);
            if (pheno_vals[sample_uidx] >= val) {
              ClearBit(sample_uidx, sample_include);
            }
          }
          break;
        case kCmpOperatorLeq:
          for (uint32_t sample_idx = 0; sample_idx < sample_intersect_ct; ++sample_idx, ++sample_uidx) {
            MovU32To1Bit(sample_include_intersect, &sample_uidx);
            if (pheno_vals[sample_uidx] > val) {
              ClearBit(sample_uidx, sample_include);
            }
          }
          break;
        case kCmpOperatorGe:
          for (uint32_t sample_idx = 0; sample_idx < sample_intersect_ct; ++sample_idx, ++sample_uidx) {
            MovU32To1Bit(sample_include_intersect, &sample_uidx);
            if (pheno_vals[sample_uidx] <= val) {
              ClearBit(sample_uidx, sample_include);
            }
          }
          break;
        case kCmpOperatorGeq:
          for (uint32_t sample_idx = 0; sample_idx < sample_intersect_ct; ++sample_idx, ++sample_uidx) {
            MovU32To1Bit(sample_include_intersect, &sample_uidx);
            if (pheno_vals[sample_uidx] < val) {
              ClearBit(sample_uidx, sample_include);
            }
          }
          break;
        case kCmpOperatorEq:
          for (uint32_t sample_idx = 0; sample_idx < sample_intersect_ct; ++sample_idx, ++sample_uidx) {
            MovU32To1Bit(sample_include_intersect, &sample_uidx);
            if (pheno_vals[sample_uidx] != val) {
              ClearBit(sample_uidx, sample_include);
            }
          }
          break;
      }
    } else {
      if ((binary_op != kCmpOperatorNoteq) && (binary_op != kCmpOperatorEq)) {
        snprintf(g_logbuf, kLogbufSize, "Error: --%s-if operator type mismatch (binary and categorical phenotypes only support == and !=).\n", is_remove? "remove" : "keep");
        goto KeepRemoveIf_ret_INCONSISTENT_INPUT_WW;
      }
      if (cur_pheno_col->type_code == kPhenoDtypeCc) {
        uint32_t val_12 = 0;  // 1 = control, 2 = case
        if (val_slen == 1) {
          val_12 = affection_01 + ctou32(cur_val_str[0]) - 48;
          if ((val_12 != 1) && (val_12 != 2)) {
            val_12 = 0;
          }
        } else if (val_slen == 4) {
          if (MatchUpperK(cur_val_str, "CASE")) {
            val_12 = 2;
          } else if (MatchUpperK(cur_val_str, "CTRL")) {
            val_12 = 1;
          }
        } else if (MatchUpperKLen(cur_val_str, "CONTROL", val_slen)) {
          val_12 = 1;
        }
        if (!val_12) {
          snprintf(g_logbuf, kLogbufSize, "Error: Invalid --%s-if value ('case'/'%c' or 'control'/'ctrl'/'%c' expected).\n", is_remove? "remove" : "keep", '2' - affection_01, '1' - affection_01);
          goto KeepRemoveIf_ret_INCONSISTENT_INPUT_WW;
        }
        if (is_remove ^ (val_12 == 2)) {
          BitvecAnd(cur_pheno_col->data.cc, raw_sample_ctl, sample_include);
        } else {
          BitvecAndNot(cur_pheno_col->data.cc, raw_sample_ctl, sample_include);
        }
      } else {
        assert(cur_pheno_col->type_code == kPhenoDtypeCat);
        const uint32_t nonnull_cat_ct = cur_pheno_col->nonnull_category_ct;
        uint32_t cat_idx = 1;
        for (; cat_idx <= nonnull_cat_ct; ++cat_idx) {
          if (!strcmp(cur_val_str, cur_pheno_col->category_names[cat_idx])) {
            break;
          }
        }
        if (cat_idx == nonnull_cat_ct + 1) {
          double dxx;
          if (ScanadvDouble(cur_val_str, &dxx)) {
            snprintf(g_logbuf, kLogbufSize, "Error: Invalid --%s-if value (category name expected).\n", is_remove? "remove" : "keep");
            goto KeepRemoveIf_ret_INCONSISTENT_INPUT_2;
          }
          // tolerate this, there are legitimate reasons for empty categories
          // to exist
          logerrprintfww("Warning: Categorical phenotype/covariate '%s' does not have a category named '%s'.\n", cur_name, cur_val_str);
          if (pheno_must_exist) {
            ZeroWArr(raw_sample_ctl, sample_include);
          }
        } else {
          const uint32_t* cur_cats = cur_pheno_col->data.cat;
          uint32_t sample_uidx = 0;
          if (pheno_must_exist) {
            for (uint32_t sample_idx = 0; sample_idx < sample_intersect_ct; ++sample_idx, ++sample_uidx) {
              MovU32To1Bit(sample_include_intersect, &sample_uidx);
              if (cur_cats[sample_uidx] != cat_idx) {
                ClearBit(sample_uidx, sample_include);
              }
            }
          } else {
            for (uint32_t sample_idx = 0; sample_idx < sample_intersect_ct; ++sample_idx, ++sample_uidx) {
              MovU32To1Bit(sample_include_intersect, &sample_uidx);
              if (cur_cats[sample_uidx] == cat_idx) {
                ClearBit(sample_uidx, sample_include);
              }
            }
          }
        }
      }
    }

    const uint32_t new_sample_ct = PopcountWords(sample_include, raw_sample_ctl);
    const uint32_t removed_sample_ct = orig_sample_ct - new_sample_ct;
    logprintf("--%s-if: %u sample%s removed.\n", is_remove? "remove" : "keep", removed_sample_ct, (removed_sample_ct == 1)? "" : "s");
    *sample_ct_ptr = new_sample_ct;
  }
  while (0) {
  KeepRemoveIf_ret_NOMEM:
    reterr = kPglRetNomem;
    break;
  KeepRemoveIf_ret_INCONSISTENT_INPUT_WW:
    WordWrapB(0);
  KeepRemoveIf_ret_INCONSISTENT_INPUT_2:
    logerrputsb();
    reterr = kPglRetInconsistentInput;
    break;
  }
 KeepRemoveIf_ret_1:
  BigstackReset(bigstack_mark);
  return reterr;
}

PglErr KeepRemoveCatsInternal(const PhenoCol* cur_pheno_col, const char* cats_fname, const char* cat_names_flattened, uint32_t raw_sample_ct, uint32_t is_remove, uint32_t max_thread_ct, uintptr_t* sample_include, uint32_t* sample_ct_ptr) {
  unsigned char* bigstack_mark = g_bigstack_base;
  GzTokenStream gts;
  PreinitGzTokenStream(&gts);
  PglErr reterr = kPglRetSuccess;
  {
    const uint32_t orig_sample_ct = *sample_ct_ptr;
    if (!orig_sample_ct) {
      goto KeepRemoveCatsInternal_ret_1;
    }
    const uint32_t raw_sample_ctl = BitCtToWordCt(raw_sample_ct);
    const uint32_t cat_ct = cur_pheno_col->nonnull_category_ct + 1;
    const uint32_t cat_ctl = BitCtToWordCt(cat_ct);
    uintptr_t* affected_samples;
    uintptr_t* cat_include;
    if (bigstack_calloc_w(raw_sample_ctl, &affected_samples) ||
        bigstack_alloc_w(cat_ctl, &cat_include)) {
      goto KeepRemoveCatsInternal_ret_NOMEM;
    }
    SetAllBits(cat_ct, cat_include);
    const char* const* category_names = cur_pheno_col->category_names;
    uint32_t* cat_id_htable;
    uint32_t id_htable_size;
    reterr = AllocAndPopulateIdHtableMt(cat_include, category_names, cat_ct, max_thread_ct, &cat_id_htable, nullptr, &id_htable_size);
    if (reterr) {
      goto KeepRemoveCatsInternal_ret_1;
    }
    ZeroWArr(cat_ctl, cat_include);
    if (cats_fname) {
      reterr = InitGzTokenStream(cats_fname, &gts, g_textbuf);
      if (reterr) {
        goto KeepRemoveCatsInternal_ret_1;
      }
      uintptr_t skip_ct = 0;
      uint32_t token_slen;
      while (1) {
        char* token_start = AdvanceGzTokenStream(&gts, &token_slen);
        if (!token_start) {
          break;
        }
        token_start[token_slen] = '\0';
        const uint32_t cur_cat_idx = IdHtableFind(token_start, category_names, cat_id_htable, token_slen, id_htable_size);
        if (cur_cat_idx == UINT32_MAX) {
          ++skip_ct;
        } else {
          SetBit(cur_cat_idx, cat_include);
        }
      }
      if (token_slen) {
        // error code
        if (token_slen == UINT32_MAX) {
          snprintf(g_logbuf, kLogbufSize, "Error: Excessively long ID in --%s-cats file.\n", is_remove? "remove" : "keep");
          goto KeepRemoveCatsInternal_ret_MALFORMED_INPUT_2;
        }
        goto KeepRemoveCatsInternal_ret_READ_FAIL;
      }
      if (CloseGzTokenStream(&gts)) {
        goto KeepRemoveCatsInternal_ret_READ_FAIL;
      }
      if (skip_ct) {
        logerrprintf("Warning: %" PRIuPTR " --%s-cats categor%s not present.\n", skip_ct, is_remove? "remove" : "keep", (skip_ct == 1)? "y" : "ies");
      }
    }
    if (cat_names_flattened) {
      uint32_t skip_ct = 0;
      const char* cat_names_iter = cat_names_flattened;
      do {
        const uint32_t cat_name_slen = strlen(cat_names_iter);
        const uint32_t cur_cat_idx = IdHtableFind(cat_names_iter, category_names, cat_id_htable, cat_name_slen, id_htable_size);
        if (cur_cat_idx == UINT32_MAX) {
          ++skip_ct;
        } else {
          SetBit(cur_cat_idx, cat_include);
        }
        cat_names_iter = &(cat_names_iter[cat_name_slen + 1]);
      } while (*cat_names_iter);
      if (skip_ct) {
        logerrprintf("Warning: %u --%s-cat-names categor%s not present.\n", skip_ct, is_remove? "remove" : "keep", (skip_ct == 1)? "y" : "ies");
      }
    }
    const uint32_t selected_cat_ct = PopcountWords(cat_include, cat_ctl);
    if (!selected_cat_ct) {
      logerrprintf("Warning: No matching --%s-cat-names category names.\n", is_remove? "remove-cats/--remove" : "keep-cats/--keep");
    } else {
      const uint32_t* cur_cats = cur_pheno_col->data.cat;
      uint32_t sample_uidx = 0;
      for (uint32_t sample_idx = 0; sample_idx < orig_sample_ct; ++sample_idx, ++sample_uidx) {
        MovU32To1Bit(sample_include, &sample_uidx);
        const uint32_t cur_cat_idx = cur_cats[sample_uidx];
        if (IsSet(cat_include, cur_cat_idx)) {
          SetBit(sample_uidx, affected_samples);
        }
      }
      if (is_remove) {
        BitvecAndNot(affected_samples, raw_sample_ctl, sample_include);
      } else {
        BitvecAnd(affected_samples, raw_sample_ctl, sample_include);
      }
      const uint32_t new_sample_ct = PopcountWords(sample_include, raw_sample_ctl);
      const uint32_t removed_sample_ct = orig_sample_ct - new_sample_ct;
      logprintfww("--%s-cat-names: %u categor%s selected, %u sample%s removed.\n", is_remove? "remove-cats/--remove" : "keep-cats/--keep", selected_cat_ct, (selected_cat_ct == 1)? "y" : "ies", removed_sample_ct, (removed_sample_ct == 1)? "" : "s");
      *sample_ct_ptr = new_sample_ct;
    }
  }
  while (0) {
  KeepRemoveCatsInternal_ret_NOMEM:
    reterr = kPglRetNomem;
    break;
  KeepRemoveCatsInternal_ret_READ_FAIL:
    reterr = kPglRetReadFail;
    break;
  KeepRemoveCatsInternal_ret_MALFORMED_INPUT_2:
    logerrputsb();
    reterr = kPglRetMalformedInput;
    break;
  }
 KeepRemoveCatsInternal_ret_1:
  CloseGzTokenStream(&gts);
  BigstackReset(bigstack_mark);
  return reterr;
}

PglErr KeepRemoveCats(const char* cats_fname, const char* cat_names_flattened, const char* cat_phenoname, const PhenoCol* pheno_cols, const char* pheno_names, const PhenoCol* covar_cols, const char* covar_names, uint32_t raw_sample_ct, uint32_t pheno_ct, uintptr_t max_pheno_name_blen, uint32_t covar_ct, uintptr_t max_covar_name_blen, uint32_t is_remove, uint32_t max_thread_ct, uintptr_t* sample_include, uint32_t* sample_ct_ptr) {
  PglErr reterr = kPglRetSuccess;
  {
    if (!(*sample_ct_ptr)) {
      goto KeepRemoveCats_ret_1;
    }
    if (!cat_phenoname) {
      // Default behavior:
      // 1. If at least one categorical phenotype exists, fail on >= 2, select
      //    it if one.
      // 2. Otherwise, fail if 0 or >= 2 categorical covariates, select the
      //    categorical covariate if there's exactly one.
      uint32_t cat_pheno_idx = UINT32_MAX;
      const PhenoCol* cur_pheno_col = nullptr;
      for (uint32_t pheno_idx = 0; pheno_idx < pheno_ct; ++pheno_idx) {
        if (pheno_cols[pheno_idx].type_code == kPhenoDtypeCat) {
          if (cat_pheno_idx != UINT32_MAX) {
            snprintf(g_logbuf, kLogbufSize, "Error: Multiple categorical phenotypes present. Use --%s-cat-pheno to specify which phenotype/covariate you want to filter on.\n", is_remove? "remove" : "keep");
            goto KeepRemoveCats_ret_INCONSISTENT_INPUT_WW;
          }
          cat_pheno_idx = pheno_idx;
        }
      }
      if (cat_pheno_idx != UINT32_MAX) {
        cur_pheno_col = &(pheno_cols[cat_pheno_idx]);
      } else {
        for (uint32_t covar_idx = 0; covar_idx < covar_ct; ++covar_idx) {
          if (covar_cols[covar_idx].type_code == kPhenoDtypeCat) {
            if (cat_pheno_idx != UINT32_MAX) {
              snprintf(g_logbuf, kLogbufSize, "Error: Multiple categorical covariates and no categorical phenotype present. Use --%s-cat-pheno to specify which phenotype/covariate you want to filter on.\n", is_remove? "remove" : "keep");
              goto KeepRemoveCats_ret_INCONSISTENT_INPUT_WW;
            }
            cat_pheno_idx = covar_idx;
          }
        }
        if (cat_pheno_idx == UINT32_MAX) {
          snprintf(g_logbuf, kLogbufSize, "Error: --%s-cat-names requires a categorical phenotype or covariate.\n", is_remove? "remove-cats/--remove" : "keep-cats/--keep");
          goto KeepRemoveCats_ret_INCONSISTENT_INPUT_WW;
        }
        cur_pheno_col = &(covar_cols[cat_pheno_idx]);
      }
      reterr = KeepRemoveCatsInternal(cur_pheno_col, cats_fname, cat_names_flattened, raw_sample_ct, is_remove, max_thread_ct, sample_include, sample_ct_ptr);
      if (reterr) {
        goto KeepRemoveCats_ret_1;
      }
    } else {
      const uintptr_t name_blen = 1 + strlen(cat_phenoname);
      uint32_t success = 0;
      if (name_blen <= max_pheno_name_blen) {
        for (uint32_t pheno_idx = 0; pheno_idx < pheno_ct; ++pheno_idx) {
          if (!memcmp(cat_phenoname, &(pheno_names[pheno_idx * max_pheno_name_blen]), name_blen)) {
            const PhenoCol* cur_pheno_col = &(pheno_cols[pheno_idx]);
            if (cur_pheno_col->type_code != kPhenoDtypeCat) {
              snprintf(g_logbuf, kLogbufSize, "Error: '%s' is not a categorical phenotype.\n", cat_phenoname);
              goto KeepRemoveCats_ret_INCONSISTENT_INPUT_WW;
            }
            reterr = KeepRemoveCatsInternal(cur_pheno_col, cats_fname, cat_names_flattened, raw_sample_ct, is_remove, max_thread_ct, sample_include, sample_ct_ptr);
            if (reterr) {
              goto KeepRemoveCats_ret_1;
            }
            success = 1;
            break;
          }
        }
      }
      if (name_blen <= max_covar_name_blen) {
        for (uint32_t covar_idx = 0; covar_idx < covar_ct; ++covar_idx) {
          if (!memcmp(cat_phenoname, &(covar_names[covar_idx * max_covar_name_blen]), name_blen)) {
            const PhenoCol* cur_pheno_col = &(covar_cols[covar_idx]);
            if (cur_pheno_col->type_code != kPhenoDtypeCat) {
              snprintf(g_logbuf, kLogbufSize, "Error: '%s' is not a categorical covariate.\n", cat_phenoname);
              goto KeepRemoveCats_ret_INCONSISTENT_INPUT_WW;
            }
            reterr = KeepRemoveCatsInternal(cur_pheno_col, cats_fname, cat_names_flattened, raw_sample_ct, is_remove, max_thread_ct, sample_include, sample_ct_ptr);
            if (reterr) {
              goto KeepRemoveCats_ret_1;
            }
            success = 1;
            break;
          }
        }
      }
      if (!success) {
        snprintf(g_logbuf, kLogbufSize, "Error: --%s-cat-pheno phenotype/covariate not loaded.\n", is_remove? "remove" : "keep");
        goto KeepRemoveCats_ret_INCONSISTENT_INPUT_2;
      }
    }
  }
  while (0) {
  KeepRemoveCats_ret_INCONSISTENT_INPUT_WW:
    WordWrapB(0);
  KeepRemoveCats_ret_INCONSISTENT_INPUT_2:
    logerrputsb();
    reterr = kPglRetInconsistentInput;
    break;
  }
 KeepRemoveCats_ret_1:
  return reterr;
}

void ComputeAlleleFreqs(const uintptr_t* variant_include, const uintptr_t* variant_allele_idxs, const uint64_t* founder_allele_dosages, uint32_t variant_ct, uint32_t maf_succ, double* allele_freqs) {
  // ok for maj_alleles or allele_freqs to be nullptr
  // note that founder_allele_dosages is in 32768ths
  uint32_t cur_allele_ct = 2;
  uint32_t variant_uidx = 0;
  for (uint32_t variant_idx = 0; variant_idx < variant_ct; ++variant_idx, ++variant_uidx) {
    MovU32To1Bit(variant_include, &variant_uidx);
    uintptr_t variant_allele_idx_base;
    if (!variant_allele_idxs) {
      variant_allele_idx_base = 2 * variant_uidx;
    } else {
      variant_allele_idx_base = variant_allele_idxs[variant_uidx];
      cur_allele_ct = variant_allele_idxs[variant_uidx + 1] - variant_allele_idx_base;
    }
    const uint64_t* cur_founder_allele_dosages = &(founder_allele_dosages[variant_allele_idx_base]);
    uint64_t tot_dosage = 0;
    for (uint32_t allele_idx = 0; allele_idx < cur_allele_ct; ++allele_idx) {
      tot_dosage += cur_founder_allele_dosages[allele_idx];
    }
    // todo: try changing this expression
    const uint64_t cur_maf_succ_dosage = (maf_succ | (!tot_dosage)) * kDosageMax;

    tot_dosage += cur_maf_succ_dosage * cur_allele_ct;
    double* cur_allele_freqs_base = &(allele_freqs[variant_allele_idx_base - variant_uidx]);
    const double tot_dosage_recip = 1.0 / u63tod(tot_dosage);
    const uint32_t cur_allele_ct_m1 = cur_allele_ct - 1;
    for (uint32_t allele_idx = 0; allele_idx < cur_allele_ct_m1; ++allele_idx) {
      const double cur_dosage = u63tod(cur_founder_allele_dosages[allele_idx] + cur_maf_succ_dosage);
      cur_allele_freqs_base[allele_idx] = cur_dosage * tot_dosage_recip;
    }
  }
}

CONSTU31(kMaxReadFreqAlleles, 255);

// relevant column types:
// 0: variant ID
// 1: ref allele code
// 2: all alt allele codes (potentially just alt1)
//
// (3-4 are --freq only)
// 3: ref freq/count
// 4: either all freqs/counts, or all-but-ref
//
// 5: obs ct (only relevant for --freq, but can be in --geno-counts)
//
// (6-11 are --geno-counts/--freqx only)
// 6: hom-ref count
// 7: het ref-alt counts (worst case, just ref-alt1)
// 8: altx-alty counts (worst case, just hom-alt1), or all pairs
// 9: hap-ref count
// 10: hap-alt counts (worst case, just hap-alt1), or all hap counts
// 11: --geno-counts numeq (if present, ignore 6..10)
//
// overrideable:
// 12->2: ALT1
// 13->4: ALT1_FREQ/ALT1_CT
// 14->7: HET_REF_ALT1_CT
// 15->8: HOM_ALT1_CT
// 16->10: HAP_ALT1_CT
ENUM_U31_DEF_START()
  kfReadFreqColVarId = 0,
  kfReadFreqColRefAllele,
  kfReadFreqColAltAlleles,

  kfReadFreqColRefFreq,
  kfReadFreqColAltFreqs,

  kfReadFreqColObsCt,

  kfReadFreqColHomRefCt,
  kfReadFreqColHetRefAltCts,
  kfReadFreqColNonrefDiploidCts,
  kfReadFreqColHapRefCt,
  kfReadFreqColHapAltCts,
  kfReadFreqColGenoCtNumeq,

  kfReadFreqColAlt1Allele,
  kfReadFreqColAlt1Freq,
  kfReadFreqColHetRefAlt1Ct,
  kfReadFreqColHomAlt1Ct,
  kfReadFreqColHapAlt1Ct,

  kfReadFreqColNull
ENUM_U31_DEF_END(ReadFreqColidx);

FLAGSET_DEF_START()
  kfReadFreqColset0,
  kfReadFreqColsetVarId = (1 << kfReadFreqColVarId),
  kfReadFreqColsetRefAllele = (1 << kfReadFreqColRefAllele),
  kfReadFreqColsetAltAlleles = (1 << kfReadFreqColAltAlleles),
  kfReadFreqColsetBase = (kfReadFreqColsetVarId | kfReadFreqColsetRefAllele | kfReadFreqColsetAltAlleles),

  kfReadFreqColsetRefFreq = (1 << kfReadFreqColRefFreq),
  kfReadFreqColsetAltFreqs = (1 << kfReadFreqColAltFreqs),
  kfReadFreqColsetAfreqOnly = (kfReadFreqColsetRefFreq | kfReadFreqColsetAltFreqs),

  kfReadFreqColsetObsCt = (1 << kfReadFreqColObsCt),

  kfReadFreqColsetHomRefCt = (1 << kfReadFreqColHomRefCt),
  kfReadFreqColsetHetRefAltCts = (1 << kfReadFreqColHetRefAltCts),
  kfReadFreqColsetNonrefDiploidCts = (1 << kfReadFreqColNonrefDiploidCts),
  kfReadFreqColsetHapRefCt = (1 << kfReadFreqColHapRefCt),
  kfReadFreqColsetHapAltCts = (1 << kfReadFreqColHapAltCts),
  kfReadFreqColsetGcountDefault = ((kfReadFreqColsetHapAltCts * 2) - kfReadFreqColsetHomRefCt),

  kfReadFreqColsetGenoCtNumeq = (1 << kfReadFreqColGenoCtNumeq),
  kfReadFreqColsetGcountOnly = (kfReadFreqColsetGcountDefault | kfReadFreqColsetGenoCtNumeq),

  kfReadFreqColsetAlt1Allele = (1 << kfReadFreqColAlt1Allele),
  kfReadFreqColsetAlt1Freq = (1 << kfReadFreqColAlt1Freq),
  kfReadFreqColsetHetRefAlt1Ct = (1 << kfReadFreqColHetRefAlt1Ct),
  kfReadFreqColsetHomAlt1Ct = (1 << kfReadFreqColHomAlt1Ct),
  kfReadFreqColsetHapAlt1Ct = (1 << kfReadFreqColHapAlt1Ct)
FLAGSET_DEF_END(ReadFreqColFlags);

PglErr ReadAlleleFreqs(const uintptr_t* variant_include, const char* const* variant_ids, const uintptr_t* variant_allele_idxs, const char* const* allele_storage, const char* read_freq_fname, uint32_t raw_variant_ct, uint32_t variant_ct, uint32_t max_alt_allele_ct, uint32_t max_variant_id_slen, uint32_t max_allele_slen, uint32_t maf_succ, uint32_t max_thread_ct, double* allele_freqs) {
  // support PLINK 1.9 --freq/--freqx, and 2.0 --freq/--geno-counts.
  // GCTA-format no longer supported since it inhibits the allele consistency
  // check.
  unsigned char* bigstack_mark = g_bigstack_base;
  uintptr_t line_idx = 0;
  PglErr reterr = kPglRetSuccess;
  ReadLineStream read_freq_rls;
  PreinitRLstream(&read_freq_rls);
  {
    double* cur_allele_freqs;
    uintptr_t* matched_loaded_alleles;
    uintptr_t* matched_internal_alleles;
    uint32_t* loaded_to_internal_allele_idx;
    uintptr_t* already_seen;
    if (bigstack_calloc_d(kMaxReadFreqAlleles, &cur_allele_freqs) ||
        bigstack_alloc_w(BitCtToWordCt(kMaxReadFreqAlleles), &matched_loaded_alleles) ||
        bigstack_alloc_w(BitCtToWordCt(max_alt_allele_ct + 1), &matched_internal_alleles) ||
        bigstack_alloc_u32(kMaxReadFreqAlleles, &loaded_to_internal_allele_idx) ||
        bigstack_calloc_w(BitCtToWordCt(raw_variant_ct), &already_seen)) {
      goto ReadAlleleFreqs_ret_NOMEM;
    }

    char* line_iter;
    reterr = SizeAndInitRLstreamRaw(read_freq_fname, bigstack_left() / 8, &read_freq_rls, &line_iter);
    if (reterr) {
      goto ReadAlleleFreqs_ret_1;
    }
    uint32_t* variant_id_htable = nullptr;
    uint32_t variant_id_htable_size;
    reterr = AllocAndPopulateIdHtableMt(variant_include, variant_ids, variant_ct, max_thread_ct, &variant_id_htable, nullptr, &variant_id_htable_size);
    if (reterr) {
      goto ReadAlleleFreqs_ret_1;
    }
    do {
      ++line_idx;
      reterr = RlsNextLstrip(&read_freq_rls, &line_iter);
      if (reterr) {
        if (reterr == kPglRetEof) {
          logerrputs("Error: Empty --read-freq file.\n");
          goto ReadAlleleFreqs_ret_MALFORMED_INPUT;
        }
        goto ReadAlleleFreqs_ret_READ_RLSTREAM;
      }
      // automatically skip header lines that start with '##' or '# '
    } while (IsEolnKns(*line_iter) || ((*line_iter == '#') && (ctou32(line_iter[1]) <= '#')));
    char* linebuf_first_token = line_iter;

    uint32_t col_skips[kfReadFreqColNull];
    ReadFreqColidx col_types[kfReadFreqColNull];
    uint32_t overrideable_pos[kfReadFreqColNull - kfReadFreqColAlt1Allele];
    uint32_t geno_counts = 0;
    uint32_t main_eq = 0;
    uint32_t is_numeq = 0;
    uint32_t use_obs_ct = 0;
    uint32_t infer_one_freq = 0;
    uint32_t infer_freq_loaded_idx = 0;
    uint32_t relevant_col_ct = 0;

    // interpretation of ColAltAlleles
    uint32_t allele_list_just_alt1 = 1;

    uint32_t is_frac = 0;  // if true, one frequency can be missing
    // could add consistency check (can't mix FREQ and CT)

    // interpretation of ColAltFreqs and ColNonrefDiploidCts
    uint32_t main_allele_idx_start = 1;
    uint32_t main_list_just_alt1 = 1;

    // interpretation of ColHapAltCts
    uint32_t hap_allele_idx_start = 1;
    uint32_t hap_list_just_alt1 = 1;

    uint32_t het_list_just_alt1 = 1;  // ColHetRefAltCts

    uint32_t biallelic_only = 0;

    ReadFreqColFlags header_cols = kfReadFreqColset0;
    if (*linebuf_first_token == '#') {
      // PLINK 2.0
      // guaranteed nonspace
      const char* linebuf_iter = &(linebuf_first_token[1]);

      uint32_t col_idx = 0;
      while (1) {
        const char* token_end = CurTokenEnd(linebuf_iter);
        const uint32_t token_slen = token_end - linebuf_iter;
        ReadFreqColidx cur_colidx = kfReadFreqColNull;
        if (token_slen <= 4) {
          if (strequal_k(linebuf_iter, "ID", token_slen)) {
            cur_colidx = kfReadFreqColVarId;
          } else if (token_slen == 3) {
            if (!memcmp(linebuf_iter, "REF", 3)) {
              cur_colidx = kfReadFreqColRefAllele;
            } else if (!memcmp(linebuf_iter, "ALT", 3)) {
              cur_colidx = kfReadFreqColAltAlleles;
              if (allele_list_just_alt1) {
                header_cols &= ~kfReadFreqColsetAlt1Allele;
                allele_list_just_alt1 = 0;
              }
            } else if (!memcmp(linebuf_iter, "CTS", 3)) {
              goto ReadAlleleFreqs_freqmain_found1;
            }
          } else if (strequal_k(linebuf_iter, "ALT1", token_slen) && allele_list_just_alt1) {
            cur_colidx = kfReadFreqColAlt1Allele;
          }
        } else if (strequal_k(linebuf_iter, "REF_FREQ", token_slen) ||
                   strequal_k(linebuf_iter, "REF_CT", token_slen)) {
          cur_colidx = kfReadFreqColRefFreq;
          if (linebuf_iter[4] == 'F') {
            is_frac = 1;
          }
        } else if ((strequal_k(linebuf_iter, "ALT1_FREQ", token_slen) ||
                    strequal_k(linebuf_iter, "ALT1_CT", token_slen)) &&
                   main_list_just_alt1) {
          cur_colidx = kfReadFreqColAlt1Freq;
          if (linebuf_iter[5] == 'F') {
            is_frac = 1;
          }
        } else if (strequal_k(linebuf_iter, "ALT_FREQS", token_slen) ||
                   strequal_k(linebuf_iter, "ALT_CTS", token_slen)) {
          if (linebuf_iter[4] == 'F') {
            is_frac = 1;
          }
          goto ReadAlleleFreqs_freqmain_found2;
        } else if (strequal_k(linebuf_iter, "FREQS", token_slen)) {
          is_frac = 1;
          goto ReadAlleleFreqs_freqmain_found1;
        } else if (strequal_k(linebuf_iter, "ALT_NUM_FREQS", token_slen) ||
                   strequal_k(linebuf_iter, "ALT_NUM_CTS", token_slen)) {
          is_numeq = 1;
          goto ReadAlleleFreqs_freqmain_found2;
        } else if (strequal_k(linebuf_iter, "NUM_FREQS", token_slen) ||
                   strequal_k(linebuf_iter, "NUM_CTS", token_slen)) {
          is_numeq = 1;
        ReadAlleleFreqs_freqmain_found1:
          main_allele_idx_start = 0;
        ReadAlleleFreqs_freqmain_found2:
          cur_colidx = kfReadFreqColAltFreqs;
          if (main_list_just_alt1) {
            header_cols &= ~kfReadFreqColsetAlt1Freq;
            main_list_just_alt1 = 0;
          }
        } else if (strequal_k(linebuf_iter, "OBS_CT", token_slen)) {
          cur_colidx = kfReadFreqColObsCt;
        } else if (strequal_k(linebuf_iter, "HOM_REF_CT", token_slen)) {
          cur_colidx = kfReadFreqColHomRefCt;
        } else if (strequal_k(linebuf_iter, "HET_REF_ALT1_CT", token_slen) && het_list_just_alt1) {
          cur_colidx = kfReadFreqColHetRefAlt1Ct;
        } else if (strequal_k(linebuf_iter, "HET_REF_ALT_CTS", token_slen)) {
          cur_colidx = kfReadFreqColHetRefAltCts;
          if (het_list_just_alt1) {
            header_cols &= ~kfReadFreqColsetHetRefAlt1Ct;
            het_list_just_alt1 = 0;
          }
        } else if (strequal_k(linebuf_iter, "HOM_ALT1_CT", token_slen) && main_list_just_alt1) {
          cur_colidx = kfReadFreqColHomAlt1Ct;
        } else if (strequal_k(linebuf_iter, "NONREF_DIPLOID_GENO_CTS", token_slen)) {
          goto ReadAlleleFreqs_countmain_found;
        } else if (strequal_k(linebuf_iter, "DIPLOID_GENO_CTS", token_slen)) {
          main_allele_idx_start = 0;
        ReadAlleleFreqs_countmain_found:
          cur_colidx = kfReadFreqColNonrefDiploidCts;
          if (main_list_just_alt1) {
            header_cols &= ~kfReadFreqColsetHomAlt1Ct;
            // could make this use a different variable than FREQS does
            main_list_just_alt1 = 0;
          }
        } else if (strequal_k(linebuf_iter, "HAP_REF_CT", token_slen)) {
          cur_colidx = kfReadFreqColHapRefCt;
        } else if (strequal_k(linebuf_iter, "HAP_ALT1_CT", token_slen) && hap_list_just_alt1) {
          cur_colidx = kfReadFreqColHapAlt1Ct;
        } else if (strequal_k(linebuf_iter, "HAP_ALT_CTS", token_slen)) {
          goto ReadAlleleFreqs_hapmain_found;
        } else if (strequal_k(linebuf_iter, "HAP_CTS", token_slen)) {
          hap_allele_idx_start = 0;
        ReadAlleleFreqs_hapmain_found:
          cur_colidx = kfReadFreqColHapAltCts;
          if (hap_list_just_alt1) {
            header_cols &= ~kfReadFreqColsetHapAlt1Ct;
            hap_list_just_alt1 = 0;
          }
        } else if (strequal_k(linebuf_iter, "GENO_NUM_CTS", token_slen)) {
          cur_colidx = kfReadFreqColGenoCtNumeq;
          is_numeq = 1;
        }
        if (cur_colidx != kfReadFreqColNull) {
          const ReadFreqColFlags cur_colset = S_CAST(ReadFreqColFlags, 1U << cur_colidx);
          if (header_cols & cur_colset) {
            logerrputs("Error: Conflicting columns in header line of --read-freq file.\n");
            goto ReadAlleleFreqs_ret_MALFORMED_INPUT;
          }
          if (cur_colidx >= kfReadFreqColAlt1Allele) {
            overrideable_pos[cur_colidx - kfReadFreqColAlt1Allele] = relevant_col_ct;
          }
          header_cols |= cur_colset;
          col_skips[relevant_col_ct] = col_idx;
          col_types[relevant_col_ct++] = cur_colidx;
        }
        linebuf_iter = FirstNonTspace(token_end);
        if (IsEolnKns(*linebuf_iter)) {
          break;
        }
        ++col_idx;
      }
      ReadFreqColFlags semifinal_header_cols = header_cols;
      if (header_cols & kfReadFreqColsetAlt1Allele) {
        header_cols ^= kfReadFreqColsetAltAlleles | kfReadFreqColsetAlt1Allele;
        col_types[overrideable_pos[0]] = kfReadFreqColAltAlleles;
      }
      if (header_cols & kfReadFreqColsetAlt1Freq) {
        header_cols ^= kfReadFreqColsetAltFreqs | kfReadFreqColsetAlt1Freq;
        col_types[overrideable_pos[kfReadFreqColAlt1Freq - kfReadFreqColAlt1Allele]] = kfReadFreqColAltFreqs;
      }
      if (header_cols & kfReadFreqColsetHetRefAlt1Ct) {
        header_cols ^= kfReadFreqColsetHetRefAltCts | kfReadFreqColsetHetRefAlt1Ct;
        col_types[overrideable_pos[kfReadFreqColHetRefAlt1Ct - kfReadFreqColAlt1Allele]] = kfReadFreqColHetRefAltCts;
      }
      if (header_cols & kfReadFreqColsetHomAlt1Ct) {
        header_cols ^= kfReadFreqColsetNonrefDiploidCts | kfReadFreqColsetHomAlt1Ct;
        col_types[overrideable_pos[kfReadFreqColHomAlt1Ct - kfReadFreqColAlt1Allele]] = kfReadFreqColNonrefDiploidCts;
      }
      if (header_cols & kfReadFreqColsetHapAlt1Ct) {
        header_cols ^= kfReadFreqColsetHapAltCts | kfReadFreqColsetHapAlt1Ct;
        col_types[overrideable_pos[kfReadFreqColHapAlt1Ct - kfReadFreqColAlt1Allele]] = kfReadFreqColHapAltCts;
      }
      if ((semifinal_header_cols != header_cols) && (!(header_cols & kfReadFreqColsetGenoCtNumeq))) {
        // we're treating at least one ALT1 column as if it spoke for all ALT
        // alleles
        biallelic_only = 1;
      }

      line_iter = K_CAST(char*, linebuf_iter);
      main_eq = is_numeq;
      semifinal_header_cols = header_cols;
      if (header_cols & kfReadFreqColsetAfreqOnly) {
        if (header_cols & kfReadFreqColsetGcountOnly) {
          logerrputs("Error: Conflicting columns in header line of --read-freq file (--freq and\n--geno-counts values mixed together).\n");
          goto ReadAlleleFreqs_ret_MALFORMED_INPUT;
        }
        ReadFreqColFlags header_cols_exempt = kfReadFreqColset0;
        if ((header_cols & kfReadFreqColsetAltFreqs) && (!is_numeq)) {
          // {ALT_}FREQS can be formatted as either
          //   0.5,0,0.2
          // or
          //   A=0.5,G=0.2
          // Look at the first nonheader line to distinguish between these two.
          reterr = RlsNextNonemptyLstrip(&read_freq_rls, &line_idx, &line_iter);
          if (reterr) {
            if (reterr == kPglRetEof) {
              logerrputs("Error: Empty --read-freq file.\n");
              goto ReadAlleleFreqs_ret_MALFORMED_INPUT;
            }
            goto ReadAlleleFreqs_ret_READ_RLSTREAM;
          }
          linebuf_first_token = line_iter;
          linebuf_iter = line_iter;
          const char* alt_freq_str = nullptr;
          for (uint32_t relevant_col_idx = 0; relevant_col_idx < relevant_col_ct; ++relevant_col_idx) {
            if (col_types[relevant_col_idx] == kfReadFreqColAltFreqs) {
              alt_freq_str = NextTokenMult0(linebuf_iter, col_skips[relevant_col_idx]);
              break;
            }
          }
          if (!alt_freq_str) {
            goto ReadAlleleFreqs_ret_MISSING_TOKENS;
          }
          const uint32_t alt_freq_slen = CurTokenEnd(alt_freq_str) - alt_freq_str;
          // bare '.' can only appear in eq formats
          main_eq = ((alt_freq_slen == 1) && (*alt_freq_str == '.')) || (memchr(alt_freq_str, '=', alt_freq_slen) != nullptr);
          if (main_eq) {
            header_cols_exempt = kfReadFreqColsetAltAlleles;
            if (!main_allele_idx_start) {
              header_cols_exempt |= kfReadFreqColsetRefAllele;
            }
            header_cols &= ~header_cols_exempt;
          }
        } else {
          // may as well not reprocess header line
          line_iter = AdvToDelim(line_iter, '\n');
          linebuf_first_token = line_iter;
        }
        if (((header_cols & kfReadFreqColsetBase) | header_cols_exempt) != kfReadFreqColsetBase) {
          logerrputs("Error: Missing column(s) in --read-freq file (ID, REF, ALT{1} usually\nrequired).\n");
          goto ReadAlleleFreqs_ret_MALFORMED_INPUT;
        }
        if (!main_allele_idx_start) {
          header_cols &= ~kfReadFreqColsetRefFreq;
        } else {
          if ((header_cols & (kfReadFreqColsetRefFreq | kfReadFreqColsetAltFreqs)) != (kfReadFreqColsetRefFreq | kfReadFreqColsetAltFreqs)) {
            if (main_list_just_alt1) {
              biallelic_only = 1;
            }
            infer_one_freq = 1;
            infer_freq_loaded_idx = (header_cols / kfReadFreqColsetRefFreq) & 1;
            if (!is_frac) {
              if (!(header_cols & kfReadFreqColsetObsCt)) {
                logerrputs("Error: Missing column(s) in --read-freq file (at least two of {REF_CT, ALT1_CT,\nALT_CTS, OBS_CT} must be present).\n");
                goto ReadAlleleFreqs_ret_MALFORMED_INPUT;
              }
              use_obs_ct = 1;
            }
          }
        }
        logputs("--read-freq: PLINK 2 --freq file detected.\n");
      } else if (header_cols & kfReadFreqColsetGcountOnly) {
        if ((header_cols & kfReadFreqColsetBase) != kfReadFreqColsetBase) {
          logerrputs("Error: Missing column(s) in --read-freq file (ID, REF, ALT{1} required).\n");
          goto ReadAlleleFreqs_ret_MALFORMED_INPUT;
        }
        // possible todo: allow one frequency/count to be missing.  (not really
        // necessary since PLINK 1.9 --freqx does not leave anything out,
        // unlike PLINK 1.x --freq)
        if (header_cols & kfReadFreqColsetGenoCtNumeq) {
          // don't need anything but GENO_NUM_CTS
          header_cols &= ~kfReadFreqColsetGcountDefault;
        } else {
          // require both diploid and haploid columns for now.  (could
          // conditionally drop one of these requirements later.)
          if (!(header_cols & kfReadFreqColsetNonrefDiploidCts)) {
            logerrputs("Error: Missing column(s) in --read-freq file (HOM_ALT1_CT,\nNONREF_DIPLOID_GENO_CTS, or DIPLOID_GENO_CTS required).\n");
            goto ReadAlleleFreqs_ret_MALFORMED_INPUT;
          }
          if (!main_allele_idx_start) {
            header_cols &= ~(kfReadFreqColsetHomRefCt | kfReadFreqColsetHetRefAltCts);
          } else if ((header_cols & (kfReadFreqColsetHomRefCt | kfReadFreqColsetHetRefAltCts)) != (kfReadFreqColsetHomRefCt | kfReadFreqColsetHetRefAltCts)) {
            logerrputs("Error: Missing column(s) in --read-freq file (HOM_REF_CT, HET_REF_ALT1_CT, or\nHET_REF_ALT_CTS required unless {DIPLOID_}GENO_CTS present).\n");
            goto ReadAlleleFreqs_ret_MALFORMED_INPUT;
          }
          if (!(header_cols & kfReadFreqColsetHapAltCts)) {
            logerrputs("Error: Missing column(s) in --read-freq file (HAP_ALT1_CT, HAP_ALT_CTS, or\nHAP_CTS required).\n");
            goto ReadAlleleFreqs_ret_MALFORMED_INPUT;
          }
          if (!hap_allele_idx_start) {
            header_cols &= ~kfReadFreqColsetHapRefCt;
          } else if (!(header_cols & kfReadFreqColsetHapRefCt)) {
            logerrputs("Error: Missing column(s) in --read-freq file (HAP_REF_CT required unless\nHAP_CTS or GENO_CTS present).\n");
            goto ReadAlleleFreqs_ret_MALFORMED_INPUT;
          }
        }
        geno_counts = 1;
        logputs("--read-freq: PLINK 2 --geno-counts file detected.\n");
        line_iter = AdvToDelim(line_iter, '\n');
        linebuf_first_token = line_iter;  // don't rescan header
      } else {
        logerrputs("Error: Missing column(s) in --read-freq file (no frequencies/counts).\n");
        goto ReadAlleleFreqs_ret_MALFORMED_INPUT;
      }
      if (!use_obs_ct) {
        header_cols &= ~kfReadFreqColsetObsCt;
      }
      if (semifinal_header_cols != header_cols) {
        // remove redundant columns
        uint32_t relevant_col_idx_read = 0;
        while ((S_CAST(uint32_t, header_cols) >> col_types[relevant_col_idx_read]) & 1) {
          ++relevant_col_idx_read;
        }
        uint32_t relevant_col_idx_write = relevant_col_idx_read;
        for (; relevant_col_idx_read < relevant_col_ct; ++relevant_col_idx_read) {
          const ReadFreqColidx cur_colidx = col_types[relevant_col_idx_read];
          if ((S_CAST(uint32_t, header_cols) >> cur_colidx) & 1) {
            col_types[relevant_col_idx_write] = cur_colidx;
            col_skips[relevant_col_idx_write] = col_skips[relevant_col_idx_read];
            ++relevant_col_idx_write;
          }
        }
        relevant_col_ct = relevant_col_idx_write;
      }
      for (uint32_t uii = relevant_col_ct - 1; uii; --uii) {
        col_skips[uii] -= col_skips[uii - 1];
      }
    } else {
      // PLINK 1.x
      // .frq:       CHR  SNP  A1  A2  MAF        NCHROBS
      // .frq.count: CHR  SNP  A1  A2  C1         C2       G0
      // .frqx:      CHR  SNP  A1  A2  C(HOM A1)  C(HET)   C(HOM A2)  C(HAP A1)
      //   C(HAP A2)  C(MISSING)
      // (yeah, the spaces in the .frqx header were a mistake, should have used
      // underscores.  oh well, live and learn.)
      col_skips[0] = 1;
      col_skips[1] = 1;
      col_skips[2] = 1;
      col_skips[3] = 1;

      col_types[0] = kfReadFreqColVarId;
      // doesn't matter if we treat A1 or A2 as ref
      col_types[1] = kfReadFreqColRefAllele;
      col_types[2] = kfReadFreqColAltAlleles;
      biallelic_only = 1;
      if (StrStartsWithUnsafe(linebuf_first_token, "CHR\tSNP\tA1\tA2\tC(HOM A1)\tC(HET)\tC(HOM A2)\tC(HAP A1)\tC(HAP A2)\tC(MISSING)")) {
        col_skips[4] = 1;
        col_skips[5] = 1;
        col_skips[6] = 1;
        col_skips[7] = 1;

        col_types[3] = kfReadFreqColHomRefCt;
        col_types[4] = kfReadFreqColHetRefAltCts;
        col_types[5] = kfReadFreqColNonrefDiploidCts;
        col_types[6] = kfReadFreqColHapRefCt;
        col_types[7] = kfReadFreqColHapAltCts;
        header_cols = kfReadFreqColsetBase | kfReadFreqColsetGcountOnly;
        geno_counts = 1;
        relevant_col_ct = 8;
        logputs("--read-freq: PLINK 1.9 --freqx file detected.\n");
      } else {
        if (!tokequal_k(linebuf_first_token, "CHR")) {
          goto ReadAlleleFreqs_ret_UNRECOGNIZED_HEADER;
        }
        const char* linebuf_iter = FirstNonTspace(&(linebuf_first_token[3]));
        if (!tokequal_k(linebuf_iter, "SNP")) {
          goto ReadAlleleFreqs_ret_UNRECOGNIZED_HEADER;
        }
        linebuf_iter = FirstNonTspace(&(linebuf_iter[3]));
        if (!tokequal_k(linebuf_iter, "A1")) {
          goto ReadAlleleFreqs_ret_UNRECOGNIZED_HEADER;
        }
        linebuf_iter = FirstNonTspace(&(linebuf_iter[2]));
        if (!tokequal_k(linebuf_iter, "A2")) {
          goto ReadAlleleFreqs_ret_UNRECOGNIZED_HEADER;
        }
        linebuf_iter = FirstNonTspace(&(linebuf_iter[2]));
        col_types[3] = kfReadFreqColRefFreq;
        if (tokequal_k(linebuf_iter, "MAF")) {
          is_frac = 1;
          infer_one_freq = 1;
          infer_freq_loaded_idx = 1;
          header_cols = kfReadFreqColsetBase | kfReadFreqColsetRefFreq;
          relevant_col_ct = 4;
          logputs("--read-freq: PLINK 1.x --freq file detected.\n");
        } else {
          if (!tokequal_k(linebuf_iter, "C1")) {
            goto ReadAlleleFreqs_ret_UNRECOGNIZED_HEADER;
          }
          linebuf_iter = FirstNonTspace(&(linebuf_iter[2]));
          if (!tokequal_k(linebuf_iter, "C2")) {
            goto ReadAlleleFreqs_ret_UNRECOGNIZED_HEADER;
          }
          col_skips[4] = 1;
          col_types[4] = kfReadFreqColAltFreqs;
          header_cols = kfReadFreqColsetBase | kfReadFreqColsetAfreqOnly;
          relevant_col_ct = 5;
          logputs("--read-freq: PLINK 1.x '--freq counts' file detected.\n");
        }
        line_iter = K_CAST(char*, linebuf_iter);
      }
      line_iter = AdvToDelim(line_iter, '\n');
      linebuf_first_token = line_iter;  // don't rescan header
    }
    assert(relevant_col_ct <= 8);

    double freq_max = 4294967295.0;
    if (is_frac) {
      maf_succ = 0;
      freq_max = 1.0;
    }
    uintptr_t skipped_variant_ct = 0;
    uint32_t loaded_variant_ct = 0;
    uint32_t cur_allele_ct = 2;
    while (1) {
      if (!IsEolnKns(*linebuf_first_token)) {
        // not const since tokens may be null-terminated or comma-terminated
        // later
        char* token_ptrs[12];
        uint32_t token_slens[12];
        line_iter = TokenLex0(linebuf_first_token, R_CAST(uint32_t*, col_types), col_skips, relevant_col_ct, token_ptrs, token_slens);
        if (!line_iter) {
          goto ReadAlleleFreqs_ret_MISSING_TOKENS;
        }
        // characters may be modified, so better find the \n now just in case
        // it gets clobbered
        line_iter = AdvToDelim(line_iter, '\n');
        const char* variant_id_start = token_ptrs[kfReadFreqColVarId];
        const uint32_t variant_id_slen = token_slens[kfReadFreqColVarId];
        uint32_t variant_uidx = VariantIdDupflagHtableFind(variant_id_start, variant_ids, variant_id_htable, variant_id_slen, variant_id_htable_size, max_variant_id_slen);
        if (variant_uidx >> 31) {
          if (variant_uidx == UINT32_MAX) {
            goto ReadAlleleFreqs_skip_variant;
          }
          snprintf(g_logbuf, kLogbufSize, "Error: --read-freq variant ID '%s' appears multiple times in main dataset.\n", variant_ids[variant_uidx & 0x7fffffff]);
          goto ReadAlleleFreqs_ret_MALFORMED_INPUT_WW;
        }
        if (IsSet(already_seen, variant_uidx)) {
          snprintf(g_logbuf, kLogbufSize, "Error: Variant ID '%s' appears multiple times in --read-freq file.\n", variant_ids[variant_uidx]);
          goto ReadAlleleFreqs_ret_MALFORMED_INPUT_WW;
        }
        SetBit(variant_uidx, already_seen);

        uintptr_t variant_allele_idx_base;
        if (!variant_allele_idxs) {
          variant_allele_idx_base = variant_uidx * 2;
        } else {
          variant_allele_idx_base = variant_allele_idxs[variant_uidx];
          cur_allele_ct = variant_allele_idxs[variant_uidx + 1] - variant_allele_idx_base;
          if (biallelic_only) {
            goto ReadAlleleFreqs_skip_variant;
          }
        }
        ZeroWArr(BitCtToWordCt(cur_allele_ct), matched_internal_alleles);
        const char* const* cur_alleles = &(allele_storage[variant_allele_idx_base]);
        uint32_t loaded_allele_ct = 0;
        if (header_cols & kfReadFreqColsetRefAllele) {
          uint32_t cur_loaded_allele_code_slen = token_slens[kfReadFreqColRefAllele];
          uint32_t unmatched_allele_ct = cur_allele_ct;
          char* cur_loaded_allele_code = token_ptrs[kfReadFreqColRefAllele];
          cur_loaded_allele_code[cur_loaded_allele_code_slen] = '\0';
          char* loaded_allele_code_iter;
          char* loaded_allele_code_end;
          if (header_cols & kfReadFreqColsetAltAlleles) {
            loaded_allele_code_iter = token_ptrs[kfReadFreqColAltAlleles];
            loaded_allele_code_end = &(loaded_allele_code_iter[token_slens[kfReadFreqColAltAlleles]]);
            *loaded_allele_code_end++ = ',';
          } else {
            // special case: with --freq alteq or alteqz column, we only need
            // to scrape REF here
            loaded_allele_code_iter = &(cur_loaded_allele_code[cur_loaded_allele_code_slen + 1]);
            loaded_allele_code_end = loaded_allele_code_iter;
          }
          uint32_t widx = 0;
          while (1) {
            if (!(loaded_allele_ct % kBitsPerWord)) {
              widx = loaded_allele_ct / kBitsPerWord;
              matched_loaded_alleles[widx] = 0;
            }
            if (cur_loaded_allele_code_slen <= max_allele_slen) {
              uint32_t internal_allele_idx = 0;
              uint32_t unmatched_allele_idx = 0;
              for (; unmatched_allele_idx < unmatched_allele_ct; ++unmatched_allele_idx, ++internal_allele_idx) {
                MovU32To0Bit(matched_internal_alleles, &internal_allele_idx);
                if (!strcmp(cur_loaded_allele_code, cur_alleles[internal_allele_idx])) {
                  break;
                }
              }
              if (unmatched_allele_idx != unmatched_allele_ct) {
                // success
                if (IsSet(matched_internal_alleles, internal_allele_idx)) {
                  snprintf(g_logbuf, kLogbufSize, "Error: Duplicate allele code on line %" PRIuPTR " of --read-freq file.\n", line_idx);
                  goto ReadAlleleFreqs_ret_MALFORMED_INPUT_2;
                }
                SetBit(internal_allele_idx, matched_internal_alleles);
                SetBit(loaded_allele_ct, matched_loaded_alleles);
                loaded_to_internal_allele_idx[loaded_allele_ct] = internal_allele_idx;
              }
            }
            ++loaded_allele_ct;
            if (loaded_allele_code_iter == loaded_allele_code_end) {
              break;
            }
            if (loaded_allele_ct == kMaxReadFreqAlleles) {
              snprintf(g_logbuf, kLogbufSize, "Error: --read-freq file entry for variant ID '%s' has more than %u ALT alleles.\n", variant_ids[variant_uidx], kMaxReadFreqAlleles - 1);
              goto ReadAlleleFreqs_ret_MALFORMED_INPUT_WW;
            }
            cur_loaded_allele_code = loaded_allele_code_iter;
            loaded_allele_code_iter = S_CAST(char*, memchr(loaded_allele_code_iter, ',', loaded_allele_code_end - cur_loaded_allele_code));
            cur_loaded_allele_code_slen = loaded_allele_code_iter - cur_loaded_allele_code;
            *loaded_allele_code_iter++ = '\0';
          }
        }

        double* allele_freqs_write = &(allele_freqs[variant_allele_idx_base - variant_uidx]);
        if (geno_counts) {
          ZeroDArr(cur_allele_ct, cur_allele_freqs);
          if (is_numeq) {
            const uint32_t full_slen = token_slens[kfReadFreqColGenoCtNumeq];
            char* geno_num_cts = token_ptrs[kfReadFreqColGenoCtNumeq];
            if (full_slen > 1) {
              geno_num_cts[full_slen] = ',';
              const char* geno_num_cts_iter = geno_num_cts;
              const char* geno_num_cts_end = &(geno_num_cts[full_slen]);
#ifndef __LP64__
              const uint32_t cap_div_10 = (loaded_allele_ct - 1) / 10;
              const uint32_t cap_mod_10 = (loaded_allele_ct - 1) % 10;
#endif
              while (1) {
                uint32_t second_loaded_allele_idx = UINT32_MAX;
                uint32_t first_loaded_allele_idx;
#ifdef __LP64__
                if (ScanmovUintCapped(loaded_allele_ct - 1, &geno_num_cts_iter, &first_loaded_allele_idx)) {
                  goto ReadAlleleFreqs_ret_INVALID_FREQS;
                }
                if (*geno_num_cts_iter == '/') {
                  ++geno_num_cts_iter;
                  if (ScanmovUintCapped(loaded_allele_ct - 1, &geno_num_cts_iter, &second_loaded_allele_idx)) {
                    goto ReadAlleleFreqs_ret_INVALID_FREQS;
                  }
                }
#else
                if (ScanmovUintCapped32(cap_div_10, cap_mod_10, &geno_num_cts_iter, &first_loaded_allele_idx)) {
                  goto ReadAlleleFreqs_ret_INVALID_FREQS;
                }
                if (*geno_num_cts_iter == '/') {
                  ++geno_num_cts_iter;
                  if (ScanmovUintCapped32(cap_div_10, cap_mod_10, &geno_num_cts_iter, &second_loaded_allele_idx)) {
                    goto ReadAlleleFreqs_ret_INVALID_FREQS;
                  }
                }
#endif
                if (*geno_num_cts_iter != '=') {
                  goto ReadAlleleFreqs_ret_INVALID_FREQS;
                }
                ++geno_num_cts_iter;
                double dxx;
                const char* cur_ct_end = ScanadvDouble(geno_num_cts_iter, &dxx);
                if ((!cur_ct_end) || (*cur_ct_end != ',') || (dxx < 0.0) || (dxx > 4294967295.0)) {
                  goto ReadAlleleFreqs_ret_INVALID_FREQS;
                }
                if (IsSet(matched_loaded_alleles, first_loaded_allele_idx)) {
                  cur_allele_freqs[loaded_to_internal_allele_idx[first_loaded_allele_idx]] += dxx;
                }
                if ((second_loaded_allele_idx != UINT32_MAX) && IsSet(matched_loaded_alleles, second_loaded_allele_idx)) {
                  cur_allele_freqs[loaded_to_internal_allele_idx[second_loaded_allele_idx]] += dxx;
                }
                geno_num_cts_iter = cur_ct_end;
                if (geno_num_cts_iter == geno_num_cts_end) {
                  break;
                }
                ++geno_num_cts_iter;
              }
            } else if (*geno_num_cts != '.') {
              goto ReadAlleleFreqs_ret_INVALID_FREQS;
            }
          } else {
            const uint32_t internal0 = IsSet(matched_loaded_alleles, 0)? loaded_to_internal_allele_idx[0] : UINT32_MAX;
            if (header_cols & kfReadFreqColsetHomRefCt) {
              if (internal0 != UINT32_MAX) {
                const char* hom_ref_str = token_ptrs[kfReadFreqColHomRefCt];
                double dxx;
                const char* hom_ref_end = ScanadvDouble(hom_ref_str, &dxx);
                if ((!hom_ref_end) || (hom_ref_end != &(hom_ref_str[token_slens[kfReadFreqColHomRefCt]])) || (dxx < 0.0) || (dxx > 4294967295.0)) {
                  goto ReadAlleleFreqs_ret_INVALID_FREQS;
                }
                cur_allele_freqs[internal0] += 2 * dxx;
              }

              char* het_refalt = token_ptrs[kfReadFreqColHetRefAltCts];
              const uint32_t het_refalt_slen = token_slens[kfReadFreqColHetRefAltCts];
              het_refalt[het_refalt_slen] = ',';
              const char* het_refalt_iter = het_refalt;
              const char* het_refalt_end = &(het_refalt[het_refalt_slen]);
              for (uint32_t alt_allele_idx = 1; alt_allele_idx < cur_allele_ct; ++alt_allele_idx) {
                if (het_refalt_iter >= het_refalt_end) {
                  goto ReadAlleleFreqs_ret_INVALID_FREQS;
                }
                double dxx;
                const char* cur_entry_end = ScanadvDouble(het_refalt_iter, &dxx);
                if ((!cur_entry_end) || (*cur_entry_end != ',') || (dxx < 0.0) || (dxx > 4294967295.0)) {
                  goto ReadAlleleFreqs_ret_INVALID_FREQS;
                }
                if (internal0 != UINT32_MAX) {
                  cur_allele_freqs[internal0] += dxx;
                }
                if (IsSet(matched_loaded_alleles, alt_allele_idx)) {
                  cur_allele_freqs[loaded_to_internal_allele_idx[alt_allele_idx]] += dxx;
                }
                het_refalt_iter = &(cur_entry_end[1]);
              }
            }
            // ColNonrefDiploidCts required
            char* diploid_cts = token_ptrs[kfReadFreqColNonrefDiploidCts];
            const uint32_t diploid_cts_slen = token_slens[kfReadFreqColNonrefDiploidCts];
            diploid_cts[diploid_cts_slen] = ',';
            const char* diploid_cts_iter = diploid_cts;
            const char* diploid_cts_end = &(diploid_cts[diploid_cts_slen]);
            for (uint32_t second_allele_idx = main_allele_idx_start; second_allele_idx < cur_allele_ct; ++second_allele_idx) {
              uint32_t internalx = UINT32_MAX;
              if (IsSet(matched_loaded_alleles, second_allele_idx)) {
                internalx = loaded_to_internal_allele_idx[second_allele_idx];
              }
              // 1/1, 1/2, 2/2, 1/3, ...
              for (uint32_t first_allele_idx = main_allele_idx_start; first_allele_idx <= second_allele_idx; ++first_allele_idx) {
                if (diploid_cts_iter >= diploid_cts_end) {
                  goto ReadAlleleFreqs_ret_INVALID_FREQS;
                }
                double dxx;
                const char* cur_entry_end = ScanadvDouble(diploid_cts_iter, &dxx);
                if ((!cur_entry_end) || (*cur_entry_end != ',') || (dxx < 0.0) || (dxx > 4294967295.0)) {
                  goto ReadAlleleFreqs_ret_INVALID_FREQS;
                }
                if (IsSet(matched_loaded_alleles, first_allele_idx)) {
                  cur_allele_freqs[loaded_to_internal_allele_idx[first_allele_idx]] += dxx;
                }
                if (internalx != UINT32_MAX) {
                  cur_allele_freqs[internalx] += dxx;
                }
                diploid_cts_iter = &(cur_entry_end[1]);
              }
            }

            if ((header_cols & kfReadFreqColsetHapRefCt) && (internal0 != UINT32_MAX)) {
              const char* hap_ref_str = token_ptrs[kfReadFreqColHapRefCt];
              double dxx;
              const char* hap_ref_end = ScanadvDouble(hap_ref_str, &dxx);
              if ((!hap_ref_end) || (hap_ref_end != &(hap_ref_str[token_slens[kfReadFreqColHapRefCt]])) || (dxx < 0.0) || (dxx > 4294967295.0)) {
                goto ReadAlleleFreqs_ret_INVALID_FREQS;
              }
              cur_allele_freqs[internal0] += dxx;
            }
            // ColHapAltCts required
            char* hap_alt = token_ptrs[kfReadFreqColHapAltCts];
            const uint32_t hap_alt_slen = token_slens[kfReadFreqColHapAltCts];
            hap_alt[hap_alt_slen] = ',';
            const char* hap_alt_iter = hap_alt;
            const char* hap_alt_end = &(hap_alt[hap_alt_slen]);
            for (uint32_t alt_allele_idx = 1; alt_allele_idx < cur_allele_ct; ++alt_allele_idx) {
              if (hap_alt_iter >= hap_alt_end) {
                goto ReadAlleleFreqs_ret_INVALID_FREQS;
              }
              double dxx;
              const char* cur_entry_end = ScanadvDouble(hap_alt_iter, &dxx);
              if ((!cur_entry_end) || (*cur_entry_end != ',') || (dxx < 0.0) || (dxx > 4294967295.0)) {
                goto ReadAlleleFreqs_ret_INVALID_FREQS;
              }
              if (IsSet(matched_loaded_alleles, alt_allele_idx)) {
                cur_allele_freqs[loaded_to_internal_allele_idx[alt_allele_idx]] += dxx;
              }
              hap_alt_iter = &(cur_entry_end[1]);
            }
          }
        } else {
          if ((header_cols & kfReadFreqColsetRefFreq) && IsSet(matched_loaded_alleles, 0)) {
            const char* ref_freq_str = token_ptrs[kfReadFreqColRefFreq];
            double dxx;
            if (!ScanadvDouble(ref_freq_str, &dxx)) {
              if (IsNanStr(ref_freq_str, token_slens[kfReadFreqColRefFreq])) {
                goto ReadAlleleFreqs_skip_variant;
              }
              snprintf(g_logbuf, kLogbufSize, "Error: Invalid REF frequency/count on line %" PRIuPTR " of --read-freq file.\n", line_idx);
              goto ReadAlleleFreqs_ret_MALFORMED_INPUT_WW;
            }
            if ((dxx < 0.0) || (dxx > freq_max)) {
              snprintf(g_logbuf, kLogbufSize, "Error: Invalid REF frequency/count on line %" PRIuPTR " of --read-freq file.\n", line_idx);
              goto ReadAlleleFreqs_ret_MALFORMED_INPUT_WW;
            }
            cur_allele_freqs[loaded_to_internal_allele_idx[0]] = dxx;
          }
          if (header_cols & kfReadFreqColsetAltFreqs) {
            const uint32_t full_slen = token_slens[kfReadFreqColAltFreqs];
            char* alt_freq_start = token_ptrs[kfReadFreqColAltFreqs];
            alt_freq_start[full_slen] = ',';
            if (!main_eq) {
              const char* alt_freq_iter = alt_freq_start;
              const char* alt_freq_end = &(alt_freq_start[full_slen]);
              for (uint32_t allele_idx = main_allele_idx_start; allele_idx < loaded_allele_ct; ++allele_idx, ++alt_freq_iter) {
                if (alt_freq_iter >= alt_freq_end) {
                  goto ReadAlleleFreqs_ret_INVALID_FREQS;
                }
                if (!IsSet(matched_loaded_alleles, allele_idx)) {
                  alt_freq_iter = AdvToDelim(alt_freq_iter, ',');
                  continue;
                }
                double dxx;
                const char* cur_freq_end = ScanadvDouble(alt_freq_iter, &dxx);
                if (!cur_freq_end) {
                  cur_freq_end = AdvToDelim(alt_freq_iter, ',');
                  if (IsNanStr(alt_freq_iter, cur_freq_end - alt_freq_iter)) {
                    goto ReadAlleleFreqs_skip_variant;
                  }
                  goto ReadAlleleFreqs_ret_INVALID_FREQS;
                }
                if ((*cur_freq_end != ',') || (dxx < 0.0) || (dxx > freq_max)) {
                  goto ReadAlleleFreqs_ret_INVALID_FREQS;
                }
                alt_freq_iter = cur_freq_end;
                cur_allele_freqs[loaded_to_internal_allele_idx[allele_idx]] = dxx;
              }
            } else {
              ZeroDArr(cur_allele_ct, cur_allele_freqs);
              if ((full_slen > 1) || (*alt_freq_start != '.')) {
                if (is_numeq) {
                  const char* alt_freq_iter = alt_freq_start;
                  const char* alt_freq_end = &(alt_freq_start[full_slen]);
#ifndef __LP64__
                  const uint32_t cap_div_10 = (loaded_allele_ct - 1) / 10;
                  const uint32_t cap_mod_10 = (loaded_allele_ct - 1) % 10;
#endif
                  while (1) {
                    const char* cur_entry_end = AdvToDelim(alt_freq_iter, ',');
                    uint32_t loaded_allele_idx;
#ifdef __LP64__
                    if (ScanmovUintCapped(loaded_allele_ct - 1, &alt_freq_iter, &loaded_allele_idx)) {
                      goto ReadAlleleFreqs_ret_INVALID_FREQS;
                    }
#else
                    if (ScanmovUintCapped32(cap_div_10, cap_mod_10, &alt_freq_iter, &loaded_allele_idx)) {
                      goto ReadAlleleFreqs_ret_INVALID_FREQS;
                    }
#endif
                    if (*alt_freq_iter != '=') {
                      goto ReadAlleleFreqs_ret_INVALID_FREQS;
                    }
                    if (IsSet(matched_loaded_alleles, loaded_allele_idx)) {
                      const uint32_t internal_allele_idx = loaded_to_internal_allele_idx[loaded_allele_idx];
                      if (cur_allele_freqs[internal_allele_idx]) {
                        snprintf(g_logbuf, kLogbufSize, "Error: Duplicate entry on line %" PRIuPTR " of --read-freq file.\n", line_idx);
                        goto ReadAlleleFreqs_ret_MALFORMED_INPUT_2;
                      }
                      ++alt_freq_iter;
                      double dxx;
                      const char* cur_freq_end = ScanadvDouble(alt_freq_iter, &dxx);
                      if (!cur_freq_end) {
                        if (IsNanStr(alt_freq_iter, cur_entry_end - alt_freq_iter)) {
                          goto ReadAlleleFreqs_skip_variant;
                        }
                        goto ReadAlleleFreqs_ret_INVALID_FREQS;
                      }
                      if ((cur_freq_end != cur_entry_end) || (dxx < 0.0) || (dxx > freq_max)) {
                        goto ReadAlleleFreqs_ret_INVALID_FREQS;
                      }
                      cur_allele_freqs[internal_allele_idx] = dxx;
                    }
                    alt_freq_iter = cur_entry_end;
                    if (alt_freq_iter == alt_freq_end) {
                      break;
                    }
                    ++alt_freq_iter;
                  }
                } else {
                  char* alt_freq_iter = alt_freq_start;
                  char* alt_freq_end = &(alt_freq_start[full_slen]);
                  while (1) {
                    char* cur_entry_end = AdvToDelim(alt_freq_iter, ',');
                    const uint32_t cur_entry_slen = cur_entry_end - alt_freq_iter;
                    char* eq_ptr = S_CAST(char*, memchr(alt_freq_iter, '=', cur_entry_slen));
                    if (!eq_ptr) {
                      goto ReadAlleleFreqs_ret_INVALID_FREQS;
                    }

                    // currently necessary for strcmp()
                    *eq_ptr = '\0';

                    uint32_t internal_allele_idx = 0;
                    // O(n^2), may want to replace with O(n log n)
                    for (; internal_allele_idx < cur_allele_ct; ++internal_allele_idx) {
                      if (!strcmp(alt_freq_iter, cur_alleles[internal_allele_idx])) {
                        if (cur_allele_freqs[internal_allele_idx]) {
                          snprintf(g_logbuf, kLogbufSize, "Error: Duplicate entry on line %" PRIuPTR " of --read-freq file.\n", line_idx);
                          goto ReadAlleleFreqs_ret_MALFORMED_INPUT_2;
                        }
                        alt_freq_iter = &(eq_ptr[1]);
                        double dxx;
                        const char* cur_freq_end = ScanadvDouble(alt_freq_iter, &dxx);
                        if (!cur_freq_end) {
                          if (IsNanStr(alt_freq_iter, cur_entry_end - alt_freq_iter)) {
                            goto ReadAlleleFreqs_skip_variant;
                          }
                          goto ReadAlleleFreqs_ret_INVALID_FREQS;
                        }
                        if ((cur_freq_end != cur_entry_end) || (dxx < 0.0) || (dxx > freq_max)) {
                          goto ReadAlleleFreqs_ret_INVALID_FREQS;
                        }
                        cur_allele_freqs[internal_allele_idx] = dxx;
                        break;
                      }
                    }
                    alt_freq_iter = cur_entry_end;
                    if (alt_freq_iter == alt_freq_end) {
                      break;
                    }
                    ++alt_freq_iter;
                  }
                }
              }
            }
          }
        }
        if (infer_one_freq && IsSet(matched_loaded_alleles, infer_freq_loaded_idx)) {
          double obs_ct_recip = 1.0;
          if (header_cols & kfReadFreqColsetObsCt) {
            uint32_t obs_ct_raw;
            if (ScanUintCapped(token_ptrs[kfReadFreqColObsCt], UINT32_MAX, &obs_ct_raw)) {
              snprintf(g_logbuf, kLogbufSize, "Error: Invalid allele count on line %" PRIuPTR " of --read-freq file.\n", line_idx);
              goto ReadAlleleFreqs_ret_MALFORMED_INPUT_2;
            }
            uint64_t obs_ct = obs_ct_raw + S_CAST(uint64_t, maf_succ) * cur_allele_ct;
            if (!obs_ct) {
              goto ReadAlleleFreqs_skip_variant;
            }
            obs_ct_recip = 1.0 / u63tod(obs_ct);
          }
          const uint32_t infer_freq_internal_idx = loaded_to_internal_allele_idx[infer_freq_loaded_idx];
          if (cur_allele_ct == 2) {
            // optimize common case
            double known_freq_d = cur_allele_freqs[1 - infer_freq_internal_idx];
            if (maf_succ) {
              known_freq_d += 1;
            }
            double known_scaled_freq = known_freq_d * obs_ct_recip;
            if (known_scaled_freq <= 1.0) {
              if (infer_freq_internal_idx) {
                allele_freqs_write[0] = known_scaled_freq;
              } else {
                allele_freqs_write[0] = 1.0 - known_scaled_freq;
              }
            } else if (known_scaled_freq <= (1.0 / 0.99)) {
              if (infer_freq_internal_idx) {
                allele_freqs_write[0] = 1.0;
              } else {
                allele_freqs_write[0] = 0.0;
              }
            } else {
              snprintf(g_logbuf, kLogbufSize, "Error: Frequency/count too large on line %" PRIuPTR " of --read-freq file.\n", line_idx);
              goto ReadAlleleFreqs_ret_MALFORMED_INPUT_2;
            }
          } else {
            if (maf_succ) {
              for (uint32_t internal_allele_idx = 0; internal_allele_idx < cur_allele_ct; ++internal_allele_idx) {
                cur_allele_freqs[internal_allele_idx] += 1;
              }
            }
            cur_allele_freqs[infer_freq_internal_idx] = 0.0;
            double known_freq_sum_d = 0.0;
            for (uint32_t internal_allele_idx = 0; internal_allele_idx < cur_allele_ct; ++internal_allele_idx) {
              known_freq_sum_d += cur_allele_freqs[internal_allele_idx];
            }
            double known_scaled_freq_sum = known_freq_sum_d * obs_ct_recip;
            if (known_scaled_freq_sum <= (1.0 / 0.99)) {
              if (known_scaled_freq_sum > 1.0) {
                // possible rounding error, rescale
                obs_ct_recip = 1.0 / known_scaled_freq_sum;
                known_scaled_freq_sum = 1.0;
              }
              const uint32_t cur_allele_ct_m1 = cur_allele_ct - 1;
              for (uint32_t internal_allele_idx = 0; internal_allele_idx < cur_allele_ct_m1; ++internal_allele_idx) {
                double dxx;
                if (internal_allele_idx == infer_freq_internal_idx) {
                  dxx = 1.0 - known_scaled_freq_sum;
                } else {
                  dxx = obs_ct_recip * cur_allele_freqs[internal_allele_idx];
                }
                allele_freqs_write[internal_allele_idx] = dxx;
              }
            } else {
              snprintf(g_logbuf, kLogbufSize, "Error: Frequency/count too large on line %" PRIuPTR " of --read-freq file.\n", line_idx);
              goto ReadAlleleFreqs_ret_MALFORMED_INPUT_2;
            }
          }
        } else {
          // complete frequency or count data
          if (maf_succ) {
            for (uint32_t internal_allele_idx = 0; internal_allele_idx < cur_allele_ct; ++internal_allele_idx) {
              cur_allele_freqs[internal_allele_idx] += 1;
            }
          }
          double tot_freq = 0.0;
          for (uint32_t internal_allele_idx = 0; internal_allele_idx < cur_allele_ct; ++internal_allele_idx) {
            tot_freq += cur_allele_freqs[internal_allele_idx];
          }
          if (tot_freq == 0.0) {
            goto ReadAlleleFreqs_skip_variant;
          }
          const double tot_freq_recip = 1.0 / tot_freq;
          const uint32_t cur_allele_ct_m1 = cur_allele_ct - 1;
          for (uint32_t internal_allele_idx = 0; internal_allele_idx < cur_allele_ct_m1; ++internal_allele_idx) {
            allele_freqs_write[internal_allele_idx] = tot_freq_recip * cur_allele_freqs[internal_allele_idx];
          }
        }

        ++loaded_variant_ct;
        if (!(loaded_variant_ct % 10000)) {
          printf("\r--read-freq: Frequencies for %uk variants loaded.", loaded_variant_ct / 1000);
          fflush(stdout);
        }
      } else {
        line_iter = AdvToDelim(line_iter, '\n');
      }
      while (0) {
      ReadAlleleFreqs_skip_variant:
        ++skipped_variant_ct;
      }
      ++line_iter;
      ++line_idx;
      reterr = RlsPostlfNext(&read_freq_rls, &line_iter);
      if (reterr) {
        if (reterr == kPglRetEof) {
          reterr = kPglRetSuccess;
          break;
        }
        goto ReadAlleleFreqs_ret_READ_RLSTREAM;
      }
      linebuf_first_token = FirstNonTspace(line_iter);
    }
    putc_unlocked('\r', stdout);
    logprintf("--read-freq: Frequencies for %u variant%s loaded.\n", loaded_variant_ct, (loaded_variant_ct == 1)? "" : "s");
    if (skipped_variant_ct) {
      logerrprintfww("Warning: %" PRIuPTR " entr%s skipped due to missing variant IDs, mismatching allele codes, and/or zero observations.\n", skipped_variant_ct, (skipped_variant_ct == 1)? "y" : "ies");
    }
  }
  while (0) {
  ReadAlleleFreqs_ret_READ_RLSTREAM:
    RLstreamErrPrint("--read-freq file", &read_freq_rls, &reterr);
    break;
  ReadAlleleFreqs_ret_NOMEM:
    reterr = kPglRetNomem;
    break;
  ReadAlleleFreqs_ret_UNRECOGNIZED_HEADER:
    logerrputs("Error: Unrecognized header line in --read-freq file.\n");
    reterr = kPglRetMalformedInput;
    break;
  ReadAlleleFreqs_ret_MISSING_TOKENS:
    logerrprintfww("Error: Line %" PRIuPTR " of --read-freq file has fewer tokens than expected.\n", line_idx);
    reterr = kPglRetMalformedInput;
    break;
  ReadAlleleFreqs_ret_INVALID_FREQS:
    snprintf(g_logbuf, kLogbufSize, "Error: Invalid frequencies/counts on line %" PRIuPTR " of --read-freq file.\n", line_idx);
  ReadAlleleFreqs_ret_MALFORMED_INPUT_WW:
    WordWrapB(0);
  ReadAlleleFreqs_ret_MALFORMED_INPUT_2:
    logputs("\n");
    logerrputsb();
  ReadAlleleFreqs_ret_MALFORMED_INPUT:
    reterr = kPglRetMalformedInput;
    break;
  }
 ReadAlleleFreqs_ret_1:
  CleanupRLstream(&read_freq_rls);
  BigstackReset(bigstack_mark);
  return reterr;
}

void ComputeMajAlleles(const uintptr_t* variant_include, const uintptr_t* variant_allele_idxs, const double* allele_freqs, uint32_t variant_ct, AltAlleleCt* maj_alleles) {
  uint32_t cur_allele_ct = 2;
  uint32_t variant_uidx = 0;
  for (uint32_t variant_idx = 0; variant_idx < variant_ct; ++variant_idx, ++variant_uidx) {
    MovU32To1Bit(variant_include, &variant_uidx);
    uintptr_t allele_idx_base;
    if (!variant_allele_idxs) {
      allele_idx_base = variant_uidx;
    } else {
      allele_idx_base = variant_allele_idxs[variant_uidx];
      cur_allele_ct = variant_allele_idxs[variant_uidx + 1] - allele_idx_base;
      allele_idx_base -= variant_uidx;
    }
    const double* cur_allele_freqs_base = &(allele_freqs[allele_idx_base]);
    if (cur_allele_ct == 2) {
      maj_alleles[variant_uidx] = (cur_allele_freqs_base[0] < 0.5);
    } else {
      uint32_t maj_allele_idx = 0;
      double max_freq = cur_allele_freqs_base[0];
      double tot_alt_freq = max_freq;
      const uint32_t cur_allele_ct_m1 = cur_allele_ct - 1;
      for (uint32_t allele_idx = 1; allele_idx < cur_allele_ct_m1; ++allele_idx) {
        const double cur_freq = cur_allele_freqs_base[allele_idx];
        tot_alt_freq += cur_freq;
        if (cur_freq > max_freq) {
          maj_allele_idx = allele_idx;
          max_freq = cur_freq;
        }
      }
      if (max_freq + tot_alt_freq <= 1.0) {
        maj_allele_idx = cur_allele_ct_m1;
      }
      maj_alleles[variant_uidx] = maj_allele_idx;
    }
  }
}


// multithread globals
static PgenReader** g_pgr_ptrs = nullptr;
static uintptr_t** g_genovecs = nullptr;
static uint32_t* g_read_variant_uidx_starts = nullptr;
static uintptr_t** g_missing_hc_acc1 = nullptr;
static uintptr_t** g_missing_dosage_acc1 = nullptr;
static uintptr_t** g_hethap_acc1 = nullptr;

static const uintptr_t* g_variant_include = nullptr;
static const ChrInfo* g_cip = nullptr;
static const uintptr_t* g_sex_male = nullptr;
static uint32_t g_raw_sample_ct = 0;
static uint32_t g_cur_block_size = 0;
static uint32_t g_calc_thread_ct = 0;
static PglErr g_error_ret = kPglRetSuccess;

THREAD_FUNC_DECL LoadSampleMissingCtsThread(void* arg) {
  const uintptr_t tidx = R_CAST(uintptr_t, arg);
  const uintptr_t* variant_include = g_variant_include;
  const ChrInfo* cip = g_cip;
  const uintptr_t* sex_male = g_sex_male;
  const uint32_t raw_sample_ct = g_raw_sample_ct;
  const uint32_t raw_sample_ctaw = BitCtToAlignedWordCt(raw_sample_ct);
  const uint32_t acc1_vec_ct = BitCtToVecCt(raw_sample_ct);
  const uint32_t acc4_vec_ct = acc1_vec_ct * 4;
  const uint32_t acc8_vec_ct = acc1_vec_ct * 8;
  const uint32_t calc_thread_ct = g_calc_thread_ct;
  const uint32_t x_code = cip->xymt_codes[kChrOffsetX];
  const uint32_t y_code = cip->xymt_codes[kChrOffsetY];
  uintptr_t* genovec_buf = g_genovecs[tidx];
  uintptr_t* missing_hc_acc1 = g_missing_hc_acc1[tidx];
  uintptr_t* missing_hc_acc4 = &(missing_hc_acc1[acc1_vec_ct * kWordsPerVec]);
  uintptr_t* missing_hc_acc8 = &(missing_hc_acc4[acc4_vec_ct * kWordsPerVec]);
  uintptr_t* missing_hc_acc32 = &(missing_hc_acc8[acc8_vec_ct * kWordsPerVec]);
  ZeroWArr(acc1_vec_ct * kWordsPerVec * 45, missing_hc_acc1);
  uintptr_t* missing_dosage_acc1 = nullptr;
  uintptr_t* missing_dosage_acc4 = nullptr;
  uintptr_t* missing_dosage_acc8 = nullptr;
  uintptr_t* missing_dosage_acc32 = nullptr;
  if (g_missing_dosage_acc1) {
    missing_dosage_acc1 = g_missing_dosage_acc1[tidx];
    missing_dosage_acc4 = &(missing_dosage_acc1[acc1_vec_ct * kWordsPerVec]);
    missing_dosage_acc8 = &(missing_dosage_acc4[acc4_vec_ct * kWordsPerVec]);
    missing_dosage_acc32 = &(missing_dosage_acc8[acc8_vec_ct * kWordsPerVec]);
    ZeroWArr(acc1_vec_ct * kWordsPerVec * 45, missing_dosage_acc1);
  }
  // could make this optional
  // (could technically make missing_hc optional too...)
  uintptr_t* hethap_acc1 = g_hethap_acc1[tidx];
  uintptr_t* hethap_acc4 = &(hethap_acc1[acc1_vec_ct * kWordsPerVec]);
  uintptr_t* hethap_acc8 = &(hethap_acc4[acc4_vec_ct * kWordsPerVec]);
  uintptr_t* hethap_acc32 = &(hethap_acc8[acc8_vec_ct * kWordsPerVec]);
  ZeroWArr(acc1_vec_ct * kWordsPerVec * 45, hethap_acc1);
  uint32_t all_ct_rem15 = 15;
  uint32_t all_ct_rem255d15 = 17;
  uint32_t hap_ct_rem15 = 15;
  uint32_t hap_ct_rem255d15 = 17;
  while (1) {
    PgenReader* pgrp = g_pgr_ptrs[tidx];
    const uint32_t is_last_block = g_is_last_thread_block;
    const uint32_t cur_block_size = g_cur_block_size;
    const uint32_t cur_idx_ct = (((tidx + 1) * cur_block_size) / calc_thread_ct) - ((tidx * cur_block_size) / calc_thread_ct);
    uint32_t variant_uidx = g_read_variant_uidx_starts[tidx];
    uint32_t chr_end = 0;
    uintptr_t* cur_hets = nullptr;
    uint32_t is_diploid_x = 0;
    uint32_t is_y = 0;
    for (uint32_t cur_idx = 0; cur_idx < cur_idx_ct; ++cur_idx, ++variant_uidx) {
      MovU32To1Bit(variant_include, &variant_uidx);
      if (variant_uidx >= chr_end) {
        const uint32_t chr_fo_idx = GetVariantChrFoIdx(cip, variant_uidx);
        const uint32_t chr_idx = cip->chr_file_order[chr_fo_idx];
        chr_end = cip->chr_fo_vidx_start[chr_fo_idx + 1];
        cur_hets = hethap_acc1;
        is_diploid_x = 0;
        is_y = 0;
        if (chr_idx == x_code) {
          is_diploid_x = !IsSet(cip->haploid_mask, 0);
        } else if (chr_idx == y_code) {
          is_y = 1;
        } else {
          if (!IsSet(cip->haploid_mask, chr_idx)) {
            cur_hets = nullptr;
          }
        }
      }
      // could instead have missing_hc and (missing_hc - missing_dosage); that
      // has the advantage of letting you skip one of the two increment
      // operations when the variant is all hardcalls.
      PglErr reterr = PgrGetMissingnessPD(nullptr, nullptr, raw_sample_ct, variant_uidx, pgrp, missing_hc_acc1, missing_dosage_acc1, cur_hets, genovec_buf);
      if (reterr) {
        g_error_ret = reterr;
        break;
      }
      if (is_y) {
        BitvecAnd(sex_male, raw_sample_ctaw, missing_hc_acc1);
        if (missing_dosage_acc1) {
          BitvecAnd(sex_male, raw_sample_ctaw, missing_dosage_acc1);
        }
      }
      VcountIncr1To4(missing_hc_acc1, acc1_vec_ct, missing_hc_acc4);
      if (missing_dosage_acc1) {
        VcountIncr1To4(missing_dosage_acc1, acc1_vec_ct, missing_dosage_acc4);
      }
      if (!(--all_ct_rem15)) {
        Vcount0Incr4To8(acc4_vec_ct, missing_hc_acc4, missing_hc_acc8);
        if (missing_dosage_acc1) {
          Vcount0Incr4To8(acc4_vec_ct, missing_dosage_acc4, missing_dosage_acc8);
        }
        all_ct_rem15 = 15;
        if (!(--all_ct_rem255d15)) {
          Vcount0Incr8To32(acc8_vec_ct, missing_hc_acc8, missing_hc_acc32);
          if (missing_dosage_acc1) {
            Vcount0Incr8To32(acc8_vec_ct, missing_dosage_acc8, missing_dosage_acc32);
          }
          all_ct_rem255d15 = 17;
        }
      }
      if (cur_hets) {
        if (is_diploid_x) {
          BitvecAnd(sex_male, raw_sample_ctaw, cur_hets);
        }
        VcountIncr1To4(cur_hets, acc1_vec_ct, hethap_acc4);
        if (!(--hap_ct_rem15)) {
          Vcount0Incr4To8(acc4_vec_ct, hethap_acc4, hethap_acc8);
          hap_ct_rem15 = 15;
          if (!(--hap_ct_rem255d15)) {
            Vcount0Incr8To32(acc8_vec_ct, hethap_acc8, hethap_acc32);
            hap_ct_rem255d15 = 17;
          }
        }
      }
    }
    if (is_last_block) {
      VcountIncr4To8(missing_hc_acc4, acc4_vec_ct, missing_hc_acc8);
      VcountIncr8To32(missing_hc_acc8, acc8_vec_ct, missing_hc_acc32);
      if (missing_dosage_acc1) {
        VcountIncr4To8(missing_dosage_acc4, acc4_vec_ct, missing_dosage_acc8);
        VcountIncr8To32(missing_dosage_acc8, acc8_vec_ct, missing_dosage_acc32);
      }
      VcountIncr4To8(hethap_acc4, acc4_vec_ct, hethap_acc8);
      VcountIncr8To32(hethap_acc8, acc8_vec_ct, hethap_acc32);
      THREAD_RETURN;
    }
    THREAD_BLOCK_FINISH(tidx);
  }
}

PglErr LoadSampleMissingCts(const uintptr_t* sex_male, const uintptr_t* variant_include, const ChrInfo* cip, uint32_t raw_variant_ct, uint32_t variant_ct, uint32_t raw_sample_ct, uint32_t max_thread_ct, uintptr_t pgr_alloc_cacheline_ct, PgenFileInfo* pgfip, uint32_t* sample_missing_hc_cts, uint32_t* sample_missing_dosage_cts, uint32_t* sample_hethap_cts) {
  assert(sample_missing_hc_cts || sample_missing_dosage_cts);
  unsigned char* bigstack_mark = g_bigstack_base;
  PglErr reterr = kPglRetSuccess;
  {
    if (!variant_ct) {
      ZeroU32Arr(raw_sample_ct, sample_missing_hc_cts);
      if (sample_missing_dosage_cts) {
        ZeroU32Arr(raw_sample_ct, sample_missing_dosage_cts);
      }
      ZeroU32Arr(raw_sample_ct, sample_hethap_cts);
      goto LoadSampleMissingCts_ret_1;
    }
    // this doesn't seem to saturate below 35 threads
    uint32_t calc_thread_ct = (max_thread_ct > 2)? (max_thread_ct - 1) : max_thread_ct;
    const uint32_t acc1_vec_ct = BitCtToVecCt(raw_sample_ct);
    const uintptr_t acc1_alloc_cacheline_ct = DivUp(acc1_vec_ct * (45 * k1LU * kBytesPerVec), kCacheline);
    g_sex_male = sex_male;
    uintptr_t thread_alloc_cacheline_ct = 2 * acc1_alloc_cacheline_ct;
    g_missing_dosage_acc1 = nullptr;
    if (sample_missing_dosage_cts) {
      if (bigstack_alloc_wp(calc_thread_ct, &g_missing_dosage_acc1)) {
        goto LoadSampleMissingCts_ret_NOMEM;
      }
      thread_alloc_cacheline_ct += acc1_alloc_cacheline_ct;
    }
    if (bigstack_alloc_wp(calc_thread_ct, &g_missing_hc_acc1) ||
        bigstack_alloc_wp(calc_thread_ct, &g_hethap_acc1)) {
      goto LoadSampleMissingCts_ret_NOMEM;
    }
    unsigned char* main_loadbufs[2];
    pthread_t* threads;
    uint32_t read_block_size;
    if (PgenMtLoadInit(variant_include, raw_sample_ct, raw_variant_ct, bigstack_left(), pgr_alloc_cacheline_ct, thread_alloc_cacheline_ct, 0, pgfip, &calc_thread_ct, &g_genovecs, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &read_block_size, main_loadbufs, &threads, &g_pgr_ptrs, &g_read_variant_uidx_starts)) {
      goto LoadSampleMissingCts_ret_NOMEM;
    }
    const uintptr_t acc1_alloc = acc1_alloc_cacheline_ct * kCacheline;
    for (uint32_t tidx = 0; tidx < calc_thread_ct; ++tidx) {
      g_missing_hc_acc1[tidx] = S_CAST(uintptr_t*, bigstack_alloc_raw(acc1_alloc));
      if (g_missing_dosage_acc1) {
        g_missing_dosage_acc1[tidx] = S_CAST(uintptr_t*, bigstack_alloc_raw(acc1_alloc));
      }
      g_hethap_acc1[tidx] = S_CAST(uintptr_t*, bigstack_alloc_raw(acc1_alloc));
    }
    g_variant_include = variant_include;
    g_cip = cip;
    g_raw_sample_ct = raw_sample_ct;
    g_calc_thread_ct = calc_thread_ct;

    // nearly identical to LoadAlleleAndGenoCounts()
    logputs("Calculating sample missingness rates... ");
    fputs("0%", stdout);
    fflush(stdout);
    uint32_t pct = 0;

    const uint32_t read_block_sizel = BitCtToWordCt(read_block_size);
    const uint32_t read_block_ct_m1 = (raw_variant_ct - 1) / read_block_size;
    uint32_t parity = 0;
    uint32_t read_block_idx = 0;
    uint32_t variant_idx = 0;
    uint32_t is_last_block = 0;
    uint32_t cur_read_block_size = read_block_size;
    uint32_t next_print_variant_idx = variant_ct / 100;

    while (1) {
      uintptr_t cur_loaded_variant_ct = 0;
      if (!is_last_block) {
        while (read_block_idx < read_block_ct_m1) {
          cur_loaded_variant_ct = PopcountWords(&(variant_include[read_block_idx * read_block_sizel]), read_block_sizel);
          if (cur_loaded_variant_ct) {
            break;
          }
          ++read_block_idx;
        }
        if (read_block_idx == read_block_ct_m1) {
          cur_read_block_size = raw_variant_ct - (read_block_idx * read_block_size);
          cur_loaded_variant_ct = PopcountWords(&(variant_include[read_block_idx * read_block_sizel]), BitCtToWordCt(cur_read_block_size));
        }
        if (PgfiMultiread(variant_include, read_block_idx * read_block_size, read_block_idx * read_block_size + cur_read_block_size, cur_loaded_variant_ct, pgfip)) {
          if (variant_idx) {
            JoinThreads2z(calc_thread_ct, 0, threads);
            g_cur_block_size = 0;
            ErrorCleanupThreads2z(LoadSampleMissingCtsThread, calc_thread_ct, threads);
          }
          goto LoadSampleMissingCts_ret_READ_FAIL;
        }
      }
      if (variant_idx) {
        JoinThreads2z(calc_thread_ct, is_last_block, threads);
        reterr = g_error_ret;
        if (reterr) {
          if (!is_last_block) {
            g_cur_block_size = 0;
            ErrorCleanupThreads2z(LoadSampleMissingCtsThread, calc_thread_ct, threads);
          }
          if (reterr == kPglRetMalformedInput) {
            logputs("\n");
            logerrputs("Error: Malformed .pgen file.\n");
          }
          goto LoadSampleMissingCts_ret_1;
        }
      }
      if (!is_last_block) {
        g_cur_block_size = cur_loaded_variant_ct;
        ComputeUidxStartPartition(variant_include, cur_loaded_variant_ct, calc_thread_ct, read_block_idx * read_block_size, g_read_variant_uidx_starts);
        for (uint32_t tidx = 0; tidx < calc_thread_ct; ++tidx) {
          g_pgr_ptrs[tidx]->fi.block_base = pgfip->block_base;
          g_pgr_ptrs[tidx]->fi.block_offset = pgfip->block_offset;
        }
        is_last_block = (variant_idx + cur_loaded_variant_ct == variant_ct);
        if (SpawnThreads2z(LoadSampleMissingCtsThread, calc_thread_ct, is_last_block, threads)) {
          goto LoadSampleMissingCts_ret_THREAD_CREATE_FAIL;
        }
      }

      parity = 1 - parity;
      if (variant_idx == variant_ct) {
        break;
      }
      if (variant_idx >= next_print_variant_idx) {
        if (pct > 10) {
          putc_unlocked('\b', stdout);
        }
        pct = (variant_idx * 100LLU) / variant_ct;
        printf("\b\b%u%%", pct++);
        fflush(stdout);
        next_print_variant_idx = (pct * S_CAST(uint64_t, variant_ct)) / 100;
      }

      ++read_block_idx;
      variant_idx += cur_loaded_variant_ct;
      // crucially, this is independent of the PgenReader block_base
      // pointers
      pgfip->block_base = main_loadbufs[parity];
    }
    const uint32_t sample_ctv = acc1_vec_ct * kBitsPerVec;
    const uintptr_t acc32_offset = acc1_vec_ct * (13 * k1LU * kWordsPerVec);
    uint32_t* scrambled_missing_hc_cts = nullptr;
    uint32_t* scrambled_missing_dosage_cts = nullptr;
    uint32_t* scrambled_hethap_cts = nullptr;
    scrambled_missing_hc_cts = R_CAST(uint32_t*, &(g_missing_hc_acc1[0][acc32_offset]));
    if (g_missing_dosage_acc1) {
      scrambled_missing_dosage_cts = R_CAST(uint32_t*, &(g_missing_dosage_acc1[0][acc32_offset]));
    }
    scrambled_hethap_cts = R_CAST(uint32_t*, &(g_hethap_acc1[0][acc32_offset]));
    for (uint32_t tidx = 1; tidx < calc_thread_ct; ++tidx) {
      uint32_t* thread_scrambled_missing_hc_cts = R_CAST(uint32_t*, &(g_missing_hc_acc1[tidx][acc32_offset]));
      for (uint32_t uii = 0; uii < sample_ctv; ++uii) {
        scrambled_missing_hc_cts[uii] += thread_scrambled_missing_hc_cts[uii];
      }
      if (scrambled_missing_dosage_cts) {
        uint32_t* thread_scrambled_missing_dosage_cts = R_CAST(uint32_t*, &(g_missing_dosage_acc1[tidx][acc32_offset]));
        for (uint32_t uii = 0; uii < sample_ctv; ++uii) {
          scrambled_missing_dosage_cts[uii] += thread_scrambled_missing_dosage_cts[uii];
        }
      }
      uint32_t* thread_scrambled_hethap_cts = R_CAST(uint32_t*, &(g_hethap_acc1[tidx][acc32_offset]));
      for (uint32_t uii = 0; uii < sample_ctv; ++uii) {
        scrambled_hethap_cts[uii] += thread_scrambled_hethap_cts[uii];
      }
    }
    for (uint32_t sample_uidx = 0; sample_uidx < raw_sample_ct; ++sample_uidx) {
      const uint32_t scrambled_idx = VcountScramble1(sample_uidx);
      sample_missing_hc_cts[sample_uidx] = scrambled_missing_hc_cts[scrambled_idx];
      if (sample_missing_dosage_cts) {
        sample_missing_dosage_cts[sample_uidx] = scrambled_missing_dosage_cts[scrambled_idx];
      }
      sample_hethap_cts[sample_uidx] = scrambled_hethap_cts[scrambled_idx];
    }
    if (pct > 10) {
      putc_unlocked('\b', stdout);
    }
    fputs("\b\b", stdout);
    logprintf("done.\n");
  }
  while (0) {
  LoadSampleMissingCts_ret_NOMEM:
    reterr = kPglRetNomem;
    break;
  LoadSampleMissingCts_ret_READ_FAIL:
    reterr = kPglRetNomem;
    break;
  LoadSampleMissingCts_ret_THREAD_CREATE_FAIL:
    reterr = kPglRetNomem;
    break;
  }
 LoadSampleMissingCts_ret_1:
  BigstackReset(bigstack_mark);
  return reterr;
}

PglErr MindFilter(const uint32_t* sample_missing_cts, const uint32_t* sample_hethap_cts, const SampleIdInfo* siip, uint32_t raw_sample_ct, uint32_t variant_ct, uint32_t variant_ct_y, double mind_thresh, uintptr_t* sample_include, uintptr_t* sex_male, uint32_t* sample_ct_ptr, char* outname, char* outname_end) {
  unsigned char* bigstack_mark = g_bigstack_base;
  PglErr reterr = kPglRetSuccess;
  {
    const uint32_t orig_sample_ct = *sample_ct_ptr;
    if (!orig_sample_ct) {
      goto MindFilter_ret_1;
    }
    const uint32_t raw_sample_ctl = BitCtToWordCt(raw_sample_ct);

    uint32_t max_missing_cts[2];
    mind_thresh *= 1 + kSmallEpsilon;
    max_missing_cts[0] = S_CAST(int32_t, u31tod(variant_ct - variant_ct_y) * mind_thresh);
    max_missing_cts[1] = S_CAST(int32_t, u31tod(variant_ct) * mind_thresh);
    uintptr_t* newly_excluded;
    if (bigstack_calloc_w(raw_sample_ctl, &newly_excluded)) {
      goto MindFilter_ret_NOMEM;
    }
    uint32_t sample_uidx = 0;
    for (uint32_t sample_idx = 0; sample_idx < orig_sample_ct; ++sample_idx, ++sample_uidx) {
      MovU32To1Bit(sample_include, &sample_uidx);
      uint32_t cur_missing_geno_ct = sample_missing_cts[sample_uidx];
      if (sample_hethap_cts) {
        cur_missing_geno_ct += sample_hethap_cts[sample_uidx];
      }
      if (cur_missing_geno_ct > max_missing_cts[IsSet(sex_male, sample_uidx)]) {
        SetBit(sample_uidx, newly_excluded);
      }
    }
    const uint32_t removed_ct = PopcountWords(newly_excluded, raw_sample_ctl);
    // don't bother with allow_no_samples check here, better to have that in
    // just one place
    logprintf("%u sample%s removed due to missing genotype data (--mind).\n", removed_ct, (removed_ct == 1)? "" : "s");
    if (removed_ct) {
      BitvecAndNot(newly_excluded, raw_sample_ctl, sample_include);
      BitvecAndNot(newly_excluded, raw_sample_ctl, sex_male);
      snprintf(outname_end, kMaxOutfnameExtBlen, ".mindrem.id");
      reterr = WriteSampleIds(newly_excluded, siip, outname, removed_ct);
      if (reterr) {
        goto MindFilter_ret_1;
      }
      logprintfww("ID%s written to %s .\n", (removed_ct == 1)? "" : "s", outname);
      *sample_ct_ptr -= removed_ct;
    }
  }
  while (0) {
  MindFilter_ret_NOMEM:
    reterr = kPglRetNomem;
    break;
  }
 MindFilter_ret_1:
  BigstackReset(bigstack_mark);
  return reterr;
}

void EnforceGenoThresh(const ChrInfo* cip, const uint32_t* variant_missing_cts, const uint32_t* variant_hethap_cts, uint32_t sample_ct, uint32_t male_ct, uint32_t first_hap_uidx, double geno_thresh, uintptr_t* variant_include, uint32_t* variant_ct_ptr) {
  const uint32_t prefilter_variant_ct = *variant_ct_ptr;
  geno_thresh *= 1 + kSmallEpsilon;
  const uint32_t missing_max_ct_nony = S_CAST(int32_t, geno_thresh * u31tod(sample_ct));
  const uint32_t missing_max_ct_y = S_CAST(int32_t, geno_thresh * u31tod(male_ct));
  uint32_t cur_missing_max_ct = missing_max_ct_nony;
  uint32_t removed_ct = 0;
  uint32_t variant_uidx = 0;
  uint32_t y_thresh = UINT32_MAX;
  uint32_t y_end = UINT32_MAX;
  uint32_t y_code;
  if (XymtExists(cip, kChrOffsetY, &y_code)) {
    const uint32_t y_chr_fo_idx = cip->chr_idx_to_foidx[y_code];
    y_thresh = cip->chr_fo_vidx_start[y_chr_fo_idx];
    y_end = cip->chr_fo_vidx_start[y_chr_fo_idx + 1];
  }
  for (uint32_t variant_idx = 0; variant_idx < prefilter_variant_ct; ++variant_idx, ++variant_uidx) {
    MovU32To1Bit(variant_include, &variant_uidx);
    if (variant_uidx >= y_thresh) {
      if (variant_uidx < y_end) {
        y_thresh = y_end;
        cur_missing_max_ct = missing_max_ct_y;
      } else {
        y_thresh = UINT32_MAX;
        cur_missing_max_ct = missing_max_ct_nony;
      }
    }
    uint32_t cur_missing_ct = variant_missing_cts[variant_uidx];
    if (variant_uidx >= first_hap_uidx) {
      cur_missing_ct += variant_hethap_cts[variant_uidx - first_hap_uidx];
    }
    if (cur_missing_ct > cur_missing_max_ct) {
      ClearBit(variant_uidx, variant_include);
      ++removed_ct;
    }
  }
  logprintf("--geno: %u variant%s removed due to missing genotype data.\n", removed_ct, (removed_ct == 1)? "" : "s");
  *variant_ct_ptr -= removed_ct;
}

void EnforceHweThresh(const ChrInfo* cip, const uint32_t* founder_raw_geno_cts, const uint32_t* founder_x_male_geno_cts, const uint32_t* founder_x_nosex_geno_cts, const double* hwe_x_pvals, MiscFlags misc_flags, double hwe_thresh, uint32_t nonfounders, uintptr_t* variant_include, uint32_t* variant_ct_ptr) {
  if (cip->haploid_mask[0] & 1) {
    logerrputs("Warning: --hwe has no effect since entire genome is haploid.\n");
    return;
  }
  uint32_t prefilter_variant_ct = *variant_ct_ptr;
  uint32_t x_start = UINT32_MAX;
  uint32_t x_end = UINT32_MAX;
  uint32_t x_code;
  if (XymtExists(cip, kChrOffsetX, &x_code)) {
    const uint32_t x_chr_fo_idx = cip->chr_idx_to_foidx[x_code];
    x_start = cip->chr_fo_vidx_start[x_chr_fo_idx];
    x_end = cip->chr_fo_vidx_start[x_chr_fo_idx + 1];
    // bugfix (4 Jun 2017): if no sex info available, need to skip chrX
    if (!hwe_x_pvals) {
      prefilter_variant_ct -= PopcountBitRange(variant_include, x_start, x_end);
    }
  }
  uint32_t x_thresh = x_start;
  const uint32_t midp = (misc_flags / kfMiscHweMidp) & 1;
  const uint32_t keep_fewhet = (misc_flags / kfMiscHweKeepFewhet) & 1;
  hwe_thresh *= 1 - kSmallEpsilon;
  uint32_t removed_ct = 0;
  uint32_t variant_uidx = 0;
  uint32_t min_obs = UINT32_MAX;
  uint32_t max_obs = 0;
  uint32_t is_x = 0;
  uint32_t male_ref_ct = 0;
  uint32_t male_alt_ct = 0;
  const double* hwe_x_pvals_iter = hwe_x_pvals;
  const double hwe_thresh_recip = (1 + 4 * kSmallEpsilon) / hwe_thresh;
  for (uint32_t variant_idx = 0; variant_idx < prefilter_variant_ct; ++variant_idx, ++variant_uidx) {
    MovU32To1Bit(variant_include, &variant_uidx);
    if (variant_uidx >= x_thresh) {
      is_x = (variant_uidx < x_end);
      if (is_x) {
        if (hwe_x_pvals) {
          x_thresh = x_end;
        } else {
          is_x = 0;
          x_thresh = UINT32_MAX;
          variant_uidx = AdvTo1Bit(variant_include, x_end);
        }
      } else {
        x_thresh = UINT32_MAX;
      }
    }
    const uint32_t* cur_geno_cts = &(founder_raw_geno_cts[3 * variant_uidx]);
    uint32_t homref_ct = cur_geno_cts[0];
    uint32_t hetref_ct = cur_geno_cts[1];
    uint32_t nonref_diploid_ct = cur_geno_cts[2];
    uint32_t test_failed;
    uint32_t cur_obs_ct;
    if (!is_x) {
      cur_obs_ct = homref_ct + hetref_ct + nonref_diploid_ct;
      if (!cur_obs_ct) {
        // currently happens for chrY, chrM
        continue;
      }
      if (keep_fewhet) {
        if (hetref_ct * S_CAST(uint64_t, hetref_ct) <= (4LLU * homref_ct) * nonref_diploid_ct) {
          // no p-value computed at all, so don't count this toward
          // min_obs/max_obs
          continue;
        }
      }
      if (midp) {
        test_failed = HweThreshMidp(hetref_ct, homref_ct, nonref_diploid_ct, hwe_thresh);
      } else {
        test_failed = HweThresh(hetref_ct, homref_ct, nonref_diploid_ct, hwe_thresh);
      }
    } else {
      if (founder_x_male_geno_cts) {
        const uint32_t* cur_male_geno_cts = &(founder_x_male_geno_cts[(3 * k1LU) * (variant_uidx - x_start)]);
        male_ref_ct = cur_male_geno_cts[0];
        homref_ct -= male_ref_ct;
        hetref_ct -= cur_male_geno_cts[1];
        male_alt_ct = cur_male_geno_cts[2];
        nonref_diploid_ct -= male_alt_ct;
      }
      if (founder_x_nosex_geno_cts) {
        const uint32_t* cur_nosex_geno_cts = &(founder_x_nosex_geno_cts[(3 * k1LU) * (variant_uidx - x_start)]);
        homref_ct -= cur_nosex_geno_cts[0];
        hetref_ct -= cur_nosex_geno_cts[1];
        nonref_diploid_ct -= cur_nosex_geno_cts[2];
      }
      cur_obs_ct = homref_ct + hetref_ct + nonref_diploid_ct + male_ref_ct + male_alt_ct;
      double joint_pval = *hwe_x_pvals_iter++;
      test_failed = (joint_pval < hwe_thresh);
      if (test_failed && keep_fewhet && (hetref_ct * S_CAST(uint64_t, hetref_ct) < (4LLU * homref_ct) * nonref_diploid_ct)) {
        // female-only retest
        if (joint_pval) {
          joint_pval *= hwe_thresh_recip;
        } else {
          // keep the variant iff female-only p-value also underflows
          joint_pval = 2.2250738585072013e-308;
        }
        if (midp) {
          test_failed = !HweThreshMidp(hetref_ct, homref_ct, nonref_diploid_ct, joint_pval);
        } else {
          test_failed = !HweThresh(hetref_ct, homref_ct, nonref_diploid_ct, joint_pval);
        }
      }
    }
    if (test_failed) {
      ClearBit(variant_uidx, variant_include);
      ++removed_ct;
    }
    if (cur_obs_ct < min_obs) {
      min_obs = cur_obs_ct;
    }
    if (cur_obs_ct > max_obs) {
      max_obs = cur_obs_ct;
    }
  }
  if (S_CAST(uint64_t, max_obs) * 9 > S_CAST(uint64_t, min_obs) * 10) {
    logerrputs("Warning: --hwe observation counts vary by more than 10%.  Consider using\n--geno, and/or applying different p-value thresholds to distinct subsets of\nyour data.\n");
  }
  logprintfww("--hwe%s%s: %u variant%s removed due to Hardy-Weinberg exact test (%s).\n", midp? " midp" : "", keep_fewhet? " keep-fewhet" : "", removed_ct, (removed_ct == 1)? "" : "s", nonfounders? "all samples" : "founders only");
  *variant_ct_ptr -= removed_ct;
}

void EnforceMinorFreqConstraints(const uintptr_t* variant_allele_idxs, const uint64_t* founder_allele_dosages, const double* allele_freqs, double min_maf, double max_maf, uint64_t min_allele_dosage, uint64_t max_allele_dosage, uintptr_t* variant_include, uint32_t* variant_ct_ptr) {
  const uint32_t prefilter_variant_ct = *variant_ct_ptr;
  uint32_t variant_uidx = 0;
  uint32_t removed_ct = 0;
  if ((min_maf != 0.0) || (max_maf != 1.0)) {
    // defend against floating point error
    min_maf *= 1.0 - kSmallEpsilon;
    max_maf *= 1.0 + kSmallEpsilon;
  } else {
    allele_freqs = nullptr;
  }
  const uint32_t dosage_filter = min_allele_dosage || (max_allele_dosage != (~0LLU));

  uint32_t cur_allele_ct = 2;
  for (uint32_t variant_idx = 0; variant_idx < prefilter_variant_ct; ++variant_idx, ++variant_uidx) {
    MovU32To1Bit(variant_include, &variant_uidx);
    uintptr_t variant_allele_idx_base;
    if (!variant_allele_idxs) {
      variant_allele_idx_base = 2 * variant_uidx;
    } else {
      variant_allele_idx_base = variant_allele_idxs[variant_uidx];
      cur_allele_ct = variant_allele_idxs[variant_uidx + 1] - variant_allele_idx_base;
    }
    if (allele_freqs) {
      const double cur_nonmaj_freq = GetNonmajFreq(&(allele_freqs[variant_allele_idx_base - variant_uidx]), cur_allele_ct);
      if ((cur_nonmaj_freq < min_maf) || (cur_nonmaj_freq > max_maf)) {
        ClearBit(variant_uidx, variant_include);
        ++removed_ct;
        continue;
      }
    }
    if (dosage_filter) {
      const uint64_t* cur_founder_allele_dosages = &(founder_allele_dosages[variant_allele_idx_base]);
      uint64_t max_dosage = cur_founder_allele_dosages[0];
      uint64_t nonmaj_dosage;
      if (cur_allele_ct == 2) {
        nonmaj_dosage = MINV(max_dosage, cur_founder_allele_dosages[1]);
      } else {
        uint64_t tot_dosage = max_dosage;
        for (uint32_t allele_idx = 1; allele_idx < cur_allele_ct; ++allele_idx) {
          const uint64_t cur_dosage = cur_founder_allele_dosages[allele_idx];
          tot_dosage += cur_dosage;
          if (cur_dosage > max_dosage) {
            max_dosage = cur_dosage;
          }
        }
        nonmaj_dosage = tot_dosage - max_dosage;
      }
      if ((nonmaj_dosage < min_allele_dosage) || (nonmaj_dosage > max_allele_dosage)) {
        ClearBit(variant_uidx, variant_include);
        ++removed_ct;
      }
    }
  }
  logprintfww("%u variant%s removed due to minor allele threshold(s) (--maf/--max-maf/--mac/--max-mac).\n", removed_ct, (removed_ct == 1)? "" : "s");
  *variant_ct_ptr -= removed_ct;
}

void EnforceMachR2Thresh(const ChrInfo* cip, const double* mach_r2_vals, double mach_r2_min, double mach_r2_max, uintptr_t* variant_include, uint32_t* variant_ct_ptr) {
  const uint32_t prefilter_variant_ct = *variant_ct_ptr;
  mach_r2_min *= 1 - kSmallEpsilon;
  mach_r2_max *= 1 + kSmallEpsilon;
  uint32_t removed_ct = 0;
  uint32_t variant_uidx = 0;
  const uint32_t chr_ct = cip->chr_ct;
  uint32_t relevant_variant_ct = prefilter_variant_ct;
  for (uint32_t chr_fo_idx = 0; chr_fo_idx < chr_ct; ++chr_fo_idx) {
    const uint32_t chr_idx = cip->chr_file_order[chr_fo_idx];
    // skip X, Y, MT, other haploid
    if (IsSet(cip->haploid_mask, chr_idx)) {
      relevant_variant_ct -= PopcountBitRange(variant_include, cip->chr_fo_vidx_start[chr_fo_idx], cip->chr_fo_vidx_start[chr_fo_idx + 1]);
    }
  }
  uint32_t chr_fo_idx = UINT32_MAX;
  uint32_t chr_end = 0;
  for (uint32_t variant_idx = 0; variant_idx < relevant_variant_ct; ++variant_idx, ++variant_uidx) {
    MovU32To1Bit(variant_include, &variant_uidx);
    while (variant_uidx >= chr_end) {
      uint32_t chr_idx;
      do {
        chr_idx = cip->chr_file_order[++chr_fo_idx];
      } while (IsSet(cip->haploid_mask, chr_idx));
      chr_end = cip->chr_fo_vidx_start[chr_fo_idx + 1];
      variant_uidx = AdvBoundedTo1Bit(variant_include, cip->chr_fo_vidx_start[chr_fo_idx], chr_end);
    }
    const double cur_mach_r2 = mach_r2_vals[variant_uidx];
    if ((cur_mach_r2 < mach_r2_min) || (cur_mach_r2 > mach_r2_max)) {
      ClearBit(variant_uidx, variant_include);
      ++removed_ct;
    }
  }
  logprintf("--mach-r2-filter: %u variant%s removed.\n", removed_ct, (removed_ct == 1)? "" : "s");
  *variant_ct_ptr -= removed_ct;
}

void EnforceMinBpSpace(const ChrInfo* cip, const uint32_t* variant_bps, uint32_t min_bp_space, uintptr_t* variant_include, uint32_t* variant_ct_ptr) {
  const uint32_t orig_variant_ct = *variant_ct_ptr;
  uint32_t variant_uidx = 0;
  uint32_t chr_fo_idx_p1 = 0;
  uint32_t chr_end = 0;
  uint32_t last_bp = 0;
  uint32_t removed_ct = 0;
  for (uint32_t variant_idx = 0; variant_idx < orig_variant_ct; ++variant_idx, ++variant_uidx) {
    MovU32To1Bit(variant_include, &variant_uidx);
    const uint32_t cur_bp = variant_bps[variant_uidx];
    if (variant_uidx >= chr_end) {
      do {
        chr_end = cip->chr_fo_vidx_start[++chr_fo_idx_p1];
      } while (variant_uidx >= chr_end);
      last_bp = cur_bp;
    } else {
      if (cur_bp < last_bp + min_bp_space) {
        ClearBit(variant_uidx, variant_include);
        ++removed_ct;
      } else {
        last_bp = cur_bp;
      }
    }
  }
  const uint32_t new_variant_ct = orig_variant_ct - removed_ct;
  logprintf("--bp-space: %u variant%s removed (%u remaining).\n", removed_ct, (removed_ct == 1)? "" : "s", new_variant_ct);
  *variant_ct_ptr = new_variant_ct;
}

// Generalization of plink 1.9 load_ax_alleles().
//
// Note that, when a variant has 2 (or more) nonmissing allele codes, we print
// a warning if the loaded allele doesn't match one of them (and the variant is
// unchanged).  However, if the variant is biallelic with one or two missing
// allele codes, a missing allele code is filled in.  This maintains
// compatibility with plink 1.9 --a1-allele/--a2-allele's behavior, and is good
// enough for most real-world use cases; however, it's a bit annoying to be
// unable to e.g. add the ref allele when dealing with a single-sample file
// with a het alt1/alt2 call.
// (Workaround for that case when merge is implemented: generate a
// single-sample file with all the right reference alleles, and merge with
// that.)
PglErr SetRefalt1FromFile(const uintptr_t* variant_include, const char* const* variant_ids, const uintptr_t* variant_allele_idxs, const TwoColParams* allele_flag_info, uint32_t raw_variant_ct, uint32_t variant_ct, uint32_t max_variant_id_slen, uint32_t is_alt1, uint32_t force, uint32_t max_thread_ct, const char** allele_storage, uint32_t* max_allele_slen_ptr, AltAlleleCt* refalt1_select, uintptr_t* nonref_flags, uintptr_t* previously_seen) {
  // temporary allocations on bottom, "permanent" allocations on top (so we
  // don't reset g_bigstack_end).
  // previously_seen[] should be preallocated iff both --ref-allele and
  // --alt1-allele are present in the same run.  when it is, this errors out
  // when the flags produce conflicting results.
  unsigned char* bigstack_mark = g_bigstack_base;

  // unaligned in middle of loop
  unsigned char* bigstack_end = g_bigstack_end;

  const char* flagstr = is_alt1? "--alt1-allele" : "--ref-allele";
  uintptr_t line_idx = 0;
  PglErr reterr = kPglRetSuccess;
  ReadLineStream rls;
  PreinitRLstream(&rls);
  {
    const uint32_t raw_variant_ctl = BitCtToWordCt(raw_variant_ct);
    uintptr_t* already_seen;
    if (bigstack_calloc_w(raw_variant_ctl, &already_seen)) {
      goto SetRefalt1FromFile_ret_NOMEM;
    }
    char* line_iter;
    reterr = SizeAndInitRLstreamRaw(allele_flag_info->fname, bigstack_left() / 4, &rls, &line_iter);
    if (reterr) {
      goto SetRefalt1FromFile_ret_1;
    }
    const uint32_t skip_ct = allele_flag_info->skip_ct;
    reterr = RlsSkip(skip_ct, &rls, &line_iter);
    if (reterr) {
      if (reterr == kPglRetEof) {
        snprintf(g_logbuf, kLogbufSize, "Error: Fewer lines than expected in %s.\n", allele_flag_info->fname);
        goto SetRefalt1FromFile_ret_INCONSISTENT_INPUT_WW;
      }
      goto SetRefalt1FromFile_ret_READ_RLSTREAM;
    }
    uint32_t* variant_id_htable = nullptr;
    uint32_t variant_id_htable_size;
    reterr = AllocAndPopulateIdHtableMt(variant_include, variant_ids, variant_ct, max_thread_ct, &variant_id_htable, nullptr, &variant_id_htable_size);
    if (reterr) {
      goto SetRefalt1FromFile_ret_1;
    }
    unsigned char* main_bigstack_base = g_bigstack_base;

    const uint32_t colid_first = (allele_flag_info->colid < allele_flag_info->colx);
    const char skipchar = allele_flag_info->skipchar;
    uint32_t colmin;
    uint32_t coldiff;
    if (colid_first) {
      colmin = allele_flag_info->colid - 1;
      coldiff = allele_flag_info->colx - allele_flag_info->colid;
    } else {
      colmin = allele_flag_info->colx - 1;
      coldiff = allele_flag_info->colid - allele_flag_info->colx;
    }
    line_idx = allele_flag_info->skip_ct;
    const char input_missing_geno_char = *g_input_missing_geno_ptr;
    uintptr_t skipped_variant_ct = 0;
    uintptr_t missing_allele_ct = 0;
    uint32_t allele_mismatch_warning_ct = 0;
    uint32_t rotated_variant_ct = 0;
    uint32_t fillin_variant_ct = 0;
    uint32_t max_allele_slen = *max_allele_slen_ptr;
    uint32_t cur_allele_ct = 2;
    line_iter = AdvToDelim(line_iter, '\n');
    while (1) {
      ++line_idx;
      // line is mutated, so might not be safe to use
      // RlsNext()
      ++line_iter;
      reterr = RlsPostlfNext(&rls, &line_iter);
      if (reterr) {
        if (reterr == kPglRetEof) {
          reterr = kPglRetSuccess;
          break;
        }
        goto SetRefalt1FromFile_ret_READ_RLSTREAM;
      }
      char* linebuf_first_token = FirstNonTspace(line_iter);
      char cc = *linebuf_first_token;
      if (IsEolnKns(cc) || (cc == skipchar)) {
        continue;
      }
      // er, replace this with TokenLex0()...
      char* variant_id_start;
      char* allele_start;
      if (colid_first) {
        variant_id_start = NextTokenMult0(linebuf_first_token, colmin);
        allele_start = NextTokenMult(variant_id_start, coldiff);
        if (!allele_start) {
          goto SetRefalt1FromFile_ret_MISSING_TOKENS;
        }
        line_iter = AdvToDelim(allele_start, '\n');
      } else {
        allele_start = NextTokenMult0(linebuf_first_token, colmin);
        variant_id_start = NextTokenMult(allele_start, coldiff);
        if (!variant_id_start) {
          goto SetRefalt1FromFile_ret_MISSING_TOKENS;
        }
        line_iter = AdvToDelim(variant_id_start, '\n');
      }
      char* token_end = CurTokenEnd(variant_id_start);
      const uint32_t variant_id_slen = token_end - variant_id_start;
      const uint32_t variant_uidx = VariantIdDupflagHtableFind(variant_id_start, variant_ids, variant_id_htable, variant_id_slen, variant_id_htable_size, max_variant_id_slen);
      if (variant_uidx >> 31) {
        if (variant_uidx == UINT32_MAX) {
          ++skipped_variant_ct;
          continue;
        }
        snprintf(g_logbuf, kLogbufSize, "Error: %s variant ID '%s' appears multiple times in main dataset.\n", flagstr, variant_ids[variant_uidx & 0x7fffffff]);
        goto SetRefalt1FromFile_ret_MALFORMED_INPUT_WW;
      }
      token_end = CurTokenEnd(allele_start);
      const uint32_t allele_slen = token_end - allele_start;
      if (allele_slen == 1) {
        const char allele_char = *allele_start;
        // don't overwrite anything with missing code
        if ((allele_char == '.') || (allele_char == input_missing_geno_char)) {
          ++missing_allele_ct;
          continue;
        }
      }
      if (IsSet(already_seen, variant_uidx)) {
        snprintf(g_logbuf, kLogbufSize, "Error: Duplicate variant ID '%s' in %s file.\n", variant_ids[variant_uidx], flagstr);
        goto SetRefalt1FromFile_ret_MALFORMED_INPUT_WW;
      }
      SetBit(variant_uidx, already_seen);
      *token_end = '\0';
      uintptr_t variant_allele_idx_base;
      if (!variant_allele_idxs) {
        variant_allele_idx_base = variant_uidx * 2;
      } else {
        variant_allele_idx_base = variant_allele_idxs[variant_uidx];
        cur_allele_ct = variant_allele_idxs[variant_uidx + 1] - variant_allele_idx_base;
      }
      const char** cur_alleles = &(allele_storage[variant_allele_idx_base]);
      uint32_t allele_idx = 0;
      for (; allele_idx < cur_allele_ct; ++allele_idx) {
        if (!strcmp(allele_start, cur_alleles[allele_idx])) {
          break;
        }
      }
      // note that when both --ref-allele and --alt1-allele are present in the
      // same run, --alt1-allele must deal with the possibility of a
      // pre-altered refalt1_select[].
      AltAlleleCt* cur_refalt1_select = &(refalt1_select[2 * variant_uidx]);
      // this is always zero or one
      uint32_t orig_main_allele_idx = cur_refalt1_select[is_alt1];

      if (allele_idx == cur_allele_ct) {
        if (cur_allele_ct > 2) {
          // could happen millions of times, so micromanage this instead of
          // using logpreprintfww()
          char* write_iter = strcpya(g_logbuf, "Warning: ");
          // strlen("--ref-allele") == 12, strlen("--alt1-allele") == 13
          write_iter = memcpya(write_iter, flagstr, 12 + is_alt1);
          write_iter = strcpya(write_iter, " mismatch for multiallelic variant '");
          write_iter = strcpya(write_iter, variant_ids[variant_uidx]);
          write_iter = memcpya(write_iter, "'.\n", 4);
          if (allele_mismatch_warning_ct < 3) {
            logerrputsb();
          } else {
            logputs_silent(g_logbuf);
          }
          ++allele_mismatch_warning_ct;
          continue;
        }
        const char** new_allele_ptr = &(cur_alleles[orig_main_allele_idx]);
        const char* main_allele = *new_allele_ptr;
        uint32_t is_ref_changing = 1 - orig_main_allele_idx;
        // if main allele is missing, we just fill it in.  otherwise...
        if (!strequal_k_unsafe(main_allele, ".")) {
          new_allele_ptr = &(cur_alleles[1 - orig_main_allele_idx]);
          const char* other_allele = *new_allele_ptr;
          if (!strequal_k_unsafe(other_allele, ".")) {
            char* write_iter = strcpya(g_logbuf, "Warning: ");
            write_iter = memcpya(write_iter, flagstr, 12 + is_alt1);
            write_iter = strcpya(write_iter, " mismatch for biallelic variant '");
            write_iter = strcpya(write_iter, variant_ids[variant_uidx]);
            write_iter = memcpya(write_iter, "'.\n", 4);
            if (allele_mismatch_warning_ct < 3) {
              logerrputsb();
            } else {
              logputs_silent(g_logbuf);
            }
            ++allele_mismatch_warning_ct;
            continue;
          }
          // swap alleles for biallelic variant.  no previously_seen[] check
          // needed, it's impossible to get here if --ref-allele and
          // --alt1-allele assignments conflict.
          cur_refalt1_select[0] = 1;
          cur_refalt1_select[1] = 0;
          ++rotated_variant_ct;
          is_ref_changing = 1;
        }
        if (is_ref_changing) {
          if (!IsSet(nonref_flags, variant_uidx)) {
            if (!force) {
              goto SetRefalt1FromFile_ret_NOFORCE;
            }
          } else {
            ClearBit(variant_uidx, nonref_flags);
          }
        }
        if (allele_slen == 1) {
          *new_allele_ptr = &(g_one_char_strs[2 * ctou32(allele_start[0])]);
        } else {
          // No in-place-overwrite case here since
          // 1. *new_allele_ptr was always '.'
          // 2. More importantly, when --loop-cats is used,
          //    allele_storage_backup[n] must point to the unaltered original
          //    string.
          if (S_CAST(uintptr_t, bigstack_end - main_bigstack_base) <= allele_slen) {
            goto SetRefalt1FromFile_ret_NOMEM;
          }
          if (allele_slen > max_allele_slen) {
            max_allele_slen = allele_slen;
          }
          const uint32_t allele_blen = allele_slen + 1;
          bigstack_end -= allele_blen;
          memcpy(bigstack_end, allele_start, allele_blen);
          *new_allele_ptr = R_CAST(const char*, bigstack_end);
        }
        ++fillin_variant_ct;
        continue;
      }
      if (allele_idx == orig_main_allele_idx) {
        continue;
      }
      // both --ref-allele and --alt1-allele in current run, and they
      // contradict each other.  error out instead of producing a
      // order-of-operations dependent result.
      if (is_alt1 && previously_seen && IsSet(previously_seen, variant_uidx) && (allele_idx == cur_refalt1_select[0])) {
        snprintf(g_logbuf, kLogbufSize, "Error: --ref-allele and --alt1-allele assignments conflict for variant '%s'.\n", variant_ids[variant_uidx]);
        goto SetRefalt1FromFile_ret_INCONSISTENT_INPUT_WW;
      }

      // need to swap/rotate alleles.
      if ((!is_alt1) || (!allele_idx)) {
        if (!IsSet(nonref_flags, variant_uidx)) {
          if (!force) {
            goto SetRefalt1FromFile_ret_NOFORCE;
          }
        } else if (!is_alt1) {
          ClearBit(variant_uidx, nonref_flags);
        }
      }
      if (!is_alt1) {
        cur_refalt1_select[0] = allele_idx;
        if (allele_idx == 1) {
          cur_refalt1_select[1] = 0;
        }
      } else {
        cur_refalt1_select[1] = allele_idx;

        // cases:
        // 1. --alt1-allele run without --ref-allele, or previously_seen not
        //    set for this variant.  then allele_idx must be zero, and we need
        //    to change cur_refalt1_select[0] from 0 to 1 and mark the variant
        //    has having a provisional reference allele.
        //    orig_main_allele_idx is 1 here.
        // 2. --ref-allele and --alt1-allele both run on this variant, and
        //    --ref-allele confirmed the initial ref allele setting.  then,
        //    since there's no conflict but alt1 is changing, allele_idx must
        //    be >1, and we leave cur_refalt1_select[0] unchanged at 0.
        // 3. --ref-allele and --alt1-allele both run on this variant, and
        //    --ref-allele swapped ref and alt1.  then, since there's no
        //    conflict but alt1 is changing, allele_idx must be >1, and we
        //    leave cur_refalt_select[0] unchanged at 1.
        // 4. --ref-allele and --alt1-allele both run on this variant, and
        //    cur_refalt1_select[0] >1.  allele_idx could be either zero or
        //    >1, but we know it doesn't conflict cur_refalt1_select[0] so we
        //    don't change the latter.  orig_main_allele_idx is still 1 here.
        // cheapest way I see to detect case 1 is comparison of allele_idx with
        //   cur_refalt1_select[0].
        if (cur_refalt1_select[0] == allele_idx) {
          cur_refalt1_select[0] = 1;
          SetBit(variant_uidx, nonref_flags);
        }
      }
      ++rotated_variant_ct;
    }
    if (allele_mismatch_warning_ct > 3) {
      fprintf(stderr, "%u more allele-mismatch warning%s: see log file.\n", allele_mismatch_warning_ct - 3, (allele_mismatch_warning_ct == 4)? "" : "s");
    }
    if (fillin_variant_ct) {
      if (rotated_variant_ct) {
        logprintfww("%s: %u set%s of allele codes rotated, %u allele code%s filled in.\n", flagstr, rotated_variant_ct, (rotated_variant_ct == 1)? "" : "s", fillin_variant_ct, (fillin_variant_ct == 1)? "" : "s");
      } else {
        logprintf("%s: %u allele code%s filled in.\n", flagstr, fillin_variant_ct, (fillin_variant_ct == 1)? "" : "s");
      }
    } else if (rotated_variant_ct) {
      logprintf("%s: %u set%s of allele codes rotated.\n", flagstr, rotated_variant_ct, (rotated_variant_ct == 1)? "" : "s");
    } else {
      logprintf("%s: No variants changed.\n", flagstr);
    }
    if (skipped_variant_ct) {
      logerrprintfww("Warning: %" PRIuPTR " variant ID%s in %s file missing from main dataset.\n", skipped_variant_ct, (skipped_variant_ct == 1)? "" : "s", flagstr);
    }
    if (missing_allele_ct) {
      logerrprintfww("Warning: %" PRIuPTR " allele code%s in %s file were missing (%s skipped).\n", missing_allele_ct, (missing_allele_ct == 1)? "" : "s", flagstr, (missing_allele_ct == 1)? "this entry was" : "these entries were");
    }
    if (previously_seen && (!is_alt1)) {
      memcpy(previously_seen, already_seen, raw_variant_ctl * sizeof(intptr_t));
    }
  }
  while (0) {
  SetRefalt1FromFile_ret_NOMEM:
    reterr = kPglRetNomem;
    break;
  SetRefalt1FromFile_ret_READ_RLSTREAM:
    {
      char file_descrip_buf[32];
      snprintf(file_descrip_buf, 32, "%s file", flagstr);
      RLstreamErrPrint(file_descrip_buf, &rls, &reterr);
    }
    break;
  SetRefalt1FromFile_ret_MISSING_TOKENS:
    logerrprintfww("Error: Line %" PRIuPTR " of %s file has fewer tokens than expected.\n", line_idx, flagstr);
    reterr = kPglRetMalformedInput;
    break;
  SetRefalt1FromFile_ret_MALFORMED_INPUT_WW:
    WordWrapB(0);
    logerrputsb();
    reterr = kPglRetMalformedInput;
    break;
  SetRefalt1FromFile_ret_NOFORCE:
    snprintf(g_logbuf, kLogbufSize, "Error: Line %" PRIuPTR " of %s file contradicts 'known' reference allele. Add the 'force' modifier to %s to force an allele swap anyway.\n", line_idx, flagstr, flagstr);
  SetRefalt1FromFile_ret_INCONSISTENT_INPUT_WW:
    WordWrapB(0);
    logerrputsb();
    reterr = kPglRetInconsistentInput;
    break;
  }
 SetRefalt1FromFile_ret_1:
  CleanupRLstream(&rls);
  BigstackReset(bigstack_mark);
  BigstackEndSet(bigstack_end);
  return reterr;
}

PglErr RefFromFaProcessContig(const uintptr_t* variant_include, const uint32_t* variant_bps, const uintptr_t* variant_allele_idxs, const char* const* allele_storage, const ChrInfo* cip, uint32_t force, uint32_t chr_fo_idx, uint32_t variant_uidx_last, char* seqbuf, char* seqbuf_end, AltAlleleCt* refalt1_select, uintptr_t* nonref_flags, uint32_t* changed_ct_ptr, uint32_t* validated_ct_ptr, uint32_t* downgraded_ct_ptr) {
  uint32_t variant_uidx = AdvTo1Bit(variant_include, cip->chr_fo_vidx_start[chr_fo_idx]);
  const uint32_t bp_end = seqbuf_end - seqbuf;
  if (variant_bps[variant_uidx_last] >= bp_end) {
    const uint32_t chr_idx = cip->chr_file_order[chr_fo_idx];
    if (!force) {
      char* write_iter = strcpya(g_logbuf, "Error: Contig '");
      write_iter = chrtoa(cip, chr_idx, write_iter);
      snprintf(write_iter, kLogbufSize - kMaxIdSlen - 32, "' in --ref-from-fa file is too short; it is likely to be mismatched with your data. Add the 'force' modifier if this wasn't a mistake, and you just want to mark all reference alleles past the end as provisional.\n");
      WordWrapB(0);
      logerrputsb();
      return kPglRetInconsistentInput;
    } else {
      char* write_iter = strcpya(g_logbuf, "Warning: Contig '");
      write_iter = chrtoa(cip, chr_idx, write_iter);
      snprintf(write_iter, kLogbufSize - kMaxIdSlen - 32, "' in --ref-from-fa file is too short; it is likely to be mismatched with your data.\n");
      WordWrapB(0);
      logerrputsb();
    }
    uint32_t offset = CountSortedSmallerU32(&(variant_bps[variant_uidx]), variant_uidx_last - variant_uidx, bp_end);

    const uint32_t chr_vidx_end = cip->chr_fo_vidx_start[chr_fo_idx + 1];
    // set all bits in [variant_uidx + offset, chr_vidx_end), and count how
    // many relevant bits were set.
    const uint32_t widx_full_end = chr_vidx_end / kBitsPerWord;
    const uint32_t trailing_bit_ct = chr_vidx_end % kBitsPerWord;
    const uint32_t chr_vidx_truncate = variant_uidx + offset;
    uint32_t widx = chr_vidx_truncate / kBitsPerWord;
    uint32_t downgraded_incr;
    if (widx == widx_full_end) {
      const uintptr_t cur_mask = (k1LU << trailing_bit_ct) - (k1LU << (chr_vidx_truncate % kBitsPerWord));
      downgraded_incr = PopcountWord(variant_include[widx] & (~nonref_flags[widx]) & cur_mask);
      nonref_flags[widx] |= cur_mask;
    } else {
      downgraded_incr = 0;
      if (chr_vidx_truncate % kBitsPerWord) {
        const uintptr_t cur_mask = (~k0LU) << (chr_vidx_truncate % kBitsPerWord);
        downgraded_incr = PopcountWord(variant_include[widx] & (~nonref_flags[widx]) & cur_mask);
        nonref_flags[widx] |= cur_mask;
        ++widx;
      }
      for (; widx < widx_full_end; ++widx) {
        downgraded_incr += PopcountWord(variant_include[widx] & (~nonref_flags[widx]));
        nonref_flags[widx] = ~k0LU;
      }
      if (trailing_bit_ct) {
        const uintptr_t cur_mask = (k1LU << trailing_bit_ct) - k1LU;
        downgraded_incr += PopcountWord(variant_include[widx] & (~nonref_flags[widx]) & cur_mask);
        nonref_flags[widx] |= cur_mask;
      }
    }
    *downgraded_ct_ptr += downgraded_incr;
    if (!offset) {
      return kPglRetSuccess;
    }
    // We know that variant_bps[variant_uidx + offset - 1] < bp_end,
    // variant_bps[variant_uidx + offset] >= bp_end, and that at least one
    // variant_include[] bit is set in [variant_uidx, variant_uidx + offset)
    // (since offset > 0).  Find the last such bit.
    variant_uidx_last = FindLast1BitBefore(variant_include, chr_vidx_truncate);
  }
  *seqbuf_end = '\0';
  uint32_t changed_ct = *changed_ct_ptr;
  uint32_t validated_ct = *validated_ct_ptr;
  uint32_t cur_allele_ct = 2;
  while (1) {
    const uint32_t cur_bp = variant_bps[variant_uidx];
    uintptr_t variant_allele_idx_base = variant_uidx * 2;
    if (variant_allele_idxs) {
      variant_allele_idx_base = variant_allele_idxs[variant_uidx];
      cur_allele_ct = variant_allele_idxs[variant_uidx + 1] - variant_allele_idx_base;
    }
    const char* const* cur_alleles = &(allele_storage[variant_allele_idx_base]);
    const char* cur_ref = &(seqbuf[cur_bp]);
    int32_t consistent_allele_idx = -1;
    for (uint32_t allele_idx = 0; allele_idx < cur_allele_ct; ++allele_idx) {
      const char* cur_allele = cur_alleles[allele_idx];
      const uint32_t cur_allele_slen = strlen(cur_allele);
      if (strcaseequal(cur_allele, cur_ref, cur_allele_slen)) {
        if (consistent_allele_idx != -1) {
          // Multiple alleles could be ref (this always happens for deletions).
          // Don't try to do anything.
          consistent_allele_idx = -2;
          break;
        }
        consistent_allele_idx = allele_idx;
      }
    }
    if (consistent_allele_idx >= 0) {
      if (consistent_allele_idx) {
        if ((!IsSet(nonref_flags, variant_uidx)) && (!force)) {
          const uint32_t chr_idx = cip->chr_file_order[chr_fo_idx];
          char* write_iter = strcpya(g_logbuf, "Error: --ref-from-fa wants to change reference allele assignment at ");
          write_iter = chrtoa(cip, chr_idx, write_iter);
          *write_iter++ = ':';
          write_iter = u32toa(cur_bp, write_iter);
          snprintf(write_iter, kLogbufSize - kMaxIdSlen - 128, ", but it's marked as 'known'. Add the 'force' modifier to force this change through.\n");
          WordWrapB(0);
          logerrputsb();
          return kPglRetInconsistentInput;
        }
        refalt1_select[2 * variant_uidx] = consistent_allele_idx;
        refalt1_select[2 * variant_uidx + 1] = 0;
        ++changed_ct;
      } else {
        ++validated_ct;
      }
      ClearBit(variant_uidx, nonref_flags);
    } else if ((consistent_allele_idx == -1) && (!IsSet(nonref_flags, variant_uidx))) {
      // okay to have multiple matches, but not zero matches
      if (!force) {
        const uint32_t chr_idx = cip->chr_file_order[chr_fo_idx];
        char* write_iter = strcpya(g_logbuf, "Error: Reference allele at ");
        write_iter = chrtoa(cip, chr_idx, write_iter);
        *write_iter++ = ':';
        write_iter = u32toa(cur_bp, write_iter);
        snprintf(write_iter, kLogbufSize - kMaxIdSlen - 64, " is marked as 'known', but is inconsistent with .fa file. Add the 'force' modifier to downgrade it to provisional.\n");
        WordWrapB(0);
        logerrputsb();
        return kPglRetInconsistentInput;
      }
      SetBit(variant_uidx, nonref_flags);
      *downgraded_ct_ptr += 1;
    }
    if (variant_uidx == variant_uidx_last) {
      *changed_ct_ptr = changed_ct;
      *validated_ct_ptr = validated_ct;
      return kPglRetSuccess;
    }
    ++variant_uidx;
    MovU32To1Bit(variant_include, &variant_uidx);
  }
}

PglErr RefFromFa(const uintptr_t* variant_include, const uint32_t* variant_bps, const uintptr_t* variant_allele_idxs, const char* const* allele_storage, const ChrInfo* cip, const char* fname, uint32_t max_allele_slen, uint32_t force, AltAlleleCt* refalt1_select, uintptr_t* nonref_flags) {
  unsigned char* bigstack_mark = g_bigstack_base;
  uintptr_t line_idx = 0;
  PglErr reterr = kPglRetSuccess;
  ReadLineStream fa_rls;
  PreinitRLstream(&fa_rls);
  {
    const uint32_t chr_ct = cip->chr_ct;
    char* chr_name_buf;
    uintptr_t* chr_already_seen;
    if (bigstack_calloc_w(BitCtToWordCt(chr_ct), &chr_already_seen) ||
        bigstack_alloc_c(kMaxIdBlen, &chr_name_buf)) {
      goto RefFromFa_ret_NOMEM;
    }

    // To simplify indel/complex-variant handling, we load an entire contig at
    // a time.  Determine an upper bound for the size of this buffer.
    const uintptr_t* chr_mask = cip->chr_mask;
    uint32_t seqbuf_size = 0;
    for (uint32_t chr_fo_idx = 0; chr_fo_idx < chr_ct; ++chr_fo_idx) {
      if (!IsSet(chr_mask, cip->chr_file_order[chr_fo_idx])) {
        continue;
      }
      const int32_t chr_vidx_start_m1 = cip->chr_fo_vidx_start[chr_fo_idx] - 1;
      const int32_t chr_vidx_last = FindLast1BitBeforeBounded(variant_include, cip->chr_fo_vidx_start[chr_fo_idx + 1], chr_vidx_start_m1);
      if (chr_vidx_last != chr_vidx_start_m1) {
        const uint32_t cur_bp = variant_bps[S_CAST(uint32_t, chr_vidx_last)];
        if (cur_bp > seqbuf_size) {
          seqbuf_size = cur_bp;
        }
      }
    }
    seqbuf_size += max_allele_slen + 1;
    char* seqbuf;
    if (bigstack_alloc_c(seqbuf_size, &seqbuf)) {
      goto RefFromFa_ret_NOMEM;
    }
    // May as well handle insertion before first contig base, and deletion of
    // first base, correctly.
    seqbuf[0] = 'N';

    char* line_iter;
    reterr = SizemaxAndInitRLstreamRaw(fname, &fa_rls, &line_iter);
    if (reterr) {
      goto RefFromFa_ret_1;
    }

    char* seqbuf_end = nullptr;
    char* seq_iter = nullptr;
    // possible but low-priority todo: exploit .fai when it's present
    uint32_t chr_fo_idx = UINT32_MAX;
    uint32_t cur_vidx_last = 0;
    uint32_t skip_chr = 1;
    uint32_t changed_ct = 0;
    uint32_t validated_ct = 0;
    uint32_t downgraded_ct = 0;
    while (1) {
      ++line_idx;
      reterr = RlsNext(&fa_rls, &line_iter);
      if (reterr) {
        if (reterr == kPglRetEof) {
          reterr = kPglRetSuccess;
          break;
        }
        goto RefFromFa_ret_READ_RLSTREAM;
      }
      unsigned char ucc = line_iter[0];
      if (ucc < 'A') {
        // > = ascii 62
        // ; = ascii 59
        if ((ucc == ';') || (ucc <= '\r')) {
          continue;
        }
        if (ucc != '>') {
          snprintf(g_logbuf, kLogbufSize, "Error: Unexpected character at beginning of line %" PRIuPTR " of --ref-from-fa file.\n", line_idx);
          goto RefFromFa_ret_MALFORMED_INPUT_WW;
        }
        if (chr_fo_idx != UINT32_MAX) {
          reterr = RefFromFaProcessContig(variant_include, variant_bps, variant_allele_idxs, allele_storage, cip, force, chr_fo_idx, cur_vidx_last, seqbuf, seq_iter, refalt1_select, nonref_flags, &changed_ct, &validated_ct, &downgraded_ct);
          if (reterr) {
            goto RefFromFa_ret_1;
          }
        }
        char* chr_name_start = &(line_iter[1]);
        if (IsSpaceOrEoln(*chr_name_start)) {
          snprintf(g_logbuf, kLogbufSize, "Error: Invalid contig description on line %" PRIuPTR " of --ref-from-fa file.%s\n", line_idx, (*chr_name_start == ' ')? " (Spaces are not permitted between the leading '>' and the contig name.)" : "");
          goto RefFromFa_ret_MALFORMED_INPUT_WW;
        }
        char* chr_name_end = CurTokenEnd(chr_name_start);
        line_iter = AdvToDelim(chr_name_end, '\n');
        *chr_name_end = '\0';
        const uint32_t chr_name_slen = chr_name_end - chr_name_start;
        chr_fo_idx = UINT32_MAX;
        const uint32_t chr_idx = GetChrCode(chr_name_start, cip, chr_name_slen);
        if ((!IsI32Neg(chr_idx)) && IsSet(cip->chr_mask, chr_idx)) {
          chr_fo_idx = cip->chr_idx_to_foidx[chr_idx];
          if (IsSet(chr_already_seen, chr_fo_idx)) {
            snprintf(g_logbuf, kLogbufSize, "Error: Duplicate contig name '%s' in --ref-from-fa file.\n", chr_name_start);
            goto RefFromFa_ret_MALFORMED_INPUT_WW;
          }
          SetBit(chr_fo_idx, chr_already_seen);
          const int32_t chr_vidx_start_m1 = cip->chr_fo_vidx_start[chr_fo_idx] - 1;
          const int32_t chr_vidx_last = FindLast1BitBeforeBounded(variant_include, cip->chr_fo_vidx_start[chr_fo_idx + 1], chr_vidx_start_m1);
          if (chr_vidx_last == chr_vidx_start_m1) {
            chr_fo_idx = UINT32_MAX;
          } else {
            cur_vidx_last = chr_vidx_last;
            seqbuf_end = &(seqbuf[variant_bps[cur_vidx_last] + max_allele_slen]);
            seq_iter = &(seqbuf[1]);
          }
        }
        skip_chr = (chr_fo_idx == UINT32_MAX);
        continue;
      }
      const char* line_start = line_iter;
      line_iter = CurTokenEnd(line_iter);
      const char* seqline_end = line_iter;
      if (skip_chr) {
        continue;
      }
      ucc = *seqline_end;
      if ((ucc == ' ') || (ucc == '\t')) {
        snprintf(g_logbuf, kLogbufSize, "Error: Line %" PRIuPTR " of --ref-from-fa file is malformed.\n", line_idx);
        goto RefFromFa_ret_MALFORMED_INPUT_2;
      }
      uint32_t cur_seq_slen = seqline_end - line_start;
      const uint32_t seq_rem = seqbuf_end - seq_iter;
      if (seq_rem <= cur_seq_slen) {
        cur_seq_slen = seq_rem;
        skip_chr = 1;
      }
      const char* gap_start = S_CAST(const char*, memchr(line_start, '-', cur_seq_slen));
      if (gap_start) {
        logerrprintfww("Warning: Indeterminate-length gap present on line %" PRIuPTR " of --ref-from-fa file. Ignoring remainder of contig.\n", line_idx);
        cur_seq_slen = gap_start - line_start;
        skip_chr = 1;
      }
      seq_iter = memcpya(seq_iter, line_start, cur_seq_slen);
      // seq_iter = memcpya_toupper(seq_iter, loadbuf, cur_seq_slen);
    }
    if (chr_fo_idx != UINT32_MAX) {
      reterr = RefFromFaProcessContig(variant_include, variant_bps, variant_allele_idxs, allele_storage, cip, force, chr_fo_idx, cur_vidx_last, seqbuf, seq_iter, refalt1_select, nonref_flags, &changed_ct, &validated_ct, &downgraded_ct);
      if (reterr) {
        goto RefFromFa_ret_1;
      }
    }
    logprintf("--ref-from-fa%s: %u variant%s changed, %u validated.\n", force? " force" : "", changed_ct, (changed_ct == 1)? "" : "s", validated_ct);
    if (downgraded_ct) {
      logerrprintfww("Warning: %u reference allele%s downgraded from 'known' to 'provisional'.\n", downgraded_ct, (downgraded_ct == 1)? "" : "s");
    }
  }
  while (0) {
  RefFromFa_ret_NOMEM:
    reterr = kPglRetNomem;
    break;
  RefFromFa_ret_READ_RLSTREAM:
    RLstreamErrPrint("--ref-from-fa file", &fa_rls, &reterr);
    break;
  RefFromFa_ret_MALFORMED_INPUT_WW:
    WordWrapB(0);
  RefFromFa_ret_MALFORMED_INPUT_2:
    logerrputsb();
    reterr = kPglRetMalformedInput;
    break;
  }
 RefFromFa_ret_1:
  CleanupRLstream(&fa_rls);
  BigstackReset(bigstack_mark);
  return reterr;
}

#ifdef __cplusplus
}  // namespace plink2
#endif
